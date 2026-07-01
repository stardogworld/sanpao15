#include "sanpao15/partitioned_keyset.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <list>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "sanpao15/bitboard.h"
#include "sanpao15/external_closure.h"
#include "sanpao15/layered.h"
#include "sanpao15/position.h"
#include "sanpao15/table.h"

namespace sanpao15 {

namespace {

constexpr std::array<char, 8> BucketMagic{'S', '1', '5', 'B', 'K', 'T', '1', '\0'};
constexpr uint32_t BucketFileVersion = 1;
constexpr const char* PartitionType = "partitioned-keyset";
constexpr uint32_t PartitionManifestVersion = 1;
constexpr PartitionMethod DefaultPartitionMethod = PartitionMethod::Splitmix64Mod;
using Clock = std::chrono::steady_clock;

struct InputSummary {
    PartitionSourceKind kind = PartitionSourceKind::Keys;
    int soldierCount = 0;
    uint64_t keyCount = 0;
};

struct ManifestData {
    std::filesystem::path dir;
    std::string type;
    uint32_t version = 0;
    uint64_t rulesetHash = 0;
    std::string sourceFile;
    PartitionSourceKind sourceKind = PartitionSourceKind::Keys;
    int soldierCount = 0;
    uint32_t bucketCount = 0;
    PartitionMethod partitionMethod = PartitionMethod::Splitmix64Mod;
    uint64_t totalKeys = 0;
    std::vector<PartitionBucketInfo> buckets;
};

void writeU8(std::ostream& output, uint8_t value, const char* context) {
    output.put(static_cast<char>(value));
    if (!output) {
        throw std::runtime_error(std::string("failed to write ") + context);
    }
}

void writeU32LE(std::ostream& output, uint32_t value, const char* context) {
    for (int shift = 0; shift < 32; shift += 8) {
        writeU8(output, static_cast<uint8_t>((value >> shift) & 0xffu), context);
    }
}

void writeU64LE(std::ostream& output, uint64_t value, const char* context) {
    for (int shift = 0; shift < 64; shift += 8) {
        writeU8(output, static_cast<uint8_t>((value >> shift) & 0xffu), context);
    }
}

uint8_t readU8(std::istream& input, const char* context) {
    const int value = input.get();
    if (value == std::char_traits<char>::eof()) {
        throw std::runtime_error(std::string("unexpected end of ") + context);
    }
    return static_cast<uint8_t>(value);
}

uint32_t readU32LE(std::istream& input, const char* context) {
    uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<uint32_t>(readU8(input, context)) << shift;
    }
    return value;
}

uint64_t readU64LE(std::istream& input, const char* context) {
    uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<uint64_t>(readU8(input, context)) << shift;
    }
    return value;
}

void requireSoldierCountRange(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::runtime_error("soldier count must be in 0..15");
    }
}

void requireKeySoldierCount(uint64_t key, int soldierCount, const char* context) {
    const int actualSoldiers = popcount25(unpackPosition(key).soldiers);
    if (actualSoldiers != soldierCount) {
        throw std::runtime_error(std::string(context) + " key has unexpected soldier count");
    }
}

uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

std::string jsonEscape(const std::string& text) {
    std::string escaped;
    for (char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open partition manifest: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool isJsonWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

size_t skipWhitespace(const std::string& text, size_t pos) {
    while (pos < text.size() && isJsonWhitespace(text[pos])) {
        ++pos;
    }
    return pos;
}

std::optional<size_t> jsonKeyColon(const std::string& text, const std::string& key, size_t start = 0) {
    const std::string needle = "\"" + key + "\"";
    size_t keyPos = text.find(needle, start);
    while (keyPos != std::string::npos) {
        size_t colon = skipWhitespace(text, keyPos + needle.size());
        if (colon < text.size() && text[colon] == ':') {
            return colon;
        }
        keyPos = text.find(needle, keyPos + 1);
    }
    return std::nullopt;
}

std::string jsonStringAt(const std::string& text, size_t colon, const std::string& key) {
    size_t pos = skipWhitespace(text, colon + 1);
    if (pos >= text.size() || text[pos] != '"') {
        throw std::runtime_error("partition manifest field is not a string: " + key);
    }
    ++pos;
    std::string value;
    bool escaping = false;
    for (; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (escaping) {
            switch (ch) {
                case '"':
                    value += '"';
                    break;
                case '\\':
                    value += '\\';
                    break;
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                default:
                    value += ch;
                    break;
            }
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value += ch;
    }
    throw std::runtime_error("unterminated string in partition manifest field: " + key);
}

uint64_t jsonUintAt(const std::string& text, size_t colon, const std::string& key) {
    size_t pos = skipWhitespace(text, colon + 1);
    const size_t start = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        ++pos;
    }
    if (pos == start) {
        throw std::runtime_error("partition manifest field is not an unsigned integer: " + key);
    }
    return static_cast<uint64_t>(std::stoull(text.substr(start, pos - start)));
}

std::string requireJsonString(const std::string& text, const std::string& key, size_t start = 0) {
    const auto colon = jsonKeyColon(text, key, start);
    if (!colon.has_value()) {
        throw std::runtime_error("partition manifest missing string field: " + key);
    }
    return jsonStringAt(text, *colon, key);
}

uint64_t requireJsonUint(const std::string& text, const std::string& key, size_t start = 0) {
    const auto colon = jsonKeyColon(text, key, start);
    if (!colon.has_value()) {
        throw std::runtime_error("partition manifest missing numeric field: " + key);
    }
    return jsonUintAt(text, *colon, key);
}

std::string relativePathString(const std::filesystem::path& base, const std::filesystem::path& path) {
    std::error_code error;
    const auto relative = std::filesystem::relative(path, base, error);
    return error ? path.string() : relative.string();
}

std::string bucketFileName(uint32_t bucketId) {
    std::ostringstream out;
    out << "bucket-" << std::setw(6) << std::setfill('0') << bucketId << ".s15bucket";
    return out.str();
}

uint64_t fileSizeOrZero(const std::filesystem::path& path) {
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > std::numeric_limits<uint64_t>::max()) {
        return 0;
    }
    return static_cast<uint64_t>(size);
}

void writeRawBucketKey(std::ostream& output, uint64_t key) {
    writeU64LE(output, key, "raw bucket file");
}

std::vector<uint64_t> readRawBucketFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::vector<uint64_t> keys;
    while (true) {
        const int first = input.get();
        if (first == std::char_traits<char>::eof()) {
            break;
        }
        uint64_t key = static_cast<uint8_t>(first);
        for (int shift = 8; shift < 64; shift += 8) {
            key |= static_cast<uint64_t>(readU8(input, "raw bucket file")) << shift;
        }
        keys.push_back(key);
    }
    return keys;
}

void writeBucketFile(
    const std::filesystem::path& path,
    int soldierCount,
    uint32_t bucketId,
    uint32_t bucketCount,
    PartitionMethod method,
    const std::vector<uint64_t>& sortedUniqueKeys) {
    requireSoldierCountRange(soldierCount);
    if (bucketCount == 0 || bucketId >= bucketCount) {
        throw std::invalid_argument("invalid bucket id/count");
    }
    if (!std::is_sorted(sortedUniqueKeys.begin(), sortedUniqueKeys.end()) ||
        std::adjacent_find(sortedUniqueKeys.begin(), sortedUniqueKeys.end()) != sortedUniqueKeys.end()) {
        throw std::invalid_argument("bucket keys must be sorted and unique");
    }
    for (uint64_t key : sortedUniqueKeys) {
        requireKeySoldierCount(key, soldierCount, "bucket file");
        if (partitionBucketForKey(key, bucketCount, method) != bucketId) {
            throw std::runtime_error("bucket file key does not belong to bucket");
        }
    }

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream output(tmp, std::ios::binary);
        if (!output) {
            throw std::runtime_error("failed to open bucket file for writing: " + tmp.string());
        }
        output.write(BucketMagic.data(), static_cast<std::streamsize>(BucketMagic.size()));
        writeU32LE(output, BucketFileVersion, "bucket file");
        writeU32LE(output, static_cast<uint32_t>(soldierCount), "bucket file");
        writeU32LE(output, bucketId, "bucket file");
        writeU32LE(output, bucketCount, "bucket file");
        writeU64LE(output, static_cast<uint64_t>(sortedUniqueKeys.size()), "bucket file");
        writeU64LE(output, StandardRulesetHash, "bucket file");
        for (uint64_t key : sortedUniqueKeys) {
            writeU64LE(output, key, "bucket file");
        }
    }

    std::error_code error;
    std::filesystem::rename(tmp, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(tmp, path, error);
    }
    if (error) {
        std::filesystem::remove(tmp);
        throw std::runtime_error("failed to finalize bucket file: " + path.string());
    }
}

std::vector<uint64_t> readBucketFileChecked(
    const std::filesystem::path& path,
    int expectedSoldierCount,
    uint32_t expectedBucketId,
    uint32_t expectedBucketCount,
    PartitionMethod expectedMethod,
    std::optional<uint64_t> expectedKeyCount = std::nullopt) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open bucket file for reading: " + path.string());
    }

    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != BucketMagic) {
        throw std::runtime_error("invalid bucket file magic: " + path.string());
    }

    const uint32_t version = readU32LE(input, "bucket file");
    if (version != BucketFileVersion) {
        throw std::runtime_error("unsupported bucket file version: " + path.string());
    }
    const int soldierCount = static_cast<int>(readU32LE(input, "bucket file"));
    requireSoldierCountRange(soldierCount);
    if (soldierCount != expectedSoldierCount) {
        throw std::runtime_error("bucket file soldier count does not match manifest: " + path.string());
    }
    const uint32_t bucketId = readU32LE(input, "bucket file");
    if (bucketId != expectedBucketId) {
        throw std::runtime_error("bucket file id does not match manifest: " + path.string());
    }
    const uint32_t bucketCount = readU32LE(input, "bucket file");
    if (bucketCount != expectedBucketCount) {
        throw std::runtime_error("bucket file bucket count does not match manifest: " + path.string());
    }
    const uint64_t keyCount = readU64LE(input, "bucket file");
    if (expectedKeyCount.has_value() && keyCount != *expectedKeyCount) {
        throw std::runtime_error("bucket file key count does not match manifest: " + path.string());
    }
    const uint64_t rulesetHash = readU64LE(input, "bucket file");
    if (rulesetHash != StandardRulesetHash) {
        throw std::runtime_error("bucket file ruleset hash does not match current rules: " + path.string());
    }

    std::vector<uint64_t> keys;
    keys.reserve(static_cast<size_t>(keyCount));
    uint64_t previous = 0;
    for (uint64_t i = 0; i < keyCount; ++i) {
        const uint64_t key = readU64LE(input, "bucket file");
        if (i > 0 && key <= previous) {
            throw std::runtime_error("bucket file keys are not strictly ascending: " + path.string());
        }
        requireKeySoldierCount(key, soldierCount, "bucket file");
        if (partitionBucketForKey(key, bucketCount, expectedMethod) != bucketId) {
            throw std::runtime_error("bucket file key belongs to a different bucket: " + path.string());
        }
        keys.push_back(key);
        previous = key;
    }
    return keys;
}

InputSummary detectInputSummary(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    if (ext == ".s15layer") {
        const KeyListFileSummary summary = validateLayerFile(path);
        return {PartitionSourceKind::Layer, summary.soldierCount, summary.keyCount};
    }
    if (ext == ".s15seed") {
        const KeyListFileSummary summary = validateSeedFile(path);
        return {PartitionSourceKind::Seed, summary.soldierCount, summary.keyCount};
    }
    if (ext == ".s15keys") {
        const KeySetFileInfo info = inspectKeysFile(path);
        return {PartitionSourceKind::Keys, info.soldierCount, info.keyCount};
    }
    throw std::invalid_argument("unsupported partition input extension: " + ext);
}

std::array<char, 8> magicForSource(PartitionSourceKind kind) {
    switch (kind) {
        case PartitionSourceKind::Layer:
            return {'S', '1', '5', 'L', 'Y', 'R', '1', '\0'};
        case PartitionSourceKind::Seed:
            return {'S', '1', '5', 'S', 'E', 'D', '1', '\0'};
        case PartitionSourceKind::Keys:
            return {'S', '1', '5', 'K', 'E', 'Y', '1', '\0'};
    }
    return {'S', '1', '5', 'K', 'E', 'Y', '1', '\0'};
}

const char* streamContext(PartitionSourceKind kind) {
    switch (kind) {
        case PartitionSourceKind::Layer:
            return "layer file";
        case PartitionSourceKind::Seed:
            return "seed file";
        case PartitionSourceKind::Keys:
            return "keys file";
    }
    return "keys file";
}

class InputKeyStream {
public:
    InputKeyStream(const std::filesystem::path& path, PartitionSourceKind kind, int expectedSoldierCount)
        : input_(path, std::ios::binary), path_(path), kind_(kind) {
        if (!input_) {
            throw std::runtime_error("failed to open partition input file: " + path.string());
        }
        std::array<char, 8> magic{};
        input_.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (magic != magicForSource(kind)) {
            throw std::runtime_error("invalid partition input magic: " + path.string());
        }
        const uint32_t version = readU32LE(input_, streamContext(kind_));
        if (version != 1) {
            throw std::runtime_error("unsupported partition input version: " + path.string());
        }
        soldierCount_ = static_cast<int>(readU32LE(input_, streamContext(kind_)));
        requireSoldierCountRange(soldierCount_);
        if (soldierCount_ != expectedSoldierCount) {
            throw std::runtime_error("partition input soldier count changed while scanning");
        }
        count_ = readU64LE(input_, streamContext(kind_));
        const uint64_t rulesetHash = readU64LE(input_, streamContext(kind_));
        if (rulesetHash != StandardRulesetHash) {
            throw std::runtime_error("partition input ruleset hash does not match current rules: " + path.string());
        }
    }

    bool readNext(uint64_t& key) {
        if (read_ >= count_) {
            return false;
        }
        key = readU64LE(input_, streamContext(kind_));
        if (read_ > 0 && key <= previous_) {
            throw std::runtime_error("partition input keys are not strictly ascending: " + path_.string());
        }
        requireKeySoldierCount(key, soldierCount_, streamContext(kind_));
        previous_ = key;
        ++read_;
        return true;
    }

    uint64_t keyCount() const {
        return count_;
    }

private:
    std::ifstream input_;
    std::filesystem::path path_;
    PartitionSourceKind kind_ = PartitionSourceKind::Keys;
    int soldierCount_ = 0;
    uint64_t count_ = 0;
    uint64_t read_ = 0;
    uint64_t previous_ = 0;
};

void writeManifest(
    const std::filesystem::path& outputDir,
    const std::filesystem::path& inputFile,
    PartitionSourceKind sourceKind,
    int soldierCount,
    uint32_t bucketCount,
    PartitionMethod method,
    uint64_t totalKeys,
    const std::vector<PartitionBucketInfo>& buckets) {
    const std::filesystem::path finalPath = partitionManifestPath(outputDir);
    const std::filesystem::path tmpPath = finalPath.string() + ".tmp";
    {
        std::ofstream output(tmpPath);
        if (!output) {
            throw std::runtime_error("failed to open partition manifest for writing: " + tmpPath.string());
        }
        output << "{\n";
        output << "  \"type\": \"" << PartitionType << "\",\n";
        output << "  \"version\": " << PartitionManifestVersion << ",\n";
        output << "  \"rulesetHash\": " << StandardRulesetHash << ",\n";
        output << "  \"sourceFile\": \"" << jsonEscape(relativePathString(outputDir, inputFile)) << "\",\n";
        output << "  \"sourceKind\": \"" << partitionSourceKindToString(sourceKind) << "\",\n";
        output << "  \"soldierCount\": " << soldierCount << ",\n";
        output << "  \"bucketCount\": " << bucketCount << ",\n";
        output << "  \"partitionMethod\": \"" << partitionMethodToString(method) << "\",\n";
        output << "  \"totalKeys\": " << totalKeys << ",\n";
        output << "  \"bucketFiles\": [\n";
        for (size_t i = 0; i < buckets.size(); ++i) {
            const PartitionBucketInfo& bucket = buckets[i];
            output << "    {\n";
            output << "      \"bucketId\": " << bucket.bucketId << ",\n";
            output << "      \"file\": \"" << jsonEscape(bucket.file) << "\",\n";
            output << "      \"keyCount\": " << bucket.keyCount << ",\n";
            output << "      \"minKey\": " << bucket.minKey << ",\n";
            output << "      \"maxKey\": " << bucket.maxKey << "\n";
            output << "    }" << (i + 1 == buckets.size() ? "\n" : ",\n");
        }
        output << "  ]\n";
        output << "}\n";
    }

    std::error_code error;
    std::filesystem::rename(tmpPath, finalPath, error);
    if (error) {
        std::filesystem::remove(finalPath, error);
        error.clear();
        std::filesystem::rename(tmpPath, finalPath, error);
    }
    if (error) {
        std::filesystem::remove(tmpPath);
        throw std::runtime_error("failed to finalize partition manifest: " + finalPath.string());
    }
}

std::vector<PartitionBucketInfo> parseBucketInfos(const std::string& text) {
    const auto arrayColon = jsonKeyColon(text, "bucketFiles");
    if (!arrayColon.has_value()) {
        throw std::runtime_error("partition manifest missing bucketFiles");
    }
    size_t pos = text.find('[', *arrayColon + 1);
    if (pos == std::string::npos) {
        throw std::runtime_error("partition manifest bucketFiles is not an array");
    }
    std::vector<PartitionBucketInfo> buckets;
    while (true) {
        pos = skipWhitespace(text, pos + 1);
        if (pos < text.size() && text[pos] == ',') {
            pos = skipWhitespace(text, pos + 1);
        }
        if (pos >= text.size()) {
            throw std::runtime_error("unterminated bucketFiles array");
        }
        if (text[pos] == ']') {
            return buckets;
        }
        if (text[pos] != '{') {
            throw std::runtime_error("partition manifest bucket entry is not an object");
        }
        const size_t objectStart = pos;
        int depth = 0;
        for (; pos < text.size(); ++pos) {
            if (text[pos] == '{') {
                ++depth;
            } else if (text[pos] == '}') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (pos >= text.size()) {
            throw std::runtime_error("unterminated bucket object");
        }
        const std::string object = text.substr(objectStart, pos - objectStart + 1);
        PartitionBucketInfo bucket;
        bucket.bucketId = static_cast<uint32_t>(requireJsonUint(object, "bucketId"));
        bucket.file = requireJsonString(object, "file");
        bucket.keyCount = requireJsonUint(object, "keyCount");
        bucket.minKey = requireJsonUint(object, "minKey");
        bucket.maxKey = requireJsonUint(object, "maxKey");
        buckets.push_back(bucket);
    }
}

ManifestData readManifest(const std::filesystem::path& dir) {
    const std::filesystem::path path = partitionManifestPath(dir);
    const std::string text = readTextFile(path);
    ManifestData manifest;
    manifest.dir = dir;
    manifest.type = requireJsonString(text, "type");
    if (manifest.type != PartitionType) {
        throw std::runtime_error("partition manifest has unsupported type");
    }
    manifest.version = static_cast<uint32_t>(requireJsonUint(text, "version"));
    if (manifest.version != PartitionManifestVersion) {
        throw std::runtime_error("partition manifest has unsupported version");
    }
    manifest.rulesetHash = requireJsonUint(text, "rulesetHash");
    if (manifest.rulesetHash != StandardRulesetHash) {
        throw std::runtime_error("partition manifest ruleset hash does not match current rules");
    }
    manifest.sourceFile = requireJsonString(text, "sourceFile");
    manifest.sourceKind = partitionSourceKindFromString(requireJsonString(text, "sourceKind"));
    manifest.soldierCount = static_cast<int>(requireJsonUint(text, "soldierCount"));
    requireSoldierCountRange(manifest.soldierCount);
    manifest.bucketCount = static_cast<uint32_t>(requireJsonUint(text, "bucketCount"));
    if (manifest.bucketCount == 0) {
        throw std::runtime_error("partition manifest bucketCount must be greater than zero");
    }
    manifest.partitionMethod = partitionMethodFromString(requireJsonString(text, "partitionMethod"));
    manifest.totalKeys = requireJsonUint(text, "totalKeys");
    manifest.buckets = parseBucketInfos(text);
    if (manifest.buckets.size() != manifest.bucketCount) {
        throw std::runtime_error("partition manifest bucketFiles count does not match bucketCount");
    }
    return manifest;
}

PartitionInspection makeInspection(const ManifestData& manifest) {
    PartitionInspection inspection;
    inspection.totalKeys = manifest.totalKeys;
    inspection.bucketCount = manifest.bucketCount;
    inspection.soldierCount = manifest.soldierCount;
    inspection.partitionMethod = partitionMethodToString(manifest.partitionMethod);
    inspection.buckets = manifest.buckets;
    inspection.minBucketSize = std::numeric_limits<uint64_t>::max();
    uint64_t sum = 0;
    for (const PartitionBucketInfo& bucket : manifest.buckets) {
        inspection.minBucketSize = std::min(inspection.minBucketSize, bucket.keyCount);
        inspection.maxBucketSize = std::max(inspection.maxBucketSize, bucket.keyCount);
        if (bucket.keyCount == 0) {
            ++inspection.emptyBuckets;
        }
        sum += bucket.keyCount;
        inspection.totalBucketFileBytes += fileSizeOrZero(manifest.dir / bucket.file);
    }
    if (manifest.buckets.empty()) {
        inspection.minBucketSize = 0;
    }
    inspection.averageBucketSize =
        manifest.bucketCount == 0 ? 0.0 : static_cast<double>(sum) / static_cast<double>(manifest.bucketCount);
    return inspection;
}

bool samePath(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    std::error_code leftError;
    std::error_code rightError;
    const auto left = std::filesystem::weakly_canonical(lhs, leftError);
    const auto right = std::filesystem::weakly_canonical(rhs, rightError);
    if (!leftError && !rightError) {
        return left == right;
    }
    return std::filesystem::absolute(lhs).lexically_normal() ==
           std::filesystem::absolute(rhs).lexically_normal();
}

void requireCompatiblePartitions(
    const ManifestData& left,
    const ManifestData& right,
    const char* operationName) {
    if (left.rulesetHash != right.rulesetHash) {
        throw std::runtime_error(std::string(operationName) + " requires matching ruleset hashes");
    }
    if (left.soldierCount != right.soldierCount) {
        throw std::runtime_error(std::string(operationName) + " requires matching soldier counts");
    }
    if (left.bucketCount != right.bucketCount) {
        throw std::runtime_error(std::string(operationName) + " requires matching bucket counts");
    }
    if (left.partitionMethod != right.partitionMethod) {
        throw std::runtime_error(std::string(operationName) + " requires matching partition methods");
    }
}

enum class PartitionOp {
    Difference,
    Union,
};

PartitionedKeySetOpStats runPartitionedSetOp(
    const std::filesystem::path& leftPartitionDir,
    const std::filesystem::path& rightPartitionDir,
    const std::filesystem::path& outputPartitionDir,
    bool overwrite,
    PartitionOp op) {
    if (outputPartitionDir.empty()) {
        throw std::invalid_argument("partition operation output dir is required");
    }
    if (samePath(outputPartitionDir, leftPartitionDir) || samePath(outputPartitionDir, rightPartitionDir)) {
        throw std::invalid_argument("partition operation output dir must differ from input dirs");
    }
    if (std::filesystem::exists(outputPartitionDir) && !overwrite) {
        throw std::runtime_error("partition operation output dir already exists; overwrite is required: " +
                                 outputPartitionDir.string());
    }

    const auto started = Clock::now();
    const ManifestData left = readManifest(leftPartitionDir);
    const ManifestData right = readManifest(rightPartitionDir);
    const char* opName = op == PartitionOp::Difference ? "partitionedDifference" : "partitionedUnion";
    requireCompatiblePartitions(left, right, opName);

    std::filesystem::remove_all(outputPartitionDir);
    std::filesystem::create_directories(outputPartitionDir);

    PartitionedKeySetOpStats stats;
    stats.leftKeys = left.totalKeys;
    stats.rightKeys = right.totalKeys;
    stats.bucketCount = left.bucketCount;
    stats.soldierCount = left.soldierCount;
    stats.partitionMethod = left.partitionMethod;

    std::vector<PartitionBucketInfo> buckets;
    buckets.reserve(left.bucketCount);
    for (uint32_t bucket = 0; bucket < left.bucketCount; ++bucket) {
        const PartitionBucketInfo& leftInfo = left.buckets.at(bucket);
        const PartitionBucketInfo& rightInfo = right.buckets.at(bucket);
        if (leftInfo.bucketId != bucket || rightInfo.bucketId != bucket) {
            throw std::runtime_error(std::string(opName) + " manifest bucket order is invalid");
        }
        const std::vector<uint64_t> leftKeys = readBucketFileChecked(
            left.dir / leftInfo.file,
            left.soldierCount,
            bucket,
            left.bucketCount,
            left.partitionMethod,
            leftInfo.keyCount);
        const std::vector<uint64_t> rightKeys = readBucketFileChecked(
            right.dir / rightInfo.file,
            right.soldierCount,
            bucket,
            right.bucketCount,
            right.partitionMethod,
            rightInfo.keyCount);

        std::vector<uint64_t> outputKeys;
        if (op == PartitionOp::Difference) {
            outputKeys.reserve(leftKeys.size());
            std::set_difference(
                leftKeys.begin(),
                leftKeys.end(),
                rightKeys.begin(),
                rightKeys.end(),
                std::back_inserter(outputKeys));
        } else {
            outputKeys.reserve(leftKeys.size() + rightKeys.size());
            std::set_union(
                leftKeys.begin(),
                leftKeys.end(),
                rightKeys.begin(),
                rightKeys.end(),
                std::back_inserter(outputKeys));
        }

        const std::string fileName = bucketFileName(bucket);
        writeBucketFile(
            outputPartitionDir / fileName,
            left.soldierCount,
            bucket,
            left.bucketCount,
            left.partitionMethod,
            outputKeys);

        PartitionBucketInfo info;
        info.bucketId = bucket;
        info.file = fileName;
        info.keyCount = static_cast<uint64_t>(outputKeys.size());
        if (!outputKeys.empty()) {
            info.minKey = outputKeys.front();
            info.maxKey = outputKeys.back();
        }
        stats.outputKeys += info.keyCount;
        buckets.push_back(info);
    }

    writeManifest(
        outputPartitionDir,
        left.dir,
        left.sourceKind,
        left.soldierCount,
        left.bucketCount,
        left.partitionMethod,
        stats.outputKeys,
        buckets);
    stats.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    return stats;
}

}  // namespace

const char* partitionSourceKindToString(PartitionSourceKind kind) {
    switch (kind) {
        case PartitionSourceKind::Layer:
            return "layer";
        case PartitionSourceKind::Seed:
            return "seed";
        case PartitionSourceKind::Keys:
            return "keys";
    }
    return "keys";
}

PartitionSourceKind partitionSourceKindFromString(const std::string& text) {
    if (text == "layer") {
        return PartitionSourceKind::Layer;
    }
    if (text == "seed") {
        return PartitionSourceKind::Seed;
    }
    if (text == "keys") {
        return PartitionSourceKind::Keys;
    }
    throw std::runtime_error("unknown partition source kind: " + text);
}

const char* partitionMethodToString(PartitionMethod method) {
    switch (method) {
        case PartitionMethod::KeyMod:
            return "key_mod";
        case PartitionMethod::Splitmix64Mod:
            return "splitmix64_mod";
    }
    return "splitmix64_mod";
}

PartitionMethod partitionMethodFromString(const std::string& text) {
    if (text == "key_mod") {
        return PartitionMethod::KeyMod;
    }
    if (text == "splitmix64_mod") {
        return PartitionMethod::Splitmix64Mod;
    }
    throw std::runtime_error("unknown partition method: " + text);
}

const char* partitionBenchmarkModeToString(PartitionBenchmarkMode mode) {
    switch (mode) {
        case PartitionBenchmarkMode::Existing:
            return "existing";
        case PartitionBenchmarkMode::Missing:
            return "missing";
        case PartitionBenchmarkMode::Mixed:
            return "mixed";
    }
    return "existing";
}

PartitionBenchmarkMode partitionBenchmarkModeFromString(const std::string& text) {
    if (text == "existing") {
        return PartitionBenchmarkMode::Existing;
    }
    if (text == "missing") {
        return PartitionBenchmarkMode::Missing;
    }
    if (text == "mixed") {
        return PartitionBenchmarkMode::Mixed;
    }
    throw std::invalid_argument("--benchmark-mode must be existing, missing, or mixed");
}

uint32_t partitionBucketForKey(uint64_t key, uint32_t bucketCount) {
    return partitionBucketForKey(key, bucketCount, DefaultPartitionMethod);
}

uint32_t partitionBucketForKey(uint64_t key, uint32_t bucketCount, PartitionMethod method) {
    if (bucketCount == 0) {
        throw std::invalid_argument("bucketCount must be greater than zero");
    }
    switch (method) {
        case PartitionMethod::KeyMod:
            return static_cast<uint32_t>(key % bucketCount);
        case PartitionMethod::Splitmix64Mod:
            return static_cast<uint32_t>(splitmix64(key) % bucketCount);
    }
    return static_cast<uint32_t>(splitmix64(key) % bucketCount);
}

std::filesystem::path partitionManifestPath(const std::filesystem::path& partitionDir) {
    return partitionDir / "partition.json";
}

std::filesystem::path partitionBucketPath(const std::filesystem::path& partitionDir, uint32_t bucketId) {
    return partitionDir / bucketFileName(bucketId);
}

PartitionedKeySetStats buildPartitionedKeySet(const PartitionedKeySetOptions& options) {
    if (options.inputFile.empty()) {
        throw std::invalid_argument("partition input file is required");
    }
    if (options.outputDir.empty()) {
        throw std::invalid_argument("partition output dir is required");
    }
    if (options.bucketCount == 0) {
        throw std::invalid_argument("partition bucket count must be greater than zero");
    }
    if (std::filesystem::exists(options.outputDir) && !options.overwrite) {
        throw std::runtime_error("partition output dir already exists; pass --overwrite to replace it: " + options.outputDir.string());
    }

    const auto started = Clock::now();
    const InputSummary input = detectInputSummary(options.inputFile);
    std::filesystem::remove_all(options.outputDir);
    std::filesystem::create_directories(options.outputDir);
    const std::filesystem::path tmpDir = options.outputDir / "tmp";
    std::filesystem::create_directories(tmpDir);

    std::vector<std::unique_ptr<std::ofstream>> rawOutputs;
    rawOutputs.reserve(options.bucketCount);
    for (uint32_t bucket = 0; bucket < options.bucketCount; ++bucket) {
        auto output = std::make_unique<std::ofstream>(tmpDir / ("bucket-" + std::to_string(bucket) + ".raw"), std::ios::binary);
        if (!*output) {
            throw std::runtime_error("failed to open raw bucket file");
        }
        rawOutputs.push_back(std::move(output));
    }

    PartitionedKeySetStats stats;
    stats.bucketCount = options.bucketCount;
    stats.soldierCount = input.soldierCount;
    stats.partitionMethod = options.method;
    stats.sourceKind = input.kind;
    stats.bucketSizes.assign(options.bucketCount, 0);

    uint64_t scanned = 0;
    InputKeyStream stream(options.inputFile, input.kind, input.soldierCount);
    uint64_t key = 0;
    while (stream.readNext(key)) {
        const uint32_t bucket = partitionBucketForKey(key, options.bucketCount, options.method);
        writeRawBucketKey(*rawOutputs[bucket], key);
        ++scanned;
        if (options.progressInterval != 0 && scanned % options.progressInterval == 0) {
            if (options.progress) {
                options.progress(scanned);
            }
        }
    }
    for (auto& output : rawOutputs) {
        output->close();
    }
    rawOutputs.clear();
    stats.inputKeys = scanned;
    if (input.keyCount != scanned) {
        throw std::runtime_error("partition input scan count does not match input header");
    }

    std::vector<PartitionBucketInfo> buckets;
    buckets.reserve(options.bucketCount);
    for (uint32_t bucket = 0; bucket < options.bucketCount; ++bucket) {
        const std::filesystem::path rawPath = tmpDir / ("bucket-" + std::to_string(bucket) + ".raw");
        std::vector<uint64_t> keys = readRawBucketFile(rawPath);
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        const std::string fileName = bucketFileName(bucket);
        writeBucketFile(options.outputDir / fileName, input.soldierCount, bucket, options.bucketCount, options.method, keys);

        PartitionBucketInfo info;
        info.bucketId = bucket;
        info.file = fileName;
        info.keyCount = static_cast<uint64_t>(keys.size());
        if (!keys.empty()) {
            info.minKey = keys.front();
            info.maxKey = keys.back();
        }
        stats.bucketSizes[bucket] = info.keyCount;
        stats.outputKeys += info.keyCount;
        buckets.push_back(info);
    }

    stats.minBucketSize = stats.bucketSizes.empty() ? 0 : *std::min_element(stats.bucketSizes.begin(), stats.bucketSizes.end());
    stats.maxBucketSize = stats.bucketSizes.empty() ? 0 : *std::max_element(stats.bucketSizes.begin(), stats.bucketSizes.end());
    stats.emptyBuckets = static_cast<uint64_t>(std::count(stats.bucketSizes.begin(), stats.bucketSizes.end(), uint64_t{0}));
    stats.averageBucketSize =
        options.bucketCount == 0 ? 0.0 : static_cast<double>(stats.outputKeys) / static_cast<double>(options.bucketCount);

    writeManifest(
        options.outputDir,
        options.inputFile,
        input.kind,
        input.soldierCount,
        options.bucketCount,
        options.method,
        stats.outputKeys,
        buckets);
    std::filesystem::remove_all(tmpDir);
    stats.buildSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    return stats;
}

PartitionValidationResult validatePartitionedKeySet(const std::filesystem::path& partitionDir) {
    const ManifestData manifest = readManifest(partitionDir);
    uint64_t total = 0;
    for (const PartitionBucketInfo& bucket : manifest.buckets) {
        if (bucket.bucketId >= manifest.bucketCount) {
            throw std::runtime_error("partition manifest bucket id is out of range");
        }
        const std::vector<uint64_t> keys = readBucketFileChecked(
            manifest.dir / bucket.file,
            manifest.soldierCount,
            bucket.bucketId,
            manifest.bucketCount,
            manifest.partitionMethod,
            bucket.keyCount);
        if (bucket.keyCount == 0) {
            if (bucket.minKey != 0 || bucket.maxKey != 0) {
                throw std::runtime_error("empty bucket must have minKey=maxKey=0");
            }
        } else if (keys.front() != bucket.minKey || keys.back() != bucket.maxKey) {
            throw std::runtime_error("bucket min/max does not match manifest");
        }
        total += static_cast<uint64_t>(keys.size());
    }
    if (total != manifest.totalKeys) {
        throw std::runtime_error("partition key count sum does not match manifest totalKeys");
    }
    return {true, total, manifest.bucketCount, manifest.soldierCount};
}

PartitionInspection inspectPartitionedKeySet(const std::filesystem::path& partitionDir) {
    const ManifestData manifest = readManifest(partitionDir);
    return makeInspection(manifest);
}

PartitionedKeySetOpStats partitionedDifference(
    const std::filesystem::path& leftPartitionDir,
    const std::filesystem::path& rightPartitionDir,
    const std::filesystem::path& outputPartitionDir,
    bool overwrite) {
    return runPartitionedSetOp(leftPartitionDir, rightPartitionDir, outputPartitionDir, overwrite, PartitionOp::Difference);
}

PartitionedKeySetOpStats partitionedUnion(
    const std::filesystem::path& leftPartitionDir,
    const std::filesystem::path& rightPartitionDir,
    const std::filesystem::path& outputPartitionDir,
    bool overwrite) {
    return runPartitionedSetOp(leftPartitionDir, rightPartitionDir, outputPartitionDir, overwrite, PartitionOp::Union);
}

PartitionLookupBenchmark benchmarkPartitionedKeySet(
    const std::filesystem::path& partitionDir,
    uint64_t sampleCount,
    PartitionBenchmarkMode mode,
    uint32_t cacheBuckets) {
    PartitionedKeySetReaderOptions readerOptions;
    readerOptions.partitionDir = partitionDir;
    readerOptions.maxCachedBuckets = cacheBuckets;
    PartitionedKeySetReader sampleReader(readerOptions);

    std::vector<uint64_t> existing = sampleReader.sampleExistingKeys(sampleCount);
    std::vector<uint64_t> queries;
    queries.reserve(existing.size());
    const uint64_t cannonFlip = uint64_t{1} << 24;

    auto missingKeyFor = [&](uint64_t key) {
        return key ^ cannonFlip;
    };

    switch (mode) {
        case PartitionBenchmarkMode::Existing:
            queries = std::move(existing);
            break;
        case PartitionBenchmarkMode::Missing:
            for (uint64_t key : existing) {
                queries.push_back(missingKeyFor(key));
            }
            break;
        case PartitionBenchmarkMode::Mixed:
            for (size_t i = 0; i < existing.size(); ++i) {
                queries.push_back((i % 2 == 0) ? existing[i] : missingKeyFor(existing[i]));
            }
            break;
    }

    PartitionedKeySetReader queryReader(readerOptions);
    const BatchLookupStats batch = queryReader.containsBatchStats(std::span<const uint64_t>(queries.data(), queries.size()));
    PartitionLookupBenchmark result;
    result.requestedSamples = sampleCount;
    result.executedLookups = batch.queryKeys;
    result.found = batch.foundKeys;
    result.missing = batch.missingKeys;
    result.bucketsTouched = batch.bucketsTouched;
    result.bucketLoads = batch.bucketLoads;
    result.seconds = batch.seconds;
    result.lookupsPerSecond = batch.lookupsPerSecond;
    result.mode = mode;
    result.cacheBuckets = cacheBuckets;
    return result;
}

struct PartitionedKeySetReader::Impl {
    explicit Impl(PartitionedKeySetReaderOptions options)
        : manifest(readManifest(options.partitionDir)),
          maxCachedBuckets(options.maxCachedBuckets) {
        if (maxCachedBuckets == 0) {
            throw std::invalid_argument("partition cache buckets must be greater than zero");
        }
        bucketSizes.reserve(manifest.buckets.size());
        for (const PartitionBucketInfo& bucket : manifest.buckets) {
            bucketSizes.push_back(bucket.keyCount);
        }
    }

    const std::vector<uint64_t>& loadBucket(uint32_t bucketId) const {
        auto found = cache.find(bucketId);
        if (found != cache.end()) {
            lru.splice(lru.begin(), lru, found->second.lruIt);
            return found->second.keys;
        }
        const PartitionBucketInfo& bucket = manifest.buckets.at(bucketId);
        std::vector<uint64_t> keys = readBucketFileChecked(
            manifest.dir / bucket.file,
            manifest.soldierCount,
            bucketId,
            manifest.bucketCount,
            manifest.partitionMethod,
            bucket.keyCount);
        ++bucketLoads;

        lru.push_front(bucketId);
        CacheEntry entry;
        entry.keys = std::move(keys);
        entry.lruIt = lru.begin();
        auto inserted = cache.emplace(bucketId, std::move(entry));
        while (cache.size() > maxCachedBuckets) {
            const uint32_t evicted = lru.back();
            lru.pop_back();
            cache.erase(evicted);
        }
        return inserted.first->second.keys;
    }

    uint32_t bucketForKey(uint64_t key) const {
        return partitionBucketForKey(key, manifest.bucketCount, manifest.partitionMethod);
    }

    struct CacheEntry {
        std::vector<uint64_t> keys;
        std::list<uint32_t>::iterator lruIt;
    };

    ManifestData manifest;
    std::vector<uint64_t> bucketSizes;
    uint32_t maxCachedBuckets = 32;
    mutable uint64_t bucketLoads = 0;
    mutable std::list<uint32_t> lru;
    mutable std::unordered_map<uint32_t, CacheEntry> cache;
};

PartitionedKeySetReader::PartitionedKeySetReader(std::filesystem::path partitionDir)
    : PartitionedKeySetReader(PartitionedKeySetReaderOptions{std::move(partitionDir), 32}) {}

PartitionedKeySetReader::PartitionedKeySetReader(PartitionedKeySetReaderOptions options)
    : impl_(new Impl(std::move(options))) {}

PartitionedKeySetReader::~PartitionedKeySetReader() {
    delete impl_;
}

bool PartitionedKeySetReader::contains(uint64_t key) const {
    requireKeySoldierCount(key, impl_->manifest.soldierCount, "partition lookup");
    const uint32_t bucket = impl_->bucketForKey(key);
    const std::vector<uint64_t>& keys = impl_->loadBucket(bucket);
    return std::binary_search(keys.begin(), keys.end(), key);
}

std::vector<uint8_t> PartitionedKeySetReader::containsBatch(const std::vector<uint64_t>& keys) const {
    std::vector<uint8_t> result(keys.size(), uint8_t{0});
    std::vector<std::vector<size_t>> indicesByBucket(impl_->manifest.bucketCount);
    for (size_t i = 0; i < keys.size(); ++i) {
        requireKeySoldierCount(keys[i], impl_->manifest.soldierCount, "partition lookup");
        indicesByBucket[impl_->bucketForKey(keys[i])].push_back(i);
    }
    for (uint32_t bucket = 0; bucket < impl_->manifest.bucketCount; ++bucket) {
        if (indicesByBucket[bucket].empty()) {
            continue;
        }
        const std::vector<uint64_t>& bucketKeys = impl_->loadBucket(bucket);
        for (size_t index : indicesByBucket[bucket]) {
            result[index] = std::binary_search(bucketKeys.begin(), bucketKeys.end(), keys[index]) ? uint8_t{1} : uint8_t{0};
        }
    }
    return result;
}

BatchLookupStats PartitionedKeySetReader::containsBatchStats(std::span<const uint64_t> keys) const {
    const uint64_t loadsBefore = impl_->bucketLoads;
    const auto started = Clock::now();
    BatchLookupStats stats;
    stats.queryKeys = static_cast<uint64_t>(keys.size());
    std::vector<std::vector<uint64_t>> keysByBucket(impl_->manifest.bucketCount);
    for (uint64_t key : keys) {
        requireKeySoldierCount(key, impl_->manifest.soldierCount, "partition lookup");
        keysByBucket[impl_->bucketForKey(key)].push_back(key);
    }
    for (uint32_t bucket = 0; bucket < impl_->manifest.bucketCount; ++bucket) {
        if (keysByBucket[bucket].empty()) {
            continue;
        }
        ++stats.bucketsTouched;
        const std::vector<uint64_t>& bucketKeys = impl_->loadBucket(bucket);
        for (uint64_t key : keysByBucket[bucket]) {
            if (std::binary_search(bucketKeys.begin(), bucketKeys.end(), key)) {
                ++stats.foundKeys;
            } else {
                ++stats.missingKeys;
            }
        }
    }
    stats.bucketLoads = impl_->bucketLoads - loadsBefore;
    stats.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    stats.lookupsPerSecond = stats.seconds <= 0.0 ? 0.0 : static_cast<double>(stats.queryKeys) / stats.seconds;
    return stats;
}

uint64_t PartitionedKeySetReader::keyCount() const {
    return impl_->manifest.totalKeys;
}

uint32_t PartitionedKeySetReader::bucketCount() const {
    return impl_->manifest.bucketCount;
}

int PartitionedKeySetReader::soldierCount() const {
    return impl_->manifest.soldierCount;
}

PartitionMethod PartitionedKeySetReader::partitionMethod() const {
    return impl_->manifest.partitionMethod;
}

uint32_t PartitionedKeySetReader::bucketForKey(uint64_t key) const {
    return impl_->bucketForKey(key);
}

uint64_t PartitionedKeySetReader::bucketLoadCount() const {
    return impl_->bucketLoads;
}

std::vector<uint64_t> PartitionedKeySetReader::bucketSizes() const {
    return impl_->bucketSizes;
}

std::vector<uint64_t> PartitionedKeySetReader::sampleExistingKeys(uint64_t sampleCount) const {
    std::vector<uint64_t> samples;
    if (sampleCount == 0) {
        return samples;
    }
    samples.reserve(static_cast<size_t>(std::min<uint64_t>(sampleCount, impl_->manifest.totalKeys)));
    const uint64_t nonEmptyBuckets = static_cast<uint64_t>(
        std::count_if(impl_->manifest.buckets.begin(), impl_->manifest.buckets.end(), [](const PartitionBucketInfo& bucket) {
            return bucket.keyCount != 0;
        }));
    if (nonEmptyBuckets == 0) {
        return samples;
    }
    const uint64_t perBucket = std::max<uint64_t>(uint64_t{1}, (sampleCount + nonEmptyBuckets - 1) / nonEmptyBuckets);
    for (const PartitionBucketInfo& bucket : impl_->manifest.buckets) {
        if (bucket.keyCount == 0) {
            continue;
        }
        const std::vector<uint64_t>& keys = impl_->loadBucket(bucket.bucketId);
        const uint64_t take = std::min<uint64_t>(perBucket, static_cast<uint64_t>(keys.size()));
        for (uint64_t i = 0; i < take; ++i) {
            samples.push_back(keys[static_cast<size_t>(i)]);
            if (samples.size() >= sampleCount) {
                break;
            }
        }
        if (samples.size() >= sampleCount) {
            break;
        }
    }
    return samples;
}

}  // namespace sanpao15

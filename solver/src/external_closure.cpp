#include "sanpao15/external_closure.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sanpao15/bitboard.h"
#include "sanpao15/external_keyset.h"
#include "sanpao15/layered.h"
#include "sanpao15/partitioned_keyset.h"
#include "sanpao15/rules.h"
#include "sanpao15/table.h"

namespace sanpao15 {

namespace {

constexpr std::array<char, 8> KeysMagic{'S', '1', '5', 'K', 'E', 'Y', '1', '\0'};
constexpr uint32_t KeysFileVersion = 1;
using Clock = std::chrono::steady_clock;

struct ClosureCheckpointState {
    uint32_t checkpointVersion = 1;
    std::string checkpointKind = "iteration-boundary";
    bool requiresTransientRuns = false;
    bool partitionedClosure = false;
    uint32_t closurePartitionBuckets = 0;
    std::string closurePartitionMethod = "splitmix64_mod";
    int soldierCount = 0;
    std::string status = "running";
    bool complete = false;
    bool truncated = false;
    std::string truncationReason = "none";
    uint64_t seedStates = 0;
    uint64_t iterations = 0;
    uint64_t expandedStates = 0;
    uint64_t visitedStates = 0;
    uint64_t frontierStates = 0;
    uint64_t sameEdges = 0;
    uint64_t captureEdges = 0;
    uint64_t generatedCandidateKeys = 0;
    uint64_t newFrontierStates = 0;
    uint64_t duplicateOrVisitedCandidates = 0;
    uint64_t nextSeedStates = 0;
    uint64_t duplicateNextSeeds = 0;
    std::string visitedFile = "visited.s15keys";
    std::string frontierFile = "frontier.s15keys";
    std::string remainingFrontierFile = "frontier.s15keys";
    std::string nextSeedFile = "next-seeds.s15keys";
    std::string baseVisitedFile = "base-visited.s15keys";
    uint64_t baseVisitedStates = 0;
    std::string pendingCandidateFile = "pending-candidates.s15keys";
    uint64_t pendingCandidateStates = 0;
    bool pendingIteration = false;
};

void requireSoldierCountRange(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::invalid_argument("soldier count must be in 0..15");
    }
}

void requireKeySoldierCount(uint64_t key, int soldierCount, const char* kind) {
    const Position pos = unpackPosition(key);
    const int actualSoldiers = popcount25(pos.soldiers);
    if (actualSoldiers != soldierCount) {
        throw std::runtime_error(std::string(kind) + " key has unexpected soldier count");
    }
}

void writeU8(std::ostream& output, uint8_t value) {
    output.put(static_cast<char>(value));
    if (!output) {
        throw std::runtime_error("failed to write keys byte");
    }
}

void writeU32LE(std::ostream& output, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        writeU8(output, static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

void writeU64LE(std::ostream& output, uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        writeU8(output, static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

uint8_t readU8(std::istream& input) {
    const int value = input.get();
    if (value == std::char_traits<char>::eof()) {
        throw std::runtime_error("unexpected end of keys file");
    }
    return static_cast<uint8_t>(value);
}

uint32_t readU32LE(std::istream& input) {
    uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<uint32_t>(readU8(input)) << shift;
    }
    return value;
}

uint64_t readU64LE(std::istream& input) {
    uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<uint64_t>(readU8(input)) << shift;
    }
    return value;
}

class KeysReader {
public:
    explicit KeysReader(const std::filesystem::path& path, std::optional<int> expectedSoldierCount = std::nullopt)
        : input_(path, std::ios::binary), path_(path) {
        if (!input_) {
            throw std::runtime_error("failed to open keys file for reading: " + path.string());
        }

        std::array<char, 8> magic{};
        input_.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (magic != KeysMagic) {
            throw std::runtime_error("invalid keys file magic: " + path.string());
        }

        const uint32_t version = readU32LE(input_);
        if (version != KeysFileVersion) {
            throw std::runtime_error("unsupported keys file version: " + path.string());
        }

        soldierCount_ = static_cast<int>(readU32LE(input_));
        requireSoldierCountRange(soldierCount_);
        if (expectedSoldierCount.has_value() && soldierCount_ != *expectedSoldierCount) {
            throw std::runtime_error("keys file soldier count does not match expected layer: " + path.string());
        }

        count_ = readU64LE(input_);
        const uint64_t rulesetHash = readU64LE(input_);
        if (rulesetHash != StandardRulesetHash) {
            throw std::runtime_error("keys file ruleset hash does not match current rules: " + path.string());
        }
    }

    bool readNext(uint64_t& key) {
        if (read_ >= count_) {
            return false;
        }
        key = readU64LE(input_);
        if (read_ > 0 && key <= previous_) {
            throw std::runtime_error("keys file keys are not strictly ascending: " + path_.string());
        }
        requireKeySoldierCount(key, soldierCount_, "keys file");
        previous_ = key;
        ++read_;
        return true;
    }

    int soldierCount() const {
        return soldierCount_;
    }

    uint64_t keyCount() const {
        return count_;
    }

private:
    std::ifstream input_;
    std::filesystem::path path_;
    int soldierCount_ = 0;
    uint64_t count_ = 0;
    uint64_t read_ = 0;
    uint64_t previous_ = 0;
};

class KeysWriter {
public:
    KeysWriter(const std::filesystem::path& path, int soldierCount)
        : finalPath_(path), tmpPath_(path.string() + ".tmp"), soldierCount_(soldierCount) {
        requireSoldierCountRange(soldierCount_);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        output_.open(tmpPath_, std::ios::binary);
        if (!output_) {
            throw std::runtime_error("failed to open temporary keys file for writing: " + tmpPath_.string());
        }
        output_.write(KeysMagic.data(), static_cast<std::streamsize>(KeysMagic.size()));
        writeU32LE(output_, KeysFileVersion);
        writeU32LE(output_, static_cast<uint32_t>(soldierCount_));
        countOffset_ = output_.tellp();
        writeU64LE(output_, 0);
        writeU64LE(output_, StandardRulesetHash);
    }

    void write(uint64_t key) {
        if (finished_) {
            throw std::logic_error("cannot write to finished keys file");
        }
        if (count_ > 0 && key <= previous_) {
            throw std::runtime_error("keys writer received non-ascending or duplicate key");
        }
        requireKeySoldierCount(key, soldierCount_, "keys writer");
        writeU64LE(output_, key);
        previous_ = key;
        ++count_;
    }

    uint64_t finish() {
        if (finished_) {
            return count_;
        }
        output_.seekp(countOffset_);
        writeU64LE(output_, count_);
        output_.close();
        if (!output_) {
            throw std::runtime_error("failed to finalize keys file contents: " + tmpPath_.string());
        }

        std::error_code error;
        std::filesystem::rename(tmpPath_, finalPath_, error);
        if (error) {
            std::filesystem::remove(finalPath_, error);
            error.clear();
            std::filesystem::rename(tmpPath_, finalPath_, error);
        }
        if (error) {
            std::filesystem::remove(tmpPath_);
            throw std::runtime_error("failed to finalize keys file: " + finalPath_.string());
        }
        finished_ = true;
        return count_;
    }

    ~KeysWriter() {
        if (!finished_) {
            output_.close();
            std::error_code error;
            std::filesystem::remove(tmpPath_, error);
        }
    }

private:
    std::filesystem::path finalPath_;
    std::filesystem::path tmpPath_;
    int soldierCount_ = 0;
    std::ofstream output_;
    std::streampos countOffset_{};
    uint64_t count_ = 0;
    uint64_t previous_ = 0;
    bool finished_ = false;
};

std::vector<uint64_t> sortedUniqueChecked(std::vector<uint64_t> keys, int soldierCount, const char* kind) {
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    for (uint64_t key : keys) {
        requireKeySoldierCount(key, soldierCount, kind);
    }
    return keys;
}

std::filesystem::path workPath(
    const std::filesystem::path& workDir,
    const std::string& name,
    uint64_t iteration) {
    return workDir / (name + "-" + std::to_string(iteration) + ".s15keys");
}

void removeIfExists(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
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
        throw std::runtime_error("failed to open closure checkpoint manifest: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool isJsonWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::optional<size_t> jsonKeyColon(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t keyPos = text.find(needle);
    while (keyPos != std::string::npos) {
        size_t colon = keyPos + needle.size();
        while (colon < text.size() && isJsonWhitespace(text[colon])) {
            ++colon;
        }
        if (colon < text.size() && text[colon] == ':') {
            return colon;
        }
        keyPos = text.find(needle, keyPos + 1);
    }
    return std::nullopt;
}

std::optional<std::string> jsonStringValue(const std::string& text, const std::string& key) {
    const auto colonValue = jsonKeyColon(text, key);
    if (!colonValue.has_value()) {
        return std::nullopt;
    }
    const size_t colon = *colonValue;
    size_t pos = text.find('"', colon + 1);
    if (pos == std::string::npos) {
        return std::nullopt;
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
    return std::nullopt;
}

std::optional<uint64_t> jsonUintValue(const std::string& text, const std::string& key) {
    const auto colonValue = jsonKeyColon(text, key);
    if (!colonValue.has_value()) {
        return std::nullopt;
    }
    const size_t colon = *colonValue;
    size_t pos = text.find_first_not_of(" \t\r\n", colon + 1);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const size_t start = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        ++pos;
    }
    if (pos == start) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(std::stoull(text.substr(start, pos - start)));
}

std::optional<bool> jsonBoolValue(const std::string& text, const std::string& key) {
    const auto colonValue = jsonKeyColon(text, key);
    if (!colonValue.has_value()) {
        return std::nullopt;
    }
    const size_t colon = *colonValue;
    const size_t pos = text.find_first_not_of(" \t\r\n", colon + 1);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    if (text.compare(pos, 4, "true") == 0) {
        return true;
    }
    if (text.compare(pos, 5, "false") == 0) {
        return false;
    }
    return std::nullopt;
}

uint64_t requireJsonUint(const std::string& text, const std::string& key) {
    const auto value = jsonUintValue(text, key);
    if (!value.has_value()) {
        throw std::runtime_error("closure checkpoint manifest missing numeric field: " + key);
    }
    return *value;
}

bool requireJsonBool(const std::string& text, const std::string& key) {
    const auto value = jsonBoolValue(text, key);
    if (!value.has_value()) {
        throw std::runtime_error("closure checkpoint manifest missing boolean field: " + key);
    }
    return *value;
}

std::string requireJsonString(const std::string& text, const std::string& key) {
    const auto value = jsonStringValue(text, key);
    if (!value.has_value()) {
        throw std::runtime_error("closure checkpoint manifest missing string field: " + key);
    }
    return *value;
}

std::filesystem::path defaultCheckpointDir(const ExternalClosureOptions& options) {
    return options.checkpointDir.empty() ? options.workDir : options.checkpointDir;
}

std::filesystem::path checkpointVisitedPath(const std::filesystem::path& dir) {
    return dir / "visited.s15keys";
}

std::filesystem::path checkpointFrontierPath(const std::filesystem::path& dir) {
    return dir / "frontier.s15keys";
}

std::filesystem::path checkpointRemainingFrontierPath(const std::filesystem::path& dir) {
    return dir / "remaining-frontier.s15keys";
}

std::filesystem::path checkpointNextSeedPath(const std::filesystem::path& dir) {
    return dir / "next-seeds.s15keys";
}

std::filesystem::path checkpointPendingCandidatePath(const std::filesystem::path& dir) {
    return dir / "pending-candidates.s15keys";
}

std::filesystem::path checkpointBaseVisitedPath(const std::filesystem::path& dir) {
    return dir / "base-visited.s15keys";
}

std::filesystem::path checkpointPartitionDir(const std::filesystem::path& dir, const std::string& name) {
    return dir / "partitioned" / name;
}

PartitionMethod closurePartitionMethodFromString(const std::string& text) {
    return partitionMethodFromString(text);
}

void writeCheckpointPartition(
    const std::filesystem::path& keysFile,
    const std::filesystem::path& partitionDir,
    uint32_t bucketCount,
    PartitionMethod method) {
    PartitionedKeySetOptions options;
    options.inputFile = keysFile;
    options.outputDir = partitionDir;
    options.bucketCount = bucketCount;
    options.method = method;
    options.overwrite = true;
    (void)buildPartitionedKeySet(options);
}

std::string truncationReasonString(const ExternalClosureStats& stats) {
    if (stats.truncatedByMaxStates) {
        return "max-states";
    }
    if (stats.truncatedByMaxExpandedStates) {
        return "max-expanded";
    }
    if (stats.truncatedByMaxIterations) {
        return "max-iterations";
    }
    return "none";
}

void copyKeysFile(const std::filesystem::path& from, const std::filesystem::path& to, int soldierCount) {
    KeysReader reader(from, soldierCount);
    KeysWriter writer(to, soldierCount);
    uint64_t key = 0;
    while (reader.readNext(key)) {
        writer.write(key);
    }
    writer.finish();
}

void writeCheckpointManifest(
    const std::filesystem::path& checkpointDir,
    const ClosureCheckpointState& state) {
    std::filesystem::create_directories(checkpointDir);
    const std::filesystem::path finalPath = closureCheckpointManifestPath(checkpointDir);
    const std::filesystem::path tmpPath = finalPath.string() + ".tmp";
    {
        std::ofstream output(tmpPath);
        if (!output) {
            throw std::runtime_error("failed to open closure checkpoint manifest for writing: " + tmpPath.string());
        }
        output << "{\n";
        output << "  \"format\": \"sanpao15-closure-checkpoint-v2\",\n";
        output << "  \"checkpointVersion\": " << state.checkpointVersion << ",\n";
        output << "  \"checkpointKind\": \"" << jsonEscape(state.checkpointKind) << "\",\n";
        output << "  \"requiresTransientRuns\": " << (state.requiresTransientRuns ? "true" : "false") << ",\n";
        output << "  \"partitionedClosure\": " << (state.partitionedClosure ? "true" : "false") << ",\n";
        output << "  \"closurePartitionBuckets\": " << state.closurePartitionBuckets << ",\n";
        output << "  \"closurePartitionMethod\": \"" << jsonEscape(state.closurePartitionMethod) << "\",\n";
        output << "  \"rulesetHash\": " << StandardRulesetHash << ",\n";
        output << "  \"soldierCount\": " << state.soldierCount << ",\n";
        output << "  \"status\": \"" << jsonEscape(state.status) << "\",\n";
        output << "  \"complete\": " << (state.complete ? "true" : "false") << ",\n";
        output << "  \"truncated\": " << (state.truncated ? "true" : "false") << ",\n";
        output << "  \"truncationReason\": \"" << jsonEscape(state.truncationReason) << "\",\n";
        output << "  \"seedStates\": " << state.seedStates << ",\n";
        output << "  \"iterations\": " << state.iterations << ",\n";
        output << "  \"expandedStates\": " << state.expandedStates << ",\n";
        output << "  \"visitedStates\": " << state.visitedStates << ",\n";
        output << "  \"frontierStates\": " << state.frontierStates << ",\n";
        output << "  \"sameEdges\": " << state.sameEdges << ",\n";
        output << "  \"captureEdges\": " << state.captureEdges << ",\n";
        output << "  \"generatedCandidateKeys\": " << state.generatedCandidateKeys << ",\n";
        output << "  \"newFrontierStates\": " << state.newFrontierStates << ",\n";
        output << "  \"duplicateOrVisitedCandidates\": " << state.duplicateOrVisitedCandidates << ",\n";
        output << "  \"nextSeedStates\": " << state.nextSeedStates << ",\n";
        output << "  \"duplicateNextSeeds\": " << state.duplicateNextSeeds << ",\n";
        output << "  \"visitedFile\": \"" << jsonEscape(state.visitedFile) << "\",\n";
        output << "  \"frontierFile\": \"" << jsonEscape(state.frontierFile) << "\",\n";
        output << "  \"remainingFrontierFile\": \"" << jsonEscape(state.remainingFrontierFile) << "\",\n";
        output << "  \"nextSeedFile\": \"" << jsonEscape(state.nextSeedFile) << "\",\n";
        output << "  \"baseVisitedFile\": \"" << jsonEscape(state.baseVisitedFile) << "\",\n";
        output << "  \"baseVisitedStates\": " << state.baseVisitedStates << ",\n";
        output << "  \"pendingIteration\": " << (state.pendingIteration ? "true" : "false") << ",\n";
        output << "  \"pendingCandidateStates\": " << state.pendingCandidateStates << ",\n";
        output << "  \"pendingCandidateFile\": \"" << jsonEscape(state.pendingCandidateFile) << "\"\n";
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
        throw std::runtime_error("failed to finalize closure checkpoint manifest: " + finalPath.string());
    }
}

ClosureCheckpointState readCheckpointManifest(
    const std::filesystem::path& checkpointDir,
    std::optional<int> expectedSoldierCount = std::nullopt) {
    const std::string text = readTextFile(closureCheckpointManifestPath(checkpointDir));
    const uint64_t rulesetHash = requireJsonUint(text, "rulesetHash");
    if (rulesetHash != StandardRulesetHash) {
        throw std::runtime_error("closure checkpoint ruleset hash does not match current rules");
    }

    ClosureCheckpointState state;
    state.checkpointVersion = static_cast<uint32_t>(jsonUintValue(text, "checkpointVersion").value_or(1));
    state.checkpointKind = jsonStringValue(text, "checkpointKind").value_or("legacy");
    state.requiresTransientRuns = jsonBoolValue(text, "requiresTransientRuns").value_or(false);
    if (state.requiresTransientRuns) {
        throw std::runtime_error("closure checkpoint requires transient run files and is not resumable by this version");
    }
    state.partitionedClosure = jsonBoolValue(text, "partitionedClosure").value_or(false);
    state.closurePartitionBuckets = static_cast<uint32_t>(jsonUintValue(text, "closurePartitionBuckets").value_or(0));
    state.closurePartitionMethod = jsonStringValue(text, "closurePartitionMethod").value_or("splitmix64_mod");
    state.soldierCount = static_cast<int>(requireJsonUint(text, "soldierCount"));
    requireSoldierCountRange(state.soldierCount);
    if (expectedSoldierCount.has_value() && state.soldierCount != *expectedSoldierCount) {
        throw std::runtime_error("closure checkpoint soldier count does not match expected layer");
    }
    state.status = requireJsonString(text, "status");
    state.complete = requireJsonBool(text, "complete");
    state.truncated = requireJsonBool(text, "truncated");
    state.truncationReason = requireJsonString(text, "truncationReason");
    state.seedStates = requireJsonUint(text, "seedStates");
    state.iterations = requireJsonUint(text, "iterations");
    state.expandedStates = requireJsonUint(text, "expandedStates");
    state.visitedStates = requireJsonUint(text, "visitedStates");
    state.frontierStates = requireJsonUint(text, "frontierStates");
    state.sameEdges = requireJsonUint(text, "sameEdges");
    state.captureEdges = requireJsonUint(text, "captureEdges");
    state.generatedCandidateKeys = requireJsonUint(text, "generatedCandidateKeys");
    state.newFrontierStates = requireJsonUint(text, "newFrontierStates");
    state.duplicateOrVisitedCandidates = requireJsonUint(text, "duplicateOrVisitedCandidates");
    state.nextSeedStates = requireJsonUint(text, "nextSeedStates");
    state.duplicateNextSeeds = requireJsonUint(text, "duplicateNextSeeds");
    state.visitedFile = requireJsonString(text, "visitedFile");
    state.frontierFile = requireJsonString(text, "frontierFile");
    state.remainingFrontierFile = jsonStringValue(text, "remainingFrontierFile").value_or(state.frontierFile);
    state.nextSeedFile = requireJsonString(text, "nextSeedFile");
    state.baseVisitedFile = jsonStringValue(text, "baseVisitedFile").value_or("base-visited.s15keys");
    state.baseVisitedStates = jsonUintValue(text, "baseVisitedStates").value_or(state.visitedStates);
    state.pendingIteration = jsonBoolValue(text, "pendingIteration").value_or(false);
    if (state.checkpointKind == "legacy") {
        state.checkpointKind = state.pendingIteration ? "mid-iteration" : "iteration-boundary";
    }
    if (state.checkpointKind != "iteration-boundary" && state.checkpointKind != "mid-iteration") {
        throw std::runtime_error("closure checkpoint checkpointKind is unsupported");
    }
    state.pendingCandidateStates = jsonUintValue(text, "pendingCandidateStates").value_or(0);
    state.pendingCandidateFile = jsonStringValue(text, "pendingCandidateFile").value_or("pending-candidates.s15keys");
    return state;
}

void validateCheckpointFiles(const std::filesystem::path& checkpointDir, const ClosureCheckpointState& state) {
    const KeySetFileInfo visited = inspectKeysFile(checkpointDir / state.visitedFile, state.soldierCount);
    const KeySetFileInfo frontier = inspectKeysFile(checkpointDir / state.frontierFile, state.soldierCount);
    const KeySetFileInfo remainingFrontier = inspectKeysFile(checkpointDir / state.remainingFrontierFile, state.soldierCount);
    if (visited.keyCount != state.visitedStates) {
        throw std::runtime_error("closure checkpoint visited key count does not match manifest");
    }
    if (frontier.keyCount != state.frontierStates) {
        throw std::runtime_error("closure checkpoint frontier key count does not match manifest");
    }
    if (remainingFrontier.keyCount != state.frontierStates) {
        throw std::runtime_error("closure checkpoint remaining-frontier key count does not match manifest");
    }
    if (state.soldierCount > 0) {
        const KeySetFileInfo nextSeeds = inspectKeysFile(checkpointDir / state.nextSeedFile, state.soldierCount - 1);
        if (nextSeeds.keyCount != state.nextSeedStates) {
            throw std::runtime_error("closure checkpoint next-seed key count does not match manifest");
        }
    }
    if (state.pendingIteration || state.pendingCandidateStates != 0) {
        if (state.checkpointKind != "mid-iteration") {
            throw std::runtime_error("closure checkpoint pending candidates require checkpointKind=mid-iteration");
        }
        const KeySetFileInfo pending = inspectKeysFile(checkpointDir / state.pendingCandidateFile, state.soldierCount);
        if (pending.keyCount != state.pendingCandidateStates) {
            throw std::runtime_error("closure checkpoint pending-candidate key count does not match manifest");
        }
        const KeySetFileInfo baseVisited = inspectKeysFile(checkpointDir / state.baseVisitedFile, state.soldierCount);
        if (baseVisited.keyCount != state.baseVisitedStates) {
            throw std::runtime_error("closure checkpoint base-visited key count does not match manifest");
        }
    } else if (state.checkpointKind != "iteration-boundary") {
        throw std::runtime_error("closure checkpoint without pending candidates must be iteration-boundary");
    }
    if (state.partitionedClosure) {
        if (state.closurePartitionBuckets == 0) {
            throw std::runtime_error("partitioned closure checkpoint has invalid bucket count");
        }
        const PartitionMethod expectedMethod = closurePartitionMethodFromString(state.closurePartitionMethod);
        auto requirePartitionMethod = [&](const std::filesystem::path& partitionDir) {
            const PartitionInspection inspection = inspectPartitionedKeySet(partitionDir);
            if (partitionMethodFromString(inspection.partitionMethod) != expectedMethod) {
                throw std::runtime_error("partitioned closure snapshot method does not match checkpoint manifest");
            }
        };
        const PartitionValidationResult visitedPartition =
            validatePartitionedKeySet(checkpointPartitionDir(checkpointDir, "visited"));
        requirePartitionMethod(checkpointPartitionDir(checkpointDir, "visited"));
        if (visitedPartition.totalKeys != state.visitedStates ||
            visitedPartition.soldierCount != state.soldierCount ||
            visitedPartition.bucketCount != state.closurePartitionBuckets) {
            throw std::runtime_error("partitioned closure visited snapshot does not match checkpoint manifest");
        }
        const PartitionValidationResult frontierPartition =
            validatePartitionedKeySet(checkpointPartitionDir(checkpointDir, "frontier"));
        requirePartitionMethod(checkpointPartitionDir(checkpointDir, "frontier"));
        if (frontierPartition.totalKeys != state.frontierStates ||
            frontierPartition.soldierCount != state.soldierCount ||
            frontierPartition.bucketCount != state.closurePartitionBuckets) {
            throw std::runtime_error("partitioned closure frontier snapshot does not match checkpoint manifest");
        }
        const PartitionValidationResult remainingPartition =
            validatePartitionedKeySet(checkpointPartitionDir(checkpointDir, "remaining-frontier"));
        requirePartitionMethod(checkpointPartitionDir(checkpointDir, "remaining-frontier"));
        if (remainingPartition.totalKeys != state.frontierStates ||
            remainingPartition.soldierCount != state.soldierCount ||
            remainingPartition.bucketCount != state.closurePartitionBuckets) {
            throw std::runtime_error("partitioned closure remaining-frontier snapshot does not match checkpoint manifest");
        }
        if (state.soldierCount > 0) {
            const PartitionValidationResult nextSeedsPartition =
                validatePartitionedKeySet(checkpointPartitionDir(checkpointDir, "next-seeds"));
            requirePartitionMethod(checkpointPartitionDir(checkpointDir, "next-seeds"));
            if (nextSeedsPartition.totalKeys != state.nextSeedStates ||
                nextSeedsPartition.soldierCount != state.soldierCount - 1 ||
                nextSeedsPartition.bucketCount != state.closurePartitionBuckets) {
                throw std::runtime_error("partitioned closure next-seeds snapshot does not match checkpoint manifest");
            }
        }
        if (state.pendingIteration || state.pendingCandidateStates != 0) {
            const PartitionValidationResult pendingPartition =
                validatePartitionedKeySet(checkpointPartitionDir(checkpointDir, "pending-candidates"));
            requirePartitionMethod(checkpointPartitionDir(checkpointDir, "pending-candidates"));
            if (pendingPartition.totalKeys != state.pendingCandidateStates ||
                pendingPartition.soldierCount != state.soldierCount ||
                pendingPartition.bucketCount != state.closurePartitionBuckets) {
                throw std::runtime_error("partitioned closure pending-candidates snapshot does not match checkpoint manifest");
            }
            const PartitionValidationResult baseVisitedPartition =
                validatePartitionedKeySet(checkpointPartitionDir(checkpointDir, "base-visited"));
            requirePartitionMethod(checkpointPartitionDir(checkpointDir, "base-visited"));
            if (baseVisitedPartition.totalKeys != state.baseVisitedStates ||
                baseVisitedPartition.soldierCount != state.soldierCount ||
                baseVisitedPartition.bucketCount != state.closurePartitionBuckets) {
                throw std::runtime_error("partitioned closure base-visited snapshot does not match checkpoint manifest");
            }
        }
    }
}

void writeClosureCheckpoint(
    const ExternalClosureOptions& options,
    ExternalClosureStats& stats,
    const std::filesystem::path& visitedPath,
    const std::filesystem::path& frontierPath,
    const std::filesystem::path& nextSeedKeysPath,
    bool complete,
    const std::filesystem::path& pendingCandidatePath,
    const std::filesystem::path& baseVisitedPath) {
    if (!options.writeCheckpoint) {
        return;
    }

    const std::filesystem::path checkpointDir = defaultCheckpointDir(options);
    std::filesystem::create_directories(checkpointDir);
    const KeySetFileInfo visitedInfo = inspectKeysFile(visitedPath, options.soldierCount);
    const KeySetFileInfo frontierInfo = inspectKeysFile(frontierPath, options.soldierCount);
    copyKeysFile(visitedPath, checkpointVisitedPath(checkpointDir), options.soldierCount);
    copyKeysFile(frontierPath, checkpointFrontierPath(checkpointDir), options.soldierCount);
    copyKeysFile(frontierPath, checkpointRemainingFrontierPath(checkpointDir), options.soldierCount);

    uint64_t nextSeedStates = 0;
    if (options.soldierCount > 0) {
        const KeySetFileInfo nextInfo = inspectKeysFile(nextSeedKeysPath, options.soldierCount - 1);
        nextSeedStates = nextInfo.keyCount;
        copyKeysFile(nextSeedKeysPath, checkpointNextSeedPath(checkpointDir), options.soldierCount - 1);
    } else {
        writeKeysFile(checkpointNextSeedPath(checkpointDir), 0, {});
    }

    uint64_t pendingCandidateStates = 0;
    if (!pendingCandidatePath.empty()) {
        const KeySetFileInfo pendingInfo = inspectKeysFile(pendingCandidatePath, options.soldierCount);
        pendingCandidateStates = pendingInfo.keyCount;
        copyKeysFile(pendingCandidatePath, checkpointPendingCandidatePath(checkpointDir), options.soldierCount);
    } else {
        writeKeysFile(checkpointPendingCandidatePath(checkpointDir), options.soldierCount, {});
    }
    uint64_t baseVisitedStates = visitedInfo.keyCount;
    if (!pendingCandidatePath.empty()) {
        const std::filesystem::path basePath = baseVisitedPath.empty() ? visitedPath : baseVisitedPath;
        const KeySetFileInfo baseInfo = inspectKeysFile(basePath, options.soldierCount);
        baseVisitedStates = baseInfo.keyCount;
        copyKeysFile(basePath, checkpointBaseVisitedPath(checkpointDir), options.soldierCount);
    } else {
        copyKeysFile(visitedPath, checkpointBaseVisitedPath(checkpointDir), options.soldierCount);
    }

    ClosureCheckpointState state;
    state.checkpointVersion = 2;
    state.checkpointKind = !pendingCandidatePath.empty() ? "mid-iteration" : "iteration-boundary";
    state.requiresTransientRuns = false;
    state.partitionedClosure = options.partitionedClosure;
    state.closurePartitionBuckets = options.partitionedClosure ? options.closurePartitionBuckets : 0;
    state.closurePartitionMethod = options.closurePartitionMethod;
    state.soldierCount = options.soldierCount;
    state.status = complete ? "complete" : (stats.truncated ? "truncated" : "running");
    state.complete = complete;
    state.truncated = stats.truncated;
    state.truncationReason = truncationReasonString(stats);
    state.seedStates = stats.seedStates;
    state.iterations = stats.iterations;
    state.expandedStates = stats.expandedStates;
    state.visitedStates = visitedInfo.keyCount;
    state.frontierStates = frontierInfo.keyCount;
    state.sameEdges = stats.generatedSameLayerEdges;
    state.captureEdges = stats.generatedCaptureEdges;
    state.generatedCandidateKeys = stats.generatedCandidateKeys;
    state.newFrontierStates = stats.newFrontierStates;
    state.duplicateOrVisitedCandidates = stats.duplicateOrVisitedCandidates;
    state.nextSeedStates = nextSeedStates;
    state.duplicateNextSeeds = stats.duplicateNextSeeds;
    state.remainingFrontierFile = checkpointRemainingFrontierPath(checkpointDir).filename().string();
    state.pendingIteration = !pendingCandidatePath.empty();
    state.baseVisitedStates = baseVisitedStates;
    state.pendingCandidateStates = pendingCandidateStates;

    if (options.partitionedClosure) {
        if (options.closurePartitionBuckets == 0) {
            throw std::invalid_argument("partitioned closure bucket count must be greater than zero");
        }
        const auto partitionStarted = Clock::now();
        const PartitionMethod method = closurePartitionMethodFromString(options.closurePartitionMethod);
        writeCheckpointPartition(
            checkpointVisitedPath(checkpointDir),
            checkpointPartitionDir(checkpointDir, "visited"),
            options.closurePartitionBuckets,
            method);
        writeCheckpointPartition(
            checkpointFrontierPath(checkpointDir),
            checkpointPartitionDir(checkpointDir, "frontier"),
            options.closurePartitionBuckets,
            method);
        writeCheckpointPartition(
            checkpointRemainingFrontierPath(checkpointDir),
            checkpointPartitionDir(checkpointDir, "remaining-frontier"),
            options.closurePartitionBuckets,
            method);
        writeCheckpointPartition(
            checkpointNextSeedPath(checkpointDir),
            checkpointPartitionDir(checkpointDir, "next-seeds"),
            options.closurePartitionBuckets,
            method);
        if (!pendingCandidatePath.empty()) {
            writeCheckpointPartition(
                checkpointPendingCandidatePath(checkpointDir),
                checkpointPartitionDir(checkpointDir, "pending-candidates"),
                options.closurePartitionBuckets,
                method);
            writeCheckpointPartition(
                checkpointBaseVisitedPath(checkpointDir),
                checkpointPartitionDir(checkpointDir, "base-visited"),
                options.closurePartitionBuckets,
                method);
        }
        stats.partitionSeconds += std::chrono::duration<double>(Clock::now() - partitionStarted).count();
        ++stats.partitionedSnapshotsWritten;
    }
    writeCheckpointManifest(checkpointDir, state);

    stats.checkpointWritten = true;
    stats.partitionedClosure = options.partitionedClosure;
    stats.closurePartitionBuckets = options.partitionedClosure ? options.closurePartitionBuckets : 0;
    stats.checkpointDir = checkpointDir.string();
    stats.finalStates = visitedInfo.keyCount;
    stats.finalFrontierStates = frontierInfo.keyCount;
    stats.nextSeedStates = nextSeedStates;
}

void copyKeysToLayerFile(const std::filesystem::path& keysFile, const std::filesystem::path& layerFile, int soldierCount) {
    KeysReader reader(keysFile, soldierCount);
    std::vector<uint64_t> keys;
    keys.reserve(static_cast<size_t>(reader.keyCount()));
    uint64_t key = 0;
    while (reader.readNext(key)) {
        keys.push_back(key);
    }
    writeLayerFile(layerFile, soldierCount, keys);
}

void copyKeysToSeedFile(const std::filesystem::path& keysFile, const std::filesystem::path& seedFile, int soldierCount) {
    KeysReader reader(keysFile, soldierCount);
    std::vector<uint64_t> keys;
    keys.reserve(static_cast<size_t>(reader.keyCount()));
    uint64_t key = 0;
    while (reader.readNext(key)) {
        keys.push_back(key);
    }
    writeSeedFile(seedFile, soldierCount, keys);
}

void reportProgress(
    const ExternalClosureOptions& options,
    const ExternalClosureStats& stats,
    uint64_t frontierStates,
    uint64_t candidateStates,
    uint64_t nextFrontierStates,
    bool complete,
    double elapsedSeconds) {
    if (!options.progress) {
        return;
    }

    ExternalClosureProgressInfo info;
    info.soldierCount = stats.soldierCount;
    info.iteration = stats.iterations;
    info.frontierStates = frontierStates;
    info.visitedStates = stats.finalStates;
    info.candidateStates = candidateStates;
    info.nextFrontierStates = nextFrontierStates;
    info.expandedStates = stats.expandedStates;
    info.generatedSameLayerEdges = stats.generatedSameLayerEdges;
    info.generatedCaptureEdges = stats.generatedCaptureEdges;
    info.nextSeedStates = stats.nextSeedStates;
    info.duplicateNextSeeds = stats.duplicateNextSeeds;
    info.complete = complete;
    info.truncated = stats.truncated;
    info.elapsedSeconds = elapsedSeconds;
    options.progress(info);
}

}  // namespace

void writeKeysFile(
    const std::filesystem::path& path,
    int soldierCount,
    const std::vector<uint64_t>& keys) {
    const std::vector<uint64_t> sortedKeys = sortedUniqueChecked(keys, soldierCount, "keys file");
    KeysWriter writer(path, soldierCount);
    for (uint64_t key : sortedKeys) {
        writer.write(key);
    }
    writer.finish();
}

KeysFileData readKeysFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount) {
    KeysReader reader(path, expectedSoldierCount);
    KeysFileData data;
    data.soldierCount = reader.soldierCount();
    data.keys.reserve(static_cast<size_t>(reader.keyCount()));
    uint64_t key = 0;
    while (reader.readNext(key)) {
        data.keys.push_back(key);
    }
    return data;
}

KeySetFileInfo inspectKeysFile(
    const std::filesystem::path& path,
    std::optional<int> expectedSoldierCount) {
    KeysReader reader(path, expectedSoldierCount);
    uint64_t key = 0;
    while (reader.readNext(key)) {
    }
    KeySetFileInfo info;
    info.path = path;
    info.soldierCount = reader.soldierCount();
    info.keyCount = reader.keyCount();
    return info;
}

KeySetOpStats sortedDifference(
    const std::filesystem::path& left,
    const std::filesystem::path& right,
    const std::filesystem::path& output) {
    const auto started = Clock::now();
    KeysReader leftReader(left);
    KeysReader rightReader(right);
    if (leftReader.soldierCount() != rightReader.soldierCount()) {
        throw std::runtime_error("sortedDifference requires matching soldier counts");
    }
    KeysWriter writer(output, leftReader.soldierCount());

    KeySetOpStats stats;
    stats.leftKeys = leftReader.keyCount();
    stats.rightKeys = rightReader.keyCount();

    uint64_t leftKey = 0;
    uint64_t rightKey = 0;
    bool hasLeft = leftReader.readNext(leftKey);
    bool hasRight = rightReader.readNext(rightKey);
    while (hasLeft) {
        while (hasRight && rightKey < leftKey) {
            hasRight = rightReader.readNext(rightKey);
        }
        if (!hasRight || leftKey < rightKey) {
            writer.write(leftKey);
            ++stats.outputKeys;
        }
        hasLeft = leftReader.readNext(leftKey);
    }
    writer.finish();
    stats.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    return stats;
}

KeySetOpStats sortedUnion(
    const std::filesystem::path& left,
    const std::filesystem::path& right,
    const std::filesystem::path& output) {
    const auto started = Clock::now();
    KeysReader leftReader(left);
    KeysReader rightReader(right);
    if (leftReader.soldierCount() != rightReader.soldierCount()) {
        throw std::runtime_error("sortedUnion requires matching soldier counts");
    }
    KeysWriter writer(output, leftReader.soldierCount());

    KeySetOpStats stats;
    stats.leftKeys = leftReader.keyCount();
    stats.rightKeys = rightReader.keyCount();

    uint64_t leftKey = 0;
    uint64_t rightKey = 0;
    bool hasLeft = leftReader.readNext(leftKey);
    bool hasRight = rightReader.readNext(rightKey);
    while (hasLeft || hasRight) {
        if (!hasRight || (hasLeft && leftKey < rightKey)) {
            writer.write(leftKey);
            ++stats.outputKeys;
            hasLeft = leftReader.readNext(leftKey);
        } else if (!hasLeft || rightKey < leftKey) {
            writer.write(rightKey);
            ++stats.outputKeys;
            hasRight = rightReader.readNext(rightKey);
        } else {
            writer.write(leftKey);
            ++stats.outputKeys;
            hasLeft = leftReader.readNext(leftKey);
            hasRight = rightReader.readNext(rightKey);
        }
    }
    writer.finish();
    stats.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    return stats;
}

std::filesystem::path closureCheckpointManifestPath(const std::filesystem::path& checkpointDir) {
    return checkpointDir / "closure-state.json";
}

ExternalClosureCheckpointInfo inspectClosureCheckpoint(
    const std::filesystem::path& checkpointDir,
    std::optional<int> expectedSoldierCount) {
    const ClosureCheckpointState state = readCheckpointManifest(checkpointDir, expectedSoldierCount);
    validateCheckpointFiles(checkpointDir, state);

    ExternalClosureCheckpointInfo info;
    info.checkpointDir = checkpointDir;
    info.checkpointVersion = state.checkpointVersion;
    info.checkpointKind = state.checkpointKind;
    info.requiresTransientRuns = state.requiresTransientRuns;
    info.soldierCount = state.soldierCount;
    info.seedStates = state.seedStates;
    info.visitedStates = state.visitedStates;
    info.frontierStates = state.frontierStates;
    info.nextSeedStates = state.nextSeedStates;
    info.iterations = state.iterations;
    info.expandedStates = state.expandedStates;
    info.complete = state.complete;
    info.truncated = state.truncated;
    info.stableCheckpoint = !state.requiresTransientRuns;
    return info;
}

uint64_t cleanupStaleClosureRuns(const std::filesystem::path& checkpointDir) {
    (void)inspectClosureCheckpoint(checkpointDir);
    const std::filesystem::path runsDir = checkpointDir / "runs";
    if (!std::filesystem::exists(runsDir)) {
        return 0;
    }
    uint64_t removedEntries = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(runsDir, error)) {
        if (error) {
            break;
        }
        ++removedEntries;
    }
    error.clear();
    std::filesystem::remove_all(runsDir, error);
    if (error) {
        throw std::runtime_error("failed to remove stale closure runs: " + runsDir.string());
    }
    return removedEntries;
}

ClosureCheckpointRepairResult repairClosureCheckpoint(
    const std::filesystem::path& checkpointDir,
    std::optional<int> expectedSoldierCount,
    bool dryRun) {
    ClosureCheckpointState state = readCheckpointManifest(checkpointDir, expectedSoldierCount);
    validateCheckpointFiles(checkpointDir, state);

    ClosureCheckpointRepairResult result;
    result.checkpointDir = checkpointDir;
    result.dryRun = dryRun;
    result.beforeKind = state.checkpointKind;
    result.beforeRequiresTransientRuns = state.requiresTransientRuns;
    const uint32_t beforeVersion = state.checkpointVersion;

    state.checkpointVersion = 2;
    state.checkpointKind =
        (state.pendingIteration || state.pendingCandidateStates != 0) ? "mid-iteration" : "iteration-boundary";
    state.requiresTransientRuns = false;
    state.remainingFrontierFile = state.frontierFile;

    result.afterKind = state.checkpointKind;
    result.afterRequiresTransientRuns = false;
    result.repaired =
        result.beforeKind != result.afterKind ||
        result.beforeRequiresTransientRuns != result.afterRequiresTransientRuns ||
        beforeVersion != state.checkpointVersion;

    if (!dryRun) {
        writeCheckpointManifest(checkpointDir, state);
    }
    return result;
}

ExternalClosureStats buildLayerClosureExternal(const ExternalClosureOptions& options) {
    if (options.workDir.empty()) {
        throw std::invalid_argument("external closure workDir is required");
    }
    if (options.seedFile.empty()) {
        throw std::invalid_argument("external closure seedFile is required");
    }
    if (options.outputLayerFile.empty()) {
        throw std::invalid_argument("external closure outputLayerFile is required");
    }
    if (options.soldierCount > 0 && options.outputNextSeedFile.empty()) {
        throw std::invalid_argument("external closure outputNextSeedFile is required for layers above zero");
    }
    if (options.chunkKeyLimit == 0) {
        throw std::invalid_argument("external closure chunkKeyLimit must be greater than zero");
    }
    requireSoldierCountRange(options.soldierCount);

    const auto totalStarted = Clock::now();
    std::filesystem::create_directories(options.workDir);
    const std::filesystem::path checkpointDir = defaultCheckpointDir(options);
    if (options.writeCheckpoint) {
        std::filesystem::create_directories(checkpointDir);
    }

    ExternalClosureStats stats;
    stats.soldierCount = options.soldierCount;
    stats.checkpointDir = checkpointDir.string();

    std::filesystem::path visitedPath;
    std::filesystem::path frontierPath;
    std::filesystem::path nextSeedKeysPath;
    std::filesystem::path pendingCandidatePath;
    std::filesystem::path baseVisitedPath;
    const std::filesystem::path visited0 = workPath(options.workDir, "visited", 0);
    const std::filesystem::path frontier0 = workPath(options.workDir, "frontier", 0);

    uint64_t resumeExpandedBase = 0;
    uint64_t resumeIterationBase = 0;
    if (options.resume) {
        const ClosureCheckpointState checkpoint = readCheckpointManifest(checkpointDir, options.soldierCount);
        validateCheckpointFiles(checkpointDir, checkpoint);
        if (checkpoint.complete) {
            throw std::runtime_error("closure checkpoint is already complete; resume is not needed");
        }

        stats.resumed = true;
        stats.seedStates = checkpoint.seedStates;
        stats.finalStates = checkpoint.visitedStates;
        stats.iterations = checkpoint.iterations;
        stats.expandedStates = checkpoint.expandedStates;
        stats.generatedSameLayerEdges = checkpoint.sameEdges;
        stats.generatedCaptureEdges = checkpoint.captureEdges;
        stats.generatedCandidateKeys = checkpoint.generatedCandidateKeys;
        stats.newFrontierStates = checkpoint.newFrontierStates;
        stats.duplicateOrVisitedCandidates = checkpoint.duplicateOrVisitedCandidates;
        stats.nextSeedStates = checkpoint.nextSeedStates;
        stats.duplicateNextSeeds = checkpoint.duplicateNextSeeds;
        stats.initialVisitedStates = checkpoint.visitedStates;
        stats.initialFrontierStates = checkpoint.frontierStates;
        stats.finalFrontierStates = checkpoint.frontierStates;
        resumeExpandedBase = stats.expandedStates;
        resumeIterationBase = stats.iterations;

        visitedPath = workPath(options.workDir, "resume-visited", stats.iterations);
        frontierPath = workPath(options.workDir, "resume-frontier", stats.iterations);
        copyKeysFile(checkpointDir / checkpoint.visitedFile, visitedPath, options.soldierCount);
        copyKeysFile(checkpointDir / checkpoint.remainingFrontierFile, frontierPath, options.soldierCount);
        if (options.soldierCount > 0) {
            nextSeedKeysPath = workPath(options.workDir, "resume-next-seeds", stats.iterations);
            copyKeysFile(checkpointDir / checkpoint.nextSeedFile, nextSeedKeysPath, options.soldierCount - 1);
        }
        if (checkpoint.pendingIteration) {
            pendingCandidatePath = workPath(options.workDir, "resume-pending-candidates", stats.iterations);
            copyKeysFile(checkpointDir / checkpoint.pendingCandidateFile, pendingCandidatePath, options.soldierCount);
            baseVisitedPath = workPath(options.workDir, "resume-base-visited", stats.iterations);
            copyKeysFile(checkpointDir / checkpoint.baseVisitedFile, baseVisitedPath, options.soldierCount);
        }
    } else {
        const SeedFileData seeds = readSeedFile(options.seedFile, options.soldierCount);
        stats.seedStates = static_cast<uint64_t>(seeds.keys.size());

        writeKeysFile(visited0, options.soldierCount, seeds.keys);
        writeKeysFile(frontier0, options.soldierCount, seeds.keys);
        visitedPath = visited0;
        frontierPath = frontier0;
        stats.finalStates = stats.seedStates;
        stats.initialVisitedStates = stats.seedStates;
        stats.initialFrontierStates = stats.seedStates;

        if (options.soldierCount > 0) {
            nextSeedKeysPath = workPath(options.workDir, "next-seeds", 0);
            writeKeysFile(nextSeedKeysPath, options.soldierCount - 1, {});
        }
    }

    bool reachedEmptyFrontier = stats.seedStates == 0;
    uint64_t lastCheckpointExpanded = stats.expandedStates;
    if (!options.resume && options.writeCheckpoint && stats.seedStates == 0) {
        writeClosureCheckpoint(options, stats, visitedPath, frontierPath, nextSeedKeysPath, true, {}, {});
    }
    while (!reachedEmptyFrontier) {
        const KeySetFileInfo frontierInfo = inspectKeysFile(frontierPath, options.soldierCount);
        if (frontierInfo.keyCount == 0) {
            if (pendingCandidatePath.empty()) {
                reachedEmptyFrontier = true;
                break;
            }
        }
        if (options.maxIterations != 0 && stats.iterations - resumeIterationBase >= options.maxIterations) {
            stats.truncated = true;
            stats.truncatedByMaxIterations = true;
            break;
        }
        if (options.maxExpandedStates != 0 && stats.expandedStates - resumeExpandedBase >= options.maxExpandedStates) {
            stats.truncated = true;
            stats.truncatedByMaxExpandedStates = true;
            break;
        }

        const bool continuingPendingIteration = !pendingCandidatePath.empty();
        const std::filesystem::path previousPendingCandidatePath = pendingCandidatePath;
        const std::filesystem::path previousBaseVisitedPath = baseVisitedPath;
        const uint64_t iteration = stats.iterations + 1;
        const auto expansionStarted = Clock::now();

        ExternalKeySetOptions candidateOptions;
        candidateOptions.tempDir = options.workDir / "runs" / ("candidates-" + std::to_string(iteration));
        candidateOptions.chunkKeyLimit = options.chunkKeyLimit;
        candidateOptions.keepTempFiles = options.keepTempFiles;
        ExternalKeySetBuilder candidates(candidateOptions);

        std::unique_ptr<ExternalKeySetBuilder> nextSeedDelta;
        if (options.soldierCount > 0) {
            ExternalKeySetOptions seedOptions;
            seedOptions.tempDir = options.workDir / "runs" / ("next-seeds-" + std::to_string(iteration));
            seedOptions.chunkKeyLimit = options.chunkKeyLimit;
            seedOptions.keepTempFiles = options.keepTempFiles;
            nextSeedDelta = std::make_unique<ExternalKeySetBuilder>(seedOptions);
        }

        bool stoppedByLimit = false;
        std::filesystem::path remainingFrontierPath;
        {
            KeysReader frontierReader(frontierPath, options.soldierCount);
            uint64_t key = 0;
            while (frontierReader.readNext(key)) {
                if (options.maxExpandedStates != 0 && stats.expandedStates - resumeExpandedBase >= options.maxExpandedStates) {
                    stats.truncated = true;
                    stats.truncatedByMaxExpandedStates = true;
                    stoppedByLimit = true;
                    remainingFrontierPath = workPath(options.workDir, "remaining-frontier", iteration);
                    KeysWriter remainingWriter(remainingFrontierPath, options.soldierCount);
                    remainingWriter.write(key);
                    while (frontierReader.readNext(key)) {
                        remainingWriter.write(key);
                    }
                    remainingWriter.finish();
                    break;
                }
                const Position pos = unpackPosition(key);
                const int keySoldiers = popcount25(pos.soldiers);
                if (keySoldiers != options.soldierCount) {
                    throw std::logic_error("external frontier contains a state with the wrong soldier count");
                }
                ++stats.expandedStates;
                if (isTerminal(pos)) {
                    continue;
                }

                const std::vector<Move> moves = generateLegalMoves(pos);
                for (const Move& move : moves) {
                    const Position next = applyMove(pos, move);
                    const int nextSoldiers = popcount25(next.soldiers);
                    const uint64_t nextKey = packPosition(next);
                    if (nextSoldiers == options.soldierCount) {
                        candidates.add(nextKey);
                        ++stats.generatedSameLayerEdges;
                        ++stats.generatedCandidateKeys;
                        continue;
                    }
                    if (nextSoldiers == options.soldierCount - 1) {
                        if (options.soldierCount == 0) {
                            throw std::logic_error("0-soldier layer generated a capture transition");
                        }
                        nextSeedDelta->add(nextKey);
                        ++stats.generatedCaptureEdges;
                        continue;
                    }
                    throw std::logic_error("illegal soldier count transition during external closure");
                }
            }
        }
        stats.expansionSeconds += std::chrono::duration<double>(Clock::now() - expansionStarted).count();

        const std::filesystem::path candidatesPath = workPath(
            options.workDir,
            continuingPendingIteration ? "candidates-resume" : "candidates",
            iteration);
        const auto candidateStarted = Clock::now();
        {
            KeysWriter candidateWriter(candidatesPath, options.soldierCount);
            candidates.finishToStream([&](uint64_t candidateKey) {
                candidateWriter.write(candidateKey);
            });
            candidateWriter.finish();
        }
        const ExternalKeySetStats candidateStats = candidates.stats();
        stats.candidateDedupSeconds +=
            std::chrono::duration<double>(Clock::now() - candidateStarted).count();

        std::filesystem::path iterationCandidatesPath = candidatesPath;
        uint64_t iterationCandidateCount = candidateStats.uniqueKeys;
        if (continuingPendingIteration) {
            iterationCandidatesPath = workPath(options.workDir, "candidates-merged", iteration);
            const KeySetOpStats candidateUnionStats = sortedUnion(pendingCandidatePath, candidatesPath, iterationCandidatesPath);
            stats.unionSeconds += candidateUnionStats.seconds;
            iterationCandidateCount = candidateUnionStats.outputKeys;
        }

        if (nextSeedDelta) {
            const std::filesystem::path nextDeltaPath = workPath(options.workDir, "next-seed-delta", iteration);
            {
                KeysWriter nextDeltaWriter(nextDeltaPath, options.soldierCount - 1);
                nextSeedDelta->finishToStream([&](uint64_t seedKey) {
                    nextDeltaWriter.write(seedKey);
                });
                nextDeltaWriter.finish();
            }
            const ExternalKeySetStats seedDeltaStats = nextSeedDelta->stats();
            stats.duplicateNextSeeds += seedDeltaStats.duplicateKeys;

            const std::filesystem::path mergedNextSeeds = workPath(options.workDir, "next-seeds", iteration);
            const KeySetOpStats seedUnionStats = sortedUnion(nextSeedKeysPath, nextDeltaPath, mergedNextSeeds);
            stats.duplicateNextSeeds +=
                seedUnionStats.leftKeys + seedUnionStats.rightKeys - seedUnionStats.outputKeys;
            stats.nextSeedStates = seedUnionStats.outputKeys;
            stats.unionSeconds += seedUnionStats.seconds;
            if (!options.keepTempFiles) {
                removeIfExists(nextSeedKeysPath);
                removeIfExists(nextDeltaPath);
            }
            nextSeedKeysPath = mergedNextSeeds;
        }

        const std::filesystem::path nextFrontierPath = workPath(options.workDir, "next-frontier", iteration);
        const std::filesystem::path iterationBaseVisitedPath =
            continuingPendingIteration && !baseVisitedPath.empty() ? baseVisitedPath : visitedPath;
        const KeySetOpStats diffStats = sortedDifference(iterationCandidatesPath, iterationBaseVisitedPath, nextFrontierPath);
        stats.differenceSeconds += diffStats.seconds;
        stats.newFrontierStates = diffStats.outputKeys;
        stats.duplicateOrVisitedCandidates += candidateStats.duplicateKeys;
        stats.duplicateOrVisitedCandidates +=
            diffStats.leftKeys >= diffStats.outputKeys ? diffStats.leftKeys - diffStats.outputKeys : 0;

        const std::filesystem::path nextVisitedPath = workPath(options.workDir, "visited", iteration);
        const KeySetOpStats unionStats = sortedUnion(iterationBaseVisitedPath, nextFrontierPath, nextVisitedPath);
        stats.unionSeconds += unionStats.seconds;
        stats.finalStates = unionStats.outputKeys;
        if (options.maxDiscoveredStates != 0 && stats.finalStates >= options.maxDiscoveredStates) {
            stats.truncated = true;
            stats.truncatedByMaxStates = true;
        }
        if (!stoppedByLimit) {
            ++stats.iterations;
        }

        std::filesystem::path pendingFrontierPath = nextFrontierPath;
        uint64_t pendingFrontierStates = diffStats.outputKeys;
        std::filesystem::path checkpointCandidatePath;
        if (stoppedByLimit) {
            pendingFrontierPath = remainingFrontierPath;
            pendingFrontierStates = inspectKeysFile(remainingFrontierPath, options.soldierCount).keyCount;
            checkpointCandidatePath = iterationCandidatesPath;
        } else if (continuingPendingIteration) {
            pendingCandidatePath.clear();
        }

        const double elapsed = std::chrono::duration<double>(Clock::now() - totalStarted).count();
        reportProgress(
            options,
            stats,
            frontierInfo.keyCount,
            iterationCandidateCount,
            pendingFrontierStates,
            false,
            elapsed);

        if (!options.keepTempFiles) {
            removeIfExists(frontierPath);
            if (!stoppedByLimit || candidatesPath != iterationCandidatesPath) {
                removeIfExists(candidatesPath);
            }
            if (continuingPendingIteration) {
                removeIfExists(previousPendingCandidatePath);
                if (!stoppedByLimit) {
                    removeIfExists(previousBaseVisitedPath);
                }
                if (!stoppedByLimit) {
                    removeIfExists(iterationCandidatesPath);
                }
            }
            if ((visitedPath != visited0 || nextVisitedPath != visited0) &&
                visitedPath != iterationBaseVisitedPath) {
                removeIfExists(visitedPath);
            }
            if (stoppedByLimit) {
                removeIfExists(nextFrontierPath);
            }
        }

        frontierPath = pendingFrontierPath;
        visitedPath = nextVisitedPath;
        pendingCandidatePath = checkpointCandidatePath;
        baseVisitedPath = stoppedByLimit ? iterationBaseVisitedPath : std::filesystem::path{};
        reachedEmptyFrontier = pendingFrontierStates == 0;
        stats.finalFrontierStates = pendingFrontierStates;
        const bool shouldCheckpoint =
            options.writeCheckpoint &&
            (options.checkpointInterval == 0
                 ? false
                 : (stats.expandedStates - lastCheckpointExpanded >= options.checkpointInterval));
        if (shouldCheckpoint) {
            writeClosureCheckpoint(
                options,
                stats,
                visitedPath,
                frontierPath,
                nextSeedKeysPath,
                false,
                pendingCandidatePath,
                baseVisitedPath);
            lastCheckpointExpanded = stats.expandedStates;
        }
        if (stoppedByLimit || stats.truncatedByMaxStates) {
            break;
        }
    }

    stats.complete = reachedEmptyFrontier && !stats.truncated;
    if (!stats.truncated) {
        stats.finalFrontierStates = 0;
    } else {
        stats.finalFrontierStates = inspectKeysFile(frontierPath, options.soldierCount).keyCount;
    }
    if (options.writeCheckpoint &&
        (!stats.checkpointWritten || stats.truncated || stats.complete)) {
        writeClosureCheckpoint(
            options,
            stats,
            visitedPath,
            frontierPath,
            nextSeedKeysPath,
            stats.complete,
            pendingCandidatePath,
            baseVisitedPath);
    }

    copyKeysToLayerFile(visitedPath, options.outputLayerFile, options.soldierCount);
    if (options.soldierCount > 0) {
        copyKeysToSeedFile(nextSeedKeysPath, options.outputNextSeedFile, options.soldierCount - 1);
    } else if (!options.outputNextSeedFile.empty()) {
        writeSeedFile(options.outputNextSeedFile, 0, {});
    }

    stats.totalSeconds = std::chrono::duration<double>(Clock::now() - totalStarted).count();
    reportProgress(options, stats, stats.finalFrontierStates, 0, 0, true, stats.totalSeconds);

    if (!options.keepTempFiles) {
        removeIfExists(frontierPath);
        removeIfExists(visitedPath);
        removeIfExists(visited0);
        if (!nextSeedKeysPath.empty()) {
            removeIfExists(nextSeedKeysPath);
        }
        std::error_code error;
        std::filesystem::remove_all(options.workDir / "runs", error);
        for (const auto& entry : std::filesystem::directory_iterator(options.workDir, error)) {
            if (error) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto extension = entry.path().extension();
            if (extension == ".s15keys" || extension == ".s15run") {
                const bool isCheckpoint =
                    options.writeCheckpoint &&
                    (entry.path() == checkpointVisitedPath(checkpointDir) ||
                     entry.path() == checkpointFrontierPath(checkpointDir) ||
                     entry.path() == checkpointRemainingFrontierPath(checkpointDir) ||
                     entry.path() == checkpointNextSeedPath(checkpointDir) ||
                     entry.path() == checkpointPendingCandidatePath(checkpointDir) ||
                     entry.path() == checkpointBaseVisitedPath(checkpointDir));
                if (!isCheckpoint) {
                    removeIfExists(entry.path());
                }
            }
        }
    }

    return stats;
}

}  // namespace sanpao15

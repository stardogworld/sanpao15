#include "sanpao15/dense_table.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>

#include "sanpao15/dense_index.h"
#include "sanpao15/table.h"

namespace sanpao15 {

namespace {

constexpr std::array<char, 8> DenseResultMagic{'S', '1', '5', 'R', 'E', 'S', '1', '\0'};
constexpr uint32_t DenseResultVersion = 1;

void writeU8(std::ostream& output, uint8_t value) {
    output.put(static_cast<char>(value));
    if (!output) {
        throw std::runtime_error("failed to write dense result byte");
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
        throw std::runtime_error("unexpected end of dense result file");
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

DenseResultEncoding encodingFromU32(uint32_t value) {
    switch (value) {
        case 1:
            return DenseResultEncoding::Byte;
        case 2:
            return DenseResultEncoding::Packed2Bit;
        default:
            throw std::runtime_error("unsupported dense result encoding");
    }
}

uint32_t encodingToU32(DenseResultEncoding encoding) {
    return static_cast<uint32_t>(encoding);
}

void validateSoldierCount(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::invalid_argument("soldier count must be in 0..15");
    }
}

DenseResultFileInfo readDenseResultHeader(std::istream& input) {
    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != DenseResultMagic) {
        throw std::runtime_error("invalid .s15res magic");
    }

    DenseResultFileInfo info;
    info.version = readU32LE(input);
    if (info.version != DenseResultVersion) {
        throw std::runtime_error("unsupported .s15res version");
    }
    info.rulesetHash = readU64LE(input);
    info.soldierCount = static_cast<int>(readU32LE(input));
    validateSoldierCount(info.soldierCount);
    info.stateCount = readU64LE(input);
    info.encoding = encodingFromU32(readU32LE(input));
    info.payloadBytes = readU64LE(input);

    if (info.stateCount != denseStateCount(info.soldierCount)) {
        throw std::runtime_error(".s15res state count does not match soldier-count layer");
    }
    if (info.payloadBytes != denseResultPayloadBytes(info.stateCount, info.encoding)) {
        throw std::runtime_error(".s15res payload byte count does not match encoding");
    }
    return info;
}

void writeDenseResultHeader(std::ostream& output, const DenseResultFileInfo& info) {
    output.write(DenseResultMagic.data(), static_cast<std::streamsize>(DenseResultMagic.size()));
    if (!output) {
        throw std::runtime_error("failed to write .s15res magic");
    }
    writeU32LE(output, DenseResultVersion);
    writeU64LE(output, info.rulesetHash);
    writeU32LE(output, static_cast<uint32_t>(info.soldierCount));
    writeU64LE(output, info.stateCount);
    writeU32LE(output, encodingToU32(info.encoding));
    writeU64LE(output, info.payloadBytes);
}

void validateHeaderExpectations(
    const DenseResultFileInfo& info,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount) {
    if (info.rulesetHash != expectedRulesetHash) {
        throw std::runtime_error(".s15res ruleset hash mismatch");
    }
    if (expectedSoldierCount >= 0 && info.soldierCount != expectedSoldierCount) {
        throw std::runtime_error(".s15res soldier count mismatch");
    }
}

void writePayload(std::ostream& output, const std::vector<uint8_t>& payload) {
    if (!payload.empty()) {
        output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    }
    if (!output) {
        throw std::runtime_error("failed to write .s15res payload");
    }
}

std::vector<uint8_t> readPayload(std::istream& input, uint64_t payloadBytes) {
    if (payloadBytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error(".s15res payload is too large for this platform");
    }
    std::vector<uint8_t> payload(static_cast<size_t>(payloadBytes), 0);
    if (!payload.empty()) {
        input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    }
    if (!input) {
        throw std::runtime_error("unexpected end of .s15res payload");
    }
    return payload;
}

void rejectTrailingBytes(std::istream& input) {
    const int extra = input.get();
    if (extra != std::char_traits<char>::eof()) {
        throw std::runtime_error(".s15res contains trailing bytes");
    }
}

}  // namespace

uint8_t encodeOutcome(Outcome outcome) {
    switch (outcome) {
        case Outcome::Unknown:
            return 0;
        case Outcome::CannonWin:
            return 1;
        case Outcome::SoldierWin:
            return 2;
        case Outcome::Draw:
            return 3;
    }
    throw std::invalid_argument("invalid outcome");
}

Outcome decodeOutcome(uint8_t value) {
    switch (value) {
        case 0:
            return Outcome::Unknown;
        case 1:
            return Outcome::CannonWin;
        case 2:
            return Outcome::SoldierWin;
        case 3:
            return Outcome::Draw;
        default:
            throw std::runtime_error("invalid dense outcome value");
    }
}

DenseOutcomeTable::DenseOutcomeTable(uint64_t stateCount, Outcome initial) {
    if (stateCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("dense outcome table is too large for this platform");
    }
    outcomes_.assign(static_cast<size_t>(stateCount), encodeOutcome(initial));
}

Outcome DenseOutcomeTable::get(uint64_t index) const {
    if (index >= size()) {
        throw std::out_of_range("dense outcome index out of range");
    }
    return decodeOutcome(outcomes_[static_cast<size_t>(index)]);
}

void DenseOutcomeTable::set(uint64_t index, Outcome outcome) {
    if (index >= size()) {
        throw std::out_of_range("dense outcome index out of range");
    }
    outcomes_[static_cast<size_t>(index)] = encodeOutcome(outcome);
}

uint64_t DenseOutcomeTable::size() const {
    return static_cast<uint64_t>(outcomes_.size());
}

uint64_t DenseOutcomeTable::bytes() const {
    return static_cast<uint64_t>(outcomes_.size());
}

const std::vector<uint8_t>& DenseOutcomeTable::payload() const {
    return outcomes_;
}

PackedOutcomeTable2Bit::PackedOutcomeTable2Bit(uint64_t stateCount, Outcome initial)
    : stateCount_(stateCount),
      packed_(static_cast<size_t>((stateCount + 3u) / 4u), 0) {
    if ((stateCount + 3u) / 4u > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("packed dense outcome table is too large for this platform");
    }
    if (initial != Outcome::Unknown) {
        for (uint64_t index = 0; index < stateCount_; ++index) {
            set(index, initial);
        }
    }
}

Outcome PackedOutcomeTable2Bit::get(uint64_t index) const {
    if (index >= stateCount_) {
        throw std::out_of_range("packed outcome index out of range");
    }
    const size_t byteIndex = static_cast<size_t>(index / 4u);
    const int shift = static_cast<int>((index % 4u) * 2u);
    return decodeOutcome(static_cast<uint8_t>((packed_[byteIndex] >> shift) & 0x3u));
}

void PackedOutcomeTable2Bit::set(uint64_t index, Outcome outcome) {
    if (index >= stateCount_) {
        throw std::out_of_range("packed outcome index out of range");
    }
    const size_t byteIndex = static_cast<size_t>(index / 4u);
    const int shift = static_cast<int>((index % 4u) * 2u);
    const uint8_t clearMask = static_cast<uint8_t>(~(uint8_t{0x3u} << shift));
    packed_[byteIndex] = static_cast<uint8_t>((packed_[byteIndex] & clearMask) | (encodeOutcome(outcome) << shift));
}

uint64_t PackedOutcomeTable2Bit::size() const {
    return stateCount_;
}

uint64_t PackedOutcomeTable2Bit::bytes() const {
    return static_cast<uint64_t>(packed_.size());
}

const std::vector<uint8_t>& PackedOutcomeTable2Bit::payload() const {
    return packed_;
}

uint64_t denseResultPayloadBytes(uint64_t stateCount, DenseResultEncoding encoding) {
    switch (encoding) {
        case DenseResultEncoding::Byte:
            return stateCount;
        case DenseResultEncoding::Packed2Bit:
            return (stateCount + 3u) / 4u;
    }
    throw std::invalid_argument("invalid dense result encoding");
}

void saveDenseResultTable(
    const DenseOutcomeTable& table,
    int soldierCount,
    const std::filesystem::path& path,
    uint64_t rulesetHash) {
    validateSoldierCount(soldierCount);
    if (table.size() != denseStateCount(soldierCount)) {
        throw std::invalid_argument("dense byte table size does not match soldier count");
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open .s15res for writing: " + path.string());
    }
    writeDenseResultHeader(output, DenseResultFileInfo{
        DenseResultVersion,
        rulesetHash,
        soldierCount,
        table.size(),
        DenseResultEncoding::Byte,
        table.bytes(),
    });
    writePayload(output, table.payload());
}

void saveDenseResultTable2Bit(
    const PackedOutcomeTable2Bit& table,
    int soldierCount,
    const std::filesystem::path& path,
    uint64_t rulesetHash) {
    validateSoldierCount(soldierCount);
    if (table.size() != denseStateCount(soldierCount)) {
        throw std::invalid_argument("dense 2-bit table size does not match soldier count");
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open .s15res for writing: " + path.string());
    }
    writeDenseResultHeader(output, DenseResultFileInfo{
        DenseResultVersion,
        rulesetHash,
        soldierCount,
        table.size(),
        DenseResultEncoding::Packed2Bit,
        table.bytes(),
    });
    writePayload(output, table.payload());
}

DenseOutcomeTable loadDenseResultTableByte(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open .s15res for reading: " + path.string());
    }
    const DenseResultFileInfo info = readDenseResultHeader(input);
    validateHeaderExpectations(info, expectedRulesetHash, expectedSoldierCount);
    if (info.encoding != DenseResultEncoding::Byte) {
        throw std::runtime_error(".s15res is not byte encoded");
    }

    const std::vector<uint8_t> payload = readPayload(input, info.payloadBytes);
    rejectTrailingBytes(input);
    DenseOutcomeTable table(info.stateCount);
    for (uint64_t index = 0; index < info.stateCount; ++index) {
        table.set(index, decodeOutcome(payload[static_cast<size_t>(index)]));
    }
    return table;
}

PackedOutcomeTable2Bit loadDenseResultTable2Bit(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open .s15res for reading: " + path.string());
    }
    const DenseResultFileInfo info = readDenseResultHeader(input);
    validateHeaderExpectations(info, expectedRulesetHash, expectedSoldierCount);
    if (info.encoding != DenseResultEncoding::Packed2Bit) {
        throw std::runtime_error(".s15res is not 2-bit encoded");
    }

    const std::vector<uint8_t> payload = readPayload(input, info.payloadBytes);
    rejectTrailingBytes(input);
    PackedOutcomeTable2Bit table(info.stateCount);
    for (uint64_t index = 0; index < info.stateCount; ++index) {
        const size_t byteIndex = static_cast<size_t>(index / 4u);
        const int shift = static_cast<int>((index % 4u) * 2u);
        table.set(index, decodeOutcome(static_cast<uint8_t>((payload[byteIndex] >> shift) & 0x3u)));
    }
    return table;
}

DenseResultFileInfo inspectDenseResultFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("failed to open .s15res for inspection: " + path.string());
    }
    const std::streamoff fileSize = input.tellg();
    input.seekg(0);
    const DenseResultFileInfo info = readDenseResultHeader(input);
    const uint64_t headerBytes = 8u + 4u + 8u + 4u + 8u + 4u + 8u;
    if (fileSize < 0 || static_cast<uint64_t>(fileSize) != headerBytes + info.payloadBytes) {
        throw std::runtime_error(".s15res file size does not match header");
    }
    return info;
}

DenseResultFileInfo validateDenseResultFile(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount) {
    const DenseResultFileInfo info = inspectDenseResultFile(path);
    validateHeaderExpectations(info, expectedRulesetHash, expectedSoldierCount);
    return info;
}

void createEmptyDenseResultFile(
    int soldierCount,
    const std::filesystem::path& path,
    DenseResultEncoding encoding,
    uint64_t rulesetHash) {
    validateSoldierCount(soldierCount);
    const uint64_t stateCount = denseStateCount(soldierCount);
    const uint64_t payloadBytes = denseResultPayloadBytes(stateCount, encoding);
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open .s15res for writing: " + path.string());
    }
    writeDenseResultHeader(output, DenseResultFileInfo{
        DenseResultVersion,
        rulesetHash,
        soldierCount,
        stateCount,
        encoding,
        payloadBytes,
    });

    std::array<char, 65536> zeros{};
    uint64_t remaining = payloadBytes;
    while (remaining > 0) {
        const uint64_t chunk = std::min<uint64_t>(remaining, zeros.size());
        output.write(zeros.data(), static_cast<std::streamsize>(chunk));
        if (!output) {
            throw std::runtime_error("failed to write empty .s15res payload");
        }
        remaining -= chunk;
    }
}

}  // namespace sanpao15

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "sanpao15/move.h"

namespace sanpao15 {

enum class DenseResultEncoding : uint32_t {
    Byte = 1,
    Packed2Bit = 2,
};

class DenseOutcomeTable {
public:
    explicit DenseOutcomeTable(uint64_t stateCount, Outcome initial = Outcome::Unknown);

    Outcome get(uint64_t index) const;
    void set(uint64_t index, Outcome outcome);

    uint64_t size() const;
    uint64_t bytes() const;
    const std::vector<uint8_t>& payload() const;

private:
    std::vector<uint8_t> outcomes_;
};

class PackedOutcomeTable2Bit {
public:
    explicit PackedOutcomeTable2Bit(uint64_t stateCount, Outcome initial = Outcome::Unknown);

    Outcome get(uint64_t index) const;
    void set(uint64_t index, Outcome outcome);
    Outcome getUnchecked(uint64_t index) const;
    void setUnchecked(uint64_t index, Outcome outcome);

    uint64_t size() const;
    uint64_t bytes() const;
    const std::vector<uint8_t>& payload() const;
    std::vector<uint8_t>& mutablePayload();

private:
    uint64_t stateCount_ = 0;
    std::vector<uint8_t> packed_;
};

struct DenseResultFileInfo {
    uint32_t version = 0;
    uint64_t rulesetHash = 0;
    int soldierCount = 0;
    uint64_t stateCount = 0;
    DenseResultEncoding encoding = DenseResultEncoding::Byte;
    uint64_t payloadBytes = 0;
};

uint8_t encodeOutcome(Outcome outcome);
Outcome decodeOutcome(uint8_t value);

uint64_t denseResultPayloadBytes(uint64_t stateCount, DenseResultEncoding encoding);

void saveDenseResultTable(
    const DenseOutcomeTable& table,
    int soldierCount,
    const std::filesystem::path& path,
    uint64_t rulesetHash);
void saveDenseResultTable2Bit(
    const PackedOutcomeTable2Bit& table,
    int soldierCount,
    const std::filesystem::path& path,
    uint64_t rulesetHash);

DenseOutcomeTable loadDenseResultTableByte(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount);
PackedOutcomeTable2Bit loadDenseResultTable2Bit(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount);

DenseResultFileInfo inspectDenseResultFile(const std::filesystem::path& path);
DenseResultFileInfo validateDenseResultFile(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount = -1);

void createEmptyDenseResultFile(
    int soldierCount,
    const std::filesystem::path& path,
    DenseResultEncoding encoding,
    uint64_t rulesetHash);

void resetOutcomeTable(PackedOutcomeTable2Bit& table, Outcome value = Outcome::Unknown);

}  // namespace sanpao15

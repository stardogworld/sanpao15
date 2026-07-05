#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "sanpao15/mapped_file.h"
#include "sanpao15/move.h"

namespace sanpao15 {

enum class DenseResultEncoding : uint32_t {
    Byte = 1,
    Packed2Bit = 2,
};

class PackedOutcomeTable2BitView;

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
    PackedOutcomeTable2BitView view() const noexcept;

private:
    uint64_t stateCount_ = 0;
    std::vector<uint8_t> packed_;
};

class PackedOutcomeTable2BitView {
public:
    PackedOutcomeTable2BitView() = default;
    PackedOutcomeTable2BitView(const uint8_t* payload, uint64_t stateCount, uint64_t payloadBytes) noexcept;

    uint64_t size() const noexcept;
    uint64_t bytes() const noexcept;
    bool empty() const noexcept;
    Outcome getUnchecked(uint64_t index) const;

private:
    const uint8_t* payload_ = nullptr;
    uint64_t stateCount_ = 0;
    uint64_t payloadBytes_ = 0;
};

struct DenseResultFileInfo {
    uint32_t version = 0;
    uint64_t rulesetHash = 0;
    int soldierCount = 0;
    uint64_t stateCount = 0;
    DenseResultEncoding encoding = DenseResultEncoding::Byte;
    uint64_t payloadBytes = 0;
};

class MappedPackedOutcomeTable2Bit {
public:
    MappedPackedOutcomeTable2Bit(
        const std::filesystem::path& path,
        uint64_t expectedRulesetHash,
        int expectedSoldierCount);

    const DenseResultFileInfo& info() const noexcept;
    PackedOutcomeTable2BitView view() const noexcept;

private:
    ReadOnlyMappedFile file_;
    DenseResultFileInfo info_;
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

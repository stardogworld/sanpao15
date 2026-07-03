#include "sanpao15/material_target_distance.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>

#include "sanpao15/bitboard.h"
#include "sanpao15/dense_index.h"
#include "sanpao15/dense_successor.h"
#include "sanpao15/lowk_tablebase.h"
#include "sanpao15/notation.h"
#include "sanpao15/parallel.h"
#include "sanpao15/rules.h"
#include "sanpao15/ruleset.h"

namespace sanpao15 {

namespace {

constexpr std::array<char, 8> MtdMagic{'S', '1', '5', 'M', 'T', 'D', '1', '\0'};
constexpr uint32_t MtdVersion = 2;
constexpr uint64_t MtdHeaderBytes = 44;
constexpr size_t MtdIoBlockBytes = 8u * 1024u * 1024u;
constexpr uint8_t UnassignedMaterial = 0xffu;

void writeU8(std::ostream& output, uint8_t value) {
    output.put(static_cast<char>(value));
    if (!output) {
        throw std::runtime_error("failed to write .s15mtd byte");
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
        throw std::runtime_error("unexpected end of .s15mtd file");
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

void requireLayer(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 15) {
        throw std::invalid_argument("MTD layer must be in 0..15");
    }
}

MtdEncoding encodingFromU32(uint32_t value) {
    switch (value) {
        case 1:
            return MtdEncoding::Packed12Material4Distance8;
        default:
            throw std::runtime_error("unsupported .s15mtd encoding");
    }
}

MtdFileInfo readMtdHeader(std::istream& input) {
    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != MtdMagic) {
        throw std::runtime_error("invalid .s15mtd magic");
    }

    MtdFileInfo info;
    info.version = readU32LE(input);
    if (info.version == 1) {
        throw std::runtime_error("prototype MTD semantic version is obsolete; regenerate .s15mtd");
    }
    if (info.version != MtdVersion) {
        throw std::runtime_error("unsupported .s15mtd version");
    }
    info.rulesetHash = readU64LE(input);
    info.soldierCount = static_cast<int>(readU32LE(input));
    requireLayer(info.soldierCount);
    info.stateCount = readU64LE(input);
    info.encoding = encodingFromU32(readU32LE(input));
    info.payloadBytes = readU64LE(input);
    if (info.stateCount != denseStateCount(info.soldierCount)) {
        throw std::runtime_error(".s15mtd state count does not match soldier-count layer");
    }
    if (info.payloadBytes != mtdPayloadBytes(info.stateCount)) {
        throw std::runtime_error(".s15mtd payload byte count does not match encoding");
    }
    return info;
}

void writeMtdHeader(std::ostream& output, const MtdFileInfo& info) {
    output.write(MtdMagic.data(), static_cast<std::streamsize>(MtdMagic.size()));
    if (!output) {
        throw std::runtime_error("failed to write .s15mtd magic");
    }
    writeU32LE(output, MtdVersion);
    writeU64LE(output, info.rulesetHash);
    writeU32LE(output, static_cast<uint32_t>(info.soldierCount));
    writeU64LE(output, info.stateCount);
    writeU32LE(output, static_cast<uint32_t>(info.encoding));
    writeU64LE(output, info.payloadBytes);
}

void writeBytesChunked(
    std::ostream& output,
    const uint8_t* data,
    uint64_t byteCount,
    const std::string& errorContext) {
    if (byteCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error(errorContext + " is too large for this platform");
    }
    uint64_t offset = 0;
    while (offset < byteCount) {
        const uint64_t remaining = byteCount - offset;
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, MtdIoBlockBytes));
        output.write(
            reinterpret_cast<const char*>(data + static_cast<size_t>(offset)),
            static_cast<std::streamsize>(chunk));
        if (!output) {
            std::ostringstream message;
            message << "failed to write " << errorContext
                    << " after " << offset << " of " << byteCount << " bytes";
            throw std::runtime_error(message.str());
        }
        offset += chunk;
    }
}

std::vector<uint8_t> readPayloadChunked(std::istream& input, uint64_t payloadBytes, size_t chunkBytes) {
    if (payloadBytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error(".s15mtd payload is too large for this platform");
    }
    if (chunkBytes == 0) {
        throw std::invalid_argument(".s15mtd read chunk size must be positive");
    }
    std::vector<uint8_t> payload(static_cast<size_t>(payloadBytes), 0);
    uint64_t offset = 0;
    while (offset < payloadBytes) {
        const uint64_t remaining = payloadBytes - offset;
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, static_cast<uint64_t>(chunkBytes)));
        input.read(
            reinterpret_cast<char*>(payload.data() + static_cast<size_t>(offset)),
            static_cast<std::streamsize>(chunk));
        const std::streamsize bytesRead = input.gcount();
        if (bytesRead != static_cast<std::streamsize>(chunk)) {
            const uint64_t totalRead = offset + static_cast<uint64_t>(std::max<std::streamsize>(bytesRead, 0));
            std::ostringstream message;
            message << "unexpected end of .s15mtd payload after "
                    << totalRead << " of " << payloadBytes << " bytes";
            throw std::runtime_error(message.str());
        }
        offset += chunk;
    }
    return payload;
}

std::vector<uint8_t> readPayload(std::istream& input, uint64_t payloadBytes) {
    return readPayloadChunked(input, payloadBytes, MtdIoBlockBytes);
}

void rejectTrailingBytes(std::istream& input) {
    const int extra = input.get();
    if (extra != std::char_traits<char>::eof()) {
        throw std::runtime_error(".s15mtd contains trailing bytes");
    }
}

void validateExpectations(const MtdFileInfo& info, uint64_t expectedRulesetHash, int expectedSoldierCount) {
    if (info.rulesetHash != expectedRulesetHash) {
        throw std::runtime_error(".s15mtd ruleset hash mismatch");
    }
    if (expectedSoldierCount >= 0 && info.soldierCount != expectedSoldierCount) {
        throw std::runtime_error(".s15mtd soldier count mismatch");
    }
}

void validateEntryForLayer(MtdEntry entry, int soldierCount) {
    if (entry.materialTarget > 15 || entry.materialTarget > soldierCount) {
        throw std::runtime_error(".s15mtd materialTarget is outside 0..soldierCount");
    }
}

std::string jsonEscape(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
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
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string rulesetHashHex() {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << StandardRulesetHash;
    return out.str();
}

PackedOutcomeTable2Bit loadDenseResultAnyEncoding(const std::filesystem::path& path, int soldierCount) {
    const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, soldierCount);
    if (info.encoding == DenseResultEncoding::Packed2Bit) {
        return loadDenseResultTable2Bit(path, StandardRulesetHash, soldierCount);
    }
    const DenseOutcomeTable byteTable = loadDenseResultTableByte(path, StandardRulesetHash, soldierCount);
    PackedOutcomeTable2Bit packed(byteTable.size());
    for (uint64_t index = 0; index < byteTable.size(); ++index) {
        packed.setUnchecked(index, byteTable.get(index));
    }
    return packed;
}

struct LoadedWdlTableView {
    std::optional<PackedOutcomeTable2Bit> owned;
    std::optional<MappedPackedOutcomeTable2Bit> mapped;
    PackedOutcomeTable2BitView view;
    uint64_t mappedBytes = 0;
    uint64_t explicitRamBytes = 0;
};

LoadedWdlTableView loadWdlTableView(
    const std::filesystem::path& path,
    int soldierCount,
    MtdTableStore store) {
    LoadedWdlTableView loaded;
    if (store == MtdTableStore::Mmap) {
        const DenseResultFileInfo info = inspectDenseResultFile(path);
        if (info.rulesetHash != StandardRulesetHash) {
            throw std::runtime_error(".s15res ruleset hash mismatch");
        }
        if (info.soldierCount != soldierCount) {
            throw std::runtime_error(".s15res soldier count mismatch");
        }
        if (info.encoding == DenseResultEncoding::Packed2Bit) {
            loaded.mapped.emplace(path, StandardRulesetHash, soldierCount);
            loaded.view = loaded.mapped->view();
            loaded.mappedBytes = loaded.mapped->info().payloadBytes + MtdHeaderBytes;
            return loaded;
        }
        throw std::runtime_error(".s15res mmap view requires packed 2-bit encoding");
    }
    loaded.owned = loadDenseResultAnyEncoding(path, soldierCount);
    loaded.view = loaded.owned->view();
    loaded.explicitRamBytes = loaded.owned->bytes();
    return loaded;
}

struct LoadedMtdTableView {
    std::optional<PackedMtdTable12> owned;
    std::optional<MappedMtdTable12> mapped;
    MtdTable12View view;
    uint64_t mappedBytes = 0;
    uint64_t explicitRamBytes = 0;
};

LoadedMtdTableView loadMtdTableView(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount,
    MtdTableStore store) {
    LoadedMtdTableView loaded;
    if (store == MtdTableStore::Mmap) {
        loaded.mapped.emplace(path, expectedRulesetHash, expectedSoldierCount);
        loaded.view = loaded.mapped->view();
        loaded.mappedBytes = loaded.mapped->info().fileSize;
        return loaded;
    }
    loaded.owned = loadMtdTable(path, expectedRulesetHash, expectedSoldierCount);
    loaded.view = loaded.owned->view();
    loaded.explicitRamBytes = loaded.owned->bytes();
    return loaded;
}

std::filesystem::path wdlLayerPath(const std::filesystem::path& dir, int soldierCount) {
    return lowKLayerResultPath(dir, soldierCount);
}

bool wdlPreserving(Outcome current, Outcome successor) {
    return current != Outcome::Unknown && successor == current;
}

size_t outcomeIndex(Outcome outcome) {
    return static_cast<size_t>(outcome);
}

Side winnerForOutcome(Outcome outcome) {
    switch (outcome) {
        case Outcome::CannonWin:
            return Side::Cannon;
        case Outcome::SoldierWin:
            return Side::Soldier;
        default:
            throw std::invalid_argument("MTD win-distance solver requires a decisive outcome");
    }
}

bool isWinningOutcome(Outcome outcome) {
    return outcome == Outcome::CannonWin || outcome == Outcome::SoldierWin;
}

bool isTerminalForMtdOutcome(const DenseTerminalInfo& terminal, Outcome outcome) {
    return terminal.terminal && terminal.outcome == outcome;
}

template <class T>
void releaseVectorCapacity(std::vector<T>& value) {
    std::vector<T>().swap(value);
}

uint64_t denseBitsetBytesForBits(uint64_t bitCount) {
    const uint64_t wordCount = bitCount / 64u + (bitCount % 64u == 0 ? 0u : 1u);
    if (wordCount > std::numeric_limits<uint64_t>::max() / sizeof(uint64_t)) {
        throw std::overflow_error("dense bitset byte size overflows uint64_t");
    }
    return wordCount * sizeof(uint64_t);
}

Outcome successorOutcomeFor(
    const DenseSuccessor& successor,
    const PackedOutcomeTable2BitView& currentWdl,
    const PackedOutcomeTable2BitView* lowerWdl) {
    if (successor.kind == DenseSuccessorKind::SameLayer) {
        return currentWdl.getUnchecked(successor.toIndex);
    }
    if (lowerWdl == nullptr) {
        throw std::logic_error("capture successor requires lower WDL layer");
    }
    return lowerWdl->getUnchecked(successor.toIndex);
}

MtdEntry successorMtdFor(
    const DenseSuccessor& successor,
    const std::vector<uint8_t>& currentMaterial,
    const MtdDistanceWork& currentDistance,
    const MtdTable12View* lowerMtd) {
    if (successor.kind == DenseSuccessorKind::SameLayer) {
        return mtdEntryFromWorkArrays(currentMaterial, currentDistance, successor.toIndex);
    }
    if (lowerMtd == nullptr) {
        throw std::logic_error("capture successor requires lower MTD layer");
    }
    return lowerMtd->getUnchecked(successor.toIndex);
}

MtdEntry successorMtdFromTables(
    const DenseSuccessor& successor,
    const MtdTable12View& currentMtd,
    const MtdTable12View* lowerMtd) {
    if (successor.kind == DenseSuccessorKind::SameLayer) {
        return currentMtd.getUnchecked(successor.toIndex);
    }
    if (lowerMtd == nullptr) {
        throw std::logic_error("capture successor requires lower MTD layer");
    }
    return lowerMtd->getUnchecked(successor.toIndex);
}

MtdEntry lookupSuccessorMtd(const DenseSuccessor& successor, const std::filesystem::path& mtdDir) {
    return lookupMtdEntryAt(mtdDir, successor.toSoldierCount, successor.toIndex);
}

bool thresholdChildTrue(
    const DenseSuccessor& successor,
    Outcome currentOutcome,
    const PackedOutcomeTable2BitView& currentWdl,
    const PackedOutcomeTable2BitView* lowerWdl,
    const MtdTable12View* lowerMtd,
    const std::vector<uint8_t>& currentMaterial,
    const DenseBitset& thresholdNewTrue,
    int threshold) {
    const Outcome childOutcome = successorOutcomeFor(successor, currentWdl, lowerWdl);
    if (!wdlPreserving(currentOutcome, childOutcome)) {
        return false;
    }
    if (successor.kind == DenseSuccessorKind::CaptureToLowerLayer) {
        if (lowerMtd == nullptr) {
            throw std::logic_error("capture threshold requires lower MTD layer");
        }
        return lowerMtd->getUnchecked(successor.toIndex).materialTarget <= threshold;
    }
    return currentMaterial[static_cast<size_t>(successor.toIndex)] != UnassignedMaterial ||
        thresholdNewTrue.getUnchecked(successor.toIndex);
}

uint8_t computeDistanceForState(
    int soldierCount,
    uint64_t index,
    const Position& pos,
    uint8_t materialTarget,
    const PackedOutcomeTable2BitView& currentWdl,
    const PackedOutcomeTable2BitView* lowerWdl,
    const MtdTable12View* lowerMtd,
    const std::vector<uint8_t>& currentMaterial,
    const MtdDistanceWork& currentDistance,
    std::vector<DenseSuccessor>& successors) {
    if (materialTarget == soldierCount) {
        return 0;
    }
    const Outcome currentOutcome = currentWdl.getUnchecked(index);
    if (currentOutcome == Outcome::Unknown) {
        return MtdSaturatedDistance;
    }

    generateDenseSuccessorsFromPosition(soldierCount, index, pos, successors);
    bool any = false;
    uint8_t best = pos.side == Side::Cannon ? MtdSaturatedDistance : 0;
    for (const DenseSuccessor& successor : successors) {
        const Outcome childOutcome = successorOutcomeFor(successor, currentWdl, lowerWdl);
        if (!wdlPreserving(currentOutcome, childOutcome)) {
            continue;
        }
        const MtdEntry child = successorMtdFor(successor, currentMaterial, currentDistance, lowerMtd);
        if (child.materialTarget != materialTarget) {
            continue;
        }
        const uint8_t candidate = saturatedAdd1(child.guaranteeDistance);
        any = true;
        if (pos.side == Side::Cannon) {
            best = std::min(best, candidate);
        } else {
            best = std::max(best, candidate);
        }
    }
    return any ? best : MtdSaturatedDistance;
}

uint32_t checkedMtdQueueIndex(uint64_t index);

void solveWinningOutcomeMtd(
    int k,
    Outcome outcome,
    const PackedOutcomeTable2BitView& currentWdl,
    const PackedOutcomeTable2BitView* lowerWdl,
    const MtdTable12View* lowerMtd,
    std::vector<uint8_t>& material,
    MtdDistanceWork& distance,
    uint64_t& queuePeak,
    uint64_t& iterations,
    uint32_t threads) {
    const uint64_t stateCount = denseStateCount(k);
    const uint32_t effectiveThreads = normalizeThreadCount(threads, stateCount);
    const Side winner = winnerForOutcome(outcome);
    DenseBitset finalized(stateCount);
    DenseBitset hasCandidate(stateCount);
    std::vector<uint8_t> loserUnresolved(static_cast<size_t>(stateCount), 0);
    std::array<std::vector<uint32_t>, 256> buckets;
    std::vector<uint32_t> predecessorIndices;
    predecessorIndices.reserve(32);
    uint64_t pendingQueueEntries = 0;

    const auto enqueue = [&](uint8_t bucketIndex, uint64_t index) {
        buckets[bucketIndex].push_back(checkedMtdQueueIndex(index));
        ++pendingQueueEntries;
        queuePeak = std::max(queuePeak, pendingQueueEntries);
    };

    const auto schedule = [&](uint64_t index, Side side, uint8_t candidateDistance, uint8_t candidateMaterial, bool forceEnqueue = false) {
        if (finalized.getUnchecked(index)) {
            return;
        }
        uint8_t& currentMaterial = material[static_cast<size_t>(index)];
        const bool winnerToMove = side == winner;
        bool shouldSchedule = false;
        if (!hasCandidate.getUnchecked(index)) {
            hasCandidate.setUnchecked(index);
            distance.setUnchecked(index, candidateDistance);
            currentMaterial = candidateMaterial;
            shouldSchedule = true;
        } else if (winnerToMove) {
            const uint8_t currentDistance = distance.getUnchecked(index);
            if (candidateDistance < currentDistance ||
                (candidateDistance == currentDistance &&
                 ((winner == Side::Cannon && candidateMaterial < currentMaterial) ||
                  (winner == Side::Soldier && candidateMaterial > currentMaterial)))) {
                distance.setUnchecked(index, candidateDistance);
                currentMaterial = candidateMaterial;
                shouldSchedule = true;
            }
        } else {
            const uint8_t currentDistance = distance.getUnchecked(index);
            if (candidateDistance > currentDistance ||
                (candidateDistance == currentDistance &&
                 ((winner == Side::Cannon && candidateMaterial > currentMaterial) ||
                  (winner == Side::Soldier && candidateMaterial < currentMaterial)))) {
                distance.setUnchecked(index, candidateDistance);
                currentMaterial = candidateMaterial;
                shouldSchedule = true;
            }
        }
        if (shouldSchedule || forceEnqueue) {
            enqueue(distance.getUnchecked(index), index);
        }
    };

    using BucketSet = std::array<std::vector<uint32_t>, 256>;
    std::vector<BucketSet> initialBuckets(effectiveThreads);
    parallelForRanges(stateCount, effectiveThreads, [&](uint64_t begin, uint64_t end, uint32_t threadId) {
        std::vector<DenseSuccessor> successors;
        BucketSet& localBuckets = initialBuckets[threadId];
        const auto enqueueInitial = [&](uint8_t bucketIndex, uint64_t index) {
            localBuckets[bucketIndex].push_back(checkedMtdQueueIndex(index));
        };

        for (uint64_t index = begin; index < end; ++index) {
            if (currentWdl.getUnchecked(index) != outcome) {
                continue;
            }
            material[static_cast<size_t>(index)] = static_cast<uint8_t>(k);
            const Position pos = positionFromDenseIndex(k, index);
            generateDenseSuccessorsFromPosition(k, index, pos, successors);
            const DenseTerminalInfo terminal = terminalOutcomeForPositionWithSuccessors(pos, successors);
            if (isTerminalForMtdOutcome(terminal, outcome)) {
                material[static_cast<size_t>(index)] = static_cast<uint8_t>(k);
                distance.setUncheckedAtomic(index, 0);
                hasCandidate.setUncheckedAtomic(index);
                enqueueInitial(0, index);
                continue;
            }

            uint16_t unresolvedSameLayerChildren = 0;
            bool hasKnownChild = false;
            uint8_t knownDistance = pos.side == winner ? MtdSaturatedDistance : 0;
            uint8_t knownMaterial = pos.side == winner
                ? (winner == Side::Cannon ? 15 : 0)
                : (winner == Side::Cannon ? 0 : 15);
            for (const DenseSuccessor& successor : successors) {
                const Outcome childOutcome = successorOutcomeFor(successor, currentWdl, lowerWdl);
                if (childOutcome != outcome) {
                    continue;
                }
                if (successor.kind == DenseSuccessorKind::CaptureToLowerLayer) {
                    if (lowerMtd == nullptr) {
                        throw std::logic_error("winning MTD capture requires lower MTD layer");
                    }
                    const MtdEntry child = lowerMtd->getUnchecked(successor.toIndex);
                    const uint8_t candidate = saturatedAdd1(child.guaranteeDistance);
                    hasKnownChild = true;
                    if (pos.side == winner) {
                        if (candidate < knownDistance ||
                            (candidate == knownDistance &&
                             ((winner == Side::Cannon && child.materialTarget < knownMaterial) ||
                              (winner == Side::Soldier && child.materialTarget > knownMaterial)))) {
                            knownDistance = candidate;
                            knownMaterial = child.materialTarget;
                        }
                    } else if (candidate > knownDistance ||
                               (candidate == knownDistance &&
                                ((winner == Side::Cannon && child.materialTarget > knownMaterial) ||
                                 (winner == Side::Soldier && child.materialTarget < knownMaterial)))) {
                        knownDistance = candidate;
                        knownMaterial = child.materialTarget;
                    }
                } else {
                    ++unresolvedSameLayerChildren;
                }
            }
            if (pos.side == winner) {
                if (hasKnownChild) {
                    hasCandidate.setUncheckedAtomic(index);
                    distance.setUncheckedAtomic(index, knownDistance);
                    material[static_cast<size_t>(index)] = knownMaterial;
                    enqueueInitial(knownDistance, index);
                }
                continue;
            }
            if (unresolvedSameLayerChildren > std::numeric_limits<uint8_t>::max()) {
                throw std::overflow_error("MTD win-distance remaining count overflowed uint8_t");
            }
            loserUnresolved[static_cast<size_t>(index)] = static_cast<uint8_t>(unresolvedSameLayerChildren);
            if (hasKnownChild) {
                material[static_cast<size_t>(index)] = knownMaterial;
                distance.setUncheckedAtomic(index, knownDistance);
                hasCandidate.setUncheckedAtomic(index);
            }
            if (unresolvedSameLayerChildren == 0) {
                const uint8_t seedDistance = hasKnownChild ? knownDistance : MtdSaturatedDistance;
                material[static_cast<size_t>(index)] = hasKnownChild ? knownMaterial : static_cast<uint8_t>(k);
                distance.setUncheckedAtomic(index, seedDistance);
                hasCandidate.setUncheckedAtomic(index);
                enqueueInitial(seedDistance, index);
            }
        }
    });
    for (uint16_t bucketIndex = 0; bucketIndex <= MtdSaturatedDistance; ++bucketIndex) {
        for (BucketSet& localBuckets : initialBuckets) {
            std::vector<uint32_t>& localBucket = localBuckets[bucketIndex];
            pendingQueueEntries += localBucket.size();
            buckets[bucketIndex].insert(buckets[bucketIndex].end(), localBucket.begin(), localBucket.end());
            std::vector<uint32_t>().swap(localBucket);
        }
        if (bucketIndex == MtdSaturatedDistance) {
            break;
        }
    }
    queuePeak = std::max(queuePeak, pendingQueueEntries);

    for (uint16_t bucketIndex = 0; bucketIndex <= MtdSaturatedDistance; ++bucketIndex) {
        std::vector<uint32_t>& bucket = buckets[bucketIndex];
        for (size_t head = 0; head < bucket.size(); ++head) {
            const uint32_t childIndex = bucket[head];
            if (pendingQueueEntries > 0) {
                --pendingQueueEntries;
            }
            if (currentWdl.getUnchecked(childIndex) != outcome ||
                finalized.getUnchecked(childIndex) ||
                !distance.isSolvedUnchecked(childIndex) ||
                distance.getUnchecked(childIndex) != bucketIndex) {
                continue;
            }
            finalized.setUnchecked(childIndex);
            ++iterations;

            const Position child = positionFromDenseIndex(k, childIndex);
            const Outcome childOutcome = currentWdl.getUnchecked(childIndex);
            const Side parentSide = opposite(child.side);
            const uint8_t childDistance = distance.getUnchecked(childIndex);
            const uint8_t candidateDistance = saturatedAdd1(childDistance);
            const uint8_t candidateMaterial = material[static_cast<size_t>(childIndex)];
            generateDensePredecessorIndicesFromPosition(k, childIndex, child, predecessorIndices);
            for (uint32_t parentIndex : predecessorIndices) {
                if (currentWdl.getUnchecked(parentIndex) != outcome ||
                    finalized.getUnchecked(parentIndex)) {
                    continue;
                }
                if (!wdlPreserving(currentWdl.getUnchecked(parentIndex), childOutcome)) {
                    continue;
                }
                if (parentSide == winner) {
                    schedule(parentIndex, parentSide, candidateDistance, candidateMaterial);
                } else {
                    uint8_t& unresolved = loserUnresolved[static_cast<size_t>(parentIndex)];
                    if (unresolved == 0) {
                        continue;
                    }
                    if (!hasCandidate.getUnchecked(parentIndex) ||
                        candidateDistance > distance.getUnchecked(parentIndex) ||
                        (candidateDistance == distance.getUnchecked(parentIndex) &&
                         ((winner == Side::Cannon && candidateMaterial > material[static_cast<size_t>(parentIndex)]) ||
                          (winner == Side::Soldier && candidateMaterial < material[static_cast<size_t>(parentIndex)])))) {
                        hasCandidate.setUnchecked(parentIndex);
                        distance.setUnchecked(parentIndex, candidateDistance);
                        material[static_cast<size_t>(parentIndex)] = candidateMaterial;
                    }
                    --unresolved;
                    if (unresolved == 0) {
                        schedule(
                            parentIndex,
                            parentSide,
                            distance.getUnchecked(parentIndex),
                            material[static_cast<size_t>(parentIndex)],
                            true);
                    }
                }
            }
        }
        releaseVectorCapacity(bucket);
        if (bucketIndex == MtdSaturatedDistance) {
            break;
        }
    }

    parallelForRanges(stateCount, effectiveThreads, [&](uint64_t begin, uint64_t end, uint32_t) {
        for (uint64_t index = begin; index < end; ++index) {
            if (currentWdl.getUnchecked(index) == outcome && !finalized.getUnchecked(index)) {
                distance.setUncheckedAtomic(index, MtdSaturatedDistance);
                if (material[static_cast<size_t>(index)] == UnassignedMaterial) {
                    material[static_cast<size_t>(index)] = static_cast<uint8_t>(k);
                }
            }
        }
    });
    for (std::vector<uint32_t>& bucket : buckets) {
        releaseVectorCapacity(bucket);
    }
    releaseVectorCapacity(predecessorIndices);
    releaseVectorCapacity(loserUnresolved);
}

struct MtdMemoryEstimate {
    uint64_t explicitRamBytes = 0;
    uint64_t mappedBytes = 0;
    uint64_t currentWdlBytes = 0;
    uint64_t lowerWdlBytes = 0;
    uint64_t lowerMtdBytes = 0;
    uint64_t queueScratchBytes = 0;
};

MtdMemoryEstimate estimateMtdMemoryBytes(
    int soldierCount,
    uint64_t stateCount,
    MtdTableStore lowerMtdStore,
    MtdTableStore wdlStore) {
    const uint64_t bitsetBytes = denseBitsetBytesForBits(stateCount);
    MtdMemoryEstimate estimate;
    estimate.explicitRamBytes += stateCount;  // material targets
    estimate.explicitRamBytes += stateCount;  // uint8 distance values
    estimate.explicitRamBytes += bitsetBytes;  // distance solved bits
    estimate.currentWdlBytes = denseResultPayloadBytes(stateCount, DenseResultEncoding::Packed2Bit);
    if (wdlStore == MtdTableStore::Mmap) {
        estimate.mappedBytes += MtdHeaderBytes + estimate.currentWdlBytes;
    } else {
        estimate.explicitRamBytes += estimate.currentWdlBytes;
    }
    if (soldierCount >= MinSoldiersForSoldierSurvival) {
        estimate.explicitRamBytes += bitsetBytes * 5u;  // winning/draw finalized and candidate flags plus draw threshold bits
        estimate.explicitRamBytes += stateCount * 3u;  // loserUnresolved, remaining, soldierUnresolved byte counters
        estimate.queueScratchBytes = stateCount * sizeof(uint32_t);
        estimate.explicitRamBytes += estimate.queueScratchBytes;
    }
    if (soldierCount >= MinSoldiersForSoldierSurvival) {
        estimate.lowerWdlBytes = denseResultPayloadBytes(denseStateCount(soldierCount - 1), DenseResultEncoding::Packed2Bit);
        estimate.lowerMtdBytes = mtdPayloadBytes(denseStateCount(soldierCount - 1));
        if (wdlStore == MtdTableStore::Mmap) {
            estimate.mappedBytes += MtdHeaderBytes + estimate.lowerWdlBytes;
        } else {
            estimate.explicitRamBytes += estimate.lowerWdlBytes;
        }
        if (lowerMtdStore == MtdTableStore::Mmap) {
            estimate.mappedBytes += MtdHeaderBytes + estimate.lowerMtdBytes;
        } else {
            estimate.explicitRamBytes += estimate.lowerMtdBytes;
        }
    }
    return estimate;
}

void writeMtdStatsJson(const MtdLayerSolveResult& result) {
    const std::filesystem::path parent = result.statsPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    const std::filesystem::path tempPath = result.statsPath.string() + ".tmp";
    std::ofstream out(tempPath);
    if (!out) {
        throw std::runtime_error("failed to open .mtd.solve.json for writing: " + tempPath.string());
    }

    out << "{\n";
    out << "  \"format\": \"sanpao15-material-target-distance-layer-stats\",\n";
    out << "  \"version\": 1,\n";
    out << "  \"semanticVersion\": 2,\n";
    out << "  \"semanticName\": \"outcome-aware-material-target-distance\",\n";
    out << "  \"distanceMeaning\": {\n";
    out << "    \"CannonWin\": \"shortest forced cannon win in plies\",\n";
    out << "    \"SoldierWin\": \"shortest forced soldier encirclement win in plies\",\n";
    out << "    \"Draw\": \"adversarial delay to the draw material target in plies\"\n";
    out << "  },\n";
    out << "  \"ruleset\": \"" << RulesetName << "\",\n";
    out << "  \"rulesetHash\": \"" << rulesetHashHex() << "\",\n";
    out << "  \"soldierCount\": " << result.soldierCount << ",\n";
    out << "  \"threads\": " << result.threads << ",\n";
    out << "  \"parallelStages\": [\n";
    for (size_t i = 0; i < result.parallelStages.size(); ++i) {
        out << "    \"" << jsonEscape(result.parallelStages[i]) << "\""
            << (i + 1 == result.parallelStages.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"stateCount\": " << result.stateCount << ",\n";
    out << "  \"encoding\": \"" << mtdEncodingToString(MtdEncoding::Packed12Material4Distance8) << "\",\n";
    out << "  \"outputPath\": \"" << jsonEscape(result.outputPath.string()) << "\",\n";
    out << "  \"payloadBytes\": " << mtdPayloadBytes(result.stateCount) << ",\n";
    out << "  \"outputBytes\": " << result.outputBytes << ",\n";
    out << "  \"outcomeCounts\": {\n";
    out << "    \"Unknown\": " << result.outcomeCounts[outcomeIndex(Outcome::Unknown)] << ",\n";
    out << "    \"CannonWin\": " << result.outcomeCounts[outcomeIndex(Outcome::CannonWin)] << ",\n";
    out << "    \"SoldierWin\": " << result.outcomeCounts[outcomeIndex(Outcome::SoldierWin)] << ",\n";
    out << "    \"Draw\": " << result.outcomeCounts[outcomeIndex(Outcome::Draw)] << "\n";
    out << "  },\n";
    out << "  \"materialTargetCounts\": {\n";
    for (size_t i = 0; i < result.materialTargetCounts.size(); ++i) {
        out << "    \"" << i << "\": " << result.materialTargetCounts[i]
            << (i + 1 == result.materialTargetCounts.size() ? "\n" : ",\n");
    }
    out << "  },\n";
    out << "  \"cannonMaxCapturesCounts\": {\n";
    for (size_t i = 0; i < result.cannonMaxCapturesCounts.size(); ++i) {
        out << "    \"" << i << "\": " << result.cannonMaxCapturesCounts[i]
            << (i + 1 == result.cannonMaxCapturesCounts.size() ? "\n" : ",\n");
    }
    out << "  },\n";
    out << "  \"distanceCounts\": {\n";
    for (size_t i = 0; i < result.distanceCounts.size(); ++i) {
        if (result.distanceCounts[i] == 0) {
            continue;
        }
        out << "    \"" << i << "\": " << result.distanceCounts[i] << ",\n";
    }
    out << "    \"_end\": 0\n";
    out << "  },\n";
    out << "  \"maxExactDistance\": " << static_cast<int>(result.maxExactDistance) << ",\n";
    out << "  \"saturatedDistanceCount\": " << result.saturatedDistanceCount << ",\n";
    out << "  \"stageMaterialSeconds\": " << std::setprecision(12) << result.stageMaterialSeconds << ",\n";
    out << "  \"stageDistanceSeconds\": " << std::setprecision(12) << result.stageDistanceSeconds << ",\n";
    out << "  \"totalSeconds\": " << std::setprecision(12) << result.totalSeconds << ",\n";
    out << "  \"memoryMode\": {\n";
    out << "    \"lowerMtdStore\": \"" << mtdTableStoreToString(result.lowerMtdStore) << "\",\n";
    out << "    \"wdlStore\": \"" << mtdTableStoreToString(result.wdlStore) << "\"\n";
    out << "  },\n";
    out << "  \"estimatedMemoryBytes\": " << result.estimatedMemoryBytes << ",\n";
    out << "  \"estimatedExplicitRamBytes\": " << result.estimatedExplicitRamBytes << ",\n";
    out << "  \"estimatedMappedBytes\": " << result.estimatedMappedBytes << ",\n";
    out << "  \"estimatedCurrentWdlBytes\": " << result.estimatedCurrentWdlBytes << ",\n";
    out << "  \"estimatedLowerWdlBytes\": " << result.estimatedLowerWdlBytes << ",\n";
    out << "  \"estimatedLowerMtdBytes\": " << result.estimatedLowerMtdBytes << ",\n";
    out << "  \"estimatedQueueScratchBytes\": " << result.estimatedQueueScratchBytes << ",\n";
    out << "  \"queuePeak\": " << result.queuePeak << ",\n";
    out << "  \"materialIterations\": " << result.materialIterations << ",\n";
    out << "  \"distanceIterations\": " << result.distanceIterations << "\n";
    out << "}\n";
    if (!out) {
        throw std::runtime_error("failed to write .mtd.solve.json: " + tempPath.string());
    }
    out.close();
    if (!out) {
        throw std::runtime_error("failed to close .mtd.solve.json: " + tempPath.string());
    }
    if (std::filesystem::exists(result.statsPath)) {
        std::filesystem::remove(result.statsPath);
    }
    std::filesystem::rename(tempPath, result.statsPath);
}

void fillStatsFromWriteStats(MtdLayerSolveResult& result, const MtdLayerWriteStats& stats) {
    result.materialTargetCounts.fill(0);
    result.cannonMaxCapturesCounts.fill(0);
    result.distanceCounts.fill(0);
    result.materialTargetCounts = stats.materialTargetCounts;
    result.cannonMaxCapturesCounts = stats.cannonMaxCapturesCounts;
    result.distanceCounts = stats.distanceCounts;
    result.maxExactDistance = stats.maxExactDistance;
    result.saturatedDistanceCount = stats.saturatedDistanceCount;
}

void validateWdlLayer(const std::filesystem::path& wdlDir, int soldierCount) {
    const std::filesystem::path path = wdlLayerPath(wdlDir, soldierCount);
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("missing WDL layer: " + path.string());
    }
    const DenseResultFileInfo info = inspectDenseResultFile(path);
    if (info.rulesetHash != StandardRulesetHash) {
        throw std::runtime_error(".s15res ruleset hash mismatch");
    }
    if (info.soldierCount != soldierCount) {
        throw std::runtime_error(".s15res soldier count mismatch");
    }
}

uint32_t checkedMtdQueueIndex(uint64_t index) {
    if (index > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        throw std::overflow_error("MTD prototype queue requires 32-bit dense indexes");
    }
    return static_cast<uint32_t>(index);
}

uint8_t minMaterialTargetInTable(const PackedMtdTable12& table) {
    uint8_t minTarget = 15;
    for (uint64_t index = 0; index < table.size(); ++index) {
        minTarget = std::min(minTarget, table.getUnchecked(index).materialTarget);
    }
    return minTarget;
}

}  // namespace

uint16_t encodeMtdEntry(MtdEntry entry) {
    if (entry.materialTarget > 15) {
        throw std::invalid_argument("materialTarget must fit in 4 bits");
    }
    return static_cast<uint16_t>(entry.materialTarget | (static_cast<uint16_t>(entry.guaranteeDistance) << 4u));
}

MtdEntry decodeMtdEntry(uint16_t encoded) {
    if ((encoded & 0xf000u) != 0) {
        throw std::invalid_argument("encoded MTD entry must be 12 bits");
    }
    return MtdEntry{
        static_cast<uint8_t>(encoded & 0x0fu),
        static_cast<uint8_t>((encoded >> 4u) & 0xffu),
    };
}

uint8_t saturatedAdd1(uint8_t distance) {
    return distance >= 254u ? MtdSaturatedDistance : static_cast<uint8_t>(distance + 1u);
}

uint64_t mtdPayloadBytes(uint64_t stateCount) {
    if (stateCount > (std::numeric_limits<uint64_t>::max() / 3u) * 2u) {
        throw std::overflow_error(".s15mtd payload size overflows uint64_t");
    }
    return (stateCount * 3u + 1u) / 2u;
}

int firstMtdDrawMaterialThreshold() {
    return MinSoldiersForSoldierSurvival;
}

uint64_t mtdDrawMaterialThresholdRounds(int soldierCount) {
    requireLayer(soldierCount);
    if (soldierCount <= firstMtdDrawMaterialThreshold()) {
        return 0;
    }
    return static_cast<uint64_t>(soldierCount - firstMtdDrawMaterialThreshold());
}

MtdEntry mtdEntryFromWorkArrays(
    const std::vector<uint8_t>& material,
    const MtdDistanceWork& distance,
    uint64_t index) {
    if (index >= static_cast<uint64_t>(material.size()) || index >= distance.size()) {
        throw std::out_of_range("MTD work-array index out of range");
    }
    const uint8_t materialTarget = material[static_cast<size_t>(index)];
    if (materialTarget == UnassignedMaterial) {
        throw std::runtime_error("MTD work arrays contain unassigned material target");
    }
    if (!distance.isSolvedUnchecked(index)) {
        throw std::logic_error("same-layer MTD successor distance is unsolved");
    }
    return MtdEntry{materialTarget, distance.getUnchecked(index)};
}

MtdDistanceWork::MtdDistanceWork(uint64_t stateCount) {
    reset(stateCount);
}

void MtdDistanceWork::reset(uint64_t stateCount) {
    if (stateCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("MTD distance work array is too large for this platform");
    }
    values.assign(static_cast<size_t>(stateCount), MtdSaturatedDistance);
    solved.reset(stateCount, false);
}

uint64_t MtdDistanceWork::size() const noexcept {
    return static_cast<uint64_t>(values.size());
}

uint64_t MtdDistanceWork::bytes() const noexcept {
    return static_cast<uint64_t>(values.size()) + solved.bytes();
}

bool MtdDistanceWork::isSolved(uint64_t index) const {
    if (index >= size()) {
        throw std::out_of_range("MTD distance index out of range");
    }
    return solved.getUnchecked(index);
}

bool MtdDistanceWork::isSolvedUnchecked(uint64_t index) const noexcept {
    return solved.getUnchecked(index);
}

uint8_t MtdDistanceWork::get(uint64_t index) const {
    if (index >= size()) {
        throw std::out_of_range("MTD distance index out of range");
    }
    if (!solved.getUnchecked(index)) {
        throw std::logic_error("MTD guarantee distance is unsolved");
    }
    return values[static_cast<size_t>(index)];
}

uint8_t MtdDistanceWork::getUnchecked(uint64_t index) const noexcept {
    return values[static_cast<size_t>(index)];
}

void MtdDistanceWork::set(uint64_t index, uint8_t value) {
    if (index >= size()) {
        throw std::out_of_range("MTD distance index out of range");
    }
    setUnchecked(index, value);
}

void MtdDistanceWork::setUnchecked(uint64_t index, uint8_t value) noexcept {
    values[static_cast<size_t>(index)] = value;
    solved.setUnchecked(index);
}

void MtdDistanceWork::setUncheckedAtomic(uint64_t index, uint8_t value) noexcept {
    values[static_cast<size_t>(index)] = value;
    solved.setUncheckedAtomic(index);
}

void MtdDistanceWork::markUnsolved(uint64_t index) {
    if (index >= size()) {
        throw std::out_of_range("MTD distance index out of range");
    }
    markUnsolvedUnchecked(index);
}

void MtdDistanceWork::markUnsolvedUnchecked(uint64_t index) noexcept {
    values[static_cast<size_t>(index)] = MtdSaturatedDistance;
    solved.clearUnchecked(index);
}

void MtdDistanceWork::fillSolved(uint8_t value) {
    std::fill(values.begin(), values.end(), value);
    solved.fill(true);
}

MtdThresholdStampScratch::MtdThresholdStampScratch(uint64_t stateCount) {
    reset(stateCount);
}

void MtdThresholdStampScratch::reset(uint64_t stateCount) {
    if (stateCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("threshold stamp state count is too large for this platform");
    }
    stamps_.assign(static_cast<size_t>(stateCount), uint8_t{0});
    currentStamp_ = 0;
}

void MtdThresholdStampScratch::nextRound() {
    ++currentStamp_;
    if (currentStamp_ == 0) {
        std::fill(stamps_.begin(), stamps_.end(), uint8_t{0});
        currentStamp_ = 1;
    }
}

bool MtdThresholdStampScratch::isTrue(uint64_t index, bool materialAssigned) const {
    return materialAssigned ||
        (currentStamp_ != 0 && stamps_[static_cast<size_t>(index)] == currentStamp_);
}

bool MtdThresholdStampScratch::mark(uint64_t index, bool materialAssigned) {
    if (isTrue(index, materialAssigned)) {
        return false;
    }
    stamps_[static_cast<size_t>(index)] = currentStamp_;
    return true;
}

uint8_t MtdThresholdStampScratch::currentStamp() const {
    return currentStamp_;
}

const std::vector<uint8_t>& MtdThresholdStampScratch::stamps() const {
    return stamps_;
}

MtdWdlLayerScanSummary scanSolvedWdlLayer(
    const PackedOutcomeTable2BitView& table,
    int soldierCount,
    const char* label,
    uint32_t threads) {
    const uint32_t effectiveThreads = normalizeThreadCount(threads, table.size());
    struct LocalScan {
        std::array<uint64_t, 4> outcomeCounts{};
        uint64_t firstUnknown = std::numeric_limits<uint64_t>::max();
    };
    std::vector<LocalScan> localScans(effectiveThreads);
    parallelForRanges(table.size(), effectiveThreads, [&](uint64_t begin, uint64_t end, uint32_t threadId) {
        LocalScan& local = localScans[threadId];
        local.outcomeCounts.fill(0);
        for (uint64_t index = begin; index < end; ++index) {
            const Outcome outcome = table.getUnchecked(index);
            ++local.outcomeCounts[outcomeIndex(outcome)];
            if (outcome == Outcome::Unknown && local.firstUnknown == std::numeric_limits<uint64_t>::max()) {
                local.firstUnknown = index;
            }
        }
    });

    MtdWdlLayerScanSummary summary;
    summary.firstUnknown = std::numeric_limits<uint64_t>::max();
    for (const LocalScan& local : localScans) {
        for (size_t i = 0; i < summary.outcomeCounts.size(); ++i) {
            summary.outcomeCounts[i] += local.outcomeCounts[i];
        }
        summary.firstUnknown = std::min(summary.firstUnknown, local.firstUnknown);
    }
    summary.hasUnknown = summary.firstUnknown != std::numeric_limits<uint64_t>::max();
    if (summary.hasUnknown) {
        std::ostringstream message;
        message << "MTD requires solved WDL table; " << label << " layer " << soldierCount
                << " has Unknown at dense index " << summary.firstUnknown;
        throw std::runtime_error(message.str());
    }
    return summary;
}

MtdWdlLayerScanSummary scanSolvedWdlLayer(
    const PackedOutcomeTable2Bit& table,
    int soldierCount,
    const char* label,
    uint32_t threads) {
    return scanSolvedWdlLayer(table.view(), soldierCount, label, threads);
}

MtdTable12View::MtdTable12View(
    const uint8_t* payload,
    uint64_t stateCount,
    uint64_t payloadBytes) noexcept
    : payload_(payload),
      stateCount_(stateCount),
      payloadBytes_(payloadBytes) {}

uint64_t MtdTable12View::size() const noexcept {
    return stateCount_;
}

uint64_t MtdTable12View::bytes() const noexcept {
    return payloadBytes_;
}

bool MtdTable12View::empty() const noexcept {
    return payload_ == nullptr || stateCount_ == 0;
}

const uint8_t* MtdTable12View::payload() const noexcept {
    return payload_;
}

MtdEntry MtdTable12View::getUnchecked(uint64_t index) const {
    const size_t byteIndex = static_cast<size_t>((index / 2u) * 3u);
    if (index % 2u == 0) {
        const uint16_t value = static_cast<uint16_t>(payload_[byteIndex]) |
            (static_cast<uint16_t>(payload_[byteIndex + 1] & 0x0fu) << 8u);
        return decodeMtdEntry(value);
    }
    const uint16_t value = static_cast<uint16_t>((payload_[byteIndex + 1] >> 4u) & 0x0fu) |
        (static_cast<uint16_t>(payload_[byteIndex + 2]) << 4u);
    return decodeMtdEntry(value);
}

PackedMtdTable12::PackedMtdTable12(uint64_t stateCount, MtdEntry initial)
    : stateCount_(stateCount),
      packed_(static_cast<size_t>(mtdPayloadBytes(stateCount)), 0) {
    if (mtdPayloadBytes(stateCount) > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("packed MTD table is too large for this platform");
    }
    if (initial.materialTarget != 0 || initial.guaranteeDistance != 0) {
        for (uint64_t index = 0; index < stateCount_; ++index) {
            setUnchecked(index, initial);
        }
    }
}

MtdEntry PackedMtdTable12::get(uint64_t index) const {
    if (index >= stateCount_) {
        throw std::out_of_range("packed MTD index out of range");
    }
    return getUnchecked(index);
}

void PackedMtdTable12::set(uint64_t index, MtdEntry entry) {
    if (index >= stateCount_) {
        throw std::out_of_range("packed MTD index out of range");
    }
    setUnchecked(index, entry);
}

MtdEntry PackedMtdTable12::getUnchecked(uint64_t index) const {
    return view().getUnchecked(index);
}

void PackedMtdTable12::setUnchecked(uint64_t index, MtdEntry entry) {
    const uint16_t value = encodeMtdEntry(entry);
    const size_t byteIndex = static_cast<size_t>((index / 2u) * 3u);
    if (index % 2u == 0) {
        packed_[byteIndex] = static_cast<uint8_t>(value & 0xffu);
        packed_[byteIndex + 1] = static_cast<uint8_t>((packed_[byteIndex + 1] & 0xf0u) | ((value >> 8u) & 0x0fu));
    } else {
        packed_[byteIndex + 1] = static_cast<uint8_t>((packed_[byteIndex + 1] & 0x0fu) | ((value & 0x0fu) << 4u));
        packed_[byteIndex + 2] = static_cast<uint8_t>((value >> 4u) & 0xffu);
    }
}

uint64_t PackedMtdTable12::size() const {
    return stateCount_;
}

uint64_t PackedMtdTable12::bytes() const {
    return static_cast<uint64_t>(packed_.size());
}

const std::vector<uint8_t>& PackedMtdTable12::payload() const {
    return packed_;
}

std::vector<uint8_t>& PackedMtdTable12::mutablePayload() {
    return packed_;
}

MtdTable12View PackedMtdTable12::view() const noexcept {
    return MtdTable12View(packed_.data(), stateCount_, static_cast<uint64_t>(packed_.size()));
}

MappedMtdTable12::MappedMtdTable12(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount)
    : file_(path) {
    info_ = inspectMtdFile(path);
    validateExpectations(info_, expectedRulesetHash, expectedSoldierCount);
    if (file_.size() != MtdHeaderBytes + info_.payloadBytes) {
        throw std::runtime_error(".s15mtd file size does not match header");
    }
}

const MtdFileInfo& MappedMtdTable12::info() const noexcept {
    return info_;
}

MtdTable12View MappedMtdTable12::view() const noexcept {
    return MtdTable12View(file_.data() + MtdHeaderBytes, info_.stateCount, info_.payloadBytes);
}

std::filesystem::path mtdLayerPath(const std::filesystem::path& dir, int soldierCount) {
    requireLayer(soldierCount);
    std::ostringstream name;
    name << "layer-" << std::setw(2) << std::setfill('0') << soldierCount << ".s15mtd";
    return dir / name.str();
}

std::filesystem::path mtdLayerStatsPath(const std::filesystem::path& dir, int soldierCount) {
    std::filesystem::path path = mtdLayerPath(dir, soldierCount);
    path.replace_extension(".mtd.solve.json");
    return path;
}

void saveMtdTable(
    const PackedMtdTable12& table,
    int soldierCount,
    const std::filesystem::path& path,
    uint64_t rulesetHash) {
    requireLayer(soldierCount);
    if (table.size() != denseStateCount(soldierCount)) {
        throw std::invalid_argument("MTD table size does not match soldier count");
    }
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open .s15mtd for writing: " + path.string());
    }
    writeMtdHeader(output, MtdFileInfo{
        MtdVersion,
        rulesetHash,
        soldierCount,
        table.size(),
        MtdEncoding::Packed12Material4Distance8,
        table.bytes(),
        0,
    });
    if (!table.payload().empty()) {
        writeBytesChunked(
            output,
            table.payload().data(),
            static_cast<uint64_t>(table.payload().size()),
            ".s15mtd payload: " + path.string());
    }
}

MtdLayerWriteStats writeMtdTableFromArrays(
    const std::filesystem::path& path,
    int soldierCount,
    const std::vector<uint8_t>& material,
    const MtdDistanceWork& distance,
    uint64_t rulesetHash) {
    requireLayer(soldierCount);
    const uint64_t stateCount = denseStateCount(soldierCount);
    if (stateCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("MTD layer is too large for this platform");
    }
    if (material.size() != static_cast<size_t>(stateCount) || distance.size() != stateCount) {
        throw std::invalid_argument("MTD material/distance arrays do not match soldier-count layer size");
    }

    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open .s15mtd for writing: " + path.string());
    }

    const uint64_t payloadBytes = mtdPayloadBytes(stateCount);
    writeMtdHeader(output, MtdFileInfo{
        MtdVersion,
        rulesetHash,
        soldierCount,
        stateCount,
        MtdEncoding::Packed12Material4Distance8,
        payloadBytes,
        0,
    });

    MtdLayerWriteStats stats;
    stats.stateCount = stateCount;
    stats.outputBytes = MtdHeaderBytes + payloadBytes;

    const auto entryAt = [&](uint64_t index) {
        MtdEntry entry;
        try {
            entry = mtdEntryFromWorkArrays(material, distance, index);
        } catch (const std::logic_error&) {
            throw std::runtime_error("MTD writer found unsolved guarantee distance");
        } catch (const std::runtime_error&) {
            throw std::runtime_error("MTD writer found unassigned material target");
        }
        validateEntryForLayer(entry, soldierCount);
        ++stats.materialTargetCounts[entry.materialTarget];
        ++stats.cannonMaxCapturesCounts[static_cast<size_t>(soldierCount - entry.materialTarget)];
        ++stats.distanceCounts[entry.guaranteeDistance];
        if (entry.guaranteeDistance == MtdSaturatedDistance) {
            ++stats.saturatedDistanceCount;
        } else {
            stats.maxExactDistance = std::max(stats.maxExactDistance, entry.guaranteeDistance);
        }
        return entry;
    };

    std::vector<uint8_t> writeBuffer;
    writeBuffer.reserve(MtdIoBlockBytes);
    const auto appendPayloadByte = [&](uint8_t byte) {
        writeBuffer.push_back(byte);
        if (writeBuffer.size() >= MtdIoBlockBytes) {
            output.write(reinterpret_cast<const char*>(writeBuffer.data()), static_cast<std::streamsize>(writeBuffer.size()));
            if (!output) {
                throw std::runtime_error("failed to write .s15mtd payload: " + path.string());
            }
            writeBuffer.clear();
        }
    };
    const auto flushPayloadBuffer = [&] {
        if (!writeBuffer.empty()) {
            output.write(reinterpret_cast<const char*>(writeBuffer.data()), static_cast<std::streamsize>(writeBuffer.size()));
            if (!output) {
                throw std::runtime_error("failed to write .s15mtd payload: " + path.string());
            }
            writeBuffer.clear();
        }
    };

    for (uint64_t index = 0; index < stateCount; index += 2u) {
        const uint16_t first = encodeMtdEntry(entryAt(index));
        if (index + 1u == stateCount) {
            appendPayloadByte(static_cast<uint8_t>(first & 0xffu));
            appendPayloadByte(static_cast<uint8_t>((first >> 8u) & 0x0fu));
            break;
        }
        const uint16_t second = encodeMtdEntry(entryAt(index + 1u));
        appendPayloadByte(static_cast<uint8_t>(first & 0xffu));
        appendPayloadByte(static_cast<uint8_t>(((first >> 8u) & 0x0fu) | ((second & 0x0fu) << 4u)));
        appendPayloadByte(static_cast<uint8_t>((second >> 4u) & 0xffu));
    }
    flushPayloadBuffer();
    if (!output) {
        throw std::runtime_error("failed to write .s15mtd payload: " + path.string());
    }
    output.close();
    if (!output) {
        throw std::runtime_error("failed to close .s15mtd: " + path.string());
    }
    const MtdFileInfo info = validateMtdHeaderOnly(path, rulesetHash, soldierCount);
    stats.outputBytes = info.fileSize;
    return stats;
}

PackedMtdTable12 loadMtdTable(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open .s15mtd for reading: " + path.string());
    }
    const MtdFileInfo info = readMtdHeader(input);
    validateExpectations(info, expectedRulesetHash, expectedSoldierCount);
    std::vector<uint8_t> payload = readPayload(input, info.payloadBytes);
    rejectTrailingBytes(input);
    PackedMtdTable12 table(info.stateCount);
    table.mutablePayload() = std::move(payload);
    validateMtdPayload(table, info.soldierCount);
    return table;
}

MtdFileInfo inspectMtdFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("failed to open .s15mtd for inspection: " + path.string());
    }
    const std::streamoff fileSize = input.tellg();
    input.seekg(0);
    MtdFileInfo info = readMtdHeader(input);
    if (fileSize < 0 || static_cast<uint64_t>(fileSize) != MtdHeaderBytes + info.payloadBytes) {
        throw std::runtime_error(".s15mtd file size does not match header");
    }
    info.fileSize = static_cast<uint64_t>(fileSize);
    return info;
}

MtdFileInfo validateMtdHeaderOnly(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount) {
    MtdFileInfo info = inspectMtdFile(path);
    validateExpectations(info, expectedRulesetHash, expectedSoldierCount);
    return info;
}

void validateMtdPayloadView(MtdTable12View table, int soldierCount) {
    requireLayer(soldierCount);
    if (table.size() != denseStateCount(soldierCount)) {
        throw std::runtime_error(".s15mtd payload state count does not match soldier-count layer");
    }
    for (uint64_t index = 0; index < table.size(); ++index) {
        validateEntryForLayer(table.getUnchecked(index), soldierCount);
    }
    if (table.size() % 2u == 1u && table.bytes() != 0) {
        const uint8_t unusedNibble =
            static_cast<uint8_t>((table.payload()[static_cast<size_t>(table.bytes() - 1u)] >> 4u) & 0x0fu);
        if (unusedNibble != 0) {
            throw std::runtime_error(".s15mtd packed payload has non-zero unused odd-entry bits");
        }
    }
}

void validateMtdPayload(const PackedMtdTable12& table, int soldierCount) {
    validateMtdPayloadView(table.view(), soldierCount);
}

MtdFileInfo validateMtdFileFull(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount) {
    MappedMtdTable12 mapped(path, expectedRulesetHash, expectedSoldierCount);
    validateMtdPayloadView(mapped.view(), mapped.info().soldierCount);
    return mapped.info();
}

MtdFileInfo validateMtdFile(
    const std::filesystem::path& path,
    uint64_t expectedRulesetHash,
    int expectedSoldierCount) {
    return validateMtdFileFull(path, expectedRulesetHash, expectedSoldierCount);
}

MtdInspectStats inspectMtdView(const MtdFileInfo& info, MtdTable12View table, uint32_t threads) {
    MtdInspectStats stats;
    stats.info = info;
    stats.threads = normalizeThreadCount(threads, table.size());
    stats.minMaterialTarget = 15;
    struct LocalInspectStats {
        uint8_t minMaterialTarget = 15;
        uint8_t maxMaterialTarget = 0;
        uint8_t maxExactDistance = 0;
        uint64_t saturatedDistanceCount = 0;
        std::array<uint64_t, 16> materialTargetCounts{};
        std::array<uint64_t, 16> cannonMaxCapturesCounts{};
        std::array<uint64_t, 256> distanceCounts{};
    };
    std::vector<LocalInspectStats> localStats(stats.threads);
    parallelForRanges(table.size(), stats.threads, [&](uint64_t begin, uint64_t end, uint32_t threadId) {
        LocalInspectStats& local = localStats[threadId];
        for (uint64_t index = begin; index < end; ++index) {
            const MtdEntry entry = table.getUnchecked(index);
            validateEntryForLayer(entry, stats.info.soldierCount);
            local.minMaterialTarget = std::min(local.minMaterialTarget, entry.materialTarget);
            local.maxMaterialTarget = std::max(local.maxMaterialTarget, entry.materialTarget);
            ++local.materialTargetCounts[entry.materialTarget];
            ++local.cannonMaxCapturesCounts[static_cast<size_t>(stats.info.soldierCount - entry.materialTarget)];
            ++local.distanceCounts[entry.guaranteeDistance];
            if (entry.guaranteeDistance == MtdSaturatedDistance) {
                ++local.saturatedDistanceCount;
            } else {
                local.maxExactDistance = std::max(local.maxExactDistance, entry.guaranteeDistance);
            }
        }
    });
    for (const LocalInspectStats& local : localStats) {
        stats.minMaterialTarget = std::min(stats.minMaterialTarget, local.minMaterialTarget);
        stats.maxMaterialTarget = std::max(stats.maxMaterialTarget, local.maxMaterialTarget);
        stats.maxExactDistance = std::max(stats.maxExactDistance, local.maxExactDistance);
        stats.saturatedDistanceCount += local.saturatedDistanceCount;
        for (size_t i = 0; i < stats.materialTargetCounts.size(); ++i) {
            stats.materialTargetCounts[i] += local.materialTargetCounts[i];
            stats.cannonMaxCapturesCounts[i] += local.cannonMaxCapturesCounts[i];
        }
        for (size_t i = 0; i < stats.distanceCounts.size(); ++i) {
            stats.distanceCounts[i] += local.distanceCounts[i];
        }
    }
    if (table.size() == 0) {
        stats.minMaterialTarget = 0;
    }
    return stats;
}

MtdInspectStats inspectMtdTable(const std::filesystem::path& path, uint32_t threads) {
    return inspectMtdTableMapped(path, threads);
}

MtdInspectStats inspectMtdTableMapped(const std::filesystem::path& path, uint32_t threads) {
    const MtdFileInfo info = inspectMtdFile(path);
    MappedMtdTable12 mapped(path, info.rulesetHash, info.soldierCount);
    return inspectMtdView(mapped.info(), mapped.view(), threads);
}

MtdEntry lookupMtdEntryAt(
    const std::filesystem::path& mtdDir,
    int soldierCount,
    uint64_t denseIndexValue) {
    requireLayer(soldierCount);
    const uint64_t stateCount = denseStateCount(soldierCount);
    if (denseIndexValue >= stateCount) {
        throw std::out_of_range("MTD dense index is outside layer state count");
    }
    const std::filesystem::path path = mtdLayerPath(mtdDir, soldierCount);
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("missing MTD layer: " + path.string());
    }
    const MtdFileInfo info = inspectMtdFile(path);
    validateExpectations(info, StandardRulesetHash, soldierCount);
    const uint64_t pairOffset = (denseIndexValue / 2u) * 3u;
    if (pairOffset >= info.payloadBytes) {
        throw std::runtime_error(".s15mtd payload index is outside payload size: " + path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open .s15mtd for random lookup: " + path.string());
    }
    input.seekg(static_cast<std::streamoff>(MtdHeaderBytes + pairOffset));
    std::array<uint8_t, 3> bytes{};
    const std::streamsize needed = denseIndexValue % 2u == 0 && denseIndexValue + 1u == stateCount ? 2 : 3;
    input.read(reinterpret_cast<char*>(bytes.data()), needed);
    if (!input) {
        throw std::runtime_error("unexpected end of .s15mtd during random lookup: " + path.string());
    }
    if (denseIndexValue % 2u == 0) {
        return decodeMtdEntry(static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1] & 0x0fu) << 8u));
    }
    return decodeMtdEntry(static_cast<uint16_t>((bytes[1] >> 4u) & 0x0fu) | (static_cast<uint16_t>(bytes[2]) << 4u));
}

MtdEntry lookupMtdEntryForPosition(
    const std::filesystem::path& mtdDir,
    const Position& position) {
    return lookupMtdEntryAt(mtdDir, popcount25(position.soldiers), denseIndex(position));
}

MtdLayerSolveResult solveMtdLayer(const MtdLayerSolveOptions& options) {
    requireLayer(options.soldierCount);
    if (options.wdlDir.empty()) {
        throw std::invalid_argument("--solve-mtd-layer requires --wdl-dir");
    }
    if (options.mtdDir.empty()) {
        throw std::invalid_argument("--solve-mtd-layer requires --mtd-dir");
    }
    validateWdlLayer(options.wdlDir, options.soldierCount);
    if (options.soldierCount >= MinSoldiersForSoldierSurvival) {
        validateWdlLayer(options.wdlDir, options.soldierCount - 1);
        (void)validateMtdHeaderOnly(mtdLayerPath(options.mtdDir, options.soldierCount - 1), StandardRulesetHash, options.soldierCount - 1);
    }

    const std::filesystem::path outputPath = mtdLayerPath(options.mtdDir, options.soldierCount);
    const std::filesystem::path statsPath = mtdLayerStatsPath(options.mtdDir, options.soldierCount);
    if (std::filesystem::exists(outputPath) && !options.overwrite) {
        throw std::runtime_error("output .s15mtd already exists; pass --overwrite to replace it: " + outputPath.string());
    }
    if (options.writeStatsJson && std::filesystem::exists(statsPath) && !options.overwrite) {
        throw std::runtime_error("stats .mtd.solve.json already exists; pass --overwrite to replace it: " + statsPath.string());
    }

    const auto totalStart = std::chrono::steady_clock::now();
    const int k = options.soldierCount;
    const uint64_t stateCount = denseStateCount(k);
    const uint32_t solveThreads = normalizeThreadCount(options.threads, stateCount);
    LoadedWdlTableView currentWdlStorage = loadWdlTableView(wdlLayerPath(options.wdlDir, k), k, options.wdlStore);
    std::optional<LoadedWdlTableView> lowerWdlStorage;
    std::optional<LoadedMtdTableView> lowerMtdStorage;
    if (k >= MinSoldiersForSoldierSurvival) {
        lowerWdlStorage = loadWdlTableView(wdlLayerPath(options.wdlDir, k - 1), k - 1, options.wdlStore);
        lowerMtdStorage = loadMtdTableView(mtdLayerPath(options.mtdDir, k - 1), StandardRulesetHash, k - 1, options.lowerMtdStore);
    }
    const PackedOutcomeTable2BitView currentWdl = currentWdlStorage.view;
    const PackedOutcomeTable2BitView* lowerWdl = lowerWdlStorage.has_value() ? &lowerWdlStorage->view : nullptr;
    const MtdTable12View* lowerMtd = lowerMtdStorage.has_value() ? &lowerMtdStorage->view : nullptr;
    const MtdWdlLayerScanSummary currentScan = scanSolvedWdlLayer(currentWdl, k, "current", solveThreads);
    if (lowerWdl != nullptr) {
        (void)scanSolvedWdlLayer(*lowerWdl, k - 1, "lower", solveThreads);
    }

    std::vector<uint8_t> material(static_cast<size_t>(stateCount), UnassignedMaterial);
    MtdDistanceWork distance(stateCount);
    std::vector<DenseSuccessor> successors;
    uint64_t queuePeak = 0;

    const auto materialStart = std::chrono::steady_clock::now();
    uint64_t materialIterations = 0;
    uint64_t winIterations = 0;
    if (k < MinSoldiersForSoldierSurvival) {
        std::fill(material.begin(), material.end(), static_cast<uint8_t>(k));
        distance.fillSolved(0);
    } else {
        if (currentScan.outcomeCounts[outcomeIndex(Outcome::CannonWin)] != 0) {
            solveWinningOutcomeMtd(
                k,
                Outcome::CannonWin,
                currentWdl,
                lowerWdl,
                lowerMtd,
                material,
                distance,
                queuePeak,
                winIterations,
                solveThreads);
        }
        if (currentScan.outcomeCounts[outcomeIndex(Outcome::SoldierWin)] != 0) {
            solveWinningOutcomeMtd(
                k,
                Outcome::SoldierWin,
                currentWdl,
                lowerWdl,
                lowerMtd,
                material,
                distance,
                queuePeak,
                winIterations,
                solveThreads);
        }
        DenseBitset thresholdNewTrue(stateCount);
        std::vector<uint8_t> remaining(static_cast<size_t>(stateCount), 0);
        std::vector<uint32_t> queue;
        std::vector<uint32_t> predecessorIndices;
        queue.reserve(1024);
        predecessorIndices.reserve(32);
        if (currentScan.outcomeCounts[outcomeIndex(Outcome::Draw)] != 0) {
            const int firstDrawThreshold = MinSoldiersForSoldierSurvival;
            for (int threshold = firstDrawThreshold; threshold < k; ++threshold) {
                thresholdNewTrue.fill(false);
                std::fill(remaining.begin(), remaining.end(), uint8_t{0});
                queue.clear();
                size_t queueHead = 0;

                const auto materialAssigned = [&](uint64_t index) {
                    return material[static_cast<size_t>(index)] != UnassignedMaterial;
                };
                const auto isThresholdTrue = [&](uint64_t index) {
                    return materialAssigned(index) || thresholdNewTrue.getUnchecked(index);
                };
                const auto markTrue = [&](uint64_t index) {
                    if (isThresholdTrue(index)) {
                        return;
                    }
                    thresholdNewTrue.setUnchecked(index);
                    queue.push_back(checkedMtdQueueIndex(index));
                    queuePeak = std::max<uint64_t>(queuePeak, queue.size() - queueHead);
                };

                ++materialIterations;
                std::vector<std::vector<uint32_t>> initialQueues(solveThreads);
                parallelForRanges(stateCount, solveThreads, [&](uint64_t begin, uint64_t end, uint32_t threadId) {
                    std::vector<DenseSuccessor> localSuccessors;
                    std::vector<uint32_t>& localQueue = initialQueues[threadId];
                    const auto markInitialTrue = [&](uint64_t index) {
                        if (materialAssigned(index)) {
                            return;
                        }
                        localQueue.push_back(checkedMtdQueueIndex(index));
                    };

                    for (uint64_t index = begin; index < end; ++index) {
                        if (isThresholdTrue(index)) {
                            continue;
                        }
                        const Outcome currentOutcome = currentWdl.getUnchecked(index);
                        if (currentOutcome != Outcome::Draw) {
                            continue;
                        }
                        const Position pos = positionFromDenseIndex(k, index);
                        generateDenseSuccessorsFromPosition(k, index, pos, localSuccessors);
                        const DenseTerminalInfo terminal = terminalOutcomeForPositionWithSuccessors(pos, localSuccessors);
                        if (terminal.terminal) {
                            throw std::runtime_error("MTD draw material solver found terminal Draw state");
                        }

                        bool hasPreserving = false;
                        if (pos.side == Side::Cannon) {
                            bool hasTrueChild = false;
                            for (const DenseSuccessor& successor : localSuccessors) {
                                const Outcome childOutcome = successorOutcomeFor(
                                    successor,
                                    currentWdl,
                                    lowerWdl);
                                if (!wdlPreserving(currentOutcome, childOutcome)) {
                                    continue;
                                }
                                hasPreserving = true;
                                if (thresholdChildTrue(
                                        successor,
                                        currentOutcome,
                                        currentWdl,
                                        lowerWdl,
                                        lowerMtd,
                                        material,
                                        thresholdNewTrue,
                                        threshold)) {
                                    hasTrueChild = true;
                                    break;
                                }
                            }
                            if (hasPreserving && hasTrueChild) {
                                markInitialTrue(index);
                            }
                            continue;
                        }

                        uint16_t falsePreservingChildren = 0;
                        for (const DenseSuccessor& successor : localSuccessors) {
                            const Outcome childOutcome = successorOutcomeFor(
                                successor,
                                currentWdl,
                                lowerWdl);
                            if (!wdlPreserving(currentOutcome, childOutcome)) {
                                continue;
                            }
                            hasPreserving = true;
                            if (!thresholdChildTrue(
                                    successor,
                                    currentOutcome,
                                    currentWdl,
                                    lowerWdl,
                                    lowerMtd,
                                    material,
                                    thresholdNewTrue,
                                    threshold)) {
                                ++falsePreservingChildren;
                            }
                        }
                        if (hasPreserving && falsePreservingChildren == 0) {
                            markInitialTrue(index);
                            continue;
                        }
                        if (falsePreservingChildren > std::numeric_limits<uint8_t>::max()) {
                            throw std::overflow_error("MTD threshold remaining count overflowed uint8_t");
                        }
                        remaining[static_cast<size_t>(index)] = static_cast<uint8_t>(falsePreservingChildren);
                    }
                });
                for (std::vector<uint32_t>& localQueue : initialQueues) {
                    for (uint32_t index : localQueue) {
                        if (!materialAssigned(index) && !thresholdNewTrue.getUnchecked(index)) {
                            thresholdNewTrue.setUnchecked(index);
                            queue.push_back(index);
                        }
                    }
                    std::vector<uint32_t>().swap(localQueue);
                }
                queuePeak = std::max<uint64_t>(queuePeak, queue.size() - queueHead);

                while (queueHead < queue.size()) {
                    const uint32_t childIndex = queue[queueHead++];
                    ++materialIterations;
                    const Outcome childOutcome = currentWdl.getUnchecked(childIndex);
                    if (childOutcome != Outcome::Draw) {
                        continue;
                    }
                    const Position child = positionFromDenseIndex(k, childIndex);
                    const Side parentSide = opposite(child.side);
                    generateDensePredecessorIndicesFromPosition(k, childIndex, child, predecessorIndices);
                    for (uint32_t parentIndex : predecessorIndices) {
                        if (isThresholdTrue(parentIndex)) {
                            continue;
                        }
                        const Outcome parentOutcome = currentWdl.getUnchecked(parentIndex);
                        if (!wdlPreserving(parentOutcome, childOutcome)) {
                            continue;
                        }
                        if (parentSide == Side::Cannon) {
                            markTrue(parentIndex);
                            continue;
                        }
                        uint8_t& remainingCount = remaining[static_cast<size_t>(parentIndex)];
                        if (remainingCount == 0) {
                            continue;
                        }
                        --remainingCount;
                        if (remainingCount == 0) {
                            markTrue(parentIndex);
                        }
                    }
                }

                parallelForRanges(stateCount, solveThreads, [&](uint64_t begin, uint64_t end, uint32_t) {
                    for (uint64_t index = begin; index < end; ++index) {
                        if (material[static_cast<size_t>(index)] == UnassignedMaterial &&
                            thresholdNewTrue.getUnchecked(index)) {
                            material[static_cast<size_t>(index)] = static_cast<uint8_t>(threshold);
                        }
                    }
                });
                releaseVectorCapacity(queue);
            }
        }
        releaseVectorCapacity(remaining);
        releaseVectorCapacity(predecessorIndices);
        parallelForRanges(stateCount, solveThreads, [&](uint64_t begin, uint64_t end, uint32_t) {
            for (uint64_t index = begin; index < end; ++index) {
                if (currentWdl.getUnchecked(index) == Outcome::Draw &&
                    material[static_cast<size_t>(index)] == UnassignedMaterial) {
                    material[static_cast<size_t>(index)] = static_cast<uint8_t>(k);
                    distance.setUncheckedAtomic(index, 0);
                }
            }
        });
    }
    const auto materialFinish = std::chrono::steady_clock::now();

    const auto distanceStart = std::chrono::steady_clock::now();
    uint64_t distanceIterations = 0;
    if (k >= MinSoldiersForSoldierSurvival &&
        currentScan.outcomeCounts[outcomeIndex(Outcome::Draw)] != 0) {
        DenseBitset finalized(stateCount);
        DenseBitset hasCandidate(stateCount);
        std::vector<uint8_t> soldierUnresolved(static_cast<size_t>(stateCount), 0);
        std::array<std::vector<uint32_t>, 256> buckets;
        std::vector<uint32_t> predecessorIndices;
        predecessorIndices.reserve(32);
        uint64_t pendingQueueEntries = 0;

        const auto enqueueDistance = [&](uint8_t bucketIndex, uint64_t index) {
            buckets[bucketIndex].push_back(checkedMtdQueueIndex(index));
            ++pendingQueueEntries;
            queuePeak = std::max(queuePeak, pendingQueueEntries);
        };

        const auto scheduleDistance = [&](uint64_t index, Side side, uint8_t candidate, bool forceEnqueue = false) {
            if (finalized.getUnchecked(index)) {
                return;
            }
            bool shouldSchedule = false;
            if (!hasCandidate.getUnchecked(index)) {
                hasCandidate.setUnchecked(index);
                distance.setUnchecked(index, candidate);
                shouldSchedule = true;
            } else if (side == Side::Cannon) {
                const uint8_t current = distance.getUnchecked(index);
                if (candidate < current) {
                    distance.setUnchecked(index, candidate);
                    shouldSchedule = true;
                }
            } else {
                const uint8_t current = distance.getUnchecked(index);
                if (candidate > current) {
                    distance.setUnchecked(index, candidate);
                    shouldSchedule = true;
                }
            }
            if (!shouldSchedule && !forceEnqueue) {
                return;
            }
            enqueueDistance(distance.getUnchecked(index), index);
        };

        using BucketSet = std::array<std::vector<uint32_t>, 256>;
        std::vector<BucketSet> initialDistanceBuckets(solveThreads);
        parallelForRanges(stateCount, solveThreads, [&](uint64_t begin, uint64_t end, uint32_t threadId) {
            std::vector<DenseSuccessor> localSuccessors;
            BucketSet& localBuckets = initialDistanceBuckets[threadId];
            const auto enqueueInitialDistance = [&](uint8_t bucketIndex, uint64_t index) {
                localBuckets[bucketIndex].push_back(checkedMtdQueueIndex(index));
            };
            for (uint64_t index = begin; index < end; ++index) {
                if (currentWdl.getUnchecked(index) != Outcome::Draw) {
                    continue;
                }
                const uint8_t target = material[static_cast<size_t>(index)];
                if (target == k) {
                    finalized.setUncheckedAtomic(index);
                    distance.setUncheckedAtomic(index, 0);
                    continue;
                }
                const Position pos = positionFromDenseIndex(k, index);
                generateDenseSuccessorsFromPosition(k, index, pos, localSuccessors);
                uint16_t unresolvedSameLayer = 0;
                bool hasKnownOptimal = false;
                uint8_t knownDistance = pos.side == Side::Cannon ? MtdSaturatedDistance : 0;
                for (const DenseSuccessor& successor : localSuccessors) {
                    const Outcome childOutcome = successorOutcomeFor(
                        successor,
                        currentWdl,
                        lowerWdl);
                    if (childOutcome != Outcome::Draw) {
                        continue;
                    }
                    if (successor.kind == DenseSuccessorKind::CaptureToLowerLayer) {
                        if (lowerMtd == nullptr) {
                            throw std::logic_error("draw distance capture requires lower MTD layer");
                        }
                        const MtdEntry child = lowerMtd->getUnchecked(successor.toIndex);
                        if (child.materialTarget != target) {
                            continue;
                        }
                        hasKnownOptimal = true;
                        const uint8_t candidate = saturatedAdd1(child.guaranteeDistance);
                        if (pos.side == Side::Cannon) {
                            knownDistance = std::min(knownDistance, candidate);
                        } else {
                            knownDistance = std::max(knownDistance, candidate);
                        }
                    } else {
                        if (material[static_cast<size_t>(successor.toIndex)] != target) {
                            continue;
                        }
                        ++unresolvedSameLayer;
                    }
                }
                if (unresolvedSameLayer > std::numeric_limits<uint8_t>::max()) {
                    throw std::overflow_error("MTD distance unresolved count overflowed uint8_t");
                }
                soldierUnresolved[static_cast<size_t>(index)] = static_cast<uint8_t>(unresolvedSameLayer);
                if (pos.side == Side::Cannon) {
                    if (hasKnownOptimal) {
                        distance.setUncheckedAtomic(index, knownDistance);
                        hasCandidate.setUncheckedAtomic(index);
                        enqueueInitialDistance(knownDistance, index);
                    }
                    continue;
                }
                if (hasKnownOptimal) {
                    distance.setUncheckedAtomic(index, knownDistance);
                    hasCandidate.setUncheckedAtomic(index);
                }
                if (unresolvedSameLayer == 0) {
                    const uint8_t seedDistance = hasKnownOptimal ? knownDistance : MtdSaturatedDistance;
                    distance.setUncheckedAtomic(index, seedDistance);
                    hasCandidate.setUncheckedAtomic(index);
                    enqueueInitialDistance(seedDistance, index);
                }
            }
        });
        for (uint16_t bucketIndex = 0; bucketIndex <= MtdSaturatedDistance; ++bucketIndex) {
            for (BucketSet& localBuckets : initialDistanceBuckets) {
                std::vector<uint32_t>& localBucket = localBuckets[bucketIndex];
                pendingQueueEntries += localBucket.size();
                buckets[bucketIndex].insert(buckets[bucketIndex].end(), localBucket.begin(), localBucket.end());
                std::vector<uint32_t>().swap(localBucket);
            }
            if (bucketIndex == MtdSaturatedDistance) {
                break;
            }
        }
        queuePeak = std::max(queuePeak, pendingQueueEntries);

        for (uint16_t bucketIndex = 0; bucketIndex <= MtdSaturatedDistance; ++bucketIndex) {
            std::vector<uint32_t>& bucket = buckets[bucketIndex];
            for (size_t head = 0; head < bucket.size(); ++head) {
                const uint32_t childIndex = bucket[head];
                if (pendingQueueEntries > 0) {
                    --pendingQueueEntries;
                }
                if (finalized.getUnchecked(childIndex) ||
                    currentWdl.getUnchecked(childIndex) != Outcome::Draw ||
                    !distance.isSolvedUnchecked(childIndex) ||
                    distance.getUnchecked(childIndex) != bucketIndex) {
                    continue;
                }
                finalized.setUnchecked(childIndex);
                ++distanceIterations;

                const uint8_t childTarget = material[static_cast<size_t>(childIndex)];
                if (childTarget == k) {
                    continue;
                }
                const Position child = positionFromDenseIndex(k, childIndex);
                const Side parentSide = opposite(child.side);
                const uint8_t candidate = saturatedAdd1(distance.getUnchecked(childIndex));
                generateDensePredecessorIndicesFromPosition(k, childIndex, child, predecessorIndices);
                for (uint32_t parentIndex : predecessorIndices) {
                    if (finalized.getUnchecked(parentIndex) ||
                        currentWdl.getUnchecked(parentIndex) != Outcome::Draw ||
                        material[static_cast<size_t>(parentIndex)] != childTarget) {
                        continue;
                    }
                    if (parentSide == Side::Cannon) {
                        scheduleDistance(parentIndex, parentSide, candidate);
                    } else {
                        uint8_t& unresolved = soldierUnresolved[static_cast<size_t>(parentIndex)];
                        if (unresolved == 0) {
                            continue;
                        }
                        if (!hasCandidate.getUnchecked(parentIndex)) {
                            hasCandidate.setUnchecked(parentIndex);
                            distance.setUnchecked(parentIndex, candidate);
                        } else {
                            distance.setUnchecked(
                                parentIndex,
                                std::max(distance.getUnchecked(parentIndex), candidate));
                        }
                        --unresolved;
                        if (unresolved == 0) {
                            scheduleDistance(parentIndex, parentSide, distance.getUnchecked(parentIndex), true);
                        }
                    }
                }
            }
            releaseVectorCapacity(bucket);
            if (bucketIndex == MtdSaturatedDistance) {
                break;
            }
        }

        parallelForRanges(stateCount, solveThreads, [&](uint64_t begin, uint64_t end, uint32_t) {
            for (uint64_t index = begin; index < end; ++index) {
                if (currentWdl.getUnchecked(index) == Outcome::Draw &&
                    material[static_cast<size_t>(index)] != k &&
                    !finalized.getUnchecked(index)) {
                    distance.setUncheckedAtomic(index, MtdSaturatedDistance);
                }
            }
        });
        for (std::vector<uint32_t>& bucket : buckets) {
            releaseVectorCapacity(bucket);
        }
        releaseVectorCapacity(predecessorIndices);
        releaseVectorCapacity(soldierUnresolved);
    }
    const auto distanceFinish = std::chrono::steady_clock::now();

    const std::filesystem::path tmpPath = outputPath.string() + ".tmp";
    if (std::filesystem::exists(tmpPath)) {
        std::filesystem::remove(tmpPath);
    }
    const MtdLayerWriteStats writeStats = writeMtdTableFromArrays(tmpPath, k, material, distance, StandardRulesetHash);
    if (std::filesystem::exists(outputPath)) {
        std::filesystem::remove(outputPath);
    }
    std::filesystem::rename(tmpPath, outputPath);

    MtdLayerSolveResult result;
    result.soldierCount = k;
    result.threads = solveThreads;
    result.stateCount = stateCount;
    result.outputPath = outputPath;
    result.statsPath = statsPath;
    result.parallelStages = {
        "wdlSolvedOutcomeScan",
        "winningInitialScan",
        "drawThresholdInitialScan",
        "drawDistanceInitialScan",
    };
    result.outputBytes = std::filesystem::file_size(outputPath);
    result.stageMaterialSeconds = std::chrono::duration<double>(materialFinish - materialStart).count();
    result.stageDistanceSeconds = std::chrono::duration<double>(distanceFinish - distanceStart).count();
    const auto totalFinish = std::chrono::steady_clock::now();
    result.totalSeconds = std::chrono::duration<double>(totalFinish - totalStart).count();
    const MtdMemoryEstimate memoryEstimate =
        estimateMtdMemoryBytes(k, stateCount, options.lowerMtdStore, options.wdlStore);
    result.lowerMtdStore = options.lowerMtdStore;
    result.wdlStore = options.wdlStore;
    result.estimatedMemoryBytes = memoryEstimate.explicitRamBytes;
    result.estimatedExplicitRamBytes = memoryEstimate.explicitRamBytes;
    result.estimatedMappedBytes = memoryEstimate.mappedBytes;
    result.estimatedCurrentWdlBytes = memoryEstimate.currentWdlBytes;
    result.estimatedLowerWdlBytes = memoryEstimate.lowerWdlBytes;
    result.estimatedLowerMtdBytes = memoryEstimate.lowerMtdBytes;
    result.estimatedQueueScratchBytes = memoryEstimate.queueScratchBytes;
    result.queuePeak = queuePeak;
    result.distanceIterations = distanceIterations + winIterations;
    result.materialIterations = materialIterations;
    result.outcomeCounts = currentScan.outcomeCounts;
    fillStatsFromWriteStats(result, writeStats);
    if (options.writeStatsJson) {
        writeMtdStatsJson(result);
    }
    return result;
}

MtdRangeSolveResult solveMtdRange(const MtdRangeSolveOptions& options) {
    requireLayer(options.startLayer);
    requireLayer(options.endLayer);
    if (options.startLayer > options.endLayer) {
        throw std::invalid_argument("--solve-mtd-range START must be <= END");
    }
    if (options.wdlDir.empty()) {
        throw std::invalid_argument("--solve-mtd-range requires --wdl-dir");
    }
    if (options.mtdDir.empty()) {
        throw std::invalid_argument("--solve-mtd-range requires --mtd-dir");
    }
    if (options.resume && options.overwrite) {
        throw std::invalid_argument("--resume and --overwrite cannot be used together");
    }
    std::filesystem::create_directories(options.mtdDir);

    MtdRangeSolveResult result;
    result.startLayer = options.startLayer;
    result.endLayer = options.endLayer;
    result.wdlDir = options.wdlDir;
    result.mtdDir = options.mtdDir;
    const auto start = std::chrono::steady_clock::now();
    for (int k = options.startLayer; k <= options.endLayer; ++k) {
        const std::filesystem::path outputPath = mtdLayerPath(options.mtdDir, k);
        if (options.resume && std::filesystem::exists(outputPath)) {
            const MtdFileInfo info = validateMtdHeaderOnly(outputPath, StandardRulesetHash, k);
            MtdLayerSolveResult layer;
            layer.soldierCount = k;
            layer.threads = normalizeThreadCount(options.threads, info.stateCount);
            layer.stateCount = info.stateCount;
            layer.outputPath = outputPath;
            layer.statsPath = mtdLayerStatsPath(options.mtdDir, k);
            layer.outputBytes = info.fileSize;
            result.totalOutputBytes += layer.outputBytes;
            result.layers.push_back(layer);
            continue;
        }
        MtdLayerSolveOptions layerOptions;
        layerOptions.soldierCount = k;
        layerOptions.wdlDir = options.wdlDir;
        layerOptions.mtdDir = options.mtdDir;
        layerOptions.overwrite = options.overwrite;
        layerOptions.threads = options.threads;
        layerOptions.lowerMtdStore = options.lowerMtdStore;
        layerOptions.wdlStore = options.wdlStore;
        const MtdLayerSolveResult layer = solveMtdLayer(layerOptions);
        result.totalOutputBytes += layer.outputBytes;
        result.layers.push_back(layer);
    }
    const auto finish = std::chrono::steady_clock::now();
    result.totalSeconds = std::chrono::duration<double>(finish - start).count();
    return result;
}

MtdLayerVerifyResult verifyMtdLayer(const MtdLayerVerifyOptions& options) {
    requireLayer(options.soldierCount);
    validateWdlLayer(options.wdlDir, options.soldierCount);
    LoadedWdlTableView currentWdlStorage =
        loadWdlTableView(wdlLayerPath(options.wdlDir, options.soldierCount), options.soldierCount, options.wdlStore);
    std::optional<LoadedWdlTableView> lowerWdlStorage;
    std::optional<LoadedMtdTableView> lowerMtdStorage;
    if (options.soldierCount >= MinSoldiersForSoldierSurvival) {
        validateWdlLayer(options.wdlDir, options.soldierCount - 1);
        lowerWdlStorage =
            loadWdlTableView(wdlLayerPath(options.wdlDir, options.soldierCount - 1), options.soldierCount - 1, options.wdlStore);
        lowerMtdStorage = loadMtdTableView(
            mtdLayerPath(options.mtdDir, options.soldierCount - 1),
            StandardRulesetHash,
            options.soldierCount - 1,
            options.lowerMtdStore);
    }
    LoadedMtdTableView tableStorage = loadMtdTableView(
        mtdLayerPath(options.mtdDir, options.soldierCount),
        StandardRulesetHash,
        options.soldierCount,
        options.lowerMtdStore);
    const PackedOutcomeTable2BitView currentWdl = currentWdlStorage.view;
    const PackedOutcomeTable2BitView* lowerWdl = lowerWdlStorage.has_value() ? &lowerWdlStorage->view : nullptr;
    const MtdTable12View table = tableStorage.view;
    const MtdTable12View* lowerMtd = lowerMtdStorage.has_value() ? &lowerMtdStorage->view : nullptr;
    const uint32_t verifyThreads = normalizeThreadCount(options.threads, table.size());
    (void)scanSolvedWdlLayer(currentWdl, options.soldierCount, "current", verifyThreads);
    if (lowerWdl != nullptr) {
        (void)scanSolvedWdlLayer(*lowerWdl, options.soldierCount - 1, "lower", verifyThreads);
    }

    MtdLayerVerifyResult result;
    result.soldierCount = options.soldierCount;
    result.threads = normalizeThreadCount(options.threads, table.size());
    result.stateCount = table.size();
    const uint64_t samples = options.sampleLimit == 0 || options.sampleLimit >= table.size()
        ? table.size()
        : options.sampleLimit;
    result.sampledStates = samples;

    const bool fullScan = samples == table.size();
    std::vector<uint64_t> sampleIndices;
    if (!fullScan) {
        sampleIndices.reserve(static_cast<size_t>(samples));
        uint64_t rng = 0x4D54445645524946ull ^ table.size();
        for (uint64_t sample = 0; sample < samples; ++sample) {
            rng = rng * 6364136223846793005ull + 1442695040888963407ull;
            sampleIndices.push_back(rng % table.size());
        }
    }

    struct LocalVerifyResult {
        uint64_t checkedTransitions = 0;
        uint64_t materialTargetKDistanceZero = 0;
        uint8_t maxExactDistance = 0;
        uint64_t saturatedDistanceCount = 0;
    };
    const uint32_t sampleThreads = normalizeThreadCount(options.threads, samples);
    result.threads = sampleThreads;
    std::vector<LocalVerifyResult> localResults(sampleThreads);
    parallelForRanges(samples, sampleThreads, [&](uint64_t begin, uint64_t end, uint32_t threadId) {
        LocalVerifyResult& local = localResults[threadId];
        std::vector<DenseSuccessor> successors;
        for (uint64_t sample = begin; sample < end; ++sample) {
            const uint64_t index = fullScan ? sample : sampleIndices[static_cast<size_t>(sample)];
            const MtdEntry entry = table.getUnchecked(index);
            validateEntryForLayer(entry, options.soldierCount);
            const Outcome currentOutcome = currentWdl.getUnchecked(index);
            if (currentOutcome == Outcome::Unknown) {
                throw std::runtime_error("MTD verify found Unknown WDL outcome");
            }
            if (entry.materialTarget == options.soldierCount && entry.guaranteeDistance == 0) {
                ++local.materialTargetKDistanceZero;
            }
            if (entry.guaranteeDistance == MtdSaturatedDistance) {
                ++local.saturatedDistanceCount;
            } else {
                local.maxExactDistance = std::max(local.maxExactDistance, entry.guaranteeDistance);
            }
            if (options.soldierCount < MinSoldiersForSoldierSurvival) {
                if (currentOutcome != Outcome::CannonWin) {
                    throw std::runtime_error("MTD verify expected base layers K=0..3 to be CannonWin");
                }
                if (entry.materialTarget != options.soldierCount || entry.guaranteeDistance != 0) {
                    throw std::runtime_error("MTD verify expected base layers K=0..3 to be target=k distance=0");
                }
                continue;
            }

            const Position pos = positionFromDenseIndex(options.soldierCount, index);
            generateDenseSuccessorsFromPosition(options.soldierCount, index, pos, successors);
            const DenseTerminalInfo terminal = terminalOutcomeForPositionWithSuccessors(pos, successors);
            if (terminal.terminal) {
                if (terminal.outcome != currentOutcome) {
                    throw std::runtime_error("MTD verify found terminal WDL mismatch");
                }
                if (currentOutcome == Outcome::Draw) {
                    throw std::runtime_error("MTD verify found terminal Draw state");
                }
                if (entry.materialTarget != options.soldierCount || entry.guaranteeDistance != 0) {
                    throw std::runtime_error("MTD verify found terminal win state with non-zero guarantee distance");
                }
                continue;
            }

            if (isWinningOutcome(currentOutcome)) {
                const Side winner = winnerForOutcome(currentOutcome);
                const bool winnerToMove = pos.side == winner;
                bool hasCandidate = false;
                uint8_t expectedDistance = winnerToMove ? MtdSaturatedDistance : 0;
                uint8_t expectedMaterial = winnerToMove
                    ? (winner == Side::Cannon ? 15 : 0)
                    : (winner == Side::Cannon ? 0 : 15);
                for (const DenseSuccessor& successor : successors) {
                    const Outcome childOutcome = successorOutcomeFor(
                        successor,
                        currentWdl,
                        lowerWdl);
                    if (childOutcome == Outcome::Unknown) {
                        throw std::runtime_error("MTD verify found Unknown successor WDL outcome");
                    }
                    if (childOutcome != currentOutcome) {
                        continue;
                    }
                    const MtdEntry child = successorMtdFromTables(
                        successor,
                        table,
                        lowerMtd);
                    const uint8_t candidateDistance = saturatedAdd1(child.guaranteeDistance);
                    const uint8_t candidateMaterial = child.materialTarget;
                    ++local.checkedTransitions;
                    if (!hasCandidate) {
                        hasCandidate = true;
                        expectedDistance = candidateDistance;
                        expectedMaterial = candidateMaterial;
                        continue;
                    }
                    if (winnerToMove) {
                        if (candidateDistance < expectedDistance ||
                            (candidateDistance == expectedDistance &&
                             ((winner == Side::Cannon && candidateMaterial < expectedMaterial) ||
                              (winner == Side::Soldier && candidateMaterial > expectedMaterial)))) {
                            expectedDistance = candidateDistance;
                            expectedMaterial = candidateMaterial;
                        }
                    } else if (candidateDistance > expectedDistance ||
                               (candidateDistance == expectedDistance &&
                                ((winner == Side::Cannon && candidateMaterial > expectedMaterial) ||
                                 (winner == Side::Soldier && candidateMaterial < expectedMaterial)))) {
                        expectedDistance = candidateDistance;
                        expectedMaterial = candidateMaterial;
                    }
                }
                if (!hasCandidate) {
                    throw std::runtime_error("MTD verify found win state without outcome-preserving successor");
                }
                if (entry.guaranteeDistance != expectedDistance || entry.materialTarget != expectedMaterial) {
                    std::ostringstream message;
                    message << "MTD verify found inconsistent win guarantee distance at dense index " << index
                            << ": outcome=" << outcomeToString(currentOutcome)
                            << " entryMaterial=" << static_cast<int>(entry.materialTarget)
                            << " expectedMaterial=" << static_cast<int>(expectedMaterial)
                            << " entryDistance=" << static_cast<int>(entry.guaranteeDistance)
                            << " expectedDistance=" << static_cast<int>(expectedDistance);
                    throw std::runtime_error(message.str());
                }
                continue;
            }

            if (currentOutcome != Outcome::Draw) {
                throw std::runtime_error("MTD verify found unsupported WDL outcome");
            }
            if (entry.materialTarget == options.soldierCount) {
                if (entry.guaranteeDistance != 0) {
                    throw std::runtime_error("MTD verify found Draw materialTarget == k with non-zero guarantee distance");
                }
            }
            bool hasOptimal = entry.materialTarget == options.soldierCount;
            uint8_t expectedDistance = entry.materialTarget == options.soldierCount ? 0 :
                (pos.side == Side::Cannon ? MtdSaturatedDistance : 0);
            for (const DenseSuccessor& successor : successors) {
                const Outcome childOutcome = successorOutcomeFor(
                    successor,
                    currentWdl,
                    lowerWdl);
                if (childOutcome == Outcome::Unknown) {
                    throw std::runtime_error("MTD verify found Unknown successor WDL outcome");
                }
                if (childOutcome != Outcome::Draw) {
                    continue;
                }
                const MtdEntry child = successorMtdFromTables(
                    successor,
                    table,
                    lowerMtd);
                ++local.checkedTransitions;
                if (pos.side == Side::Cannon && child.materialTarget < entry.materialTarget) {
                    throw std::runtime_error("MTD verify found cannon Draw child with smaller materialTarget");
                }
                if (pos.side == Side::Soldier && child.materialTarget > entry.materialTarget) {
                    throw std::runtime_error("MTD verify found soldier Draw child with larger materialTarget");
                }
                if (entry.materialTarget != options.soldierCount &&
                    child.materialTarget == entry.materialTarget) {
                    hasOptimal = true;
                    const uint8_t candidate = saturatedAdd1(child.guaranteeDistance);
                    if (pos.side == Side::Cannon) {
                        expectedDistance = std::min(expectedDistance, candidate);
                    } else {
                        expectedDistance = std::max(expectedDistance, candidate);
                    }
                }
            }
            if (!hasOptimal) {
                throw std::runtime_error("MTD verify found no Draw material-optimal successor");
            }
            if (entry.materialTarget != options.soldierCount && expectedDistance != entry.guaranteeDistance) {
                throw std::runtime_error("MTD verify found inconsistent Draw guarantee distance");
            }
        }
    });
    for (const LocalVerifyResult& local : localResults) {
        result.checkedTransitions += local.checkedTransitions;
        result.materialTargetKDistanceZero += local.materialTargetKDistanceZero;
        result.maxExactDistance = std::max(result.maxExactDistance, local.maxExactDistance);
        result.saturatedDistanceCount += local.saturatedDistanceCount;
    }
    return result;
}

MtdQueryResult queryMtd(const MtdQueryOptions& options) {
    MtdQueryResult result;
    result.position = options.position;
    result.soldierCount = popcount25(options.position.soldiers);
    requireLayer(result.soldierCount);
    result.denseIndex = denseIndex(options.position);
    result.outcome = lookupDenseTablebaseOutcomeAt(options.wdlDir, options.position);
    if (result.outcome == Outcome::Unknown) {
        throw std::runtime_error("MTD query requires solved WDL outcome; position is Unknown");
    }
    result.mtd = lookupMtdEntryForPosition(options.mtdDir, options.position);
    result.cannonMaxCaptures = result.soldierCount - result.mtd.materialTarget;
    result.soldierSaved = result.mtd.materialTarget;

    if (!options.includeMoves) {
        return result;
    }
    const std::vector<DenseSuccessor> successors =
        generateDenseSuccessorsFromPosition(result.soldierCount, result.denseIndex, options.position);
    for (const DenseSuccessor& successor : successors) {
        const Position successorPosition = positionFromDenseIndex(successor.toSoldierCount, successor.toIndex);
        const Outcome successorOutcome = lookupDenseTablebaseOutcomeAt(options.wdlDir, successorPosition);
        if (successorOutcome == Outcome::Unknown) {
            throw std::runtime_error("MTD query requires solved WDL outcome; successor is Unknown");
        }
        const MtdEntry successorMtd = lookupSuccessorMtd(successor, options.mtdDir);
        const bool preserving = wdlPreserving(result.outcome, successorOutcome);
        const bool materialOptimal =
            result.outcome == Outcome::Draw && preserving && successorMtd.materialTarget == result.mtd.materialTarget;
        result.moves.push_back(MtdMoveInfo{
            successor.move,
            successorPosition,
            successor.toSoldierCount,
            successor.toIndex,
            successorOutcome,
            successorMtd,
            preserving,
            materialOptimal,
            false,
        });
    }
    if (isWinningOutcome(result.outcome)) {
        const Side winner = winnerForOutcome(result.outcome);
        const bool winnerToMove = result.position.side == winner;
        bool any = false;
        uint8_t best = winnerToMove ? MtdSaturatedDistance : 0;
        for (const MtdMoveInfo& move : result.moves) {
            if (!move.wdlPreserving) {
                continue;
            }
            any = true;
            const uint8_t candidate = saturatedAdd1(move.successorMtd.guaranteeDistance);
            best = winnerToMove ? std::min(best, candidate) : std::max(best, candidate);
        }
        if (any) {
            for (MtdMoveInfo& move : result.moves) {
                move.distanceOptimal = move.wdlPreserving &&
                    saturatedAdd1(move.successorMtd.guaranteeDistance) == best;
            }
        }
    } else if (result.outcome == Outcome::Draw && result.mtd.materialTarget != result.soldierCount) {
        bool any = false;
        uint8_t best = result.position.side == Side::Cannon ? MtdSaturatedDistance : 0;
        for (const MtdMoveInfo& move : result.moves) {
            if (!move.materialOptimal) {
                continue;
            }
            any = true;
            const uint8_t candidate = saturatedAdd1(move.successorMtd.guaranteeDistance);
            best = result.position.side == Side::Cannon ? std::min(best, candidate) : std::max(best, candidate);
        }
        if (any) {
            for (MtdMoveInfo& move : result.moves) {
                move.distanceOptimal = move.materialOptimal &&
                    saturatedAdd1(move.successorMtd.guaranteeDistance) == best;
            }
        }
    }
    std::sort(result.moves.begin(), result.moves.end(), [](const MtdMoveInfo& lhs, const MtdMoveInfo& rhs) {
        return std::tie(lhs.distanceOptimal, lhs.materialOptimal, lhs.wdlPreserving, lhs.move.from, lhs.move.to, lhs.move.capture, lhs.move.capturedSquare) >
               std::tie(rhs.distanceOptimal, rhs.materialOptimal, rhs.wdlPreserving, rhs.move.from, rhs.move.to, rhs.move.capture, rhs.move.capturedSquare);
    });
    return result;
}

const char* mtdEncodingToString(MtdEncoding encoding) {
    switch (encoding) {
        case MtdEncoding::Packed12Material4Distance8:
            return "packed12-material4-distance8";
    }
    return "unknown";
}

const char* mtdTableStoreToString(MtdTableStore store) {
    switch (store) {
        case MtdTableStore::Ram:
            return "ram";
        case MtdTableStore::Mmap:
            return "mmap";
    }
    return "unknown";
}

std::string mtdDistanceToString(uint8_t distance) {
    if (distance == MtdSaturatedDistance) {
        return ">=255";
    }
    return std::to_string(distance);
}

}  // namespace sanpao15

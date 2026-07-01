#include "sanpao15/lowk_tablebase.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "sanpao15/dense_index.h"
#include "sanpao15/dense_successor.h"
#include "sanpao15/rules.h"
#include "sanpao15/table.h"

namespace sanpao15 {

namespace {

void requireLowK(int maxK) {
    if (maxK < 0 || maxK > 3) {
        throw std::invalid_argument("low-k tablebase prototype supports K in 0..3");
    }
}

void requireLayer(int soldierCount) {
    if (soldierCount < 0 || soldierCount > 3) {
        throw std::invalid_argument("low-k dense layer solver supports soldier count in 0..3");
    }
}

bool isWinForSide(Outcome outcome, Side side) {
    return outcome == winFor(side);
}

bool isOpponentWinForSide(Outcome outcome, Side side) {
    return outcome == opponentWinFor(side);
}

struct SolveGraph {
    std::vector<std::vector<uint32_t>> predecessors;
    std::vector<uint32_t> remainingNonOpponentWin;
};

void countOutcome(DenseLayerSolveResult& result, Outcome outcome) {
    switch (outcome) {
        case Outcome::Unknown:
            ++result.unknown;
            break;
        case Outcome::CannonWin:
            ++result.cannonWin;
            break;
        case Outcome::SoldierWin:
            ++result.soldierWin;
            break;
        case Outcome::Draw:
            ++result.draw;
            break;
    }
}

void countVerifyOutcome(LowKTablebaseVerifyLayerResult& result, Outcome outcome) {
    switch (outcome) {
        case Outcome::Unknown:
            ++result.unknown;
            break;
        case Outcome::CannonWin:
            ++result.cannonWin;
            break;
        case Outcome::SoldierWin:
            ++result.soldierWin;
            break;
        case Outcome::Draw:
            ++result.draw;
            break;
    }
}

PackedOutcomeTable2Bit loadDenseResultAnyEncoding(
    const std::filesystem::path& path,
    int soldierCount) {
    const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, soldierCount);
    if (info.encoding == DenseResultEncoding::Packed2Bit) {
        return loadDenseResultTable2Bit(path, StandardRulesetHash, soldierCount);
    }

    const DenseOutcomeTable byteTable = loadDenseResultTableByte(path, StandardRulesetHash, soldierCount);
    PackedOutcomeTable2Bit packed(byteTable.size());
    for (uint64_t index = 0; index < byteTable.size(); ++index) {
        packed.set(index, byteTable.get(index));
    }
    return packed;
}

std::vector<uint64_t> sampleIndexes(uint64_t stateCount, uint64_t sampleLimit) {
    if (sampleLimit == 0 || sampleLimit >= stateCount) {
        std::vector<uint64_t> indexes;
        indexes.reserve(static_cast<size_t>(stateCount));
        for (uint64_t index = 0; index < stateCount; ++index) {
            indexes.push_back(index);
        }
        return indexes;
    }

    std::vector<uint64_t> indexes;
    indexes.reserve(static_cast<size_t>(sampleLimit));
    uint64_t rng = 0x6C6F776B74626C75ull ^ stateCount;
    for (uint64_t i = 0; i < sampleLimit; ++i) {
        rng = rng * 6364136223846793005ull + 1442695040888963407ull;
        indexes.push_back(rng % stateCount);
    }
    return indexes;
}

Outcome successorOutcome(
    const DenseSuccessor& successor,
    const PackedOutcomeTable2Bit& sameLayer,
    const PackedOutcomeTable2Bit* lowerLayer) {
    if (successor.kind == DenseSuccessorKind::SameLayer) {
        return sameLayer.get(successor.toIndex);
    }
    if (lowerLayer == nullptr) {
        throw std::logic_error("capture successor requires lower-layer outcome table");
    }
    return lowerLayer->get(successor.toIndex);
}

void verifySolvedState(
    int soldierCount,
    uint64_t index,
    const PackedOutcomeTable2Bit& table,
    const PackedOutcomeTable2Bit* lowerLayer) {
    const Position pos = positionFromDenseIndex(soldierCount, index);
    const Outcome outcome = table.get(index);
    if (outcome == Outcome::Unknown) {
        throw std::runtime_error("low-k verifier found Unknown outcome");
    }

    const DenseTerminalInfo terminal = terminalOutcomeForDenseState(soldierCount, index);
    if (terminal.terminal) {
        if (outcome != terminal.outcome) {
            throw std::runtime_error("low-k verifier found terminal outcome mismatch");
        }
        return;
    }

    const std::vector<DenseSuccessor> successors = generateDenseSuccessors(soldierCount, index);
    if (successors.empty()) {
        if (outcome != opponentWinFor(pos.side)) {
            throw std::runtime_error("low-k verifier found no-move non-terminal mismatch");
        }
        return;
    }

    bool hasCurrentSideWin = false;
    bool hasDraw = false;
    for (const DenseSuccessor& successor : successors) {
        const Outcome child = successorOutcome(successor, table, lowerLayer);
        if (child == Outcome::Unknown) {
            throw std::runtime_error("low-k verifier found Unknown successor outcome");
        }
        if (isWinForSide(child, pos.side)) {
            hasCurrentSideWin = true;
        }
        if (child == Outcome::Draw) {
            hasDraw = true;
        }
    }

    if (isWinForSide(outcome, pos.side) && !hasCurrentSideWin) {
        throw std::runtime_error("low-k verifier found side-win state without winning successor");
    }
    if (isOpponentWinForSide(outcome, pos.side) && (hasCurrentSideWin || hasDraw)) {
        throw std::runtime_error("low-k verifier found opponent-win state with winning successor");
    }
    if (outcome == Outcome::Draw && (hasCurrentSideWin || !hasDraw)) {
        throw std::runtime_error("low-k verifier found invalid draw state");
    }
}

}  // namespace

DenseLayerSolveResult solveDenseLayerOutcome(
    int soldierCount,
    const PackedOutcomeTable2Bit* lowerLayer,
    PackedOutcomeTable2Bit& output) {
    requireLayer(soldierCount);
    const uint64_t stateCount = denseStateCount(soldierCount);
    if (output.size() != stateCount) {
        throw std::invalid_argument("output table size does not match dense layer");
    }
    if (soldierCount > 0 && lowerLayer == nullptr) {
        throw std::invalid_argument("lower-layer table is required for soldierCount > 0");
    }
    if (soldierCount == 0 && lowerLayer != nullptr) {
        throw std::invalid_argument("lower-layer table must be null for soldierCount == 0");
    }
    if (stateCount > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        throw std::overflow_error("low-k prototype predecessor graph requires 32-bit state ids");
    }

    const auto start = std::chrono::steady_clock::now();
    DenseLayerSolveResult result;
    result.soldierCount = soldierCount;
    result.stateCount = stateCount;

    SolveGraph graph;
    graph.predecessors.resize(static_cast<size_t>(stateCount));
    graph.remainingNonOpponentWin.assign(static_cast<size_t>(stateCount), 0);

    std::deque<uint32_t> queue;

    for (uint64_t index = 0; index < stateCount; ++index) {
        const DenseTerminalInfo terminal = terminalOutcomeForDenseState(soldierCount, index);
        if (terminal.terminal) {
            output.set(index, terminal.outcome);
            ++result.terminalStates;
            queue.push_back(static_cast<uint32_t>(index));
            continue;
        }

        const Position pos = positionFromDenseIndex(soldierCount, index);
        const std::vector<DenseSuccessor> successors = generateDenseSuccessors(soldierCount, index);
        bool resolved = false;
        uint32_t remaining = 0;
        for (const DenseSuccessor& successor : successors) {
            if (successor.kind == DenseSuccessorKind::SameLayer) {
                ++result.sameLayerEdges;
                graph.predecessors[static_cast<size_t>(successor.toIndex)].push_back(static_cast<uint32_t>(index));
                if (!resolved) {
                    ++remaining;
                }
                continue;
            }

            ++result.captureEdges;
            const Outcome child = lowerLayer->get(successor.toIndex);
            if (isWinForSide(child, pos.side) && !resolved) {
                output.set(index, child);
                queue.push_back(static_cast<uint32_t>(index));
                resolved = true;
            }
            if (child == Outcome::Draw && !resolved) {
                ++remaining;
            }
        }

        if (!resolved) {
            graph.remainingNonOpponentWin[static_cast<size_t>(index)] = remaining;
            if (remaining == 0) {
                output.set(index, opponentWinFor(pos.side));
                queue.push_back(static_cast<uint32_t>(index));
            }
        }
    }

    while (!queue.empty()) {
        const uint32_t childIndex = queue.front();
        queue.pop_front();
        const Outcome childOutcome = output.get(childIndex);
        if (childOutcome == Outcome::Unknown || childOutcome == Outcome::Draw) {
            continue;
        }

        for (uint32_t parentIndex : graph.predecessors[childIndex]) {
            if (output.get(parentIndex) != Outcome::Unknown) {
                continue;
            }
            const Position parent = positionFromDenseIndex(soldierCount, parentIndex);
            if (isWinForSide(childOutcome, parent.side)) {
                output.set(parentIndex, childOutcome);
                ++result.retrogradeResolved;
                queue.push_back(parentIndex);
                continue;
            }
            if (isOpponentWinForSide(childOutcome, parent.side)) {
                uint32_t& remaining = graph.remainingNonOpponentWin[parentIndex];
                if (remaining == 0) {
                    throw std::logic_error("remainingNonOpponentWin underflow");
                }
                --remaining;
                if (remaining == 0) {
                    output.set(parentIndex, opponentWinFor(parent.side));
                    ++result.retrogradeResolved;
                    queue.push_back(parentIndex);
                }
            }
        }
    }

    for (uint64_t index = 0; index < stateCount; ++index) {
        Outcome outcome = output.get(index);
        if (outcome == Outcome::Unknown) {
            output.set(index, Outcome::Draw);
            outcome = Outcome::Draw;
            ++result.unresolvedAsDraw;
        }
        countOutcome(result, outcome);
    }

    result.unknown = 0;
    const auto finish = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(finish - start).count();
    return result;
}

std::filesystem::path lowKLayerResultPath(const std::filesystem::path& dir, int soldierCount) {
    std::ostringstream name;
    name << "layer-" << std::setw(2) << std::setfill('0') << soldierCount << ".s15res";
    return dir / name.str();
}

std::vector<LowKTablebaseLayerResult> solveLowKTablebase(const LowKTablebaseSolveOptions& options) {
    requireLowK(options.maxK);
    if (options.outputDir.empty()) {
        throw std::invalid_argument("low-k output directory is required");
    }
    std::filesystem::create_directories(options.outputDir);

    std::vector<LowKTablebaseLayerResult> results;
    std::vector<PackedOutcomeTable2Bit> solvedLayers;
    solvedLayers.reserve(static_cast<size_t>(options.maxK + 1));

    for (int k = 0; k <= options.maxK; ++k) {
        PackedOutcomeTable2Bit table(denseStateCount(k));
        const PackedOutcomeTable2Bit* lower = k == 0 ? nullptr : &solvedLayers[static_cast<size_t>(k - 1)];
        const DenseLayerSolveResult solve = solveDenseLayerOutcome(k, lower, table);

        const std::filesystem::path outputPath = lowKLayerResultPath(options.outputDir, k);
        if (options.encoding == DenseResultEncoding::Packed2Bit) {
            saveDenseResultTable2Bit(table, k, outputPath, StandardRulesetHash);
        } else {
            DenseOutcomeTable byteTable(table.size());
            for (uint64_t index = 0; index < table.size(); ++index) {
                byteTable.set(index, table.get(index));
            }
            saveDenseResultTable(byteTable, k, outputPath, StandardRulesetHash);
        }

        const DenseResultFileInfo info = validateDenseResultFile(outputPath, StandardRulesetHash, k);
        results.push_back(LowKTablebaseLayerResult{
            solve,
            outputPath,
            std::filesystem::file_size(outputPath),
            options.encoding,
        });
        solvedLayers.push_back(std::move(table));
    }
    return results;
}

LowKTablebaseVerifyResult verifyLowKTablebase(
    const std::filesystem::path& dir,
    int maxK,
    uint64_t sampleLimit) {
    requireLowK(maxK);
    LowKTablebaseVerifyResult result;
    result.inputDir = dir;
    result.maxK = maxK;
    result.sampleLimit = sampleLimit;

    std::vector<PackedOutcomeTable2Bit> tables;
    tables.reserve(static_cast<size_t>(maxK + 1));
    for (int k = 0; k <= maxK; ++k) {
        const std::filesystem::path path = lowKLayerResultPath(dir, k);
        const DenseResultFileInfo info = validateDenseResultFile(path, StandardRulesetHash, k);
        PackedOutcomeTable2Bit table = loadDenseResultAnyEncoding(path, k);

        LowKTablebaseVerifyLayerResult layer;
        layer.soldierCount = k;
        layer.stateCount = table.size();
        layer.encoding = info.encoding;

        for (uint64_t index = 0; index < table.size(); ++index) {
            countVerifyOutcome(layer, table.get(index));
        }

        if (k == 0 && layer.cannonWin != layer.stateCount) {
            throw std::runtime_error("low-k verifier expected every k=0 state to be CannonWin");
        }
        if (layer.unknown != 0) {
            throw std::runtime_error("low-k verifier found Unknown outcomes");
        }

        const PackedOutcomeTable2Bit* lower = k == 0 ? nullptr : &tables[static_cast<size_t>(k - 1)];
        const std::vector<uint64_t> indexes = sampleIndexes(table.size(), sampleLimit);
        layer.sampledStates = static_cast<uint64_t>(indexes.size());
        for (uint64_t index : indexes) {
            verifySolvedState(k, index, table, lower);
        }

        result.layers.push_back(layer);
        tables.push_back(std::move(table));
    }

    return result;
}

}  // namespace sanpao15

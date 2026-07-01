#pragma once

#include <cstdint>
#include <vector>

#include "sanpao15/move.h"
#include "sanpao15/position.h"

namespace sanpao15 {

struct DenseLayerSize {
    int soldierCount = 0;
    uint64_t states = 0;
    uint64_t bytes2BitOutcome = 0;
    uint64_t bytes1ByteOutcome = 0;
};

uint32_t compressMaskToAvailable(uint32_t mask, uint32_t occupiedMask);
uint32_t expandMaskFromAvailable(uint32_t localMask, uint32_t occupiedMask);

uint64_t denseStateCount(int soldierCount);
std::vector<DenseLayerSize> denseLayerSizes();
uint64_t totalDenseStateCount();
uint64_t totalDenseOutcomeBytes2Bit();
uint64_t totalDenseOutcomeBytes1Byte();

uint64_t rankDensePosition(uint32_t cannonMask, uint32_t soldierMask, Side side);
uint64_t denseIndex(const Position& pos);
Position unrankDensePosition(int soldierCount, uint64_t index);
Position positionFromDenseIndex(int soldierCount, uint64_t index);

}  // namespace sanpao15

#include "sanpao15/small_count_array.h"

#include <atomic>
#include <limits>
#include <stdexcept>

namespace sanpao15 {

namespace {

uint64_t checkedByteCount(uint64_t count, SmallCountArray::Width width) {
    const uint64_t bytes = smallCountArrayBytes(count, width);
    if (bytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("small count array is too large for this platform");
    }
    return bytes;
}

}  // namespace

SmallCountArray::SmallCountArray(uint64_t count, Width width)
    : count_(count),
      width_(width),
      data_(static_cast<size_t>(checkedByteCount(count, width)), 0) {}

uint64_t SmallCountArray::size() const noexcept {
    return count_;
}

uint64_t SmallCountArray::bytes() const noexcept {
    return static_cast<uint64_t>(data_.size());
}

SmallCountArray::Width SmallCountArray::width() const noexcept {
    return width_;
}

uint8_t SmallCountArray::get(uint64_t index) const {
    requireIndex(index);
    return getUnchecked(index);
}

void SmallCountArray::set(uint64_t index, uint8_t value) {
    requireIndex(index);
    requireValue(value);
    setUnchecked(index, value);
}

void SmallCountArray::decrement(uint64_t index) {
    requireIndex(index);
    const uint8_t value = getUnchecked(index);
    if (value == 0) {
        throw std::underflow_error("small count decrement underflow");
    }
    setUnchecked(index, static_cast<uint8_t>(value - 1u));
}

uint8_t SmallCountArray::getUnchecked(uint64_t index) const noexcept {
    switch (width_) {
        case Width::Nibble4: {
            const size_t byteIndex = static_cast<size_t>(index / 2u);
            const uint8_t byte = data_[byteIndex];
            return index % 2u == 0
                ? static_cast<uint8_t>(byte & 0x0fu)
                : static_cast<uint8_t>((byte >> 4u) & 0x0fu);
        }
        case Width::Packed6: {
            const uint64_t bitOffset = index * 6u;
            const size_t byteIndex = static_cast<size_t>(bitOffset / 8u);
            const int shift = static_cast<int>(bitOffset % 8u);
            uint16_t value = static_cast<uint16_t>(data_[byteIndex] >> shift);
            if (shift > 2) {
                value |= static_cast<uint16_t>(data_[byteIndex + 1]) << (8 - shift);
            }
            return static_cast<uint8_t>(value & 0x3fu);
        }
        case Width::Byte8:
            return data_[static_cast<size_t>(index)];
    }
    return 0;
}

void SmallCountArray::setUnchecked(uint64_t index, uint8_t value) noexcept {
    switch (width_) {
        case Width::Nibble4: {
            const size_t byteIndex = static_cast<size_t>(index / 2u);
            if (index % 2u == 0) {
                data_[byteIndex] = static_cast<uint8_t>((data_[byteIndex] & 0xf0u) | (value & 0x0fu));
            } else {
                data_[byteIndex] = static_cast<uint8_t>((data_[byteIndex] & 0x0fu) | ((value & 0x0fu) << 4u));
            }
            return;
        }
        case Width::Packed6: {
            const uint64_t bitOffset = index * 6u;
            const size_t byteIndex = static_cast<size_t>(bitOffset / 8u);
            const int shift = static_cast<int>(bitOffset % 8u);
            const uint16_t clearMask = static_cast<uint16_t>(~(uint16_t{0x3fu} << shift));
            uint16_t window = data_[byteIndex];
            if (shift > 2) {
                window |= static_cast<uint16_t>(data_[byteIndex + 1]) << 8u;
            }
            window = static_cast<uint16_t>((window & clearMask) | ((value & 0x3fu) << shift));
            data_[byteIndex] = static_cast<uint8_t>(window & 0xffu);
            if (shift > 2) {
                data_[byteIndex + 1] = static_cast<uint8_t>((window >> 8u) & 0xffu);
            }
            return;
        }
        case Width::Byte8:
            data_[static_cast<size_t>(index)] = value;
            return;
    }
}

void SmallCountArray::setUncheckedAtomic(uint64_t index, uint8_t value) noexcept {
    const auto updateByteBits = [](uint8_t& target, uint8_t mask, uint8_t shiftedValue) noexcept {
        std::atomic_ref<uint8_t> byte(target);
        uint8_t current = byte.load(std::memory_order_relaxed);
        while (!byte.compare_exchange_weak(
            current,
            static_cast<uint8_t>((current & static_cast<uint8_t>(~mask)) | shiftedValue),
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {}
    };

    switch (width_) {
        case Width::Nibble4: {
            const size_t byteIndex = static_cast<size_t>(index / 2u);
            const uint8_t mask = index % 2u == 0 ? uint8_t{0x0f} : uint8_t{0xf0};
            const uint8_t shiftedValue = index % 2u == 0
                ? static_cast<uint8_t>(value & 0x0fu)
                : static_cast<uint8_t>((value & 0x0fu) << 4u);
            updateByteBits(data_[byteIndex], mask, shiftedValue);
            return;
        }
        case Width::Packed6: {
            const uint64_t bitOffset = index * 6u;
            const size_t byteIndex = static_cast<size_t>(bitOffset / 8u);
            const int shift = static_cast<int>(bitOffset % 8u);
            if (shift <= 2) {
                const uint8_t mask = static_cast<uint8_t>(0x3fu << shift);
                const uint8_t shiftedValue = static_cast<uint8_t>((value & 0x3fu) << shift);
                updateByteBits(data_[byteIndex], mask, shiftedValue);
                return;
            }
            const uint8_t lowBits = static_cast<uint8_t>(8 - shift);
            const uint8_t highBits = static_cast<uint8_t>(6 - lowBits);
            const uint8_t lowMask = static_cast<uint8_t>(((uint16_t{1} << lowBits) - 1u) << shift);
            const uint8_t lowValue = static_cast<uint8_t>((value << shift) & lowMask);
            const uint8_t highMask = static_cast<uint8_t>((uint16_t{1} << highBits) - 1u);
            const uint8_t highValue = static_cast<uint8_t>((value >> lowBits) & highMask);
            updateByteBits(data_[byteIndex], lowMask, lowValue);
            updateByteBits(data_[byteIndex + 1], highMask, highValue);
            return;
        }
        case Width::Byte8: {
            std::atomic_ref<uint8_t> byte(data_[static_cast<size_t>(index)]);
            byte.store(value, std::memory_order_relaxed);
            return;
        }
    }
}

void SmallCountArray::decrementUnchecked(uint64_t index) noexcept {
    setUnchecked(index, static_cast<uint8_t>(getUnchecked(index) - 1u));
}

void SmallCountArray::requireIndex(uint64_t index) const {
    if (index >= count_) {
        throw std::out_of_range("small count index out of range");
    }
}

void SmallCountArray::requireValue(uint8_t value) const {
    if (value > smallCountWidthMaxValue(width_)) {
        throw std::out_of_range("small count value exceeds selected width");
    }
}

uint8_t smallCountWidthMaxValue(SmallCountArray::Width width) noexcept {
    switch (width) {
        case SmallCountArray::Width::Nibble4:
            return 15;
        case SmallCountArray::Width::Packed6:
            return 63;
        case SmallCountArray::Width::Byte8:
            return 255;
    }
    return 255;
}

uint64_t smallCountArrayBytes(uint64_t count, SmallCountArray::Width width) {
    switch (width) {
        case SmallCountArray::Width::Nibble4:
            return (count + 1u) / 2u;
        case SmallCountArray::Width::Packed6:
            if (count > (std::numeric_limits<uint64_t>::max() - 7u) / 6u) {
                throw std::overflow_error("packed 6-bit count array byte size overflows uint64_t");
            }
            return (count * 6u + 7u) / 8u;
        case SmallCountArray::Width::Byte8:
            return count;
    }
    throw std::invalid_argument("invalid small count width");
}

SmallCountArray::Width chooseSmallCountWidth(uint8_t maxValue) noexcept {
    if (maxValue <= 15u) {
        return SmallCountArray::Width::Nibble4;
    }
    if (maxValue <= 63u) {
        return SmallCountArray::Width::Packed6;
    }
    return SmallCountArray::Width::Byte8;
}

const char* smallCountWidthToString(SmallCountArray::Width width) noexcept {
    switch (width) {
        case SmallCountArray::Width::Nibble4:
            return "4bit";
        case SmallCountArray::Width::Packed6:
            return "6bit";
        case SmallCountArray::Width::Byte8:
            return "8bit";
    }
    return "unknown";
}

}  // namespace sanpao15

#pragma once

#include <cstdint>
#include <vector>

namespace sanpao15 {

class SmallCountArray {
public:
    enum class Width {
        Nibble4,
        Packed6,
        Byte8,
    };

    SmallCountArray() = default;
    SmallCountArray(uint64_t count, Width width);

    uint64_t size() const noexcept;
    uint64_t bytes() const noexcept;
    Width width() const noexcept;

    uint8_t get(uint64_t index) const;
    void set(uint64_t index, uint8_t value);
    void decrement(uint64_t index);

    uint8_t getUnchecked(uint64_t index) const noexcept;
    void setUnchecked(uint64_t index, uint8_t value) noexcept;
    void setUncheckedAtomic(uint64_t index, uint8_t value) noexcept;
    void decrementUnchecked(uint64_t index) noexcept;

private:
    void requireIndex(uint64_t index) const;
    void requireValue(uint8_t value) const;

    uint64_t count_ = 0;
    Width width_ = Width::Byte8;
    std::vector<uint8_t> data_;
};

uint8_t smallCountWidthMaxValue(SmallCountArray::Width width) noexcept;
uint64_t smallCountArrayBytes(uint64_t count, SmallCountArray::Width width);
SmallCountArray::Width chooseSmallCountWidth(uint8_t maxValue) noexcept;
const char* smallCountWidthToString(SmallCountArray::Width width) noexcept;

}  // namespace sanpao15

#pragma once

#include <cstdint>
#include <vector>

namespace sanpao15 {

class DenseBitset {
public:
    DenseBitset() = default;
    explicit DenseBitset(uint64_t bitCount, bool initial = false);

    void reset(uint64_t bitCount, bool initial = false);
    uint64_t size() const noexcept;
    uint64_t bytes() const noexcept;

    bool get(uint64_t index) const;
    void set(uint64_t index);
    void clear(uint64_t index);
    void assign(uint64_t index, bool value);

    bool getUnchecked(uint64_t index) const noexcept;
    void setUnchecked(uint64_t index) noexcept;
    void setUncheckedAtomic(uint64_t index) noexcept;
    void clearUnchecked(uint64_t index) noexcept;
    void assignUnchecked(uint64_t index, bool value) noexcept;

    void fill(bool value);
    const std::vector<uint64_t>& words() const noexcept;
    std::vector<uint64_t>& mutableWords() noexcept;

private:
    void requireIndex(uint64_t index) const;
    void clearUnusedTailBits() noexcept;

    uint64_t bitCount_ = 0;
    std::vector<uint64_t> words_;
};

}  // namespace sanpao15

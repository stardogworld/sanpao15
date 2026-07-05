#include "sanpao15/dense_bitset.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>

namespace sanpao15 {

namespace {

uint64_t wordCountForBits(uint64_t bitCount) {
    return bitCount / 64u + (bitCount % 64u == 0 ? 0u : 1u);
}

}  // namespace

DenseBitset::DenseBitset(uint64_t bitCount, bool initial) {
    reset(bitCount, initial);
}

void DenseBitset::reset(uint64_t bitCount, bool initial) {
    const uint64_t wordCount = wordCountForBits(bitCount);
    if (wordCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("dense bitset is too large for this platform");
    }
    bitCount_ = bitCount;
    words_.assign(static_cast<size_t>(wordCount), initial ? ~uint64_t{0} : uint64_t{0});
    if (initial) {
        clearUnusedTailBits();
    }
}

uint64_t DenseBitset::size() const noexcept {
    return bitCount_;
}

uint64_t DenseBitset::bytes() const noexcept {
    return static_cast<uint64_t>(words_.size() * sizeof(uint64_t));
}

bool DenseBitset::get(uint64_t index) const {
    requireIndex(index);
    return getUnchecked(index);
}

void DenseBitset::set(uint64_t index) {
    requireIndex(index);
    setUnchecked(index);
}

void DenseBitset::clear(uint64_t index) {
    requireIndex(index);
    clearUnchecked(index);
}

void DenseBitset::assign(uint64_t index, bool value) {
    requireIndex(index);
    assignUnchecked(index, value);
}

bool DenseBitset::getUnchecked(uint64_t index) const noexcept {
    return (words_[static_cast<size_t>(index / 64u)] & (uint64_t{1} << (index % 64u))) != 0;
}

void DenseBitset::setUnchecked(uint64_t index) noexcept {
    words_[static_cast<size_t>(index / 64u)] |= uint64_t{1} << (index % 64u);
}

void DenseBitset::setUncheckedAtomic(uint64_t index) noexcept {
    std::atomic_ref<uint64_t> word(words_[static_cast<size_t>(index / 64u)]);
    word.fetch_or(uint64_t{1} << (index % 64u), std::memory_order_relaxed);
}

void DenseBitset::clearUnchecked(uint64_t index) noexcept {
    words_[static_cast<size_t>(index / 64u)] &= ~(uint64_t{1} << (index % 64u));
}

void DenseBitset::assignUnchecked(uint64_t index, bool value) noexcept {
    if (value) {
        setUnchecked(index);
    } else {
        clearUnchecked(index);
    }
}

void DenseBitset::fill(bool value) {
    std::fill(words_.begin(), words_.end(), value ? ~uint64_t{0} : uint64_t{0});
    if (value) {
        clearUnusedTailBits();
    }
}

const std::vector<uint64_t>& DenseBitset::words() const noexcept {
    return words_;
}

std::vector<uint64_t>& DenseBitset::mutableWords() noexcept {
    return words_;
}

void DenseBitset::requireIndex(uint64_t index) const {
    if (index >= bitCount_) {
        throw std::out_of_range("dense bitset index out of range");
    }
}

void DenseBitset::clearUnusedTailBits() noexcept {
    const uint64_t usedBitsInLastWord = bitCount_ % 64u;
    if (usedBitsInLastWord == 0 || words_.empty()) {
        return;
    }
    words_.back() &= (uint64_t{1} << usedBitsInLastWord) - 1u;
}

}  // namespace sanpao15

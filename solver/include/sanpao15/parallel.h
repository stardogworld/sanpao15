#pragma once

#include <algorithm>
#include <cstdint>
#include <exception>
#include <thread>
#include <vector>

namespace sanpao15 {

uint32_t normalizeThreadCount(uint32_t requested, uint64_t workItems);

template <class Fn>
void parallelForRanges(uint64_t count, uint32_t threads, Fn&& fn) {
    const uint32_t effectiveThreads = normalizeThreadCount(threads, count);
    if (count == 0) {
        return;
    }
    if (effectiveThreads == 1) {
        fn(uint64_t{0}, count, uint32_t{0});
        return;
    }

    const uint64_t block = (count + effectiveThreads - 1u) / effectiveThreads;
    std::vector<std::thread> workers;
    std::vector<std::exception_ptr> errors(effectiveThreads);
    workers.reserve(effectiveThreads);
    for (uint32_t threadId = 0; threadId < effectiveThreads; ++threadId) {
        const uint64_t begin = std::min<uint64_t>(count, static_cast<uint64_t>(threadId) * block);
        const uint64_t end = std::min<uint64_t>(count, begin + block);
        workers.emplace_back([&, begin, end, threadId] {
            try {
                fn(begin, end, threadId);
            } catch (...) {
                errors[threadId] = std::current_exception();
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    for (const std::exception_ptr& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

}  // namespace sanpao15

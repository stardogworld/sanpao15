#include "sanpao15/parallel.h"

#include <algorithm>
#include <thread>

namespace sanpao15 {

uint32_t normalizeThreadCount(uint32_t requested, uint64_t workItems) {
    uint32_t threads = requested;
    if (threads == 0) {
        threads = std::thread::hardware_concurrency();
        if (threads == 0) {
            threads = 1;
        }
    }
    threads = std::clamp<uint32_t>(threads, 1, 256);
    if (workItems == 0) {
        return 1;
    }
    if (workItems < threads) {
        threads = static_cast<uint32_t>(workItems);
    }
    return std::max<uint32_t>(threads, 1);
}

}  // namespace sanpao15

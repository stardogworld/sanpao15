#include "sanpao15/external_keyset.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>

namespace sanpao15 {

namespace {

constexpr std::array<char, 8> RunMagic{'S', '1', '5', 'R', 'U', 'N', '1', '\0'};
constexpr uint32_t RunFileVersion = 1;
constexpr size_t MaxOpenRunReaders = 128;
using Clock = std::chrono::steady_clock;

void writeU8(std::ostream& output, uint8_t value) {
    output.put(static_cast<char>(value));
    if (!output) {
        throw std::runtime_error("failed to write run byte");
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
        throw std::runtime_error("unexpected end of run file");
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

uint64_t fileSizeOrZero(const std::filesystem::path& path) {
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > std::numeric_limits<uint64_t>::max()) {
        return 0;
    }
    return static_cast<uint64_t>(size);
}

std::vector<uint64_t> sortedUnique(std::vector<uint64_t> keys) {
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

class RunReader {
public:
    explicit RunReader(std::filesystem::path path) : input_(path, std::ios::binary), path_(std::move(path)) {
        if (!input_) {
            throw std::runtime_error("failed to open run file for merge: " + path_.string());
        }
        std::array<char, 8> magic{};
        input_.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (magic != RunMagic) {
            throw std::runtime_error("invalid run file magic: " + path_.string());
        }
        const uint32_t version = readU32LE(input_);
        if (version != RunFileVersion) {
            throw std::runtime_error("unsupported run file version: " + path_.string());
        }
        count_ = readU64LE(input_);
    }

    bool readNext(uint64_t& key) {
        if (read_ >= count_) {
            return false;
        }
        key = readU64LE(input_);
        if (read_ > 0 && key <= previous_) {
            throw std::runtime_error("run file keys are not strictly ascending: " + path_.string());
        }
        previous_ = key;
        ++read_;
        return true;
    }

private:
    std::ifstream input_;
    std::filesystem::path path_;
    uint64_t count_ = 0;
    uint64_t read_ = 0;
    uint64_t previous_ = 0;
};

class RunWriter {
public:
    explicit RunWriter(const std::filesystem::path& path)
        : finalPath_(path), tmpPath_(path.string() + ".tmp") {
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        output_.open(tmpPath_, std::ios::binary);
        if (!output_) {
            throw std::runtime_error("failed to open temporary run file for writing: " + tmpPath_.string());
        }
        output_.write(RunMagic.data(), static_cast<std::streamsize>(RunMagic.size()));
        writeU32LE(output_, RunFileVersion);
        countOffset_ = output_.tellp();
        writeU64LE(output_, 0);
    }

    void write(uint64_t key) {
        if (finished_) {
            throw std::logic_error("cannot write to finished run file");
        }
        if (count_ > 0 && key <= previous_) {
            throw std::runtime_error("run writer received non-ascending or duplicate key");
        }
        writeU64LE(output_, key);
        previous_ = key;
        ++count_;
    }

    uint64_t finish() {
        if (finished_) {
            return count_;
        }
        output_.seekp(countOffset_);
        writeU64LE(output_, count_);
        output_.close();
        if (!output_) {
            throw std::runtime_error("failed to finalize run file contents: " + tmpPath_.string());
        }
        std::error_code error;
        std::filesystem::rename(tmpPath_, finalPath_, error);
        if (error) {
            std::filesystem::remove(finalPath_, error);
            error.clear();
            std::filesystem::rename(tmpPath_, finalPath_, error);
        }
        if (error) {
            std::filesystem::remove(tmpPath_);
            throw std::runtime_error("failed to finalize run file: " + finalPath_.string());
        }
        finished_ = true;
        return count_;
    }

    ~RunWriter() {
        if (!finished_) {
            output_.close();
            std::error_code error;
            std::filesystem::remove(tmpPath_, error);
        }
    }

private:
    std::filesystem::path finalPath_;
    std::filesystem::path tmpPath_;
    std::ofstream output_;
    std::streampos countOffset_{};
    uint64_t count_ = 0;
    uint64_t previous_ = 0;
    bool finished_ = false;
};

struct HeapItem {
    uint64_t key = 0;
    size_t run = 0;
};

struct HeapOrder {
    bool operator()(const HeapItem& lhs, const HeapItem& rhs) const {
        if (lhs.key != rhs.key) {
            return lhs.key > rhs.key;
        }
        return lhs.run > rhs.run;
    }
};

struct MergeRunStats {
    uint64_t outputKeys = 0;
    uint64_t duplicateKeys = 0;
};

MergeRunStats mergeRunPaths(
    const std::vector<std::filesystem::path>& runPaths,
    const std::function<void(uint64_t)>& emitKey) {
    MergeRunStats stats;
    std::vector<RunReader> readers;
    readers.reserve(runPaths.size());
    for (const auto& path : runPaths) {
        readers.emplace_back(path);
    }

    std::priority_queue<HeapItem, std::vector<HeapItem>, HeapOrder> heap;
    for (size_t i = 0; i < readers.size(); ++i) {
        uint64_t key = 0;
        if (readers[i].readNext(key)) {
            heap.push({key, i});
        }
    }

    bool hasLast = false;
    uint64_t last = 0;
    while (!heap.empty()) {
        const HeapItem item = heap.top();
        heap.pop();
        if (!hasLast || item.key != last) {
            emitKey(item.key);
            last = item.key;
            hasLast = true;
            ++stats.outputKeys;
        } else {
            ++stats.duplicateKeys;
        }

        uint64_t next = 0;
        if (readers[item.run].readNext(next)) {
            heap.push({next, item.run});
        }
    }
    return stats;
}

}  // namespace

void writeRunFile(const std::filesystem::path& path, const std::vector<uint64_t>& sortedUniqueKeys) {
    if (!std::is_sorted(sortedUniqueKeys.begin(), sortedUniqueKeys.end()) ||
        std::adjacent_find(sortedUniqueKeys.begin(), sortedUniqueKeys.end()) != sortedUniqueKeys.end()) {
        throw std::invalid_argument("run file keys must be sorted and unique");
    }

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream output(tmp, std::ios::binary);
        if (!output) {
            throw std::runtime_error("failed to open temporary run file for writing: " + tmp.string());
        }
        output.write(RunMagic.data(), static_cast<std::streamsize>(RunMagic.size()));
        writeU32LE(output, RunFileVersion);
        writeU64LE(output, static_cast<uint64_t>(sortedUniqueKeys.size()));
        for (uint64_t key : sortedUniqueKeys) {
            writeU64LE(output, key);
        }
    }

    std::error_code error;
    std::filesystem::rename(tmp, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(tmp, path, error);
    }
    if (error) {
        std::filesystem::remove(tmp);
        throw std::runtime_error("failed to finalize run file: " + path.string());
    }
}

RunFileData readRunFile(const std::filesystem::path& path) {
    RunReader reader(path);
    RunFileData data;
    uint64_t key = 0;
    while (reader.readNext(key)) {
        data.keys.push_back(key);
    }
    return data;
}

struct ExternalKeySetBuilder::Impl {
    explicit Impl(ExternalKeySetOptions opts) : options(std::move(opts)) {
        if (options.chunkKeyLimit == 0) {
            throw std::invalid_argument("ExternalKeySet chunkKeyLimit must be greater than zero");
        }
        if (options.tempDir.empty()) {
            throw std::invalid_argument("ExternalKeySet tempDir is required");
        }
        std::filesystem::create_directories(options.tempDir);
        started = Clock::now();
    }

    ~Impl() {
        if (!finished && !options.keepTempFiles) {
            cleanupRuns();
        }
    }

    void cleanupRuns() {
        cleanupRunPaths(runs);
    }

    void cleanupRunPaths(const std::vector<std::filesystem::path>& paths) {
        for (const auto& path : paths) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }

    std::filesystem::path nextRunPath() const {
        return options.tempDir / ("keys-" + std::to_string(runs.size()) + ".s15run");
    }

    void add(uint64_t key) {
        chunk.push_back(key);
        ++stats.addedKeys;
        if (chunk.size() >= options.chunkKeyLimit) {
            flush();
        }
    }

    void flush() {
        if (chunk.empty()) {
            return;
        }
        const auto startedSort = Clock::now();
        const uint64_t beforeDedup = static_cast<uint64_t>(chunk.size());
        std::vector<uint64_t> unique = sortedUnique(std::move(chunk));
        chunk.clear();
        stats.chunkSortSeconds += std::chrono::duration<double>(Clock::now() - startedSort).count();

        const std::filesystem::path path = nextRunPath();
        writeRunFile(path, unique);
        runs.push_back(path);
        ++stats.chunksWritten;
        stats.keysAfterChunkDedup += static_cast<uint64_t>(unique.size());
        stats.duplicateKeys += beforeDedup - static_cast<uint64_t>(unique.size());
        stats.tempBytesWritten += fileSizeOrZero(path);
    }

    void mergeTo(const std::function<void(uint64_t)>& emitKey) {
        flush();
        const auto mergeStarted = Clock::now();
        stats.uniqueKeys = 0;
        std::vector<std::filesystem::path> currentRuns = runs;
        std::vector<std::filesystem::path> intermediateRuns;
        size_t pass = 0;
        while (currentRuns.size() > MaxOpenRunReaders) {
            std::vector<std::filesystem::path> nextRuns;
            for (size_t start = 0; start < currentRuns.size(); start += MaxOpenRunReaders) {
                const size_t end = std::min(currentRuns.size(), start + MaxOpenRunReaders);
                std::vector<std::filesystem::path> group(
                    currentRuns.begin() + static_cast<std::ptrdiff_t>(start),
                    currentRuns.begin() + static_cast<std::ptrdiff_t>(end));
                const std::filesystem::path mergedPath =
                    options.tempDir / ("merge-pass-" + std::to_string(pass) + "-" + std::to_string(nextRuns.size()) + ".s15run");
                RunWriter writer(mergedPath);
                const MergeRunStats groupStats = mergeRunPaths(group, [&](uint64_t key) {
                    writer.write(key);
                });
                writer.finish();
                stats.duplicateKeys += groupStats.duplicateKeys;
                stats.tempBytesWritten += fileSizeOrZero(mergedPath);
                nextRuns.push_back(mergedPath);
                intermediateRuns.push_back(mergedPath);
            }
            if (!options.keepTempFiles) {
                cleanupRunPaths(currentRuns);
            }
            currentRuns = std::move(nextRuns);
            ++pass;
        }

        const MergeRunStats finalStats = mergeRunPaths(currentRuns, emitKey);
        stats.uniqueKeys = finalStats.outputKeys;
        stats.duplicateKeys += finalStats.duplicateKeys;
        stats.mergeSeconds += std::chrono::duration<double>(Clock::now() - mergeStarted).count();
        stats.totalSeconds = std::chrono::duration<double>(Clock::now() - started).count();
        stats.finalBytesWritten = stats.uniqueKeys * sizeof(uint64_t);
        finished = true;
        if (!options.keepTempFiles) {
            cleanupRunPaths(currentRuns);
            cleanupRunPaths(intermediateRuns);
        }
    }

    ExternalKeySetOptions options;
    ExternalKeySetStats stats;
    std::vector<uint64_t> chunk;
    std::vector<std::filesystem::path> runs;
    Clock::time_point started;
    bool finished = false;
};

ExternalKeySetBuilder::ExternalKeySetBuilder(ExternalKeySetOptions options)
    : impl_(new Impl(std::move(options))) {}

ExternalKeySetBuilder::~ExternalKeySetBuilder() {
    delete impl_;
}

void ExternalKeySetBuilder::add(uint64_t key) {
    impl_->add(key);
}

void ExternalKeySetBuilder::addBatch(const std::vector<uint64_t>& keys) {
    for (uint64_t key : keys) {
        add(key);
    }
}

void ExternalKeySetBuilder::flush() {
    impl_->flush();
}

std::vector<uint64_t> ExternalKeySetBuilder::finishToVector() {
    std::vector<uint64_t> result;
    impl_->mergeTo([&](uint64_t key) { result.push_back(key); });
    return result;
}

void ExternalKeySetBuilder::finishToStream(const std::function<void(uint64_t)>& emitKey) {
    impl_->mergeTo(emitKey);
}

const ExternalKeySetStats& ExternalKeySetBuilder::stats() const {
    return impl_->stats;
}

}  // namespace sanpao15

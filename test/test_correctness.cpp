#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "CommandExecutor.h"
#include "PersistenceManager.h"
#include "SlidingWindow.h"
#include "SystemContext.h"
#include "TextProcessor.h"

namespace fs = std::filesystem;

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

fs::path tempPath(const std::string& name) {
    return fs::temp_directory_path() / ("hotwordsystem-correctness-" + name);
}

void removeTempPath(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);
}

int countWord(const std::vector<std::pair<std::string, int>>& words, const std::string& word) {
    auto it = std::find_if(words.begin(), words.end(), [&](const auto& entry) {
        return entry.first == word;
    });
    return it == words.end() ? 0 : it->second;
}

bool containsWord(const std::vector<std::string>& words, const std::string& word) {
    return std::find(words.begin(), words.end(), word) != words.end();
}

std::vector<std::pair<std::string, int>> readHistory(PersistenceManager& persistence,
                                                      long long start,
                                                      long long end) {
    return persistence.queryHistoryTopK(start, end, 10);
}

void testPersistenceFlushAtBatchBoundaryDoesNotDeadlock() {
    const fs::path path = tempPath("batch-boundary.log");
    removeTempPath(path);

    ThreadPool pool(2);
    PersistenceManager persistence(pool, path.string());
    for (long long timestamp = 1; timestamp <= 100; ++timestamp) {
        persistence.logData(timestamp, {"boundary"});
    }

    const auto result = readHistory(persistence, 1, 100);
    expect(countWord(result, "boundary") == 100,
           "batch-boundary data was not flushed or was counted incorrectly");
    removeTempPath(path);
}

void testPersistenceResetDoesNotRestoreOldPendingData() {
    const fs::path path = tempPath("reset.log");
    removeTempPath(path);

    ThreadPool pool(2);
    PersistenceManager persistence(pool, path.string());
    persistence.logData(10, {"old"});
    persistence.clearLog();

    expect(readHistory(persistence, 0, 100).empty(),
           "RESET left old pending persistence data visible");

    persistence.logData(20, {"new"});
    const auto result = readHistory(persistence, 0, 100);
    expect(countWord(result, "old") == 0 && countWord(result, "new") == 1,
           "new data was not isolated from pre-RESET data");
    removeTempPath(path);
}

void testPersistenceQuerySeesPendingData() {
    const fs::path path = tempPath("pending-query.log");
    removeTempPath(path);

    ThreadPool pool(2);
    PersistenceManager persistence(pool, path.string());
    persistence.logData(50, {"pending"});

    const auto result = readHistory(persistence, 0, 100);
    expect(countWord(result, "pending") == 1,
           "history query did not flush the partial in-memory batch");
    removeTempPath(path);
}

void testTextProcessorCacheDoesNotDeadlockAtCleanup() {
    TextProcessor processor(
        "dict/jieba.dict.utf8",
        "dict/hmm_model.utf8",
        "dict/user.dict.utf8",
        "dict/idf.utf8",
        "dict/stop_words.utf8");

    for (int i = 0; i <= 8000; ++i) {
        processor.process("cache_sentence_" + std::to_string(i));
    }

    // Reaching the cleanup threshold is the regression check: the call must
    // return instead of attempting to lock cacheMtx a second time.
    processor.process("cache_sentence_after_cleanup");
}

void testTextProcessorCacheReflectsConfigChanges() {
    const fs::path sensitivePath = tempPath("sensitive.txt");
    removeTempPath(sensitivePath);
    {
        std::ofstream file(sensitivePath);
        file << "";
    }

    TextProcessor processor(
        "dict/jieba.dict.utf8",
        "dict/hmm_model.utf8",
        "dict/user.dict.utf8",
        "dict/idf.utf8",
        "dict/stop_words.utf8",
        sensitivePath.string());

    processor.setPosConfig({}, true);
    processor.setSensitiveConfig(false);
    processor.addSensitiveWord("苹果");

    const auto filtered = processor.process("苹果");
    expect(!containsWord(filtered, "苹果"),
           "sensitive word was not filtered before configuration change");

    processor.setSensitiveConfig(true);
    const auto allowed = processor.process("苹果");
    expect(containsWord(allowed, "苹果"),
           "cache returned a result produced under the old sensitive-word configuration");

    removeTempPath(sensitivePath);
}

void testSlidingWindowExpiresOldData() {
    SystemContext context;
    SlidingWindow window(10, context);

    window.addData(100, {"old"});
    window.addData(105, {"recent"});
    window.addData(116, {"current"});

    const auto top = window.getTopK(10);
    expect(countWord(top, "old") == 0 && countWord(top, "recent") == 0 &&
               countWord(top, "current") == 1,
           "logical window did not expire old active data");
}

void testSlidingWindowDoesNotCountExpiredOutOfOrderData() {
    SystemContext context;
    SlidingWindow window(20, context);

    window.addData(100, {"base"});
    window.addData(120, {"current"});
    window.addData(110, {"late"});
    window.addData(131, {"advance"});

    const auto top = window.getTopK(10);
    expect(countWord(top, "late") == 0 && countWord(top, "current") == 1 &&
               countWord(top, "advance") == 1,
           "late data broke activeData ordering or remained after expiration");
}

void testSlidingWindowResizeRebuildsCorrectly() {
    SystemContext context;
    SlidingWindow window(10, context);

    window.addData(100, {"a"});
    window.addData(105, {"b"});
    window.addData(120, {"c"});

    window.setWindowSize(30);
    auto expanded = window.getTopK(10);
    expect(countWord(expanded, "a") == 1 && countWord(expanded, "b") == 1 &&
               countWord(expanded, "c") == 1,
           "window expansion did not rebuild from retained storage");

    window.setWindowSize(10);
    auto reduced = window.getTopK(10);
    expect(countWord(reduced, "a") == 0 && countWord(reduced, "b") == 0 &&
               countWord(reduced, "c") == 1,
           "window reduction did not rebuild the current view");

    window.reset();
    std::thread writer([&window]() {
        for (long long timestamp = 1; timestamp <= 200; ++timestamp) {
            window.addData(timestamp, {"during_reset"});
        }
    });
    window.reset();
    writer.join();
    window.reset();
    expect(window.getTopK(10).empty(),
           "reset did not leave the window empty after concurrent input");
}

void testSlidingWindowActiveCapacityKeepsCountsConsistent() {
    SystemContext context;
    SlidingWindow window(200000, context);

    for (long long timestamp = 1; timestamp <= 100001; ++timestamp) {
        window.addData(timestamp, {"capacity"});
    }

    const auto top = window.getTopK(1);
    expect(countWord(top, "capacity") == 100000,
           "active capacity eviction did not decrement wordCounts");
}

void testRepeatedBenchmarkStartIsRejected() {
    SystemContext context;
    CommandExecutor executor(context);

    const std::string first = executor.process("[ACTION] BENCHMARK N=100000");
    expect(first.find("Benchmark started") != std::string::npos,
           "first benchmark did not start");

    const std::string second = executor.process("[ACTION] BENCHMARK N=100000");
    expect(second.find("already running") != std::string::npos,
           "a second benchmark was accepted while one was running");

    executor.process("[ACTION] BENCHMARK_STOP");
    executor.shutdown();
}

void testShutdownOrDestructionFlushesPendingPersistence() {
    const fs::path path = tempPath("destruction.log");
    removeTempPath(path);

    ThreadPool pool(2);
    {
        PersistenceManager persistence(pool, path.string());
        persistence.logData(42, {"destruction"});
    }

    std::ifstream file(path);
    const std::string contents((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    expect(contents.find("42|destruction") != std::string::npos,
           "PersistenceManager destruction lost a partial batch");
    removeTempPath(path);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: HotWordSystemCorrectness <test-name>\n";
        return 2;
    }

    const std::string name = argv[1];
    try {
        if (name == "PersistenceFlushAtBatchBoundaryDoesNotDeadlock") {
            testPersistenceFlushAtBatchBoundaryDoesNotDeadlock();
        } else if (name == "PersistenceResetDoesNotRestoreOldPendingData") {
            testPersistenceResetDoesNotRestoreOldPendingData();
        } else if (name == "PersistenceQuerySeesPendingData") {
            testPersistenceQuerySeesPendingData();
        } else if (name == "TextProcessorCacheDoesNotDeadlockAtCleanup") {
            testTextProcessorCacheDoesNotDeadlockAtCleanup();
        } else if (name == "TextProcessorCacheReflectsConfigChanges") {
            testTextProcessorCacheReflectsConfigChanges();
        } else if (name == "SlidingWindowExpiresOldData") {
            testSlidingWindowExpiresOldData();
        } else if (name == "SlidingWindowDoesNotCountExpiredOutOfOrderData") {
            testSlidingWindowDoesNotCountExpiredOutOfOrderData();
        } else if (name == "SlidingWindowResizeRebuildsCorrectly") {
            testSlidingWindowResizeRebuildsCorrectly();
        } else if (name == "SlidingWindowActiveCapacityKeepsCountsConsistent") {
            testSlidingWindowActiveCapacityKeepsCountsConsistent();
        } else if (name == "RepeatedBenchmarkStartIsRejected") {
            testRepeatedBenchmarkStartIsRejected();
        } else if (name == "ShutdownOrDestructionFlushesPendingPersistence") {
            testShutdownOrDestructionFlushesPendingPersistence();
        } else {
            std::cerr << "unknown test: " << name << '\n';
            return 2;
        }

        std::cout << "[PASS] " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        return 1;
    }
}

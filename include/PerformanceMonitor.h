#pragma once
#include <chrono>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <map>
#include <string>

// --- Windows 内存监控依赖 ---
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib") // 链接库
#endif

class PerformanceMonitor {
private:
    std::atomic<long long> totalProcessedWords{0};
    std::atomic<long long> totalProcessedLines{0};

    std::chrono::steady_clock::time_point startTime;

public:
    PerformanceMonitor() {
        startTime = std::chrono::steady_clock::now();
    }

    void record(int wordCount) {
        totalProcessedLines++;
        totalProcessedWords += wordCount;
    }

    void reset() {
        totalProcessedWords = 0;
        totalProcessedLines = 0;
        startTime = std::chrono::steady_clock::now();
    }

    // 获取当前内存占用 (单位: MB)
    double getCurrentMemoryUsageMB() {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            // WorkingSetSize 是物理内存占用
            // PrivateUsage 是提交大小 (虚拟内存)
            // 这里我们返回物理内存占用
            return pmc.WorkingSetSize / (1024.0 * 1024.0);
        }
#endif
        return 0.0;
    }

    // 返回结构化数据，供 Web API 使用
    std::map<std::string, double> getStats() {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        if (duration == 0) duration = 1;

        double seconds = duration / 1000.0;
        
        std::map<std::string, double> stats;
        stats["runtime_sec"] = seconds;
        stats["total_lines"] = static_cast<double>(totalProcessedLines.load());
        stats["total_words"] = static_cast<double>(totalProcessedWords.load());
        stats["lines_per_sec"] = totalProcessedLines / seconds;
        stats["words_per_sec"] = totalProcessedWords / seconds;
        stats["memory_mb"] = getCurrentMemoryUsageMB();
        
        return stats;
    }

    void printStats() {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        if (duration == 0) duration = 1;

        double seconds = duration / 1000.0;
        double linesPerSec = totalProcessedLines / seconds;
        double wordsPerSec = totalProcessedWords / seconds;
        double memoryMB = getCurrentMemoryUsageMB(); // 获取内存

        std::cout << "\n=== [Performance Metrics] ===" << std::endl;
        std::cout << "Running Time   : " << std::fixed << std::setprecision(2) << seconds << " s" << std::endl;
        std::cout << "Total Lines    : " << totalProcessedLines << std::endl;
        std::cout << "Total Words    : " << totalProcessedWords << std::endl;
        std::cout << "Throughput     : " << linesPerSec << " lines/sec" << std::endl;
        std::cout << "Word Rate      : " << wordsPerSec << " words/sec" << std::endl;
        std::cout << "Memory Usage   : " << memoryMB << " MB" << std::endl;
        std::cout << "=============================\n" << std::endl;
    }
};
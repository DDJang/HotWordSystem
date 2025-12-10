#pragma once
#include <chrono>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <map> // 新增
#include <string> // 新增

// --- Windows 内存监控依赖 ---
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib") // 链接库
#endif

using namespace std;
using namespace std::chrono;

class PerformanceMonitor {
private:
    atomic<long long> totalProcessedWords{0};
    atomic<long long> totalProcessedLines{0};
    steady_clock::time_point startTime;

public:
    PerformanceMonitor() {
        startTime = steady_clock::now();
    }

    void record(int wordCount) {
        totalProcessedLines++;
        totalProcessedWords += wordCount;
    }

    void reset() {
        totalProcessedWords = 0;
        totalProcessedLines = 0;
        startTime = steady_clock::now();
    }

    // 【新增】获取当前内存占用 (单位: MB)
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

    // 【新增】返回结构化数据，供 Web API 使用
    map<string, double> getStats() {
        auto now = steady_clock::now();
        auto duration = duration_cast<milliseconds>(now - startTime).count();
        if (duration == 0) duration = 1;

        double seconds = duration / 1000.0;
        
        map<string, double> stats;
        stats["runtime_sec"] = seconds;
        stats["total_lines"] = (double)totalProcessedLines.load();
        stats["total_words"] = (double)totalProcessedWords.load();
        stats["lines_per_sec"] = totalProcessedLines / seconds;
        stats["words_per_sec"] = totalProcessedWords / seconds;
        stats["memory_mb"] = getCurrentMemoryUsageMB();
        
        return stats;
    }

    void printStats() {
        auto now = steady_clock::now();
        auto duration = duration_cast<milliseconds>(now - startTime).count();
        if (duration == 0) duration = 1;

        double seconds = duration / 1000.0;
        double linesPerSec = totalProcessedLines / seconds;
        double wordsPerSec = totalProcessedWords / seconds;
        double memoryMB = getCurrentMemoryUsageMB(); // 获取内存

        cout << "\n=== [Performance Metrics] ===" << endl;
        cout << "Running Time   : " << fixed << setprecision(2) << seconds << " s" << endl;
        cout << "Total Lines    : " << totalProcessedLines << endl;
        cout << "Total Words    : " << totalProcessedWords << endl;
        cout << "Throughput     : " << linesPerSec << " lines/sec" << endl;
        cout << "Word Rate      : " << wordsPerSec << " words/sec" << endl;
        cout << "Memory Usage   : " << memoryMB << " MB" << endl; // 新增输出
        cout << "=============================\n" << endl;
    }
};
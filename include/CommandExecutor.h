#pragma once
#include <string>
#include <sstream>
#include <thread>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <future>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "ctre.hpp"
#include "SystemContext.h"
#include "GlobalUtils.h"
#include "SlidingWindow.h"

class CommandExecutor {
private:
    SystemContext& ctx;
    std::mutex commandMtx;
    std::mutex benchmarkMtx;
    std::condition_variable benchmarkCv;
    std::future<void> benchmarkFuture;
    std::atomic<bool> shutdownCompleted{false};

public:
    CommandExecutor(SystemContext& context) : ctx(context) {}

    ~CommandExecutor() {
        shutdown();
    }

    // 由 main 在停止 HTTP server 后调用，确保 benchmark、持久化和线程池
    // 都完成后再销毁 SystemContext。
    void shutdown() {
        bool expected = false;
        if (!shutdownCompleted.compare_exchange_strong(expected, true)) {
            return;
        }

        requestBenchmarkStop();
        waitForBenchmark();
        ctx.persistence->flushBatch();
        ctx.threadPool->shutdown();
    }

    // 处理入口：支持单行或多行文本
    std::string process(const std::string& fullInput) {
        std::lock_guard<std::mutex> commandLock(commandMtx);

        // 防止超大包攻击
        if (fullInput.length() > 5000000) { 
            return "Error: Input too large.";
        }

        std::stringstream ss(fullInput);
        std::string line;
        std::stringstream finalOutput;
        int successCount = 0;

        // 逐行读取
        while (std::getline(ss, line)) {
            // 去除 Windows 换行符 \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            // === 优化：快速路径判断 ===
            // 如果包含 [ACTION]，则是指令，否则尝试匹配时间戳数据或纯文本
            bool isAction = (line.find("[ACTION]") != std::string::npos);

            if (isAction) {
                handleAction(line, finalOutput);
            } else {
                // 处理数据行 (带时间戳 或 纯文本)
                if (handleData(line)) {
                    successCount++;
                }
            }
        }

        if (successCount > 0) {
            finalOutput << "Processed " << successCount << " data lines. ";
        }
        return finalOutput.str();
    }

private:
    // === 处理指令 ===
    void handleAction(const std::string& line, std::stringstream& out) {
        // 1. SET_WINDOW S=(\d+)
        if (auto m = ctre::search<R"(\[ACTION\]\s*SET_WINDOW\s*S=(\d+))">(line)) {
            long long s = GlobalUtils::safeStoll(m.get<1>().to_string(), 1, 2592000, 600);
            ctx.window->setWindowSize(s);
            out << "[Cmd] Window set to " << s << "s\n";
        }
        // 2. BENCHMARK N=(\d+)
        else if (auto m = ctre::search<R"(\[ACTION\]\s*BENCHMARK\s*N=(\d+))">(line)) {
            int n = GlobalUtils::safeStoi(m.get<1>().to_string(), 1, 100000, 0);
            if (n > 0) {
                if (startBenchmark(n)) {
                    out << "[Cmd] Benchmark started N=" << n << " (Async via ThreadPool)\n";
                } else {
                    out << "[Cmd] Benchmark is already running.\n";
                }
            } else {
                requestBenchmarkStop();
                out << "[Cmd] Benchmark stop signal sent via N=0.\n";
            }
        }
        // 3. STOP BENCHMARK
        else if (ctre::search<R"(\[ACTION\]\s*BENCHMARK_STOP)">(line)) {
            requestBenchmarkStop();
            out << "[Cmd] Benchmark stop signal sent.\n";
        }
        // 4. BENCHMARK STATUS
        else if (ctre::search<R"(\[ACTION\]\s*BENCHMARK_STATUS)">(line)) {
            // 这种情况下我们无法直接 return JSON string，因为外层在 loop。
            // 简单处理：写入 output
            out << (ctx.isBenchmarkRunning.load(std::memory_order_acquire)
                        ? R"({"is_running": true})"
                        : R"({"is_running": false})") << "\n";
        }
        // 5. HISTORY REPORT
        else if (auto m = ctre::search<R"(\[ACTION\]\s*HISTORY\s*START=(\d{1,2}:\d{1,2}:\d{1,2})\s*END=(\d{1,2}:\d{1,2}:\d{1,2})\s*STEP=(\d+))">(line)) {
            long long startT = GlobalUtils::parseTimeSeconds(m.get<1>().to_string());
            long long endT = GlobalUtils::parseTimeSeconds(m.get<2>().to_string());
            int step = GlobalUtils::safeStoi(m.get<3>().to_string(), 1, 3600, 60);
            
            // 历史报告可能很耗时，建议放入线程池（但这里为了返回结果给用户，先保持同步，或未来改为异步回调）
            ctx.persistence->generateReport(startT, endT, step, "report_output.txt");
            out << "[Cmd] Report generated.\n";
        }
        // 6. RESET
        else if (ctre::search<R"(\[ACTION\]\s*RESET)">(line)) {
            requestBenchmarkStop();
            waitForBenchmark();
            {
                std::lock_guard<std::mutex> lock(ctx.global_mutex); // 加锁保护重置过程
                ctx.window->reset();
                ctx.monitor->reset();
                ctx.persistence->clearLog();
                ctx.resetTimestamp();
            }
            out << "[Cmd] System Reset.\n";
        }
        // 7. SHUTDOWN
        else if (ctre::search<R"(\[ACTION\]\s*SHUTDOWN)">(line)) {
            ctx.shouldExit.store(true, std::memory_order_release);
            requestBenchmarkStop();
            out << "Shutdown initiated. Server will terminate shortly...\n";
        }
        // 8. TREND K=(\d+)
        else if (auto m = ctre::search<R"(\[ACTION\]\s*TREND\s*K=(\d+))">(line)) {
            int k = GlobalUtils::safeStoi(m.get<1>().to_string(), 1, 100, 10);
            auto list = ctx.window->getTrendingWords(k);
            out << "Trend Top-" << k << ":\n";
            for (auto& p : list) out << p.first << " " << std::fixed << std::setprecision(2) << p.second << "\n";
        }
        // 9. QUERY K=(\d+)
        else if (auto m = ctre::search<R"(\[ACTION\]\s*QUERY\s*K=(\d+))">(line)) {
            int k = GlobalUtils::safeStoi(m.get<1>().to_string(), 1, 100, 10);
            auto start = std::chrono::high_resolution_clock::now();
            auto list = ctx.window->getTopK(k);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

            out << "Query Top-" << k << " (Time: " << duration << " us):\n";
            for (auto& p : list) out << p.first << " : " << p.second << "\n";
        }
        // 10. SET_RETENTION R=(\d+)
        else if (auto m = ctre::search<R"(\[ACTION\]\s*SET_RETENTION\s*R=(\d+))">(line)) {
            long long r = GlobalUtils::safeStoll(m.get<1>().to_string(), 60, 2592000, 3600);
            ctx.window->setMaxRetention(r);
            out << "[Cmd] Max retention set to " << r << "s\n";
        }
        // 11. STATS
        else if (line.find("STATS") != std::string::npos) {
            ctx.monitor->printStats();
            out << "[Cmd] Stats printed to console.\n";
        }
    }

    // === 处理数据 ===
    bool handleData(const std::string& line) {
        // 1. 尝试匹配带时间戳的数据: [12:00:01] content
        if (auto m = ctre::match<R"(^\[(\d{1,2}:\d{1,2}:\d{1,2})\]\s*(.*)$)">(line)) {
            long long ts = GlobalUtils::parseTimeSeconds(m.get<1>().to_string());
            std::string content = m.get<2>().to_string();
            
            ctx.observeTimestamp(ts);
            
            internalProcess(ts, content);
            return true;
        }
        
        // 2. 纯文本 (无时间戳，自动补全)
        // 此时直接认为是数据
        const long long timestamp = ctx.nextTimestamp();
        internalProcess(timestamp, line);
        return true;
    }

    // === 核心数据处理 ===
    void internalProcess(long long ts, const std::string& content) {
        // 分词过程是纯计算，可以不加锁（假设 TextProcessor 无状态且线程安全）
        std::vector<std::string> words = ctx.processor->process(content);

        // 各个组件自己管理锁，避免嵌套锁
        ctx.window->addData(ts, words);
        ctx.persistence->logData(ts, words);
        ctx.monitor->record(static_cast<int>(words.size()));
    }

    // === 压测任务 (运行在 ThreadPool 中) ===
    void runBenchmark(int n) {
        try {
            ctx.monitor->reset();

            std::cout << "[Benchmark] Started on thread " << std::this_thread::get_id() << std::endl;

            for (int i = 0; i < n; ++i) {
                if (ctx.abortBenchmark.load(std::memory_order_acquire)) {
                    std::cout << "[System] Benchmark aborted by user." << std::endl;
                    break;
                }

                const long long timestamp = ctx.nextTimestamp();
                internalProcess(timestamp, GlobalUtils::generateRandomSentence());
            }

            std::cout << "[System] Benchmark finished." << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[System] Benchmark failed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[System] Benchmark failed with unknown error." << std::endl;
        }

        ctx.isBenchmarkRunning.store(false, std::memory_order_release);
        benchmarkCv.notify_all();
    }

    bool startBenchmark(int n) {
        std::unique_lock<std::mutex> lock(benchmarkMtx);

        // Check the atomic state before waiting on a completed task's future;
        // a repeated start must be rejected immediately while the current
        // benchmark is still running.
        if (ctx.isBenchmarkRunning.load(std::memory_order_acquire)) {
            return false;
        }

        if (benchmarkFuture.valid()) {
            try {
                benchmarkFuture.get();
            } catch (...) {
                // runBenchmark already reports task failures; do not make a
                // completed benchmark prevent the next start.
            }
        }

        bool expected = false;
        if (!ctx.isBenchmarkRunning.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            return false;
        }

        ctx.abortBenchmark.store(false, std::memory_order_release);
        try {
            benchmarkFuture = ctx.threadPool->enqueue([this, n]() {
                runBenchmark(n);
            });
        } catch (...) {
            ctx.isBenchmarkRunning.store(false, std::memory_order_release);
            benchmarkCv.notify_all();
            return false;
        }
        return true;
    }

    void requestBenchmarkStop() {
        ctx.abortBenchmark.store(true, std::memory_order_release);
    }

    void waitForBenchmark() {
        std::future<void> completed;
        {
            std::unique_lock<std::mutex> lock(benchmarkMtx);
            benchmarkCv.wait(lock, [this]() {
                return !ctx.isBenchmarkRunning.load(std::memory_order_acquire);
            });
            if (benchmarkFuture.valid()) {
                completed = std::move(benchmarkFuture);
            }
        }

        if (completed.valid()) {
            try {
                completed.get();
            } catch (...) {
                // The task wrapper reports benchmark failures; shutdown must
                // still continue to release all resources.
            }
        }
    }
};

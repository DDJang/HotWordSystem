#pragma once
#include <string>
#include <sstream>
#include <thread>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <future>
#include <mutex>

#include <ctre.hpp>
#include "SystemContext.h"
#include "GlobalUtils.h"
#include "SlidingWindow.h"

class CommandExecutor {
private:
    SystemContext& ctx;

public:
    CommandExecutor(SystemContext& context) : ctx(context) {}

    // 处理入口：支持单行或多行文本
    std::string process(const std::string& fullInput) {

        // 1. 防止超大包攻击
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
    // === 逻辑拆分：处理指令 ===
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
                ctx.abortBenchmark = false;
                // 【优化】使用线程池执行压测，而不是 detach 裸线程
                ctx.threadPool->enqueue([this, n](){ 
                    this->runBenchmark(n); 
                });
                out << "[Cmd] Benchmark started N=" << n << " (Async via ThreadPool)\n";
            } else {
                ctx.abortBenchmark = true;
                out << "[Cmd] Benchmark stop signal sent via N=0.\n";
            }
        }
        // 3. STOP BENCHMARK
        else if (ctre::search<R"(\[ACTION\]\s*BENCHMARK_STOP)">(line)) {
            ctx.abortBenchmark = true;
            out << "[Cmd] Benchmark stop signal sent.\n";
        }
        // 4. BENCHMARK STATUS
        else if (ctre::search<R"(\[ACTION\]\s*BENCHMARK_STATUS)">(line)) {
            // 这种情况下我们无法直接 return JSON string，因为外层在 loop。
            // 简单处理：写入 output
            out << (ctx.isBenchmarkRunning ? R"({"is_running": true})" : R"({"is_running": false})") << "\n";
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
            {
                std::lock_guard<std::mutex> lock(ctx.global_mutex); // 加锁保护重置过程
                ctx.abortBenchmark = true;
                ctx.isBenchmarkRunning = false;
                ctx.window->reset();
                ctx.monitor->reset();
                ctx.persistence->clearLog();
                ctx.lastTimestamp = 0;
            }
            out << "[Cmd] System Reset.\n";
        }
        // 7. SHUTDOWN
        else if (ctre::search<R"(\[ACTION\]\s*SHUTDOWN)">(line)) {
            ctx.shouldExit = true;
            ctx.abortBenchmark = true;
            out << "Shutdown initiated. Server will terminate shortly...\n";

            // 【优化】使用线程池执行延时退出任务
            ctx.threadPool->enqueue([](){
                std::this_thread::sleep_for(std::chrono::seconds(3));
                std::cout << "[System] Timed shutdown executing..." << std::endl;
                std::exit(0); 
            });
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

    // === 逻辑拆分：处理数据 ===
    bool handleData(const std::string& line) {
        // 1. 尝试匹配带时间戳的数据: [12:00:01] content
        if (auto m = ctre::match<R"(^\[(\d{1,2}:\d{1,2}:\d{1,2})\]\s*(.*)$)">(line)) {
            long long ts = GlobalUtils::parseTimeSeconds(m.get<1>().to_string());
            std::string content = m.get<2>().to_string();
            
            if (ts > ctx.lastTimestamp) ctx.lastTimestamp = ts;
            
            internalProcess(ts, content);
            return true;
        }
        
        // 2. 纯文本 (无时间戳，自动补全)
        // 此时直接认为是数据
        ctx.lastTimestamp++; // 简单递增
        internalProcess(ctx.lastTimestamp, line);
        return true;
    }

    // === 核心数据处理 (加锁保护) ===
    void internalProcess(long long ts, const std::string& content) {
        // 分词过程是纯计算，可以不加锁（假设 TextProcessor 无状态且线程安全）
        std::vector<std::string> words = ctx.processor->process(content);

        // === 临界区 ===
        // 保护 SlidingWindow, PersistenceManager, PerformanceMonitor
        // 因为 runBenchmark (线程池) 和 process (主线程) 可能会同时调用这里
        {
            std::lock_guard<std::mutex> lock(ctx.global_mutex);
            ctx.window->addData(ts, words);
            ctx.persistence->logData(ts, words);
            ctx.monitor->record(static_cast<int>(words.size()));
        }
    }

    // === 压测任务 (运行在 ThreadPool 中) ===
    void runBenchmark(int n) {
        ctx.isBenchmarkRunning = true; 
        long long startTs = ctx.lastTimestamp; 
        ctx.monitor->reset();
        
        std::cout << "[Benchmark] Started on thread " << std::this_thread::get_id() << std::endl;

        for(int i = 0; i < n; i++) {
            if (ctx.abortBenchmark) {
                std::cout << "[System] Benchmark aborted by user." << std::endl;
                break;
            }

            startTs++; 
            std::string sentence = GlobalUtils::generateRandomSentence();

            // 更新时间戳 (非严格精确，但这主要用于模拟)
            ctx.lastTimestamp = startTs;
            
            // 调用处理逻辑 (内部会自动加锁)
            internalProcess(startTs, sentence);
        }

        ctx.isBenchmarkRunning = false;
        std::cout << "[System] Benchmark finished." << std::endl;
    }
};
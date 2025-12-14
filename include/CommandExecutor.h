#pragma once
#include <string>
#include <regex>
#include <sstream>
#include <thread>
#include <iostream>
#include <iomanip>
#include <chrono>
#include "SystemContext.h"
#include "GlobalUtils.h"
#include "SlidingWindow.h" // 【新增】包含 SlidingWindow 的完整定义

class CommandExecutor {
private:
    SystemContext& ctx;
    
    // 正则表达式
    const std::regex timeRegex{R"(\[(\d{1,2}:\d{1,2}:\d{1,2})\]\s*(.*))"};
    const std::regex benchRegex{R"(\[ACTION\]\s*BENCHMARK\s*N=(\d+))"};
    const std::regex windowRegex{R"(\[ACTION\]\s*SET_WINDOW\s*S=(\d+))"};
    const std::regex historyRegex{R"(\[ACTION\]\s*HISTORY\s*START=(\d{1,2}:\d{1,2}:\d{1,2})\s*END=(\d{1,2}:\d{1,2}:\d{1,2})\s*STEP=(\d+))"};
    const std::regex resetRegex{R"(\[ACTION\]\s*RESET)"}; 
    const std::regex trendRegex{R"(\[ACTION\]\s*TREND\s*K=(\d+))"}; 
    const std::regex queryRegex{R"(\[ACTION\]\s*QUERY\s*K=(\d+))"}; 
    const std::regex retentionRegex{R"(\[ACTION\]\s*SET_RETENTION\s*R=(\d+))"};
    const std::regex shutdownRegex{R"(\[ACTION\]\s*SHUTDOWN)"};

public:
    CommandExecutor(SystemContext& context) : ctx(context) {}

    // 处理入口：支持单行或多行文本
    std::string process(const std::string& fullInput) {
        // 在函数入口处加锁，保护整个 process 过程
        std::lock_guard<std::mutex> lock(ctx.global_mutex);


        // 防止超大包攻击
        if (fullInput.length() > 5000000) { 
            return "Error: Input too large.";
        }

        std::stringstream ss(fullInput);
        std::string line;
        std::stringstream finalOutput;
        int successCount = 0;
        int lineCount = 0;

        // 逐行读取
        while (std::getline(ss, line)) {
            // 去除 Windows 换行符 \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // 去除首尾空白（可选，视需求而定，这里简单判空）
            if (line.empty()) continue;

            lineCount++;
            
            // === 内部单行处理逻辑 (内联) ===
            std::smatch match;
            std::string currentResult; // 当前行的处理结果

            // 1. 带时间戳的数据
            if (std::regex_search(line, match, timeRegex)) {
                long long ts = GlobalUtils::parseTimeSeconds(match[1].str());
                std::string content = match[2].str();
                if (ts > ctx.lastTimestamp) ctx.lastTimestamp = ts;
                
                // 执行数据处理
                std::vector<std::string> words = ctx.processor->process(content);
                ctx.window->addData(ts, words);
                ctx.persistence->logData(ts, words);
                ctx.monitor->record(static_cast<int>(words.size()));
                successCount++;
            }
            // 2. 指令: 设置窗口
            else if (std::regex_search(line, match, windowRegex)) {
                long long s = GlobalUtils::safeStoll(match[1].str(), 1, 2592000, 600);
                ctx.window->setWindowSize(s);
                finalOutput << "[Cmd] Window set to " << s << "s\n";
            }
            // 3. 指令: 压测
            else if (std::regex_search(line, match, benchRegex)) {
                int n = GlobalUtils::safeStoi(match[1].str(), 1, 100000, 0);
                ctx.abortBenchmark = false; 
                std::thread([this, n](){ runBenchmark(n); }).detach();
                finalOutput << "[Cmd] Benchmark started N=" << n << "\n";
            }
            // 4. 指令: 性能
            else if (line.find("[ACTION] STATS") != std::string::npos) {
                ctx.monitor->printStats();
                finalOutput << "[Cmd] Stats printed to console.\n";
            }
            // 5. 指令: 历史报告
            else if (std::regex_search(line, match, historyRegex)) {
                long long startT = GlobalUtils::parseTimeSeconds(match[1].str());
                long long endT = GlobalUtils::parseTimeSeconds(match[2].str());
                int step = GlobalUtils::safeStoi(match[3].str(), 1, 3600, 60);
                ctx.persistence->generateReport(startT, endT, step, "report_output.txt");
                finalOutput << "[Cmd] Report generated.\n";
            }
            // 6. 指令: 重置
            else if (std::regex_search(line, match, resetRegex)) {
                ctx.abortBenchmark = true;
                ctx.window->reset();
                ctx.monitor->reset();
                ctx.persistence->clearLog();
                ctx.lastTimestamp = 0;
                finalOutput << "[Cmd] System Reset.\n";
            }
            // 7. 指令：终止程序指令
            else if (std::regex_search(line, match, shutdownRegex)) {
                ctx.shouldExit = true; // 1. 设置标志位
                ctx.abortBenchmark = true;
                finalOutput << "Shutdown initiated. Server will terminate shortly...\n";

                // 2. 启动一个独立的倒计时线程，确保服务器最终会关闭
                //    这可以防止在没有浏览器连接的情况下，服务器永远不退出的问题。
                std::thread([](){
                    // 等待3秒，给UI足够的时间通过轮询来发现并显示关闭状态
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    std::cout << "[System] Timed shutdown executing..." << std::endl;
                    std::exit(0); // 3. 强制退出进程
                }).detach();
            }
            // 8. 指令: 趋势
            else if (std::regex_search(line, match, trendRegex)) {
                // 趋势结果比较长，如果是批量操作建议不要看趋势
                int k = GlobalUtils::safeStoi(match[1].str(), 1, 100, 10);
                auto list = ctx.window->getTrendingWords(k);
                finalOutput << "Trend Top-" << k << ":\n";
                for (auto& p : list) finalOutput << p.first << " " << std::fixed << std::setprecision(2) << p.second << "\n";
            }
            // 9. 指令: 查询
            else if (std::regex_search(line, match, queryRegex)) {
                int k = GlobalUtils::safeStoi(match[1].str(), 1, 100, 10);
                
                // 开始计时
                auto start = std::chrono::high_resolution_clock::now();
                auto list = ctx.window->getTopK(k);
                // 结束计时
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

                finalOutput << "Query Top-" << k << " (Time: " << duration << " us):\n";
                for (auto& p : list) finalOutput << p.first << " : " << p.second << "\n";
            }
            // 10. 指令: 保留时间
            else if (std::regex_search(line, match, retentionRegex)) {
                long long r = GlobalUtils::safeStoll(match[1].str(), 60, 2592000, 3600);
                ctx.window->setMaxRetention(r);
                finalOutput << "[Cmd] Max retention set to " << r << "s\n";
            }
            // 11. 无时间戳的纯文本数据 (自动补全时间)
            // 排除掉含有 [ACTION] 的行，避免指令识别失败被当成数据处理
            else if (line.find("[ACTION]") == std::string::npos) {
                ctx.lastTimestamp++;
                std::vector<std::string> words = ctx.processor->process(line);
                ctx.window->addData(ctx.lastTimestamp, words);
                ctx.persistence->logData(ctx.lastTimestamp, words);
                ctx.monitor->record(static_cast<int>(words.size()));
                successCount++;
            }
            // 12. 无法识别的行
            else {
                // 忽略
            }
        }

        if (successCount > 0) {
            finalOutput << "Processed " << successCount << " data lines. ";
        }
        
        if (finalOutput.str().empty() && lineCount > 0) {
            return "No valid data or commands processed.";
        }

        return finalOutput.str();
    }

private:
    void processData(long long ts, const std::string& content, std::stringstream& result) {
        std::vector<std::string> words = ctx.processor->process(content);
        ctx.window->addData(ts, words);
        ctx.persistence->logData(ts, words);
        ctx.monitor->record(static_cast<int>(words.size()));
        
        // 如果是自动生成的时间戳，显示格式化时间
        if (content == content) { // 这里原本逻辑是复用的，这里简单处理
            result << "Data processed (T=" << GlobalUtils::formatTime(ts) << "): " << words.size() << " words.";
        }
    }

    void runBenchmark(int n) {
        long long startTs = ctx.lastTimestamp; 
        ctx.monitor->reset();
        
        for(int i = 0; i < n; i++) {
            // 紧急刹车检查
            if (ctx.abortBenchmark) {
                std::cout << "[System] Benchmark aborted by user." << std::endl;
                return; // 直接退出线程
            }

            startTs++; 
            std::string sentence = GlobalUtils::generateRandomSentence();

            // 进入临界区，加锁
            {
                std::lock_guard<std::mutex> lock(ctx.global_mutex);

                std::vector<std::string> words = ctx.processor->process(sentence);
                ctx.window->addData(startTs, words);
                ctx.persistence->logData(startTs, words);
                
                // monitor->record 和 lastTimestamp 的更新也需要保护
                ctx.lastTimestamp = startTs;
                ctx.monitor->record(static_cast<int>(words.size()));
            }
            // 锁在这里被释放，给其他线程机会
        }
        std::cout << "[System] Benchmark finished." << std::endl;
    }
};
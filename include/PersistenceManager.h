#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <stdexcept>

#include "ThreadPool.h"

namespace fs = std::filesystem;

class PersistenceManager {
private:
    std::string logFilePath;
    ThreadPool& pool;           // 引用 Context 里的线程池
    mutable std::mutex fileMtx;  // 文件访问锁
    
    // 批量提交相关
    mutable std::mutex batchMtx;
    std::vector<std::pair<long long, std::vector<std::string>>> batchData;
    const size_t BATCH_SIZE = 100; // 每100条数据批量提交一次

    // --- 统一日志辅助函数 ---
    void logInfo(const std::string& msg) const {
        std::cout << "[Info]  " << msg << std::endl;
    }

    void logWarn(const std::string& msg) const {
        std::cout << "[Warn]  " << msg << std::endl;
    }

    void logError(const std::string& msg) const {
        std::cerr << "[Error] " << msg << std::endl;
    }

    // 严重的文件锁死/损坏警告，保持醒目
    void printCriticalFileError() const {
        std::cerr << "\n"
                  << "========================================================\n"
                  << "  [CRITICAL FILE ERROR] \n"
                  << "  The log file '" << logFilePath << "' is corrupted or locked.\n"
                  << "  The program cannot write to it.\n"
                  << "\n"
                  << "  >>> ACTION REQUIRED: <<<\n"
                  << "  1. Close this program completely.\n"
                  << "  2. Go to 'data' folder.\n"
                  << "  3. Manually DELETE 'history.log'.\n"
                  << "  4. Restart the program.\n"
                  << "========================================================\n"
                  << std::endl;
    }

public:
    PersistenceManager(ThreadPool& tp, const std::string& path = "data/history.log")
        : logFilePath(path), pool(tp) {
        fs::path p(path);

        // 1. 自动创建目录
        if (p.has_parent_path()) {
            fs::path parent = p.parent_path();
            if (!fs::exists(parent)) {
                try {
                    fs::create_directories(parent);
                    logInfo("Created directory: " + parent.string());
                } catch (const std::exception& e) {
                    logError("Failed to create directory: " + std::string(e.what()));
                }
            }
        }

        // 2. 自动创建空文件
        if (!fs::exists(p)) {
            std::ofstream tmp(p);
            if (tmp.is_open()) {
                tmp.close();
                // 可选：logInfo("Created new log file: " + path);
            } else {
                logError("Failed to create log file. Check permissions.");
            }
        }
    }
    
    // --- 析构函数：确保刷新剩余批量数据 ---
    ~PersistenceManager() {
        flushBatch();
    }

    // --- 批量写入日志 ---
    void logData(long long timestamp, const std::vector<std::string>& words) {
        if (words.empty()) return;

        {
            std::lock_guard<std::mutex> lock(batchMtx);
            batchData.emplace_back(timestamp, words);
            
            // 当批量数据达到阈值时，提交到线程池
            if (batchData.size() >= BATCH_SIZE) {
                flushBatch();
            }
        }
    }
    
    // --- 刷新批量数据 ---
    void flushBatch() {
        if (batchData.empty()) return;
        
        // 复制批量数据并清空，减少锁持有时间
        std::vector<std::pair<long long, std::vector<std::string>>> dataToWrite;
        {
            std::lock_guard<std::mutex> lock(batchMtx);
            dataToWrite.swap(batchData);
        }
        
        pool.enqueue([this, data = std::move(dataToWrite)]() {
            // 加锁：确保同一时间只有一个线程在写这个文件
            std::lock_guard<std::mutex> lock(this->fileMtx);
            
            std::ofstream ofs(this->logFilePath, std::ios::app);
            if (!ofs.is_open()) return;

            for (const auto& entry : data) {
                long long timestamp = entry.first;
                const auto& words = entry.second;
                
                ofs << timestamp << "|";
                for (std::size_t i = 0; i < words.size(); ++i) {
                    ofs << words[i];
                    if (i < words.size() - 1) ofs << ",";
                }
                ofs << "\n";
            }
        });
    }

    // --- 生成报告 ---
    void generateReport(long long startTime, long long endTime, int timeStep, const std::string& outputPath) {
        std::lock_guard<std::mutex> lock(fileMtx);

        std::ifstream ifs(logFilePath);
        
        if (!ifs.is_open()) {
            // 尝试重新创建
            std::ofstream tmp(logFilePath);
            if (tmp.is_open()) {
                tmp.close();
                logWarn("History log was missing and has been re-created (Empty).");
            } else {
                logError("History log missing and cannot be created.");
            }
            return;
        }

        std::map<long long, std::unordered_map<std::string, int>> reportData;
        std::string line;

        while (std::getline(ifs, line)) { 
            if (line.empty()) continue;

            std::size_t delimPos = line.find('|'); 
            if (delimPos == std::string::npos) continue;

            long long ts = 0;
            try {
                ts = std::stoll(line.substr(0, delimPos));
            } catch(...) { continue; }
            
            if (ts < startTime || ts > endTime) continue;

            long long bucketTime = (ts / timeStep) * timeStep;
            std::string content = line.substr(delimPos + 1);

            std::stringstream ss(content);
            std::string word;
            while (std::getline(ss, word, ',')) { 
                if (!word.empty()) {
                    reportData[bucketTime][word]++;
                }
            }
        }

        writeReportToFile(reportData, outputPath, timeStep);
    }

    // 查询历史区间的聚合 Top-K
    std::vector<std::pair<std::string, int>> queryHistoryTopK(long long startTime, long long endTime, int k) {
        std::lock_guard<std::mutex> lock(fileMtx);

        std::ifstream ifs(logFilePath);
        if (!ifs.is_open()) {
            // 这里通常不需要打印错误，直接返回空即可，由上层处理逻辑
            return {};
        }

        std::unordered_map<std::string, int> counts;
        std::string line;

        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            std::size_t delimPos = line.find('|');
            if (delimPos == std::string::npos) continue;

            long long ts = 0;
            try {
                ts = std::stoll(line.substr(0, delimPos));
            } catch(...) { continue; }
            
            if (ts < startTime || ts > endTime) continue;

            std::string content = line.substr(delimPos + 1);
            std::stringstream ss(content);
            std::string word;
            while (std::getline(ss, word, ',')) {
                if (!word.empty()) counts[word]++;
            }
        }

        std::vector<std::pair<std::string, int>> sortedWords(counts.begin(), counts.end());
        std::sort(sortedWords.begin(), sortedWords.end(), [](const auto& a, const auto& b){
            return a.second > b.second;
        });

        if (sortedWords.size() > static_cast<std::size_t>(k)) {
            sortedWords.resize(static_cast<std::size_t>(k));
        }
        return sortedWords;
    }

    // 清空日志文件
    void clearLog() {
        std::lock_guard<std::mutex> lock(fileMtx);

        std::ofstream ofs(logFilePath, std::ios::trunc);
        if (!ofs.is_open()) {
            printCriticalFileError();
            return;
        }
        logInfo("History log file cleared.");
    }

private:
    void writeReportToFile(const std::map<long long, std::unordered_map<std::string, int>>& data, const std::string& path, int step) {
        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            logError("Failed to open output report file: " + path);
            return;
        }

        ofs << "=== History Analysis Report ===\n";
        ofs << "Log File: " << logFilePath << "\n";
        ofs << "Range Step: " << step << "s\n";
        ofs << "===============================\n\n";

        if (data.empty()) {
            ofs << "No data found in this time range.\n";
        } else {
            for (const auto& bucket : data) {
                long long time = bucket.first;
                
                int h = (time / 3600) % 24;
                int m = (time / 60) % 60;
                int s = time % 60;
                
                ofs << "[" << std::setfill('0') << std::setw(2) << h << ":" 
                    << std::setw(2) << m << ":" << std::setw(2) << s << "]\n";

                std::vector<std::pair<std::string, int>> sortedWords(bucket.second.begin(), bucket.second.end());
                std::sort(sortedWords.begin(), sortedWords.end(), [](const auto& a, const auto& b){
                    return a.second > b.second;
                });

                for (std::size_t i = 0; i < 5 && i < sortedWords.size(); ++i) {
                    ofs << "  " << i+1 << ". " << sortedWords[i].first << " (" << sortedWords[i].second << ")\n";
                }
                ofs << "\n";
            }
        }
        logInfo("Report successfully saved to " + path);
    }
};
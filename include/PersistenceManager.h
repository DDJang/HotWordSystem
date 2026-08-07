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
#include <memory>
#include <mutex>
#include <atomic>
#include <utility>

// 第三方库
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "ThreadPool.h"

namespace fs = std::filesystem;

class PersistenceManager {
private:
    std::string logFilePath;
    ThreadPool& pool;           // 保留构造接口兼容性；写入由本类串行化
    mutable std::mutex operationMtx; // 串行化批处理、查询和清空操作
    mutable std::mutex fileMtx;  // 文件访问锁
    
    // 批量提交相关
    mutable std::mutex batchMtx;
    using Batch = std::vector<std::pair<long long, std::vector<std::string>>>;
    Batch batchData;
    const size_t BATCH_SIZE = 100; // 每100条数据批量提交一次

    // 压缩存储相关
    bool enableCompression = false; // 是否启用压缩
    const size_t COMPRESS_THRESHOLD = 1024; // 压缩阈值（字节）

    // 内存映射相关
    bool enableMemoryMapped = false; // 是否启用内存映射
    std::atomic<bool> isMapped = false; // 是否已映射

    // 日志轮转相关
    bool enableLogRotation = true; // 是否启用日志轮转
    const size_t MAX_LOG_SIZE = 10 * 1024 * 1024; // 最大日志大小（10MB）
    const size_t MAX_LOG_FILES = 5; // 最大日志文件数

    // 日志记录器
    mutable std::shared_ptr<spdlog::logger> logger;

    // --- 统一日志辅助函数 ---
    void initLogger() const {
        if (!logger) {
            logger = spdlog::get("PersistenceManager");
            if (!logger) {
                try {
                    logger = spdlog::stdout_color_mt("PersistenceManager");
                } catch (const spdlog::spdlog_ex&) {
                    // Another manager may have registered the logger between
                    // get() and stdout_color_mt(). Reuse the registered one.
                    logger = spdlog::get("PersistenceManager");
                }
            }
            if (!logger) return;
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
            logger->set_level(spdlog::level::info);
        }
    }

    void logInfo(const std::string& msg) const {
        initLogger();
        logger->info(msg);
    }

    void logWarn(const std::string& msg) const {
        initLogger();
        logger->warn(msg);
    }

    void logError(const std::string& msg) const {
        initLogger();
        logger->error(msg);
    }

    // 严重的文件锁死/损坏警告，保持醒目
    void printCriticalFileError() const {
        initLogger();
        logger->critical("\n========================================================");
        logger->critical("  [CRITICAL FILE ERROR]");
        logger->critical("  The log file '{}' is corrupted or locked.", logFilePath);
        logger->critical("  The program cannot write to it.");
        logger->critical("");
        logger->critical("  >>> ACTION REQUIRED: <<<");
        logger->critical("  1. Close this program completely.");
        logger->critical("  2. Go to 'data' folder.");
        logger->critical("  3. Manually DELETE 'history.log'.");
        logger->critical("  4. Restart the program.");
        logger->critical("========================================================");
    }

    // 压缩相关方法
    std::string compressData(const std::string& data) const {
        // 简单的压缩实现，实际项目中可以使用zlib等库
        // 这里只是一个示例，实际压缩效果有限
        std::string compressed;
        compressed.reserve(data.size());
        
        char prev = 0;
        int count = 0;
        
        for (char c : data) {
            if (c == prev) {
                count++;
            } else {
                if (count > 0) {
                    compressed += std::to_string(count);
                    compressed += prev;
                }
                prev = c;
                count = 1;
            }
        }
        
        if (count > 0) {
            compressed += std::to_string(count);
            compressed += prev;
        }
        
        return compressed;
    }

    std::string decompressData(const std::string& compressed) const {
        // 简单的解压实现，对应上面的压缩方法
        std::string decompressed;
        
        size_t i = 0;
        while (i < compressed.size()) {
            size_t j = i;
            while (j < compressed.size() && isdigit(compressed[j])) {
                j++;
            }
            
            if (j > i && j < compressed.size()) {
                int count = std::stoi(compressed.substr(i, j - i));
                char c = compressed[j];
                decompressed.append(count, c);
                i = j + 1;
            } else {
                decompressed += compressed[i];
                i++;
            }
        }
        
        return decompressed;
    }

    // 日志轮转相关方法
    void rotateLogUnlocked() {
        fs::path logPath(logFilePath);
        if (!fs::exists(logPath)) {
            return;
        }
        
        // 检查日志大小
        auto fileSize = fs::file_size(logPath);
        if (fileSize < MAX_LOG_SIZE) {
            return;
        }
        
        // 生成轮转文件名
        for (size_t i = MAX_LOG_FILES - 1; i > 0; i--) {
            fs::path oldPath = logPath.string() + "." + std::to_string(i);
            fs::path newPath = logPath.string() + "." + std::to_string(i + 1);
            
            if (fs::exists(oldPath)) {
                if (fs::exists(newPath)) {
                    fs::remove(newPath);
                }
                fs::rename(oldPath, newPath);
            }
        }
        
        // 重命名当前日志文件
        fs::path rotatedPath = logPath.string() + ".1";
        if (fs::exists(rotatedPath)) {
            fs::remove(rotatedPath);
        }
        fs::rename(logPath, rotatedPath);
        
        // 创建新的空日志文件
        std::ofstream newLog(logPath);
        if (newLog.is_open()) {
            newLog.close();
            logInfo("Log rotated: " + rotatedPath.string());
        }
    }

    void rotateLog() {
        std::lock_guard<std::mutex> lock(fileMtx);
        if (enableLogRotation) {
            rotateLogUnlocked();
        }
    }

    Batch takeBatch() {
        Batch dataToWrite;
        std::lock_guard<std::mutex> lock(batchMtx);
        dataToWrite.swap(batchData);
        return dataToWrite;
    }

    void writeBatch(const Batch& dataToWrite) {
        if (dataToWrite.empty()) return;

        std::lock_guard<std::mutex> lock(fileMtx);
        if (enableLogRotation) {
            rotateLogUnlocked();
        }

        std::ofstream ofs(logFilePath, std::ios::app);
        if (!ofs.is_open()) {
            printCriticalFileError();
            return;
        }

        std::string batchContent;
        for (const auto& entry : dataToWrite) {
            const long long timestamp = entry.first;
            const auto& words = entry.second;

            batchContent += std::to_string(timestamp) + "|";
            for (std::size_t i = 0; i < words.size(); ++i) {
                batchContent += words[i];
                if (i < words.size() - 1) batchContent += ",";
            }
            batchContent += "\n";
        }

        if (enableCompression && batchContent.size() > COMPRESS_THRESHOLD) {
            const std::string compressed = compressData(batchContent);
            ofs << "COMPRESSED|" << compressed.size() << "|" << compressed << "\n";
        } else {
            ofs << batchContent;
        }

        if (!ofs) {
            logError("Failed to write history log: " + logFilePath);
        }
    }

    // Must be called while operationMtx is held. The batch is moved out before
    // touching the file so batchMtx is never re-entered by the flush path.
    void flushBatchUnlocked() {
        writeBatch(takeBatch());
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

        std::unique_lock<std::mutex> operationLock(operationMtx);
        Batch dataToWrite;
        {
            std::lock_guard<std::mutex> lock(batchMtx);
            batchData.emplace_back(timestamp, words);

            if (batchData.size() >= BATCH_SIZE) {
                dataToWrite.swap(batchData);
            }
        }

        if (!dataToWrite.empty()) {
            // The operation lock prevents RESET/query from overtaking this
            // write, while the batch lock is already released.
            writeBatch(dataToWrite);
        }
    }
    
    // --- 刷新批量数据 ---
    void flushBatch() {
        std::unique_lock<std::mutex> operationLock(operationMtx);
        flushBatchUnlocked();
    }

    // --- 生成报告 ---
    void generateReport(long long startTime, long long endTime, int timeStep, const std::string& outputPath) {
        std::unique_lock<std::mutex> operationLock(operationMtx);
        flushBatchUnlocked();
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

            // 检查是否是压缩数据
            if (line.substr(0, 10) == "COMPRESSED|") {
                // 处理压缩数据
                std::size_t firstDelim = line.find('|', 10);
                if (firstDelim == std::string::npos) continue;
                
                std::size_t secondDelim = line.find('|', firstDelim + 1);
                if (secondDelim == std::string::npos) continue;
                
                try {
                    static_cast<void>(std::stoull(line.substr(10, firstDelim - 10)));
                    std::string compressedData = line.substr(secondDelim + 1);
                    
                    // 解压数据
                    std::string decompressed = decompressData(compressedData);
                    
                    // 处理解压后的数据
                    std::stringstream ss(decompressed);
                    std::string decompressedLine;
                    while (std::getline(ss, decompressedLine)) {
                        processLogLine(decompressedLine, startTime, endTime, timeStep, reportData);
                    }
                } catch(...) {
                    continue;
                }
            } else {
                // 处理普通数据
                processLogLine(line, startTime, endTime, timeStep, reportData);
            }
        }

        writeReportToFile(reportData, outputPath, timeStep);
    }

    // 处理单条日志行
    void processLogLine(const std::string& line, long long startTime, long long endTime, 
                       int timeStep, std::map<long long, std::unordered_map<std::string, int>>& reportData) {
        if (line.empty()) return;

        std::size_t delimPos = line.find('|'); 
        if (delimPos == std::string::npos) return;

        long long ts = 0;
        try {
            ts = std::stoll(line.substr(0, delimPos));
        } catch(...) { return; }
        
        if (ts < startTime || ts > endTime) return;

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

    // 查询历史区间的聚合 Top-K
    std::vector<std::pair<std::string, int>> queryHistoryTopK(long long startTime, long long endTime, int k) {
        std::unique_lock<std::mutex> operationLock(operationMtx);
        flushBatchUnlocked();
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

            // 检查是否是压缩数据
            if (line.substr(0, 10) == "COMPRESSED|") {
                // 处理压缩数据
                std::size_t firstDelim = line.find('|', 10);
                if (firstDelim == std::string::npos) continue;
                
                std::size_t secondDelim = line.find('|', firstDelim + 1);
                if (secondDelim == std::string::npos) continue;
                
                try {
                    std::string compressedData = line.substr(secondDelim + 1);
                    
                    // 解压数据
                    std::string decompressed = decompressData(compressedData);
                    
                    // 处理解压后的数据
                    std::stringstream ss(decompressed);
                    std::string decompressedLine;
                    while (std::getline(ss, decompressedLine)) {
                        processLogLineForTopK(decompressedLine, startTime, endTime, counts);
                    }
                } catch(...) {
                    continue;
                }
            } else {
                // 处理普通数据
                processLogLineForTopK(line, startTime, endTime, counts);
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

    // 处理单条日志行用于Top-K查询
    void processLogLineForTopK(const std::string& line, long long startTime, long long endTime, 
                              std::unordered_map<std::string, int>& counts) {
        if (line.empty()) return;

        std::size_t delimPos = line.find('|');
        if (delimPos == std::string::npos) return;

        long long ts = 0;
        try {
            ts = std::stoll(line.substr(0, delimPos));
        } catch(...) { return; }
        
        if (ts < startTime || ts > endTime) return;

        std::string content = line.substr(delimPos + 1);
        std::stringstream ss(content);
        std::string word;
        while (std::getline(ss, word, ',')) {
            if (!word.empty()) counts[word]++;
        }
    }

    // 清空日志文件
    void clearLog() {
        std::unique_lock<std::mutex> operationLock(operationMtx);

        // RESET discards data that has not been committed yet. Since all
        // writes are serialized by operationMtx, no older write can run after
        // the truncation and restore stale history.
        {
            std::lock_guard<std::mutex> batchLock(batchMtx);
            batchData.clear();
        }

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

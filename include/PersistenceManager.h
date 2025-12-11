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

namespace fs = std::filesystem;

class PersistenceManager {
private:
    std::string logFilePath;

public:
    PersistenceManager(const std::string& path = "data/history.log") : logFilePath(path) {
        fs::path p(path);

        // 1. 自动创建目录 (例如 data/)
        if (p.has_parent_path()) {
            fs::path parent = p.parent_path();
            if (!fs::exists(parent)) {
                try {
                    fs::create_directories(parent);
                    std::cout << "[Init] Created directory: " << parent.string() << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[Error] Failed to create directory: " << e.what() << std::endl;
                }
            }
        }

        // 2. 自动创建空文件 (防止读取时报错 "No history log found")
        if (!fs::exists(p)) {
            std::ofstream tmp(p); // 打开并立即关闭，这会创建一个空文件
            if (tmp.is_open()) {
                tmp.close();
            } else {
                std::cerr << "[Error] Failed to create log file. Check permissions." << std::endl;
            }
        }
    }

    // --- 写入日志 ---
    void logData(long long timestamp, const std::vector<std::string>& words) {
        if (words.empty()) return;

        std::ofstream ofs(logFilePath, std::ios::app);
        if (!ofs.is_open()) return;

        ofs << timestamp << "|";
        for (std::size_t i = 0; i < words.size(); ++i) {
            ofs << words[i];
            if (i < words.size() - 1) ofs << ",";
        }
        ofs << "\n";
    }

    // --- 生成报告 ---
    void generateReport(long long startTime, long long endTime, int timeStep, const std::string& outputPath) {
        std::ifstream ifs(logFilePath);
        
        // 经过构造函数的处理，这里一般不会再失败了
        // 但如果文件被意外删除，这里还是要做个防御性判断
        if (!ifs.is_open()) {
            // 尝试重新创建
            std::ofstream tmp(logFilePath);
            tmp.close();
            std::cout << "[Warn] History log was missing and has been re-created (Empty)." << std::endl;
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

    // 查询历史区间的聚合 Top-K，直接返回数据给 Web API
    std::vector<std::pair<std::string, int>> queryHistoryTopK(long long startTime, long long endTime, int k) {
        std::ifstream ifs(logFilePath);
        std::unordered_map<std::string, int> counts;
        std::string line;

        if (!ifs.is_open()) return {};

        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            std::size_t delimPos = line.find('|');
            if (delimPos == std::string::npos) continue;

            long long ts = 0;
            try {
                ts = std::stoll(line.substr(0, delimPos));
            } catch(...) { continue; }
            
            // 过滤时间
            if (ts < startTime || ts > endTime) continue;

            // 统计词频
            std::string content = line.substr(delimPos + 1);
            std::stringstream ss(content);
            std::string word;
            while (std::getline(ss, word, ',')) {
                if (!word.empty()) counts[word]++;
            }
        }

        // 排序取 Top-K
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
        std::ofstream ofs(logFilePath, std::ios::trunc);
        ofs.close();
        std::cout << "[System] History log file cleared." << std::endl;
    }

private:

    void writeReportToFile(const std::map<long long, std::unordered_map<std::string, int>>& data, const std::string& path, int step) {
        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            std::cerr << "[Error] Cannot write report to " << path << std::endl;
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
                
                // 简单的格式化时间
                int h = (time / 3600) % 24;
                int m = (time / 60) % 60;
                int s = time % 60;
                
                ofs << "[" << std::setfill('0') << std::setw(2) << h << ":" 
                    << std::setw(2) << m << ":" << std::setw(2) << s << "]\n";

                // 排序 Top-5
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
        std::cout << "[Report] Successfully saved to " << path << std::endl;
    }
};
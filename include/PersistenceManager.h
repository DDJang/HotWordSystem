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
#include <filesystem> // C++17 文件系统库

using namespace std;
namespace fs = std::filesystem;

class PersistenceManager {
private:
    string logFilePath;

public:
    PersistenceManager(const string& path = "data/history.log") : logFilePath(path) {
        fs::path p(path);

        // 1. 自动创建目录 (例如 data/)
        if (p.has_parent_path()) {
            fs::path parent = p.parent_path();
            if (!fs::exists(parent)) {
                try {
                    fs::create_directories(parent);
                    cout << "[Init] Created directory: " << parent.string() << endl;
                } catch (const exception& e) {
                    cerr << "[Error] Failed to create directory: " << e.what() << endl;
                }
            }
        }

        // 2. 自动创建空文件 (防止读取时报错 "No history log found")
        if (!fs::exists(p)) {
            ofstream tmp(p); // 打开并立即关闭，这会创建一个空文件
            if (tmp.is_open()) {
                // cout << "[Init] Created empty log file: " << path << endl;
                tmp.close();
            } else {
                cerr << "[Error] Failed to create log file. Check permissions." << endl;
            }
        }
    }

    // --- 写入日志 ---
    void logData(long long timestamp, const vector<string>& words) {
        if (words.empty()) return;

        // ios::app 表示追加模式
        ofstream ofs(logFilePath, ios::app); 
        if (!ofs.is_open()) return;

        ofs << timestamp << "|";
        for (size_t i = 0; i < words.size(); ++i) {
            ofs << words[i];
            if (i < words.size() - 1) ofs << ",";
        }
        ofs << "\n";
    }

    // --- 生成报告 ---
    void generateReport(long long startTime, long long endTime, int timeStep, const string& outputPath) {
        ifstream ifs(logFilePath);
        
        // 经过构造函数的处理，这里一般不会再失败了
        // 但如果文件被意外删除，这里还是要做个防御性判断
        if (!ifs.is_open()) {
            // 尝试重新创建
            ofstream tmp(logFilePath); 
            tmp.close();
            cout << "[Warn] History log was missing and has been re-created (Empty)." << endl;
            return;
        }

        map<long long, unordered_map<string, int>> reportData;
        string line;

        while (getline(ifs, line)) {
            if (line.empty()) continue;
            size_t delimPos = line.find('|');
            if (delimPos == string::npos) continue;

            long long ts = 0;
            try {
                ts = stoll(line.substr(0, delimPos));
            } catch(...) { continue; }
            
            if (ts < startTime || ts > endTime) continue;

            long long bucketTime = (ts / timeStep) * timeStep;
            string content = line.substr(delimPos + 1);
            stringstream ss(content);
            string word;
            while (getline(ss, word, ',')) {
                if (!word.empty()) {
                    reportData[bucketTime][word]++;
                }
            }
        }

        writeReportToFile(reportData, outputPath, timeStep);
    }

private:
    void writeReportToFile(const map<long long, unordered_map<string, int>>& data, const string& path, int step) {
        ofstream ofs(path);
        if (!ofs.is_open()) {
            cerr << "[Error] Cannot write report to " << path << endl;
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
                
                ofs << "[" << setfill('0') << setw(2) << h << ":" 
                    << setw(2) << m << ":" << setw(2) << s << "]\n";

                // 排序 Top-5
                vector<pair<string, int>> sortedWords(bucket.second.begin(), bucket.second.end());
                sort(sortedWords.begin(), sortedWords.end(), [](const auto& a, const auto& b){
                    return a.second > b.second;
                });

                for (size_t i = 0; i < 5 && i < sortedWords.size(); ++i) {
                    ofs << "  " << i+1 << ". " << sortedWords[i].first << " (" << sortedWords[i].second << ")\n";
                }
                ofs << "\n";
            }
        }
        cout << "[Report] Successfully saved to " << path << endl;
    }
};
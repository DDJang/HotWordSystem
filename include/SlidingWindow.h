#pragma once

#include <map>
#include <vector>
#include <deque>
#include <string>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <mutex>

using namespace std;

class SlidingWindow {
private:
    long long windowSizeSeconds;
    long long currentTimeCursor = 0;
    
    // 【核心修复】最大保留时间（物理存储时间），例如 3600秒 (1小时)
    // 即使窗口只有 10秒，我们也保留 1小时数据，以便用户随时把窗口拉大
    long long maxRetentionSeconds = 3600; 

    // 1. 物理存储 (Storage)：保留所有原始数据，用于“回滚”和“重算”
    // map 自动按时间排序
    map<long long, vector<string>> storage;
    
    // 2. 逻辑视图 (Active View)：仅保存当前窗口内的有效数据，用于快速增减词频
    // 使用 deque 模拟滑动窗口队列，头部是最早的数据
    deque<pair<long long, vector<string>>> activeData;

    // 实时词频统计 (对应 activeData)
    unordered_map<string, int> wordCounts;

    mutable std::mutex mtx;

public:
    SlidingWindow(long long windowSize = 600) : windowSizeSeconds(windowSize) {}

    // --- 数据接入 ---
    void addData(long long timestamp, const vector<string>& words) {
        std::lock_guard<std::mutex> lock(mtx);

        if (words.empty()) return;

        // 1. 更新系统时间
        if (timestamp > currentTimeCursor) {
            currentTimeCursor = timestamp;
        }

        // 2. 存入物理存储 (Storage) - 只要不过期太久都存
        // 注意：这里处理乱序，如果 key 已存在则追加
        if (storage.find(timestamp) == storage.end()) {
            storage[timestamp] = words;
        } else {
            storage[timestamp].insert(storage[timestamp].end(), words.begin(), words.end());
        }

        // 3. 更新逻辑视图 (Active View)
        // 只有当数据在“当前窗口”范围内时，才计入统计
        long long threshold = currentTimeCursor - windowSizeSeconds;
        if (timestamp > threshold) {
            // 计入词频
            for (const string& w : words) {
                wordCounts[w]++;
            }
            // 放入活跃队列 (注意：deque 只能处理按序到达的数据)
            // 如果是乱序旧数据(但还在窗口内)，上面的 storage 已经存了，
            // 但 deque 插入中间比较麻烦。
            // 为了简化复杂度：如果是乱序数据，且影响了统计，最稳妥的方式是【触发一次轻量级重算】
            // 或者：由于 addData 主要是处理最新时间，我们假设乱序只发生在小范围内。
            // 简单的处理：直接 push_back。如果是迟到数据，虽然时间戳小，但在队列尾部。
            // 下面 evicted 的时候会根据时间戳判断，所以 push_back 是安全的。
            activeData.push_back({timestamp, words});
        }

        // 4. 执行淘汰 (逻辑淘汰 + 物理淘汰)
        evictOutdatedData();
    }

    // --- 【关键修复】设置窗口大小 ---
    void setWindowSize(long long newSizeSeconds) {
        std::lock_guard<std::mutex> lock(mtx);

        if (newSizeSeconds <= 0) return;
        
        cout << "[System] Window size changed: " << windowSizeSeconds 
             << "s -> " << newSizeSeconds << "s (Rebuilding view...)" << endl;
        
        windowSizeSeconds = newSizeSeconds;
        
        // 【核心逻辑】完全重构视图 (Rebuild)
        // 1. 清空当前的统计和队列
        wordCounts.clear();
        activeData.clear();

        // 2. 计算新的有效时间范围
        long long threshold = currentTimeCursor - windowSizeSeconds;

        // 3. 从物理存储中，把符合新范围的数据捞回来
        // upper_bound(threshold) 返回第一个 > threshold 的元素，正是我们需要开始的地方
        auto it = storage.upper_bound(threshold);
        
        for (; it != storage.end(); ++it) {
            long long ts = it->first;
            const vector<string>& words = it->second;

            // 重新加入队列
            activeData.push_back({ts, words});
            // 重新统计词频
            for (const string& w : words) {
                wordCounts[w]++;
            }
        }
        
        // 4. 顺便清理一下太老的物理数据
        evictPhysicalStorage();
    }

    // 获取当前窗口范围
    pair<long long, long long> getWindowRange() {
        std::lock_guard<std::mutex> lock(mtx);
        long long endT = currentTimeCursor;
        long long startT = (endT > windowSizeSeconds) ? (endT - windowSizeSeconds) : 0;
        return {startT, endT};
    }

    // 趋势分析 (逻辑不变，基于 storage 计算)
    vector<pair<string, double>> getTrendingWords(int topK) {
        std::lock_guard<std::mutex> lock(mtx);
        if (storage.empty()) return {};

        long long midPoint = currentTimeCursor - (windowSizeSeconds / 2);
        long long startPoint = currentTimeCursor - windowSizeSeconds;

        unordered_map<string, int> recentCounts; 
        unordered_map<string, int> oldCounts;    

        // 直接遍历 storage 比较准确
        auto it = storage.upper_bound(startPoint);
        for (; it != storage.end(); ++it) {
            long long ts = it->first;
            const auto& words = it->second;
            if (ts >= midPoint) {
                for (const auto& w : words) recentCounts[w]++;
            } else {
                for (const auto& w : words) oldCounts[w]++;
            }
        }

        vector<pair<string, double>> trends;
        for (const auto& kv : wordCounts) {
            const string& word = kv.first;
            int r = recentCounts[word];
            int o = oldCounts[word];
            if (kv.second < 2) continue; 
            double score = (double)r - (double)o; 
            if (score != 0) trends.push_back({word, score});
        }

        auto cmp = [](const pair<string, double>& a, const pair<string, double>& b) {
            return a.second > b.second; 
        };
        sort(trends.begin(), trends.end(), cmp);
        if (trends.size() > topK) trends.resize(topK);
        return trends;
    }

    // Top-K (基于 wordCounts)
    vector<pair<string, int>> getTopK(int k) {
        std::lock_guard<std::mutex> lock(mtx);
        auto cmp = [](const pair<string, int>& a, const pair<string, int>& b) {
            return a.second > b.second;
        };
        priority_queue<pair<string, int>, vector<pair<string, int>>, decltype(cmp)> minHeap(cmp);
        for (const auto& entry : wordCounts) {
            minHeap.push(entry);
            if (minHeap.size() > k) minHeap.pop();
        }
        vector<pair<string, int>> result;
        while (!minHeap.empty()) {
            result.push_back(minHeap.top());
            minHeap.pop();
        }
        reverse(result.begin(), result.end());
        return result;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mtx);
        storage.clear();
        activeData.clear();
        wordCounts.clear();
        currentTimeCursor = 0;
    }

    // 【新增】设置最大保留时间 (Storage)
    void setMaxRetention(long long seconds) {
        std::lock_guard<std::mutex> lock(mtx);
        
        // 保护机制：物理存储时间不能小于当前的逻辑窗口时间
        if (seconds < windowSizeSeconds) {
            seconds = windowSizeSeconds;
        }
        
        cout << "[System] Max retention changed: " << maxRetentionSeconds 
             << "s -> " << seconds << "s" << endl;
             
        maxRetentionSeconds = seconds;
        
        // 如果改小了，立即触发一次清理
        evictPhysicalStorage();
    }

    // 【新增】获取当前保留时间 (供 UI 显示)
    long long getMaxRetention() {
        std::lock_guard<std::mutex> lock(mtx);
        return maxRetentionSeconds;
    }

private:
    void evictOutdatedData() {
        long long threshold = currentTimeCursor - windowSizeSeconds;

        // 1. 逻辑淘汰：仅从 activeData 和 wordCounts 中移除
        // 检查队列头部（最早进入的数据）
        while (!activeData.empty()) {
            // 如果头部数据的时间戳 <= 阈值，说明滑出窗口了
            if (activeData.front().first <= threshold) {
                const vector<string>& words = activeData.front().second;
                // 扣减词频
                for (const string& w : words) {
                    wordCounts[w]--;
                    if (wordCounts[w] <= 0) {
                        wordCounts.erase(w);
                    }
                }
                activeData.pop_front();
            } else {
                // 头部都在窗口内，后面的肯定也在，退出
                break;
            }
        }

        // 2. 物理淘汰：清理 storage 中太老的数据（比如 1小时前的）
        evictPhysicalStorage();
    }

    void evictPhysicalStorage() {
        if (storage.empty()) return;
        // 物理保留时间 = 当前时间 - maxRetentionSeconds
        long long physicalThreshold = currentTimeCursor - maxRetentionSeconds;
        
        // map 是有序的，直接检查头部
        auto it = storage.begin();
        while (it != storage.end()) {
            if (it->first <= physicalThreshold) {
                it = storage.erase(it); // 返回下一个迭代器
            } else {
                break; // 只要遇到一个没过期的，后面的肯定也没过期
            }
        }
    }
};
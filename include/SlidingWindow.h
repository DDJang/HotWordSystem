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
#include <utility>

class SlidingWindow {
private:
    long long windowSizeSeconds;
    long long currentTimeCursor = 0;
    
    // 最大保留时间（物理存储时间），例如 3600秒 (1小时)
    // 即使窗口只有 10秒，我们也保留 1小时数据，以便用户随时把窗口拉大
    long long maxRetentionSeconds = 3600; 

    // 1. 物理存储 (Storage)：保留所有原始数据，用于“回滚”和“重算”
    std::map<long long, std::vector<std::string>> storage;
    
    // 2. 逻辑视图 (Active View)：仅保存当前窗口内的有效数据，用于快速增减词频
    // 使用 deque 模拟滑动窗口队列，头部是最早的数据
    std::deque<std::pair<long long, std::vector<std::string>>> activeData;

    // 实时词频统计 (对应 activeData)
    std::unordered_map<std::string, int> wordCounts;

    mutable std::mutex mtx;

    // 最大允许存储的条目数 (防止内存爆炸)
    // 假设每条数据平均占 1KB，50万条大约占用 500MB 内存，是一个比较安全的上限
    const size_t MAX_STORAGE_ENTRIES = 500000; 


public:
    SlidingWindow(long long windowSize = 600) : windowSizeSeconds(windowSize) {}

    // --- 数据接入 ---
    void addData(long long timestamp, const std::vector<std::string>& words) {
        std::lock_guard<std::mutex> lock(mtx);

        if (words.empty()) return;

        // 1. 更新系统时间
        if (timestamp > currentTimeCursor) {
            currentTimeCursor = timestamp;
        }

        // 2. 存入物理存储 (Storage) - 只要不过期太久都存
        // 注意：这里处理乱序，如果 key 已存在则追加
        auto storageIt = storage.find(timestamp);
        if (storageIt == storage.end()) {
            storage[timestamp] = words;
        } else {
            storageIt->second.insert(storageIt->second.end(), words.begin(), words.end());
        }

        // 3. 更新逻辑视图 (Active View)
        // 只有当数据在“当前窗口”范围内时，才计入统计
        long long threshold = currentTimeCursor - windowSizeSeconds;
        if (timestamp > threshold) {
            // 计入词频
            for (const std::string& w : words) {
                wordCounts[w]++;
            }
            // 放入活跃队列
            activeData.push_back({timestamp, words});
        }

        // 4. 执行淘汰 (逻辑淘汰 + 物理淘汰)
        evictOutdatedData();
        evictOverLimitData();
    }

    // --- 设置窗口大小 ---
    void setWindowSize(long long newSizeSeconds) {
        std::lock_guard<std::mutex> lock(mtx);

        if (newSizeSeconds <= 0) return;
        
        std::cout << "[System] Window size changed: " << windowSizeSeconds 
             << "s -> " << newSizeSeconds << "s (Rebuilding view...)" << std::endl;
        
        windowSizeSeconds = newSizeSeconds;
        
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
            const std::vector<std::string>& words = it->second;

            // 重新加入队列
            activeData.push_back({ts, words});
            // 重新统计词频
            for (const std::string& w : words) {
                wordCounts[w]++;
            }
        }
        
        // 4. 顺便清理一下太老的物理数据
        evictPhysicalStorage();
    }

    // 获取当前窗口范围
    std::pair<long long, long long> getWindowRange() {
        std::lock_guard<std::mutex> lock(mtx);
        long long endT = currentTimeCursor;
        long long startT = (endT > windowSizeSeconds) ? (endT - windowSizeSeconds) : 0;
        return {startT, endT};
    }

    // 趋势分析 (基于 storage 计算)
    std::vector<std::pair<std::string, double>> getTrendingWords(int topK) {
        std::lock_guard<std::mutex> lock(mtx);
        if (storage.empty()) return {};

        long long midPoint = currentTimeCursor - (windowSizeSeconds / 2);
        long long startPoint = currentTimeCursor - windowSizeSeconds;

        std::unordered_map<std::string, int> recentCounts; 
        std::unordered_map<std::string, int> oldCounts;    

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

        std::vector<std::pair<std::string, double>> trends;
        for (const auto& kv : wordCounts) {
            const std::string& word = kv.first;
            int r = recentCounts[word];
            int o = oldCounts[word];
            if (kv.second < 2) continue; 
            double score = static_cast<double>(r) - static_cast<double>(o);
            if (score != 0) trends.push_back({word, score});
        }

        auto cmp = [](const std::pair<std::string, double>& a, const std::pair<std::string, double>& b) {
            return a.second > b.second; 
        };
        std::sort(trends.begin(), trends.end(), cmp);
        if (trends.size() > static_cast<std::size_t>(topK)) {
            trends.resize(static_cast<std::size_t>(topK));
        }
        return trends;
    }

    // Top-K (基于 wordCounts)
    std::vector<std::pair<std::string, int>> getTopK(int k) {
        std::lock_guard<std::mutex> lock(mtx);

        auto cmp = [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
            return a.second > b.second;
        };
        std::priority_queue<std::pair<std::string, int>, 
                            std::vector<std::pair<std::string, int>>, 
                            decltype(cmp)> minHeap(cmp);
        
        for (const auto& entry : wordCounts) {
            minHeap.push(entry);
            if (minHeap.size() > static_cast<std::size_t>(k)) {
                minHeap.pop();
            }
        }
        
        std::vector<std::pair<std::string, int>> result;
        while (!minHeap.empty()) {
            result.push_back(minHeap.top());
            minHeap.pop();
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mtx);
        storage.clear();
        activeData.clear();
        wordCounts.clear();
        currentTimeCursor = 0;
    }

    // 设置最大保留时间 (Storage)
    void setMaxRetention(long long seconds) {
        std::lock_guard<std::mutex> lock(mtx);
        
        // 保护机制：物理存储时间不能小于当前的逻辑窗口时间
        if (seconds < windowSizeSeconds) {
            seconds = windowSizeSeconds;
        }
        
        std::cout << "[System] Max retention changed: " << maxRetentionSeconds 
             << "s -> " << seconds << "s" << std::endl;
             
        maxRetentionSeconds = seconds;
        
        // 如果改小了，立即触发一次清理
        evictPhysicalStorage();
    }

    // 获取当前保留时间 (供 UI 显示)
    long long getMaxRetention() {
        std::lock_guard<std::mutex> lock(mtx);
        return maxRetentionSeconds;
    }

private:
    // 强制容量限制
    void evictOverLimitData() {
        // 如果 storage map 的大小超过了限制
        while (storage.size() > MAX_STORAGE_ENTRIES) {
            // storage.begin() 是最老的时间戳
            // 注意：如果被删除的数据恰好在当前的 Active Window 内（窗口极大），
            // 理论上我们也应该从 activeData 和 wordCounts 里扣除。
            // 但这种情况极少见（意味着你的窗口比内存上限还大），
            // 为了防止系统崩溃，我们优先保命（删除 storage），数据一致性做次要处理。
            
            // 简单处理：仅从物理存储删除，防止 storage 无限膨胀
            // 如果用户此时把窗口拉大到覆盖这些被删除的数据，会发现数据缺失（这是预期的牺牲）
            storage.erase(storage.begin());
        }
        
        // 同时也检查一下 activeData (逻辑队列)，防止极端情况下队列过长
        // 比如有人恶意把 windowSize 设为 10年
        while (activeData.size() > MAX_STORAGE_ENTRIES) {
            const auto& pair = activeData.front();
            const std::vector<std::string>& words = pair.second;
            
            // 既然从 active 移除了，必须扣减词频
            for (const std::string& w : words) {
                auto it = wordCounts.find(w);
                if (it != wordCounts.end()) {
                    it->second--;
                    if (it->second <= 0) wordCounts.erase(it);
                }
            }
            activeData.pop_front();
        }
    }

    void evictOutdatedData() {
        long long threshold = currentTimeCursor - windowSizeSeconds;

        // 1. 逻辑淘汰：仅从 activeData 和 wordCounts 中移除
        // 检查队列头部（最早进入的数据）
        while (!activeData.empty()) {
            // 如果头部数据的时间戳 <= 阈值，说明滑出窗口了
            if (activeData.front().first <= threshold) {
                const std::vector<std::string>& words = activeData.front().second;
                // 扣减词频
                for (const std::string& w : words) {
                    auto countIt = wordCounts.find(w);
                    if (countIt != wordCounts.end()) {
                        countIt->second--;
                        if (countIt->second <= 0) {
                            wordCounts.erase(countIt);
                        }
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
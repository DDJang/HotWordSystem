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
#include <shared_mutex>
#include <atomic>
#include <memory>

#include "SystemContext.h"

class SlidingWindow {
private:
    std::atomic<long long> windowSizeSeconds;
    std::atomic<long long> currentTimeCursor{0};
    
    // 最大保留时间（物理存储时间），例如 6000秒 (100分钟)
    // 即使窗口只有 10秒，我们也保留 100分钟数据，以便用户随时把窗口拉大
    std::atomic<long long> maxRetentionSeconds{6000};

    // 1. 物理存储 (Storage)：保留所有原始数据，用于“回滚”和“重算”
    // 使用std::map保证时间戳有序
    std::map<long long, std::vector<std::string>> storage;
    
    // 2. 逻辑视图 (Active View)：仅保存当前窗口内的有效数据，用于快速增减词频
    // 使用 deque 模拟滑动窗口队列，头部是最早的数据
    std::deque<std::pair<long long, std::vector<std::string>>> activeData;

    // 实时词频统计 (对应 activeData)
    std::unordered_map<std::string, int> wordCounts;

    SystemContext& ctx;

    // 使用读写锁替代互斥锁，减少读操作的锁竞争
    mutable std::shared_mutex rw_mtx;

    // 最大允许存储的条目数 (防止内存爆炸)
    // 假设每条数据平均占 1KB，200万条大约占用 2GB 内存，是一个比较合理的上限
    const size_t MAX_STORAGE_ENTRIES = 2000000;
    
    // activeData的最大条目数，只存储当前窗口内的数据
    // 假设窗口大小为10分钟，每秒10条数据，大约6000条
    const size_t MAX_ACTIVE_ENTRIES = 100000;
    

public:
    // 构造函数，接收 SystemContext 引用
    SlidingWindow(long long windowSize, SystemContext& context) 
        : windowSizeSeconds(windowSize), ctx(context) {}

    // --- 数据接入 ---
    void addData(long long timestamp, const std::vector<std::string>& words) {
        if (words.empty()) return;

        // reset 与 addData 通过同一把写锁互斥，不再依赖固定 sleep 或轮询。
        std::unique_lock<std::shared_mutex> lock(rw_mtx);

        // currentTimeCursor 是当前输入流的最大时间戳。乱序输入不会回退游标。
        if (timestamp > currentTimeCursor.load(std::memory_order_relaxed)) {
            currentTimeCursor.store(timestamp, std::memory_order_relaxed);
        }

        const long long windowSize = windowSizeSeconds.load(std::memory_order_relaxed);
        const long long threshold = currentTimeCursor.load(std::memory_order_relaxed) - windowSize;

        // 存入物理存储；同一时间戳的数据合并，便于 resize 后重建。
        auto storageIt = storage.find(timestamp);
        if (storageIt == storage.end()) {
            storage.emplace(timestamp, words);
        } else {
            storageIt->second.reserve(storageIt->second.size() + words.size());
            storageIt->second.insert(storageIt->second.end(), words.begin(), words.end());
        }

        // 只有严格晚于当前窗口下界的数据进入 active view。晚到但仍在窗口内
        // 的数据按时间戳插入，避免破坏 activeData 的时间顺序；更老的数据
        // 只保留在 storage（如果仍在 retention 范围内）。
        if (timestamp > threshold) {
            for (const std::string& word : words) {
                ++wordCounts[word];
            }

            auto insertPosition = std::upper_bound(
                activeData.begin(), activeData.end(), timestamp,
                [](long long value, const auto& entry) {
                    return value < entry.first;
                });
            activeData.insert(insertPosition, {timestamp, words});
        }

        // 每次接入都清理，保证查询不会看到已经超出逻辑窗口的数据。
        evictOutdatedData();
        evictOverLimitData();
    }

    // --- 设置窗口大小 ---
    void setWindowSize(long long newSizeSeconds) {
        if (newSizeSeconds <= 0) return;
        
        // 加写锁处理窗口大小变更
        {
            std::unique_lock<std::shared_mutex> lock(rw_mtx);

            long long oldSize = windowSizeSeconds;
            if (oldSize == newSizeSeconds) return;
            
            std::cout << "[System] Window size changed: " << oldSize 
                 << "s -> " << newSizeSeconds << "s (Rebuilding view...)" << std::endl;
            
            windowSizeSeconds = newSizeSeconds;
            
            // 1. 清空当前的统计和队列
            wordCounts.clear();
            activeData.clear();

            // 2. 计算新的有效时间范围
            long long threshold = currentTimeCursor - newSizeSeconds;

            // 3. 从物理存储中，把符合新范围的数据捞回来
            // upper_bound(threshold) 返回第一个 > threshold 的元素，正是我们需要开始的地方
            auto it = storage.upper_bound(threshold); 
            for (; it != storage.end(); ++it) {
                long long ts = it->first;
                const std::vector<std::string>& words = it->second;

                // 重新加入队列
                activeData.emplace_back(ts, words);
                // 重新统计词频
                for (const std::string& w : words) {
                    wordCounts[w]++;
                }
            }
            
            // 4. 顺便清理一下太老的物理数据
            evictPhysicalStorage();
            evictOverLimitData();
        }
    }

    // 获取当前窗口范围
    std::pair<long long, long long> getWindowRange() {
        // 使用读锁，允许多线程同时读取
        std::shared_lock<std::shared_mutex> lock(rw_mtx);
        long long endT = currentTimeCursor;
        long long windowSize = windowSizeSeconds;
        long long startT = (endT > windowSize) ? (endT - windowSize) : 0;
        return {startT, endT};
    }

    // 趋势分析 (基于 storage 计算)
    std::vector<std::pair<std::string, double>> getTrendingWords(int topK) {
        // 使用读锁，允许多线程同时读取
        std::shared_lock<std::shared_mutex> lock(rw_mtx);
        if (storage.empty()) return {};

        long long currentTime = currentTimeCursor;
        long long windowSize = windowSizeSeconds;
        long long midPoint = currentTime - (windowSize / 2);
        long long startPoint = currentTime - windowSize;

        std::unordered_map<std::string, int> recentCounts; 
        std::unordered_map<std::string, int> oldCounts;    

        // 直接遍历 storage 比较准确
        auto it = storage.upper_bound(startPoint);
        
        // 预分配空间，减少内存重分配
        // 对于std::map，使用std::distance计算迭代器距离
        size_t estimatedSize = std::distance(it, storage.end());
        if (estimatedSize > 0) {
            // 估算每个时间戳平均包含的单词数
            size_t avgWordsPerEntry = 5;
            recentCounts.reserve(estimatedSize * avgWordsPerEntry);
            oldCounts.reserve(estimatedSize * avgWordsPerEntry);
        }
        
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
        // 预分配空间
        trends.reserve(wordCounts.size());
        
        for (const auto& kv : wordCounts) {
            const std::string& word = kv.first;
            int r = recentCounts[word];
            int o = oldCounts[word];
            if (kv.second < 2) continue; 
            double score = static_cast<double>(r) - static_cast<double>(o);
            if (score != 0) trends.emplace_back(word, score);
        }

        // 优化排序：只排序需要的前topK个元素
        if (!trends.empty()) {
            // 使用partial_sort替代sort，只排序前topK个元素
            auto cmp = [](const std::pair<std::string, double>& a, const std::pair<std::string, double>& b) {
                return a.second > b.second; 
            };
            
            if (trends.size() > static_cast<std::size_t>(topK)) {
                std::partial_sort(trends.begin(), trends.begin() + topK, trends.end(), cmp);
                trends.resize(static_cast<std::size_t>(topK));
            } else {
                std::sort(trends.begin(), trends.end(), cmp);
            }
        }
        
        return trends;
    }

    // Top-K (基于 wordCounts)
    std::vector<std::pair<std::string, int>> getTopK(int k) {
        // 使用读锁，允许多线程同时读取
        std::shared_lock<std::shared_mutex> lock(rw_mtx);

        if (k <= 0 || wordCounts.empty()) return {};

        // 使用最小堆，保持最多k个元素
        auto cmp = [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
            return a.second > b.second;
        };
        std::priority_queue<std::pair<std::string, int>, 
                            std::vector<std::pair<std::string, int>>, 
                            decltype(cmp)> minHeap(cmp);
        
        // 预分配堆的容量，减少内存重分配
        minHeap = decltype(minHeap)(cmp, std::vector<std::pair<std::string, int>>());
        
        for (const auto& entry : wordCounts) {
            minHeap.push(entry);
            if (minHeap.size() > static_cast<std::size_t>(k)) {
                minHeap.pop();
            }
        }
        
        // 预分配结果向量的容量
        std::vector<std::pair<std::string, int>> result;
        result.reserve(minHeap.size());
        
        while (!minHeap.empty()) {
            result.push_back(minHeap.top());
            minHeap.pop();
        }
        
        // 反转结果，使其按词频降序排列
        std::reverse(result.begin(), result.end());
        return result;
    }

    void reset() {
        // addData 同样持有 rw_mtx 的写锁，因此 reset 会等待正在进行的
        // 数据接入完成，并阻止新的接入进入临界区。
        std::unique_lock<std::shared_mutex> lock(rw_mtx);
        storage.clear();
        activeData.clear();
        wordCounts.clear();
        currentTimeCursor.store(0, std::memory_order_relaxed);
    }

    // 设置最大保留时间 (Storage)
    void setMaxRetention(long long seconds) {
        // 使用写锁，因为需要修改保留时间并可能触发清理
        std::unique_lock<std::shared_mutex> lock(rw_mtx);
        
        long long currentWindowSize = windowSizeSeconds;
        // 保护机制：物理存储时间不能小于当前的逻辑窗口时间
        if (seconds < currentWindowSize) {
            seconds = currentWindowSize;
        }
        
        long long oldRetention = maxRetentionSeconds;
        if (oldRetention == seconds) return;
        
        std::cout << "[System] Max retention changed: " << oldRetention 
             << "s -> " << seconds << "s" << std::endl;
             
        maxRetentionSeconds = seconds;
        
        // 如果改小了，立即触发一次清理
        if (seconds < oldRetention) {
            evictPhysicalStorage();
        }
    }

    // 获取当前保留时间 (供 UI 显示)
    long long getMaxRetention() {
        // 使用读锁，允许多线程同时读取
        std::shared_lock<std::shared_mutex> lock(rw_mtx);
        return maxRetentionSeconds;
    }

private:
    void removeActiveEntriesAtTimestamp(long long timestamp) {
        for (auto it = activeData.begin(); it != activeData.end();) {
            if (it->first < timestamp) {
                ++it;
                continue;
            }
            if (it->first > timestamp) break;

            for (const std::string& word : it->second) {
                auto countIt = wordCounts.find(word);
                if (countIt != wordCounts.end()) {
                    if (--countIt->second <= 0) {
                        wordCounts.erase(countIt);
                    }
                }
            }
            it = activeData.erase(it);
        }
    }

    // 强制容量限制
    void evictOverLimitData() {
        bool capacityEvicted = false;

        if (storage.size() > MAX_STORAGE_ENTRIES) {
            size_t excessStorage = storage.size() - MAX_STORAGE_ENTRIES;
            auto it = storage.begin();
            
            for (size_t i = 0; i < excessStorage && it != storage.end(); ++i) {
                // storage.begin() 是最老的时间戳。若它仍在 activeData 中，
                // 先同步移除 active 条目和词频，避免当前视图继续引用已丢弃数据。
                removeActiveEntriesAtTimestamp(it->first);
                it = storage.erase(it);
                capacityEvicted = true;
            }
        }
        
        // 同时也检查一下 activeData (逻辑队列)，防止极端情况下队列过长
        // 比如有人恶意把 windowSize 设为 10年
        if (activeData.size() > MAX_ACTIVE_ENTRIES) {
            size_t excessActive = activeData.size() - MAX_ACTIVE_ENTRIES;
            capacityEvicted = true;
            
            for (size_t i = 0; i < excessActive && !activeData.empty(); ++i) {
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

        if (capacityEvicted) {
            ctx.capacityLimitEvictionOccurred.store(true, std::memory_order_release);
        }
    }

    void evictOutdatedData() {
        long long currentTime = currentTimeCursor;
        long long windowSize = windowSizeSeconds;
        long long threshold = currentTime - windowSize;

        // 1. 逻辑淘汰：仅从 activeData 和 wordCounts 中移除。
        while (!activeData.empty() && activeData.front().first <= threshold) {
            const auto& pair = activeData.front();
            const std::vector<std::string>& words = pair.second;
            
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
        }

        // 2. 物理淘汰：清理 storage 中太老的数据（比如 1小时前的）
        evictPhysicalStorage();
    }

    void evictPhysicalStorage() {
        if (storage.empty()) return;
        // 物理保留时间 = 当前时间 - maxRetentionSeconds
        long long currentTime = currentTimeCursor;
        long long retention = maxRetentionSeconds;
        long long physicalThreshold = currentTime - retention;
        
        // map 是有序的，直接检查头部
        auto it = storage.begin();
        // 一个flag，只要发生了一次删除，就设置
        bool evicted = false;

        while (it != storage.end() && it->first <= physicalThreshold) {
            it = storage.erase(it); // 返回下一个迭代器
            evicted = true; // 标记发生了驱逐
        }

        // 如果发生了驱逐，设置时间超限标志
        if (evicted) {
            ctx.timeLimitEvictionOccurred = true;
        }
    }
};

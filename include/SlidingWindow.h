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

    // 重置标志，用于控制reset操作期间的数据添加
    std::atomic<bool> isResetting{false};

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
    
    // 批量处理阈值
    const size_t BATCH_EVICTION_SIZE = 1000;


public:
    // 构造函数，接收 SystemContext 引用
    SlidingWindow(long long windowSize, SystemContext& context) 
        : windowSizeSeconds(windowSize), ctx(context) {}

    // --- 数据接入 ---
    void addData(long long timestamp, const std::vector<std::string>& words) {
        if (words.empty()) return;

        // 检查是否正在执行reset操作
        while (isResetting.load(std::memory_order_acquire)) {
            // 如果正在执行reset操作，等待直到完成
            std::this_thread::yield();
        }

        // 再次检查是否正在执行reset操作，因为可能在等待期间状态发生了变化
        if (isResetting.load(std::memory_order_acquire)) {
            return;
        }

        // 1. 更新系统时间（无锁操作）
        long long oldCursor = currentTimeCursor;
        while (timestamp > oldCursor) {
            if (currentTimeCursor.compare_exchange_weak(oldCursor, timestamp)) {
                break;
            }
            // 每次尝试更新后，都检查是否正在执行reset操作
            if (isResetting.load(std::memory_order_acquire)) {
                return;
            }
        }

        // 检查是否正在执行reset操作，因为可能在更新系统时间期间状态发生了变化
        if (isResetting.load(std::memory_order_acquire)) {
            return;
        }

        // 2. 获取窗口大小（无锁操作）
        long long windowSize = windowSizeSeconds;
        long long threshold = timestamp - windowSize;

        // 检查是否正在执行reset操作，因为可能在获取窗口大小期间状态发生了变化
        if (isResetting.load(std::memory_order_acquire)) {
            return;
        }

        // 3. 加写锁处理数据存储和更新
        {
            std::unique_lock<std::shared_mutex> lock(rw_mtx);

            // 再次检查是否正在执行reset操作，因为可能在获取锁期间状态发生了变化
            if (isResetting.load(std::memory_order_acquire)) {
                return;
            }

            // 存入物理存储 (Storage) - 只要不过期太久都存
            // 注意：这里处理乱序，如果 key 已存在则追加
            auto storageIt = storage.find(timestamp);
            if (storageIt == storage.end()) {
                storage[timestamp] = words;
            } else {
                // 预留空间，减少内存重分配
                storageIt->second.reserve(storageIt->second.size() + words.size());
                storageIt->second.insert(storageIt->second.end(), words.begin(), words.end());
            }

            // 更新逻辑视图 (Active View)
            // 只有当数据在“当前窗口”范围内时，才计入统计
            if (timestamp > threshold) {
                // 计入词频
                for (const std::string& w : words) {
                    wordCounts[w]++;
                }
                // 放入活跃队列，使用move减少拷贝
                activeData.emplace_back(timestamp, words);
            }

            // 4. 批量执行淘汰 (逻辑淘汰 + 物理淘汰)
            // 每处理一定数量的数据后再执行淘汰，减少频繁操作
            // 每1000条数据检查一次，减少检查频率
            if (activeData.size() % 1000 == 0) {
                evictOutdatedData();
                evictOverLimitData();
            }
        }
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
        // 设置重置标志为true，阻止新的数据添加
        isResetting.store(true, std::memory_order_release);
        
        // 短暂延迟，让所有正在执行addData操作的线程有机会检查isResetting标志
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 使用写锁，因为需要修改所有数据结构
        std::unique_lock<std::shared_mutex> lock(rw_mtx);
        storage.clear();
        activeData.clear();
        wordCounts.clear();
        currentTimeCursor = 0;
        
        // 重置操作完成，设置标志为false
        isResetting.store(false, std::memory_order_release);
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
    // 强制容量限制
    void evictOverLimitData() {
        // 批量删除，减少迭代次数
        if (storage.size() > MAX_STORAGE_ENTRIES) {
            size_t excessStorage = storage.size() - MAX_STORAGE_ENTRIES;
            // 限制每次删除的数量，避免长时间占用锁
            size_t batchSize = std::min(excessStorage, BATCH_EVICTION_SIZE);
            auto it = storage.begin();
            
            for (size_t i = 0; i < batchSize && it != storage.end(); ++i) {
                // storage.begin() 是最老的时间戳
                // 注意：如果被删除的数据恰好在当前的 Active Window 内（窗口极大），
                // 理论上我们也应该从 activeData 和 wordCounts 里扣除。
                // 但这种情况极少见（意味着你的窗口比内存上限还大），
                // 为了防止系统崩溃，我们优先保命（删除 storage），数据一致性做次要处理。
                
                // 简单处理：仅从物理存储删除，防止 storage 无限膨胀
                // 如果用户此时把窗口拉大到覆盖这些被删除的数据，会发现数据缺失（这是预期的牺牲）
                it = storage.erase(it);
            }

            // 设置容量超限标志
            ctx.capacityLimitEvictionOccurred = true; 
        }
        
        // 同时也检查一下 activeData (逻辑队列)，防止极端情况下队列过长
        // 比如有人恶意把 windowSize 设为 10年
        if (activeData.size() > MAX_ACTIVE_ENTRIES) {
            size_t excessActive = activeData.size() - MAX_ACTIVE_ENTRIES;
            // 限制每次删除的数量，避免长时间占用锁
            size_t batchSize = std::min(excessActive, BATCH_EVICTION_SIZE);
            
            for (size_t i = 0; i < batchSize && !activeData.empty(); ++i) {
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
    }

    void evictOutdatedData() {
        long long currentTime = currentTimeCursor;
        long long windowSize = windowSizeSeconds;
        long long threshold = currentTime - windowSize;

        // 1. 逻辑淘汰：仅从 activeData 和 wordCounts 中移除
        size_t evictedCount = 0;
        
        // 批量处理过期数据，减少循环次数
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
            evictedCount++;
            
            // 限制每次处理的数量，避免长时间占用锁
            if (evictedCount >= BATCH_EVICTION_SIZE) {
                break;
            }
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
        size_t evictedCount = 0;
        
        // 批量处理过期数据
        while (it != storage.end() && it->first <= physicalThreshold) {
            it = storage.erase(it); // 返回下一个迭代器
            evicted = true; // 标记发生了驱逐
            evictedCount++;
            
            // 限制每次处理的数量，避免长时间占用锁
            if (evictedCount >= BATCH_EVICTION_SIZE) {
                break;
            }
        }

        // 如果发生了驱逐，设置时间超限标志
        if (evicted) {
            ctx.timeLimitEvictionOccurred = true;
        }
    }
};
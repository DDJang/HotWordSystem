#pragma once

#include <map> // 核心改变：使用 map 代替 deque 以处理乱序
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <mutex> // 【新增】引入锁

using namespace std;

class SlidingWindow {
private:
    long long windowSizeSeconds;
    long long currentTimeCursor = 0;

    // 核心结构升级：使用 map (有序红黑树) 存储历史
    // Key: 时间戳, Value: 该秒内的所有词
    // 优势：自动处理乱序数据。比如先来 100秒的数据，再来 90秒的数据，map会自动排序。
    map<long long, vector<string>> history;
    
    // 实时词频统计
    unordered_map<string, int> wordCounts;

    mutable std::mutex mtx; // 【新增】互斥锁

public:
    SlidingWindow(long long windowSize = 600) : windowSizeSeconds(windowSize) {}

    // --- 功能 1 & 2: 数据接入与迟到/乱序处理 ---
    void addData(long long timestamp, const vector<string>& words) {
        std::lock_guard<std::mutex> lock(mtx); // 【新增】自动加锁

        if (words.empty()) return;

        // 1. 更新系统最大时间（保持时间向前流动）
        if (timestamp > currentTimeCursor) {
            currentTimeCursor = timestamp;
        }

        // 2. 迟到数据判定：如果数据的时间 比 (当前时间 - 窗口大小) 还要早，说明太旧了，直接丢弃
        long long threshold = currentTimeCursor - windowSizeSeconds;
        if (timestamp < threshold) {
            // [可选] 记录日志：cout << "Dropped late data: " << timestamp << endl;
            return;
        }

        // 3. 存入历史 (Map 自动排序，解决了乱序插入问题)
        // 注意：同一秒可能有多条消息，所以用 insert 拼接
        history[timestamp].insert(history[timestamp].end(), words.begin(), words.end());

        // 4. 更新总词频
        for (const string& w : words) {
            wordCounts[w]++;
        }

        // 5. 触发淘汰
        evictOutdatedData();
    }

    // --- 功能 5: 动态调整窗口大小 ---
    void setWindowSize(long long newSizeSeconds) {
        std::lock_guard<std::mutex> lock(mtx); // 【新增】加锁

        if (newSizeSeconds <= 0) return;
        cout << "[System] Window size changed from " << windowSizeSeconds 
             << "s to " << newSizeSeconds << "s" << endl;
        windowSizeSeconds = newSizeSeconds;
        
        // 调整后立即执行一次淘汰，因为窗口可能变小了
        evictOutdatedData();
    }

    // --- 功能 3: 趋势分析 (新功能) ---
    // 算法：对比 窗口后半段(最近) vs 窗口前半段(较远) 的词频
    // 返回：(词, 增长斜率)
    vector<pair<string, double>> getTrendingWords(int topK) {
        std::lock_guard<std::mutex> lock(mtx); // 【新增】加锁

        if (history.empty()) return {};

        // 定义分界线：中点
        long long midPoint = currentTimeCursor - (windowSizeSeconds / 2);
        long long startPoint = currentTimeCursor - windowSizeSeconds;

        unordered_map<string, int> recentCounts; // 后半段 (新兴)
        unordered_map<string, int> oldCounts;    // 前半段 (基准)

        // 遍历历史 map 进行分段统计
        // 这是一个 O(N) 操作，N是窗口内的秒数。
        for (auto it = history.lower_bound(startPoint); it != history.end(); ++it) {
            long long ts = it->first;
            const auto& words = it->second;

            if (ts >= midPoint) {
                for (const auto& w : words) recentCounts[w]++;
            } else {
                for (const auto& w : words) oldCounts[w]++;
            }
        }

        // 计算增长率
        // 简单公式：Trend = Recent - Old。 (正数表示增长，负数表示衰退)
        // 也可以用 (Recent - Old) / Old 计算百分比，但要处理分母为0。这里用差值简单表示热度变化。
        vector<pair<string, double>> trends;
        for (const auto& kv : wordCounts) {
            const string& word = kv.first;
            int r = recentCounts[word]; // 默认为0
            int o = oldCounts[word];    // 默认为0
            
            // 只有总词频较高时才分析趋势，过滤噪音
            if (kv.second < 2) continue; 

            double score = (double)r - (double)o; 
            if (score != 0) {
                trends.push_back({word, score});
            }
        }

        // 排序：按绝对值或者仅按增长排序？这里按“增长最快”排序
        auto cmp = [](const pair<string, double>& a, const pair<string, double>& b) {
            return a.second > b.second; 
        };
        sort(trends.begin(), trends.end(), cmp);

        if (trends.size() > topK) {
            trends.resize(topK);
        }
        return trends;
    }

    // 获取 Top-K (保持不变)
    vector<pair<string, int>> getTopK(int k) {
        std::lock_guard<std::mutex> lock(mtx); // 【新增】加锁

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

     // 【新增】清空所有数据
    void reset() {
        std::lock_guard<std::mutex> lock(mtx);
        history.clear();      // 清空历史 Map
        wordCounts.clear();   // 清空词频统计
        currentTimeCursor = 0; // 重置时间游标
        // windowSizeSeconds 保持不变，或者也可以重置为默认值
    }

private:
    void evictOutdatedData() {
        long long threshold = currentTimeCursor - windowSizeSeconds;

        // map 是有序的，begin() 就是最早的数据
        // 循环检查头部是否过期
        while (!history.empty()) {
            auto it = history.begin();
            if (it->first <= threshold) {
                // 过期了，扣减词频
                for (const string& w : it->second) {
                    wordCounts[w]--;
                    if (wordCounts[w] <= 0) {
                        wordCounts.erase(w);
                    }
                }
                // 从 map 中移除
                history.erase(it);
            } else {
                // 因为是有序的，如果头部没过期，后面的肯定也没过期，直接退出
                break;
            }
        }
    }
};
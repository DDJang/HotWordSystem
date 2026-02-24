#pragma once

#include <memory>
#include <fstream>
#include <atomic>
#include "TextProcessor.h"
#include "PersistenceManager.h"
#include "PerformanceMonitor.h"
#include "ThreadPool.h"

// 对于 SlidingWindow，只使用前向声明来打破循环依赖
class SlidingWindow;

class SystemContext {
public:
    std::unique_ptr<ThreadPool> threadPool;
    std::unique_ptr<TextProcessor> processor;
    std::unique_ptr<SlidingWindow> window;
    std::unique_ptr<PersistenceManager> persistence;
    std::unique_ptr<PerformanceMonitor> monitor;
    
    // 全局时间戳 (原子操作或仅在单线程写入，多线程读取)
    std::atomic<long long> lastTimestamp{0};

    // 压测终止信号 flag
    // true 表示需要立即停止，false 表示正常运行
    std::atomic<bool> abortBenchmark{false}; 
    std::atomic<bool> isBenchmarkRunning{false}; // 压测状态标记

    // 退出标志位
    std::atomic<bool> shouldExit{false}; 

    // 驱逐事件标志位
    // 当因达到容量上限而强制驱逐时，此标志为 true
    std::atomic<bool> capacityLimitEvictionOccurred{false};
    // 当因达到时间上限而常规驱逐时，此标志为 true
    std::atomic<bool> timeLimitEvictionOccurred{false};

    // 一个全局互斥锁，用于保护对 ctx 成员的复杂访问
    std::mutex global_mutex; 

    // 构造函数和析构函数只保留声明
    SystemContext();
    ~SystemContext(); // 对于 unique_ptr 指向不完整类型，析构函数必须在实现文件中定义
};
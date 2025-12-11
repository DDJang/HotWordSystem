#pragma once
#include <memory>
#include <fstream>
#include <atomic>
#include "TextProcessor.h"
#include "SlidingWindow.h"
#include "PersistenceManager.h"
#include "PerformanceMonitor.h"

class SystemContext {
public:
    std::unique_ptr<TextProcessor> processor;
    std::unique_ptr<SlidingWindow> window;
    std::unique_ptr<PersistenceManager> persistence;
    std::unique_ptr<PerformanceMonitor> monitor;
    
    // 全局时间戳 (原子操作或仅在单线程写入，多线程读取)
    std::atomic<long long> lastTimestamp{0};

    // 【新增】压测终止信号 flag
    // true 表示需要立即停止，false 表示正常运行
    std::atomic<bool> abortBenchmark{false}; 

    // 【新增】退出标志位
    std::atomic<bool> shouldExit{false}; 

    SystemContext() {
        // 路径配置
        const char* DICT_PATH = "dict/jieba.dict.utf8";
        const char* HMM_PATH = "dict/hmm_model.utf8";
        const char* USER_DICT_PATH = "dict/user.dict.utf8";
        const char* IDF_PATH = "dict/idf.utf8";
        const char* STOP_WORD_PATH = "dict/stop_words.utf8";
        const char* SENSITIVE_PATH = "dict/sensitive_words.txt";

        // 确保敏感词文件存在
        std::ofstream tmp(SENSITIVE_PATH, std::ios::app);
        tmp.close();

        // 初始化组件
        processor = std::make_unique<TextProcessor>(DICT_PATH, HMM_PATH, USER_DICT_PATH, IDF_PATH, STOP_WORD_PATH, SENSITIVE_PATH);
        window = std::make_unique<SlidingWindow>(600);
        persistence = std::make_unique<PersistenceManager>("data/history.log");
        monitor = std::make_unique<PerformanceMonitor>();
    }
};
#include "SystemContext.h"
#include "TextProcessor.h"
#include "SlidingWindow.h"
#include "PersistenceManager.h"
#include "PerformanceMonitor.h"
#include <fstream>
#include <filesystem>

// 这是 SystemContext 构造函数的唯一、权威的实现。
// 无论是主程序还是测试程序，都将链接到这个实现。
SystemContext::SystemContext() {
    threadPool = std::make_unique<ThreadPool>(14);

    // 路径配置
    const char* DICT_PATH = "dict/jieba.dict.utf8";
    const char* HMM_PATH = "dict/hmm_model.utf8";
    const char* USER_DICT_PATH = "dict/user.dict.utf8";
    const char* IDF_PATH = "dict/idf.utf8";
    const char* STOP_WORD_PATH = "dict/stop_words.utf8";
    const char* SENSITIVE_PATH = "dict/sensitive_words.txt";

    // 确保敏感词文件存在 (无论主程序还是测试都需要)
    if (std::filesystem::exists("dict/")) {
        std::ofstream tmp(SENSITIVE_PATH, std::ios::app);
        tmp.close();
    }

    // 初始化组件
    processor = std::make_unique<TextProcessor>(DICT_PATH, HMM_PATH, USER_DICT_PATH, IDF_PATH, STOP_WORD_PATH, SENSITIVE_PATH);
    window = std::make_unique<SlidingWindow>(600, *this);
    persistence = std::make_unique<PersistenceManager>(*threadPool, "data/history.log");
    monitor = std::make_unique<PerformanceMonitor>();
}
SystemContext::~SystemContext() = default;
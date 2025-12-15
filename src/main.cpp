#include <iostream>
#include <string>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

// 引入模块化头文件
#include "SystemContext.h"
#include "CommandExecutor.h"
#include "APIServer.h"

// 在这里包含 SlidingWindow.h，因为它在 SystemContext 的构造函数中需要
#include "SlidingWindow.h"

// SystemContext 构造函数和析构函数的实现
SystemContext::SystemContext() {
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
    window = std::make_unique<SlidingWindow>(600, *this);
    persistence = std::make_unique<PersistenceManager>("data/history.log");
    monitor = std::make_unique<PerformanceMonitor>();
}

SystemContext::~SystemContext() = default;

int main() {
    #ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    #endif

    // 1. 初始化系统上下文 (持有所有核心对象)
    SystemContext ctx;

    // 2. 初始化命令执行器 (处理业务逻辑)
    CommandExecutor executor(ctx);

    // 3. 启动 Web Server 线程
    std::thread webThread([&ctx, &executor](){
        // 传入 ctx 和 executor 供 Server 使用
        APIServer server(ctx, executor);
        server.start(8080);
    });
    webThread.detach();

    std::cout << "[Ready] System Online. Use Web UI or Console." << std::endl;

    // 4. 主线程 CLI 循环 (直接调用 executor)
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        // 直接委托给 CommandExecutor 处理
        std::cout << executor.process(line) << std::endl;
    }

    return 0;
}
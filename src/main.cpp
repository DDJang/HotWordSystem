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
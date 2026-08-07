#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

// 引入模块化头文件
#include "SystemContext.h"
#include "CommandExecutor.h"
#include "APIServer.h"
#include "SlidingWindow.h"

namespace {

#ifdef _WIN32
bool consoleLineAvailable(HANDLE inputHandle) {
    DWORD eventCount = 0;
    if (!GetNumberOfConsoleInputEvents(inputHandle, &eventCount) || eventCount == 0) {
        return false;
    }

    std::vector<INPUT_RECORD> records(eventCount);
    DWORD peeked = 0;
    if (!PeekConsoleInputW(inputHandle, records.data(), eventCount, &peeked)) {
        return false;
    }

    for (DWORD i = 0; i < peeked; ++i) {
        const INPUT_RECORD& record = records[i];
        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown &&
            (record.Event.KeyEvent.wVirtualKeyCode == VK_RETURN ||
             record.Event.KeyEvent.uChar.UnicodeChar == L'\r')) {
            return true;
        }
    }
    return false;
}
#endif

bool waitForConsoleLineOrExit(const std::atomic<bool>& shouldExit) {
    while (!shouldExit.load(std::memory_order_acquire)) {
#ifdef _WIN32
        HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
        if (inputHandle == nullptr || inputHandle == INVALID_HANDLE_VALUE) {
            return true;
        }

        DWORD consoleMode = 0;
        if (GetConsoleMode(inputHandle, &consoleMode)) {
            if (consoleLineAvailable(inputHandle)) return true;
            Sleep(50);
            continue;
        }

        // Redirected stdin may be a pipe whose wait handle is signaled even
        // when no complete line is available. Inspect the pipe first so an
        // HTTP SHUTDOWN cannot leave getline() blocked indefinitely.
        if (GetFileType(inputHandle) == FILE_TYPE_PIPE) {
            DWORD bytesAvailable = 0;
            if (!PeekNamedPipe(inputHandle, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
                return true; // closed pipe: let getline() observe EOF
            }
            if (bytesAvailable > 0) return true;
            Sleep(50);
            continue;
        }

        // Redirected file stdin: wait without blocking the main thread.
        const DWORD waitResult = WaitForSingleObject(inputHandle, 50);
        if (waitResult == WAIT_OBJECT_0) return true;
#else
        fd_set inputSet;
        FD_ZERO(&inputSet);
        FD_SET(STDIN_FILENO, &inputSet);
        timeval timeout{};
        timeout.tv_usec = 50 * 1000;
        const int result = select(STDIN_FILENO + 1, &inputSet, nullptr, nullptr, &timeout);
        if (result > 0 && FD_ISSET(STDIN_FILENO, &inputSet)) return true;
        if (result < 0) return true;
#endif
    }
    return false;
}

} // namespace

int main() {
    #ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    #endif

    // 1. 初始化系统上下文 (持有所有核心对象)
    SystemContext ctx;

    // 2. 初始化命令执行器 (处理业务逻辑)
    CommandExecutor executor(ctx);

    // 3. 启动 Web Server 线程。server 本身由 main 持有，确保线程结束前
    // 它引用的 ctx/executor 仍然有效。
    APIServer server(ctx, executor);
    std::thread webThread([&server](){
        server.start(8080);
    });

    std::cout << "[Ready] System Online. Use Web UI or Console." << std::endl;

    // 4. 主线程 CLI 循环 (直接调用 executor)
    std::string line;
    while (!ctx.shouldExit.load(std::memory_order_acquire) &&
           waitForConsoleLineOrExit(ctx.shouldExit) && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        // 直接委托给 CommandExecutor 处理
        std::cout << executor.process(line) << std::endl;
    }

    // EOF 或 SHUTDOWN 都走同一条正常关闭路径，不使用 std::exit()。
    ctx.shouldExit.store(true, std::memory_order_release);
    server.stop();
    executor.shutdown();

    if (webThread.joinable()) {
        webThread.join();
    }

    return 0;
}

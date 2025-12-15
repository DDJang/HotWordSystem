#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>

// 引入所有核心头文件
#include "SystemContext.h"
#include "GlobalUtils.h"
#include "TextProcessor.h"
#include "SlidingWindow.h"
#include "PersistenceManager.h"
#include "CommandExecutor.h"

// --- 一个简单的测试运行器框架 ---
class TestRunner {
public:
    // 运行一个测试用例
    template<typename TestFunc>
    void run(const std::string& test_name, TestFunc func) {
        total_tests_++;
        current_test_failed_ = false;
        std::cout << "\n--- 正在运行测试: " << test_name << " ---" << std::endl;
        try {
            func();
            if (!current_test_failed_) {
                // 只有在当前测试没有任何断言失败时才打印总的PASS
                std::cout << "✅ 通过: " << test_name << std::endl;
            }
        } catch (const std::exception& e) {
            fail_test("抛出异常: " + std::string(e.what()));
        } catch (...) {
            fail_test("抛出一个未知异常。");
        }
    }

    // 打印测试总结
    void summary() const {
        std::cout << "\n==================================" << std::endl;
        if (failed_tests_ == 0) {
            std::cout << "🎉 所有 " << total_tests_ << " 个测试全部通过！ 🎉" << std::endl;
        } else {
            std::cout << "❌ 测试总结: " << total_tests_ << " 个测试中有 " << failed_tests_ << " 个失败。" << std::endl;
        }
        std::cout << "==================================" << std::endl;
    }
    
    // 获取失败测试的数量，用于 main 函数返回值
    int get_fail_count() const { return failed_tests_; }

    // 断言方法
    template<typename T1, typename T2>
    void assert_eq(const T1& val1, const T2& val2, const std::string& message, const std::string& file, int line) {
        if (val1 != val2) {
            std::stringstream ss;
            ss << "期望值: " << val2 << ", 实际值: " << val1;
            fail_test(message + " | " + ss.str(), file, line);
        } else {
            std::cout << "  - ✅ 断言通过: " << message << std::endl;
        }
    }

    void assert_true(bool condition, const std::string& message, const std::string& file, int line) {
        if (!condition) {
            fail_test(message + " | 期望为 TRUE，实际为 FALSE。", file, line);
        } else {
            std::cout << "  - ✅ 断言通过: " << message << std::endl;
        }
    }

private:
    void fail_test(const std::string& message, const std::string& file = "", int line = 0) {
        if (!current_test_failed_) {
            failed_tests_++;
            current_test_failed_ = true; // 标记当前测试已失败
        }
        std::cerr << "  - ❌ 断言失败";
        if (!file.empty()) {
            // 只显示文件名，而不是完整路径
            std::cerr << " (" << std::filesystem::path(file).filename().string() << ":" << line << ")";
        }
        std::cerr << ": " << message << std::endl;
    }

    int total_tests_ = 0;
    std::atomic<int> failed_tests_ = 0;
    bool current_test_failed_ = false;
};

// 全局测试运行器实例
TestRunner runner;

// 增强后的断言宏，不会中途退出
#define ASSERT_EQ(val1, val2, msg) runner.assert_eq(val1, val2, msg, __FILE__, __LINE__)
#define ASSERT_TRUE(condition, msg) runner.assert_true(condition, msg, __FILE__, __LINE__)

// ==========================
//      测试用例开始
// ==========================

void test_GlobalUtils() {
    ASSERT_EQ(GlobalUtils::parseTimeSeconds("00:00:10"), (long long)10, "解析秒");
    ASSERT_EQ(GlobalUtils::parseTimeSeconds("01:00:00"), (long long)3600, "解析小时");
    ASSERT_EQ(GlobalUtils::parseTimeSeconds("25:00:00"), (long long)90000, "解析超过24小时");
    ASSERT_EQ(GlobalUtils::formatTime(3661), std::string("01:01:01"), "格式化时间");
    ASSERT_EQ(GlobalUtils::formatTime(86400 + 3601), std::string("[Day 2] 01:00:01"), "格式化跨天时间");
    ASSERT_EQ(GlobalUtils::safeStoi("123", 0, 200, 0), 123, "安全转换整数");
    ASSERT_EQ(GlobalUtils::safeStoi("abc", 0, 200, 5), 5, "安全转换 - 异常默认值");
    ASSERT_EQ(GlobalUtils::safeStoi("300", 0, 200, 0), 200, "安全转换 - 上限截断");
    ASSERT_EQ(GlobalUtils::safeStoi("-5", 0, 200, 0), 0, "安全转换 - 下限截断");
    ASSERT_EQ(GlobalUtils::safeStoi("123a", 0, 200, 99), 99, "安全转换 - 无效格式");
}

void test_TextProcessor() {
    if (!std::filesystem::exists("dict/jieba.dict.utf8")) {
        std::cerr << "   ⚠️ 跳过测试: 字典文件未找到。" << std::endl;
        return;
    }
    SystemContext ctx;
    auto& processor = *ctx.processor;

    auto res = processor.process("华为发布了新手机");
    ASSERT_EQ(res.size(), (size_t)3, "基础分词");
    
    processor.setPosConfig({"v"}, false); // 只允许动词
    res = processor.process("华为发布手机");
    ASSERT_EQ(res.size(), (size_t)1, "词性过滤 - 只保留动词");
    if (!res.empty()) ASSERT_EQ(res[0], std::string("发布"), "词性过滤 - 结果正确");

    processor.setPosConfig({"n", "v"}, true); // 允许名词和动词
    processor.addSensitiveWord("手机");
    processor.setSensitiveConfig(false); // 启用过滤 (allow_sensitive=false)
    res = processor.process("华为发布手机");
    ASSERT_EQ(res.size(), (size_t)2, "敏感词过滤 - 启用");

    processor.setSensitiveConfig(true); // 禁用过滤 (allow_sensitive=true)
    res = processor.process("华为发布手机");
    ASSERT_EQ(res.size(), (size_t)3, "敏感词过滤 - 禁用");
    
    processor.removeSensitiveWord("手机");
    processor.setSensitiveConfig(false); // 重新启用过滤
    res = processor.process("华为发布手机");
    ASSERT_EQ(res.size(), (size_t)3, "移除敏感词后应不再被过滤");
}

void test_SlidingWindow() {
    SystemContext ctx;
    SlidingWindow window(10, ctx); // 10秒窗口

    window.addData(1, {"苹果"});
    window.addData(5, {"香蕉"});
    window.addData(8, {"苹果"});
    
    auto top = window.getTopK(5);
    ASSERT_EQ(top.size(), (size_t)2, "添加数据后，独立词汇数为2");
    ASSERT_EQ(top[0].first, std::string("苹果"), "Top-1 应该是'苹果'");
    ASSERT_EQ(top[0].second, 2, "'苹果'的词频应该是2");

    // T=12, 窗口滑动到 [2, 12]. T=1 的苹果应该被淘汰
    window.addData(12, {"橘子"});
    
    top = window.getTopK(5);
    bool found_apple = false;
    for(const auto& p : top) {
        if (p.first == "苹果") {
            found_apple = true;
            ASSERT_EQ(p.second, 1, "窗口滑动后，'苹果'词频应变为1");
        }
    }
    ASSERT_TRUE(found_apple, "窗口滑动后，'苹果'仍应存在");

    // 动态扩大窗口到20秒, 窗口变为 [-8, 12]
    window.setWindowSize(20); 
    top = window.getTopK(5);
    found_apple = false;
    for(const auto& p : top) {
        if (p.first == "苹果") {
            found_apple = true;
            ASSERT_EQ(p.second, 2, "扩大窗口后，被淘汰的'苹果'应从物理存储中恢复");
        }
    }
    ASSERT_TRUE(found_apple, "扩大窗口后，'苹果'应存在");
}

void test_PersistenceManager() {
    std::string testLog = "test_data/pm_test.log";
    if (std::filesystem::exists(testLog)) std::filesystem::remove(testLog);
    
    PersistenceManager pm(testLog);
    
    pm.logData(100, {"你好", "世界"});
    pm.logData(101, {"你好", "C++"});
    pm.logData(300, {"忽略"}); // 在查询范围之外

    auto res = pm.queryHistoryTopK(0, 200, 5);
    ASSERT_EQ(res.size(), (size_t)3, "查询TopK应返回3个独立词");
    ASSERT_EQ(res[0].first, std::string("你好"), "历史查询Top-1应为'你好'");
    ASSERT_EQ(res[0].second, 2, "历史查询'你好'词频应为2");

    pm.clearLog();
    res = pm.queryHistoryTopK(0, 200, 5);
    ASSERT_EQ(res.size(), (size_t)0, "清空日志后查询结果应为空");

    if (std::filesystem::exists(testLog)) std::filesystem::remove(testLog);
}

// 集成测试，验证模块协同工作
void test_CommandExecutor_Integration() {
    SystemContext ctx;
    CommandExecutor executor(ctx);
    
    ctx.persistence->clearLog(); // 确保从干净的状态开始

    // 1. 测试数据注入
    executor.process("[00:00:01] 华为 手机 华为");
    
    auto top = ctx.window->getTopK(5);
    ASSERT_EQ(top[0].first, std::string("华为"), "集成测试 - Top-1应为'华为'");
    ASSERT_EQ(top[0].second, 2, "集成测试 - '华为'词频为2");
    ASSERT_EQ(ctx.lastTimestamp.load(), (long long)1, "集成测试 - 时间戳应更新");

    // 2. 测试指令执行
    executor.process("[ACTION] SET_WINDOW S=1");
    executor.process("[00:00:03] 小米"); // 这会将窗口滑动到 [2, 3]

    top = ctx.window->getTopK(5);
    ASSERT_EQ(top.size(), (size_t)1, "集成测试 - 窗口缩小后，旧数据应被淘汰");
    ASSERT_EQ(top[0].first, std::string("小米"), "集成测试 - 窗口中只应有'小米'");

    // 3. 测试RESET指令
    executor.process("[ACTION] RESET");

    top = ctx.window->getTopK(5);
    ASSERT_EQ(top.size(), (size_t)0, "集成测试 - RESET后窗口应为空");
    ASSERT_EQ(ctx.lastTimestamp.load(), (long long)0, "集成测试 - RESET后时间戳应为0");
    auto history = ctx.persistence->queryHistoryTopK(0, 100, 5);
    ASSERT_EQ(history.size(), (size_t)0, "集成测试 - RESET后历史日志应为空");
}

int main() {
    #ifdef _WIN32
    // 设置控制台为UTF-8编码，以正确显示中文字符
    system("chcp 65001 > nul");
    #endif

    std::cout << "==================================" << std::endl;
    std::cout << "      运行 HotWordSystem 测试     " << std::endl;
    std::cout << "==================================" << std::endl;

    runner.run("GlobalUtils (通用工具)", test_GlobalUtils);
    runner.run("TextProcessor (文本处理器)", test_TextProcessor);
    runner.run("SlidingWindow (滑动窗口)", test_SlidingWindow);
    runner.run("PersistenceManager (持久化管理器)", test_PersistenceManager);
    runner.run("CommandExecutor (集成测试)", test_CommandExecutor_Integration);
    
    runner.summary();

    // 如果有测试失败，返回1，否则返回0
    return runner.get_fail_count() == 0 ? 0 : 1;
}
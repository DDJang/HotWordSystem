#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <filesystem>

// 引入所有核心头文件
#include "GlobalUtils.h"
#include "TextProcessor.h"
#include "SlidingWindow.h"
#include "PersistenceManager.h"

// --- 简易测试框架宏 ---
#define ASSERT_EQ(val1, val2) \
    if ((val1) != (val2)) { \
        std::cerr << "❌ FAIL: " << __FUNCTION__ << " line " << __LINE__ \
                  << " | Expected: " << (val2) << ", Got: " << (val1) << std::endl; \
        exit(1); \
    } else { \
        std::cout << "✅ PASS: " << __FUNCTION__ << std::endl; \
    }

#define ASSERT_TRUE(condition) \
    if (!(condition)) { \
        std::cerr << "❌ FAIL: " << __FUNCTION__ << " line " << __LINE__ << " | Expected TRUE" << std::endl; \
        exit(1); \
    } else { \
        std::cout << "✅ PASS: " << __FUNCTION__ << std::endl; \
    }

// ----------------------
// 1. 测试工具类
// ----------------------
void test_GlobalUtils() {
    std::cout << "\n--- Testing GlobalUtils ---" << std::endl;
    
    // 测试时间解析
    ASSERT_EQ(GlobalUtils::parseTimeSeconds("00:00:10"), 10);
    ASSERT_EQ(GlobalUtils::parseTimeSeconds("01:00:00"), 3600);
    
    // 测试时间格式化
    ASSERT_EQ(GlobalUtils::formatTime(3661), std::string("01:01:01"));
    
    // 测试安全转换
    ASSERT_EQ(GlobalUtils::safeStoi("123", 0, 200, 0), 123);
    ASSERT_EQ(GlobalUtils::safeStoi("abc", 0, 200, 5), 5); // 异常默认值
    ASSERT_EQ(GlobalUtils::safeStoi("300", 0, 200, 0), 200); // 上限截断
    ASSERT_EQ(GlobalUtils::safeStoi("-5", 0, 200, 0), 0);    // 下限截断
}

// ----------------------
// 2. 测试文本处理 (分词、过滤)
// ----------------------
void test_TextProcessor() {
    std::cout << "\n--- Testing TextProcessor ---" << std::endl;

    // 假设运行目录在 build/ 下，字典会被复制到 dict/
    const std::string DICT_DIR = "dict/"; 
    
    // 检查字典是否存在 (CMake 应自动复制)
    if (!std::filesystem::exists(DICT_DIR + "jieba.dict.utf8")) {
        std::cerr << "⚠️ Warning: Dict files not found in " << DICT_DIR << ". Skipping TextProcessor tests." << std::endl;
        return;
    }

    TextProcessor processor(
        DICT_DIR + "jieba.dict.utf8",
        DICT_DIR + "hmm_model.utf8",
        DICT_DIR + "user.dict.utf8",
        DICT_DIR + "idf.utf8",
        DICT_DIR + "stop_words.utf8"
    );

    // 2.1 基础分词测试
    std::vector<std::string> res = processor.process("华为发布了新手机");
    // 预期: 华为(n), 发布(v), 手机(n)。 "了"是停用词或词性被滤，"新"是形
    // 默认配置下允许: n, v, a, eng 等
    bool foundHuawei = false;
    for(const auto& w : res) {
        if (w == "华为") foundHuawei = true;
    }
    ASSERT_TRUE(foundHuawei);

    // 2.2 测试词性过滤配置
    // 只允许动词
    processor.setPosConfig({"v"}, false); 
    res = processor.process("华为发布手机");
    ASSERT_EQ(res.size(), 1); // 应该只剩下 "发布"
    ASSERT_EQ(res[0], "发布");

    // 恢复默认
    processor.setPosConfig({"n", "v"}, false);

    // 2.3 测试敏感词
    processor.addSensitiveWord("手机");
    // 默认不允许敏感词
    res = processor.process("华为发布手机");
    // "手机" 应该被过滤掉
    bool foundPhone = false;
    for(const auto& w : res) if(w == "手机") foundPhone = true;
    ASSERT_TRUE(!foundPhone);

    // 开启“不过滤敏感词”
    processor.setSensitiveConfig(true);
    res = processor.process("华为发布手机");
    for(const auto& w : res) if(w == "手机") foundPhone = true;
    ASSERT_TRUE(foundPhone);
}

// ----------------------
// 3. 测试滑动窗口 (核心逻辑)
// ----------------------
void test_SlidingWindow() {
    std::cout << "\n--- Testing SlidingWindow ---" << std::endl;
    
    SlidingWindow window(10); // 窗口大小 10秒

    // T=1: apple
    window.addData(1, {"apple"});
    // T=5: banana
    window.addData(5, {"banana"});
    // T=12: apple (此时 T=1 的 apple 应该滑出窗口: 12-10 = 2，T=1 < 2，过期)
    window.addData(12, {"apple"});

    // 检查 Top-K
    auto top = window.getTopK(10);
    // 预期: apple=1 (新的), banana=1 (T=5 在 [2, 12] 内)
    // T=1 的 apple 应该没了
    
    int appleCount = 0;
    int bananaCount = 0;
    for(auto& p : top) {
        if(p.first == "apple") appleCount = p.second;
        if(p.first == "banana") bananaCount = p.second;
    }

    ASSERT_EQ(appleCount, 1);
    ASSERT_EQ(bananaCount, 1);

    // 动态调整窗口大小
    window.setWindowSize(20); // 扩大到 20秒
    // 此时 T=1 的数据应该被“捞回” (假设 Storage 还没过期)
    // 现在的 Range: [0, 12]
    top = window.getTopK(10);
    for(auto& p : top) {
        if(p.first == "apple") appleCount = p.second;
    }
    // apple 应该变成 2 (T=1 和 T=12)
    ASSERT_EQ(appleCount, 2);
}

// ----------------------
// 4. 测试持久化
// ----------------------
void test_Persistence() {
    std::cout << "\n--- Testing PersistenceManager ---" << std::endl;
    
    std::string testLog = "test_data/history_test.log";
    // 清理旧文件
    if (std::filesystem::exists(testLog)) std::filesystem::remove(testLog);

    PersistenceManager pm(testLog);
    
    // 写入数据
    pm.logData(100, {"hello", "world"});
    pm.logData(101, {"hello", "cpp"});
    
    // 查询 Top-K
    auto res = pm.queryHistoryTopK(0, 200, 5);
    // 预期: hello=2, world=1, cpp=1
    int helloCount = 0;
    for(auto& p : res) {
        if(p.first == "hello") helloCount = p.second;
    }
    ASSERT_EQ(helloCount, 2);

    // 清空日志
    pm.clearLog();
    res = pm.queryHistoryTopK(0, 200, 5);
    ASSERT_EQ(res.size(), 0);
}

int main() {
    #ifdef _WIN32
    system("chcp 65001");
    #endif

    std::cout << "==================================" << std::endl;
    std::cout << "   Running HotWordSystem Tests    " << std::endl;
    std::cout << "==================================" << std::endl;

    test_GlobalUtils();
    test_TextProcessor();
    test_SlidingWindow();
    test_Persistence();

    std::cout << "\n🎉 ALL TESTS PASSED SUCCESSFULLY! 🎉" << std::endl;
    return 0;
}
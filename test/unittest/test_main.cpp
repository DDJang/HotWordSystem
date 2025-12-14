#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <filesystem>
#include <fstream>

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

// ----------------------
// 5. 测试基于文件的数据处理 (Data-Driven)
// ----------------------
void test_FileProcessing() {
    std::cout << "\n--- Testing File Processing (testdata/sample_text.txt) ---" << std::endl;

    std::string filePath = "testdata/sample_text.txt";
    std::ifstream file(filePath);
    
    // 检查文件是否存在
    if (!file.is_open()) {
        std::cerr << "⚠️ Warning: Could not open " << filePath << ". Skipping test." << std::endl;
        return;
    }

    // 初始化组件
    // 注意：这里假设字典还在 dict/ 下，如果在 test 环境路径不同需调整
    TextProcessor processor("dict/jieba.dict.utf8", "dict/hmm_model.utf8", "dict/user.dict.utf8", "dict/idf.utf8", "dict/stop_words.utf8");
    SlidingWindow window(600);

    std::string line;
    int lineCount = 0;
    long long ts = 1000; // 模拟时间戳

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // 处理每一行
        std::vector<std::string> words = processor.process(line);
        if (!words.empty()) {
            window.addData(ts++, words); // 时间戳递增
            lineCount++;
        }
    }

    // 验证逻辑
    ASSERT_TRUE(lineCount > 0); // 确保读到了数据

    // 检查是否统计到了文件里的关键词
    auto top = window.getTopK(100);
    bool foundAI = false;
    for (const auto& p : top) {
        if (p.first == "AI" || p.first == "人工智能") foundAI = true;
    }
    ASSERT_TRUE(foundAI);
    
    std::cout << "✅ Processed " << lineCount << " lines from file successfully." << std::endl;
}

// ----------------------
// 6. 测试历史日志回放 (Log Replay)
// ----------------------
void test_LogReplay() {
    std::cout << "\n--- Testing Log Replay (testdata/mock_history.log) ---" << std::endl;

    std::string mockLogPath = "testdata/mock_history.log";
    if (!std::filesystem::exists(mockLogPath)) {
         std::cerr << "⚠️ Warning: " << mockLogPath << " not found." << std::endl;
         return;
    }

    // 使用 PersistenceManager 读取这个预制的文件
    // 注意：PersistenceManager 默认是追加模式，这里我们只读，所以没关系
    // 但为了不污染 mock 文件，建议拷贝一份或者确保只调用查询接口
    PersistenceManager pm(mockLogPath);

    // 查询我们在 mock 文件里写的时间段
    // 1700000000 ~ 1700000060
    auto result = pm.queryHistoryTopK(1700000000, 1700000060, 5);

    // 验证预期结果 (根据上面的 mock_history.log 内容)
    // 华为出现 2 次 (line 1, line 3)
    // 显卡出现 1 次
    // 苹果出现 1 次
    
    bool foundHuawei = false;
    int huaweiCount = 0;

    for (const auto& p : result) {
        std::cout << "   -> Found in Log: " << p.first << " : " << p.second << std::endl;
        if (p.first == "华为") {
            foundHuawei = true;
            huaweiCount = p.second;
        }
    }

    ASSERT_TRUE(foundHuawei);
    ASSERT_EQ(huaweiCount, 2);
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
    test_FileProcessing(); 
    test_LogReplay();

    std::cout << "\n🎉 ALL TESTS PASSED SUCCESSFULLY! 🎉" << std::endl;
    return 0;
}
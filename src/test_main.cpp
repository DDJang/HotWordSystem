#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <thread>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <map>

// -----------------------------------------------------------------
// 1. 引入所有需要测试的头文件 (确保路径正确)
// -----------------------------------------------------------------
#include "TextProcessor.h"
#include "SlidingWindow.h"
#include "PerformanceMonitor.h"
// PersistenceManager 的测试可选，如果需要则取消注释
// #include "PersistenceManager.h"

using namespace std;

// -----------------------------------------------------------------
// 2. 定义测试环境和辅助函数
// -----------------------------------------------------------------
// 辅助函数：创建一个临时的文本文件
void create_temp_file(const string& path, const vector<string>& lines) {
    ofstream ofs(path);
    for (const auto& line : lines) {
        ofs << line << endl;
    }
}

// 辅助函数：比较两个 vector 是否内容相同 (忽略顺序)
template<typename T>
void assert_vectors_equal_unordered(vector<T> v1, vector<T> v2, const string& message) {
    sort(v1.begin(), v1.end());
    sort(v2.begin(), v2.end());
    assert(v1 == v2 && message.c_str());
}

// -----------------------------------------------------------------
// 3. 编写测试用例
// -----------------------------------------------------------------

// --- 测试 TextProcessor 的清洗和分词功能 ---
void test_TextProcessor() {
    cout << "[RUNNING] TextProcessor Tests..." << endl;

    // --- 准备测试环境 ---
    // 创建临时的字典和配置文件
    create_temp_file("test_stop_words.txt", {"的", "了", "是"});
    create_temp_file("test_sensitive_words.txt", {"敏感词"});
    
    // 假设 cppjieba 的核心字典文件在指定的 "dict/" 目录下
    const char* DICT_PATH = "dict/jieba.dict.utf8";
    const char* HMM_PATH = "dict/hmm_model.utf8";
    const char* USER_DICT_PATH = "dict/user.dict.utf8";
    const char* IDF_PATH = "dict/idf.utf8";

    // 初始化 TextProcessor
    TextProcessor proc(DICT_PATH, HMM_PATH, USER_DICT_PATH, IDF_PATH, 
                       "test_stop_words.txt", "test_sensitive_words.txt");

    // --- 用例 1: 核心清洗功能 (康熙部首, 全角, 零宽空格) ---
    string dirty_str = "今天⼈⼯智能技术发展迅速，ＡＰＰＬＥ发布​新产品";
    string expected_clean_str = "今天人工智能技术发展迅速,APPLE发布新产品";
    // 注意：因为我们重写了 sanitizeAndNormalize，可以先拿到这个函数的引用来测试
    // 但为了保持接口的黑盒测试，我们直接测试 process 函数的最终结果
    
    vector<string> result1 = proc.process(dirty_str);
    assert_vectors_equal_unordered(result1, {"今天", "人工智能", "技术", "发展", "迅速", "APPLE", "发布", "新产品"}, "Test Case 1: Cleaning failed.");

    // --- 用例 2: 停用词和敏感词过滤 ---
    string filter_str = "这个敏感词真的很厉害";
    vector<string> result2 = proc.process(filter_str);
    assert_vectors_equal_unordered(result2, {"这个", "真的", "厉害"}, "Test Case 2: Filtering failed.");
    
    // --- 用例 3: 词性过滤和单字过滤 ---
    // "我" (代词r), "和" (连词c), "他" (代词r) 应该被词性过滤
    // "去" (动词v) 应该保留, "哈" (叹词e) 单字被过滤
    string pos_filter_str = "我和他都喜欢去哈";
    vector<string> result3 = proc.process(pos_filter_str);
    assert_vectors_equal_unordered(result3, {"喜欢"}, "Test Case 3: Part-of-speech filtering failed."); // 假设"都"是副词d,也被过滤
    
    cout << "[  PASSED ] TextProcessor Tests" << endl;
}


// --- 测试 SlidingWindow 的核心逻辑 ---
void test_SlidingWindow() {
    cout << "[RUNNING] SlidingWindow Tests..." << endl;

    SlidingWindow win(10); // 10秒窗口

    // --- 用例 1: 数据添加与 TopK ---
    win.addData(100, {"Apple", "Banana", "Apple"}); // Apple:2, Banana:1
    auto top1 = win.getTopK(2);
    assert(top1.size() == 2);
    assert(top1[0].first == "Apple" && top1[0].second == 2);
    assert(top1[1].first == "Banana" && top1[1].second == 1);

    // --- 用例 2: 数据过期 ---
    win.addData(111, {"Cherry"}); // 时间前进到111, 窗口[101, 111], 100的数据应过期
    auto top2 = win.getTopK(5);
    bool hasApple = false;
    for(const auto& p : top2) if(p.first == "Apple" || p.first == "Banana") hasApple = true;
    assert(!hasApple && "Test Case 2: Expired data was not evicted.");
    assert(top2.size() == 1 && top2[0].first == "Cherry");

    // --- 用例 3: 迟到数据接纳 ---
    win.addData(105, {"LateData"}); // 窗口[101, 111], 105在窗口内, 应被接纳
    auto top3 = win.getTopK(2);
    assert(top3.size() == 2);
    assert( (top3[0].first == "Cherry" && top3[1].first == "LateData") || (top3[0].first == "LateData" && top3[1].first == "Cherry") );

    // --- 用例 4: 迟到数据拒绝 ---
    win.addData(90, {"TooLateData"}); // 90不在窗口[101, 111]内, 应被拒绝
    auto top4 = win.getTopK(5);
    assert(top4.size() == 2 && "Test Case 4: Too late data was accepted.");

    cout << "[  PASSED ] SlidingWindow Tests" << endl;
}


// --- 测试 PerformanceMonitor 的计数与计算 ---
void test_PerformanceMonitor() {
    cout << "[RUNNING] PerformanceMonitor Tests..." << endl;
    
    PerformanceMonitor mon;
    
    // --- 用例 1: 记录数据 ---
    mon.record(10); // 10 words, 1 line
    mon.record(5);  // 5 words, 1 line
    
    // 模拟经过2秒
    this_thread::sleep_for(chrono::seconds(2)); 
    
    // **核心修正**: 移除不存在的 mon.stop() 调用
    // getStats() 会在调用时动态计算时间差
    auto stats = mon.getStats();
    
    assert(stats["total_lines"] == 2);
    assert(stats["total_words"] == 15);
    // 运行时间可能不精确为2s，我们检查它在一个合理的范围内
    assert(stats["runtime_sec"] >= 1.9 && stats["runtime_sec"] < 2.5);
    // QPS 应该是 2 lines / ~2 sec ≈ 1
    assert(stats["lines_per_sec"] >= 0.8 && stats["lines_per_sec"] < 1.2);
    
    // --- 用例 2: 重置 ---
    mon.reset();
    // 再次记录一些数据以确保重置后计时器也重新开始了
    mon.record(1);
    this_thread::sleep_for(chrono::milliseconds(10)); // 经过一个极短的时间
    stats = mon.getStats();
    assert(stats["total_lines"] == 1);
    assert(stats["total_words"] == 1);
    assert(stats["runtime_sec"] < 0.1); // 重置后时间应该很短

    cout << "[  PASSED ] PerformanceMonitor Tests" << endl;
}

// -----------------------------------------------------------------
// 4. 主函数，按顺序执行所有测试
// -----------------------------------------------------------------
int main() {
    cout << "=======================================" << endl;
    cout << "   Running Unit Tests for HotWordSystem" << endl;
    cout << "=======================================" << endl;

    try {
        test_TextProcessor();
        test_SlidingWindow();
        test_PerformanceMonitor();

        cout << "---------------------------------------" << endl;
        cout << "✅ ALL TESTS PASSED SUCCESSFULLY!" << endl;
        cout << "=======================================" << endl;
        
        // 清理临时文件
        remove("test_stop_words.txt");
        remove("test_sensitive_words.txt");

        return 0; // 成功
    } catch (const exception& e) {
        cerr << "---------------------------------------" << endl;
        cerr << "❌ A TEST FAILED WITH AN EXCEPTION: " << e.what() << endl;
        cerr << "=======================================" << endl;
        
        // 清理临时文件
        remove("test_stop_words.txt");
        remove("test_sensitive_words.txt");

        return 1; // 失败
    } catch (...) {
        cerr << "---------------------------------------" << endl;
        cerr << "❌ AN UNKNOWN EXCEPTION OCCURRED IN TESTS!" << endl;
        cerr << "=======================================" << endl;

        // 清理临时文件
        remove("test_stop_words.txt");
        remove("test_sensitive_words.txt");
        
        return 1; // 失败
    }
}
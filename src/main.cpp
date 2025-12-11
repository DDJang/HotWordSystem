#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <thread>
#include <fstream>
#include <sstream>
#include <mutex>
#include <direct.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "httplib.h"
#include "json.hpp"
#include "TextProcessor.h"
#include "SlidingWindow.h"
#include "PersistenceManager.h"
#include "PerformanceMonitor.h"

using namespace std;
using json = nlohmann::json;

// --- 全局组件 (为了简化代码，设为全局，实际工程建议用单例或传参) ---
TextProcessor* g_processor = nullptr;
SlidingWindow* g_window = nullptr;
PersistenceManager* g_persistence = nullptr;
PerformanceMonitor* g_monitor = nullptr;

long long g_lastTimestamp = 0; // 【新增需求5】记录最后一次的时间戳

// 正则预编译
regex timeRegex(R"(\[(\d{2}:\d{2}:\d{2})\]\s*(.*))");
regex benchRegex(R"(\[ACTION\]\s*BENCHMARK\s*N=(\d+))");
regex windowRegex(R"(\[ACTION\]\s*SET_WINDOW\s*S=(\d+))");
regex historyRegex(R"(\[ACTION\]\s*HISTORY\s*START=(\d{2}:\d{2}:\d{2})\s*END=(\d{2}:\d{2}:\d{2})\s*STEP=(\d+))");
regex resetRegex(R"(\[ACTION\]\s*RESET)"); 
regex trendRegex(R"(\[ACTION\]\s*TREND\s*K=(\d+))"); 
regex queryRegex(R"(\[ACTION\]\s*QUERY\s*K=(\d+))"); 
regex retentionRegex(R"(\[ACTION\]\s*SET_RETENTION\s*R=(\d+))");

// 辅助：解析时间
long long parseTimeSeconds(const string& timeStr) {
    int h, m, s;
    if (sscanf(timeStr.c_str(), "%d:%d:%d", &h, &m, &s) == 3) return h * 3600 + m * 60 + s;
    return 0;
}

// 辅助：秒转时间字符串 (seconds -> HH:MM:SS)
string formatTime(long long totalSeconds) {
    int h = (totalSeconds / 3600) % 24;
    int m = (totalSeconds / 60) % 60;
    int s = totalSeconds % 60;
    char buf[16];
    sprintf(buf, "%02d:%02d:%02d", h, m, s);
    return string(buf);
}

// 辅助：生成随机句子
string generateRandomSentence() {
    static vector<string> dictionary = {"华为", "小米", "苹果", "OpenAI", "显卡", "暴跌", "上涨", "发布会"};
    static vector<string> verbs = {"发布", "震惊", "颠覆", "宣布"};
    string s = "";
    int len = rand() % 5 + 3;
    for(int i=0; i<len; i++) {
        s += dictionary[rand() % dictionary.size()];
        if(i < len-1) s += verbs[rand() % verbs.size()];
    }
    return s;
}

// --- 核心业务逻辑封装 (供 CLI 和 Web 共同调用) ---
string processCommand(const string& line) {
    smatch match;
    stringstream result;

    // 1. 数据输入处理 (逻辑升级)
    if (regex_search(line, match, timeRegex)) {
        // Case A: 带有明确时间戳 [12:00:01] 华为...
        long long ts = parseTimeSeconds(match[1].str());
        string content = match[2].str();
        
        // 更新全局时钟
        if (ts > g_lastTimestamp) g_lastTimestamp = ts;

        vector<string> words = g_processor->process(content);
        g_window->addData(ts, words);
        g_persistence->logData(ts, words);
        g_monitor->record(words.size());
        result << "Data(T=" << ts << ") processed: " << words.size() << " words.";
    }
    // 【新增需求5】Case B: 没有时间戳，自动 +1
    else if (line.find("[ACTION]") == string::npos && !line.empty()) {
        g_lastTimestamp++; // 自动递增
        long long ts = g_lastTimestamp;
        
        vector<string> words = g_processor->process(line); // 这里的 line 就是内容
        g_window->addData(ts, words);
        g_persistence->logData(ts, words);
        g_monitor->record(words.size());
        
        result << "Data(Auto T=" << formatTime(ts) << ") processed: " << words.size() << " words.";
    }

    // 2. 设置窗口
    else if (regex_search(line, match, windowRegex)) {
        int s = stoi(match[1].str());
        g_window->setWindowSize(s);
        result << "Window size set to " << s << " seconds.";
    }

    // 3. 压测
    else if (regex_search(line, match, benchRegex)) {
        int n = stoi(match[1].str());
        thread([n](){
            // 压测基于当前最后时间继续
            long long startTs = g_lastTimestamp; 
            g_monitor->reset();
            
            for(int i=0; i<n; i++) {
                startTs++; 
                // ... process ...
                vector<string> words = g_processor->process(generateRandomSentence());
                g_window->addData(startTs, words);
                g_persistence->logData(startTs, words);
                g_monitor->record(words.size());
            }
            g_lastTimestamp = startTs; // 更新全局时间
            cout << "[System] Benchmark finished." << endl;
        }).detach();
        result << "Benchmark started (Append mode).";
    }

    // 4. 查看性能统计指令
    else if (line.find("[ACTION] STATS") != string::npos) {
        // 在 C++ 控制台打印详细表格
        g_monitor->printStats();
        
        // 同时也返回给网页一个简单的文本提示
        result << "Performance stats printed to Server Console.";
    }

    // 5. 历史报告
    else if (regex_search(line, match, historyRegex)) {
        long long startT = parseTimeSeconds(match[1].str());
        long long endT = parseTimeSeconds(match[2].str());
        int step = stoi(match[3].str());
        g_persistence->generateReport(startT, endT, step, "report_output.txt");
        result << "History report generated in report_output.txt";
    } 

    // 6. 清零重置
    else if (regex_search(line, match, resetRegex)) {
        // 1. 清空滑动窗口
        g_window->reset();
        
        // 2. 重置监控指标
        g_monitor->reset();
        
        // 3. 【新增】清空持久化日志 (磁盘 - 物理文件)
        g_persistence->clearLog();
        
        result << "System State Reset Successfully. (Window cleared, Time reset)";
    }

    // 7. 趋势分析
    else if (regex_search(line, match, trendRegex)) {
        int k = stoi(match[1].str());
        auto list = g_window->getTrendingWords(k); // 假设 SlidingWindow 有这个 public 函数
        
        if (list.empty()) {
            result << "No trend data available.";
        } else {
            result << "Trend Analysis (Top " << k << "): \n";
            for (auto& p : list) {
                // 格式： 显卡 [UP] 5.00
                result << p.first << " [" << (p.second > 0 ? "UP" : "DOWN") << "] " 
                       << fixed << setprecision(2) << p.second << "\n";
            }
        }
    }

    // 8. 文字版 Top-K 查询
    else if (regex_search(line, match, queryRegex)) {
        int k = stoi(match[1].str());

        // 1. 开始计时
        auto start = std::chrono::high_resolution_clock::now();

        auto list = g_window->getTopK(k);

        // 2. 结束计时
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        // 3. 输出延迟
        cout << "[Perf] Top-K Query took: " << duration << " us" << endl; // 在控制台显示
        
        if (list.empty()) {
            result << "Window is empty.";
        } else {
            result << "Top-" << k << " Hot Words (Latency: " << duration << " us):\n"; // 在网页 Log 显示
            for (auto& p : list) {
                result << "  " << p.first << " : " << p.second << "\n";
            }
        }
    }

    // 9.新增指令处理逻辑
    else if (regex_search(line, match, retentionRegex)) {
        long long r = stoll(match[1].str());
        g_window->setMaxRetention(r);
        result << "Max retention time set to " << r << " seconds.";
    }

    // 10. 其他未知指令
    else {
        result << "Unknown command or invalid format.";
    }
    return result.str();
}

// --- HTTP 服务器 ---
void startServer(int port) {
    httplib::Server svr;

    // 1. 静态页面
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        // 尝试两个位置：当前目录 OR 上级目录
        // build/dashboard.html (假如你拷贝了)
        // ../dashboard.html (源码根目录)
        string path = "dashboard.html";
        ifstream ifs(path);
        
        if (!ifs.is_open()) {
            // 如果当前目录没找到，试着去上一级找
            path = "../dashboard.html"; 
            ifs.open(path);
        }

        if(ifs.is_open()) {
            stringstream buffer;
            buffer << ifs.rdbuf();
            res.set_content(buffer.str(), "text/html");
        } else {
            // 还是找不到，报错提示当前路径
            char cwd[1024];
            _getcwd(cwd, sizeof(cwd)); // 需要 #include <direct.h>
            string err = "<h1>Error: dashboard.html not found!</h1><p>Current Dir: " + string(cwd) + "</p>";
            res.set_content(err, "text/html");
        }
    });

    // 2. GET 数据接口 (支持动态 K)
    svr.Get("/api/data", [&](const httplib::Request& req, httplib::Response& res) {
        // 默认显示 10 个，如果前端传了 k 就用前端的
        int k = 10;
        if (req.has_param("k")) {
            try {
                k = stoi(req.get_param_value("k"));
                if (k <= 0) k = 1;      // 最小显示1个
                if (k > 100) k = 100;   // 限制最大显示100个，防止页面卡顿
            } catch (...) {}
        }

        auto topList = g_window->getTopK(k);
        auto range = g_window->getWindowRange();
        long long retention = g_window->getMaxRetention(); 

        json j;
        vector<string> cats;
        vector<int> vals;
        for(auto& p : topList) {
            cats.push_back(p.first);
            vals.push_back(p.second);
        }
        j["categories"] = cats;
        j["values"] = vals;
        
        j["window_start"] = formatTime(range.first);
        j["window_end"] = formatTime(range.second);
        j["current_ts"] = formatTime(g_lastTimestamp);
        j["retention_sec"] = retention;

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.dump(), "application/json");
    });

    // 3. POST 命令接口 (用于 Web 端的按钮和输入框)
    svr.Post("/api/command", [&](const httplib::Request& req, httplib::Response& res) {
        // 前端发来的是 JSON: { "cmd": "[00:00:00] hello" }
        try {
            auto j = json::parse(req.body);
            string cmd = j["cmd"];
            
            cout << "[Web Command] " << cmd << endl; // 在控制台也打印一下
            
            string output = processCommand(cmd); // 调用核心逻辑
            
            json resp;
            resp["status"] = "ok";
            resp["message"] = output;
            res.set_content(resp.dump(), "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"status\":\"error\"}", "application/json");
        }
    });

    // 4. 【新增】获取性能统计数据的接口 (GET)
    svr.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
        // 调用 Monitor 获取数据 map
        auto stats = g_monitor->getStats();
        
        // 转为 JSON
        json j;
        j["runtime"] = stats["runtime_sec"];
        j["lines"] = stats["total_lines"];
        j["words"] = stats["total_words"];
        j["qps"] = stats["lines_per_sec"];
        j["wps"] = stats["words_per_sec"];
        j["memory"] = stats["memory_mb"];
        
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.dump(), "application/json");
    });

    // 5. 【需求2】配置接口 (GET 获取配置, POST 修改配置)
    svr.Get("/api/config", [&](const httplib::Request&, httplib::Response& res) {
        json j;
        j["sensitive_words"] = g_processor->getSensitiveWords();
        j["allow_all"] = g_processor->isAllowAll(); // 【新增】回传状态
        // 简单起见，这里就不回传 allowedTags 了，仅回传敏感词
        res.set_content(j.dump(), "application/json");
    });

    svr.Post("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            
            // 【修改】处理词性配置 (同时接收 tags 和 allow_all)
            // 只要传了 tags 或者 allow_all 任意一个，就更新配置
            if (j.contains("tags") || j.contains("allow_all")) {
                vector<string> tags;
                bool allowAll = false;

                // 尝试获取 tags
                if (j.contains("tags")) {
                    tags = j["tags"].get<vector<string>>();
                }
                
                // 尝试获取 allow_all (如果是单独发送的请求，需要保持 TextProcessor 内部状态吗？
                // 为了简单，建议前端每次都把完整状态发过来。这里我们假设前端会发 allow_all)
                if (j.contains("allow_all")) {
                    allowAll = j["allow_all"];
                } else {
                    // 如果没传，默认 false 或者需要去 query 现有状态。
                    // 简化策略：前端每次变动都必须传 allow_all
                    allowAll = g_processor->isAllowAll(); 
                }

                g_processor->setPosConfig(tags, allowAll);
            }

            // 添加敏感词
            if (j.contains("add_sensitive")) {
                string w = j["add_sensitive"];
                g_processor->addSensitiveWord(w);
            }

            // 删除敏感词
            if (j.contains("remove_sensitive")) {
                string w = j["remove_sensitive"];
                g_processor->removeSensitiveWord(w);
            }

            res.set_content("{\"status\":\"ok\"}", "application/json");
        } catch(...) { res.status = 400; }
    });

    // 6. 【需求3】历史数据查询
    svr.Get("/api/history_view", [&](const httplib::Request& req, httplib::Response& res) {
        string sStart = req.get_param_value("start");
        string sEnd = req.get_param_value("end");
        
        long long tStart = parseTimeSeconds(sStart);
        long long tEnd = parseTimeSeconds(sEnd);

        // 查询持久化层
        auto list = g_persistence->queryHistoryTopK(tStart, tEnd, 10);

        json j;
        vector<string> cats;
        vector<int> vals;
        for(auto& p : list) {
            cats.push_back(p.first);
            vals.push_back(p.second);
        }
        j["categories"] = cats;
        j["values"] = vals;
        res.set_content(j.dump(), "application/json");
    });

    // 7. 【需求4】趋势分析接口
    svr.Get("/api/trends", [&](const httplib::Request& req, httplib::Response& res) {
        int k = 10;
        if (req.has_param("k")) k = stoi(req.get_param_value("k"));

        auto trends = g_window->getTrendingWords(k);

        json list = json::array();
        for(auto& p : trends) {
            json item;
            item["word"] = p.first;
            item["score"] = p.second; // 斜率/增量
            list.push_back(item);
        }
        res.set_content(list.dump(), "application/json");
    });

    cout << "[Web] GUI Server running at http://localhost:" << port << endl;
    svr.listen("0.0.0.0", port);
}

// --- 主函数 ---
int main() {
    #ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    #endif

    // 路径配置
    const char* DICT_PATH = "dict/jieba.dict.utf8";
    const char* HMM_PATH = "dict/hmm_model.utf8";
    const char* USER_DICT_PATH = "dict/user.dict.utf8";
    const char* IDF_PATH = "dict/idf.utf8";
    const char* STOP_WORD_PATH = "dict/stop_words.utf8";
    const char* SENSITIVE_PATH = "dict/sensitive_words.txt";

    // 初始化全局对象
    { ofstream tmp(SENSITIVE_PATH, ios::app); }
    g_processor = new TextProcessor(DICT_PATH, HMM_PATH, USER_DICT_PATH, IDF_PATH, STOP_WORD_PATH, SENSITIVE_PATH);
    g_window = new SlidingWindow(600);
    g_persistence = new PersistenceManager("data/history.log");
    g_monitor = new PerformanceMonitor();

    // 启动 Web 线程
    thread webThread([](){
        startServer(8080);
    });
    webThread.detach();

    cout << "[Ready] System Online. Use Web UI or Console." << endl;

    // CLI 循环 (现在直接调用 processCommand)
    string line;
    while (getline(cin, line)) {
        if (line.empty()) continue;
        cout << processCommand(line) << endl;
    }

    return 0;
}
#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <fstream>
#include <sstream>
#include <chrono>
#include <mutex>
#include <map> 

#include "httplib.h"
#include "json.hpp"
#include "SystemContext.h"
#include "CommandExecutor.h"
#include "GlobalUtils.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#define _getcwd getcwd
#endif


using json = nlohmann::json;

class APIServer {
private:
    SystemContext& ctx;
    CommandExecutor& executor;
    httplib::Server svr;

    // 【新增】静态资源搜索路径：优先 web/ 目录，兼顾构建目录结构
    const std::vector<std::string> baseDirs = { "web/", "../web/", "./" };

public:
    APIServer(SystemContext& context, CommandExecutor& exec) : ctx(context), executor(exec) {
        setupRoutes();
    }

    void start(int port) {
        std::cout << "[Web] GUI Server running at http://localhost:" << port << std::endl;
        svr.listen("0.0.0.0", port);
    }

private:
    // 【新增】辅助：获取文件的 MIME 类型
    std::string getMimeType(const std::string& path) {
        if (path.find(".html") != std::string::npos) return "text/html";
        if (path.find(".css") != std::string::npos) return "text/css";
        if (path.find(".js") != std::string::npos) return "text/javascript";
        if (path.find(".json") != std::string::npos) return "application/json";
        if (path.find(".png") != std::string::npos) return "image/png";
        if (path.find(".jpg") != std::string::npos) return "image/jpeg";
        return "text/plain";
    }

    // 【新增】辅助：尝试从配置的目录列表中读取文件内容
    bool readFile(const std::string& filename, std::string& outContent) {
        for (const auto& dir : baseDirs) {
            std::string fullPath = dir + filename;
            std::ifstream ifs(fullPath, std::ios::binary); // 二进制读取更安全
            if (ifs.is_open()) {
                std::stringstream buffer;
                buffer << ifs.rdbuf();
                outContent = buffer.str();
                return true;
            }
        }
        return false;
    }

    void setupRoutes() {
        // 1. 【修改】通用静态文件处理 (HTML, CSS, JS)
        // 正则含义：匹配所有不以 /api/ 开头的路径
        svr.Get(R"(^/(?!api/).*)", [&](const httplib::Request& req, httplib::Response& res) {
            std::string path = req.path;
            
            // 默认首页
            if (path == "/") path = "dashboard.html";
            
            // 去掉开头的 / (例如 /style.css -> style.css)
            if (!path.empty() && path.front() == '/') path.erase(0, 1);

            std::string content;
            if (readFile(path, content)) {
                res.set_content(content, getMimeType(path));
            } else {
                // 文件未找到
                char cwd[1024]; 
                _getcwd(cwd, sizeof(cwd));
                std::string err = "<h1>404 Not Found</h1><p>File '" + path + "' not found in web/ folder.</p><p>Current Dir: " + std::string(cwd) + "</p>";
                res.status = 404;
                res.set_content(err, "text/html");
            }
        });

        // 2. GET 数据接口
        svr.Get("/api/data", [&](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(ctx.global_mutex);

            if (ctx.shouldExit) {
                json j;
                j["shutdown"] = true;
                res.set_content(j.dump(), "application/json");
                return;
            }

            int k = GlobalUtils::safeStoi(req.get_param_value("k"), 1, 100, 10);

            auto topList = ctx.window->getTopK(k);
            auto range = ctx.window->getWindowRange();
            
            // 【修复】使用 .load() 来原子性地读取 atomic 变量的值
            auto currentTs = ctx.lastTimestamp.load();
            
            auto retentionSec = ctx.window->getMaxRetention();

            json j;
            std::vector<std::string> cats;
            std::vector<int> vals;
            for(auto& p : topList) {
                cats.push_back(p.first);
                vals.push_back(p.second);
            }
            j["categories"] = cats;
            j["values"] = vals;
            j["window_start"] = GlobalUtils::formatTime(range.first);
            j["window_end"] = GlobalUtils::formatTime(range.second);
            j["current_ts"] = GlobalUtils::formatTime(currentTs);
            j["retention_sec"] = retentionSec;

            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(j.dump(), "application/json");
        });

        // 3. POST 命令接口
        svr.Post("/api/command", [&](const httplib::Request& req, httplib::Response& res) {
            try {
                auto j = json::parse(req.body);
                std::string cmd = j["cmd"];
                std::cout << "[Web Command] " << cmd << std::endl;
                
                std::string output = executor.process(cmd);
                
                json resp;
                resp["status"] = "ok";
                resp["message"] = output;

                // 【修复】使用 .load() 来原子性地读取。虽然隐式转换可能有效，但显式调用 .load() 更安全、更清晰。
                resp["timestamp"] = GlobalUtils::formatTime(ctx.lastTimestamp.load());

                res.set_content(resp.dump(), "application/json");

                if (ctx.shouldExit) {
                    std::thread([&](){
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        std::cout << "[System] Shutting down from web request..." << std::endl;
                        svr.stop();
                        std::exit(0); // 强制退出
                    }).detach();
                }
            } catch (...) {
                res.status = 400;
                res.set_content("{\"status\":\"error\"}", "application/json");
            }
        });

        // 4. STATS 接口
        svr.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
            decltype(ctx.monitor->getStats()) stats;
            {
                std::lock_guard<std::mutex> lock(ctx.global_mutex);
                stats = ctx.monitor->getStats();
            }

            json j;
            j["runtime"] = stats["runtime_sec"];
            j["lines"] = stats["total_lines"];
            j["words"] = stats["total_words"];
            j["qps"] = stats["lines_per_sec"];
            j["memory"] = stats["memory_mb"];
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(j.dump(), "application/json");
        });

        // 5. 配置接口 (GET)
        svr.Get("/api/config", [&](const httplib::Request&, httplib::Response& res) {
            json j;
            {
                std::lock_guard<std::mutex> lock(ctx.global_mutex);
                j["sensitive_words"] = ctx.processor->getSensitiveWords();
                j["allow_all"] = ctx.processor->isAllowAllPos();
                j["allow_sensitive"] = ctx.processor->isAllowAllSensitive();
                // 【新增】为了让前端能回显 tags，这里需要返回 tags
                // 请确保 TextProcessor 中有 getAllowedTags() 方法
                // 如果您还没加这个方法，前端的 tags 复选框刷新后会不显示选中状态
                j["tags"] = ctx.processor->getAllowedTags(); 
            }
            res.set_content(j.dump(), "application/json");
        });

        // 5. 配置接口 (POST)
        svr.Post("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
            try {
                auto j = json::parse(req.body);
                
                std::lock_guard<std::mutex> lock(ctx.global_mutex);
                
                if (j.contains("tags") || j.contains("allow_all")) {
                    std::vector<std::string> tags;
                    // 如果前端没传 allow_all，则获取当前状态
                    bool allowAll = j.contains("allow_all") ? j["allow_all"].get<bool>() : ctx.processor->isAllowAllPos();
                    
                    if (j.contains("tags")) {
                        tags = j["tags"].get<std::vector<std::string>>();
                    } else {
                        // 如果前端只传了 allow_all 没传 tags，保持原 tags
                        tags = ctx.processor->getAllowedTags();
                    }
                    ctx.processor->setPosConfig(tags, allowAll);
                }

                if (j.contains("allow_sensitive")) {
                    ctx.processor->setSensitiveConfig(j["allow_sensitive"]);
                }

                if (j.contains("add_sensitive")) ctx.processor->addSensitiveWord(j["add_sensitive"]);
                if (j.contains("remove_sensitive")) ctx.processor->removeSensitiveWord(j["remove_sensitive"]);

                json resp;
                resp["status"] = "ok";
                resp["message"] = "Config updated";

                // 【修复】同样，这里也使用 .load()
                resp["timestamp"] = GlobalUtils::formatTime(ctx.lastTimestamp.load());

                res.set_content(resp.dump(), "application/json");

            } catch(...) { res.status = 400; }
        });

        // 6. 历史查询
        svr.Get("/api/history_view", [&](const httplib::Request& req, httplib::Response& res) {
            long long tStart = GlobalUtils::parseTimeSeconds(req.get_param_value("start"));
            long long tEnd = GlobalUtils::parseTimeSeconds(req.get_param_value("end"));
            int k = GlobalUtils::safeStoi(req.get_param_value("k"), 1, 100, 10);

            decltype(ctx.persistence->queryHistoryTopK(0,0,0)) list;
            {
                std::lock_guard<std::mutex> lock(ctx.global_mutex);
                list = ctx.persistence->queryHistoryTopK(tStart, tEnd, k);
            }

            json j;
            std::vector<std::string> cats;
            std::vector<int> vals;
            for(auto& p : list) {
                cats.push_back(p.first);
                vals.push_back(p.second);
            }
            j["categories"] = cats;
            j["values"] = vals;
            res.set_content(j.dump(), "application/json");
        });

        // 7. 趋势
        svr.Get("/api/trends", [&](const httplib::Request& req, httplib::Response& res) {
            int k = GlobalUtils::safeStoi(req.get_param_value("k"), 1, 100, 10);
            
            decltype(ctx.window->getTrendingWords(0)) trends;
            {
                std::lock_guard<std::mutex> lock(ctx.global_mutex);
                trends = ctx.window->getTrendingWords(k);
            }

            json list = json::array();
            for(auto& p : trends) {
                json item;
                item["word"] = p.first;
                item["score"] = p.second;
                list.push_back(item);
            }
            res.set_content(list.dump(), "application/json");
        });
    }
};
#pragma once
#include "httplib.h"
#include "json.hpp"
#include "SystemContext.h"
#include "CommandExecutor.h"
#include "GlobalUtils.h"
#include <direct.h> // for _getcwd

using json = nlohmann::json;

class APIServer {
private:
    SystemContext& ctx;
    CommandExecutor& executor;
    httplib::Server svr;

public:
    APIServer(SystemContext& context, CommandExecutor& exec) : ctx(context), executor(exec) {
        setupRoutes();
    }

    void start(int port) {
        std::cout << "[Web] GUI Server running at http://localhost:" << port << std::endl;
        svr.listen("0.0.0.0", port);
    }

private:
    void setupRoutes() {
        // 1. 静态页面
        svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
            std::string path = "dashboard.html";
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                path = "../dashboard.html"; 
                ifs.open(path);
            }

            if(ifs.is_open()) {
                std::stringstream buffer;
                buffer << ifs.rdbuf();
                res.set_content(buffer.str(), "text/html");
            } else {
                char cwd[1024]; _getcwd(cwd, sizeof(cwd));
                std::string err = "<h1>Error: dashboard.html not found!</h1><p>Current Dir: " + std::string(cwd) + "</p>";
                res.set_content(err, "text/html");
            }
        });

        // 2. GET 数据接口
        svr.Get("/api/data", [&](const httplib::Request& req, httplib::Response& res) {
            int k = 10;
            if (req.has_param("k")) {
                try { k = std::stoi(req.get_param_value("k")); } catch (...) {}
                if (k <= 0) k = 1; if (k > 100) k = 100;
            }

            auto topList = ctx.window->getTopK(k);
            auto range = ctx.window->getWindowRange();
            
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
            j["current_ts"] = GlobalUtils::formatTime(ctx.lastTimestamp);
            j["retention_sec"] = ctx.window->getMaxRetention(); 

            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(j.dump(), "application/json");
        });

        // 3. POST 命令接口 (委托给 CommandExecutor)
        svr.Post("/api/command", [&](const httplib::Request& req, httplib::Response& res) {
            try {
                auto j = json::parse(req.body);
                std::string cmd = j["cmd"];
                std::cout << "[Web Command] " << cmd << std::endl;
                
                std::string output = executor.process(cmd);
                
                json resp;
                resp["status"] = "ok";
                resp["message"] = output;
                res.set_content(resp.dump(), "application/json");

                // 【新增】检查是否需要退出系统
                if (ctx.shouldExit) {
                    // 启动一个分离线程来执行退出，
                    // 确保当前的 HTTP 响应能先发送回前端，让前端知道操作成功了
                    std::thread([&](){
                        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 等 0.5秒
                        std::cout << "[System] Shutting down..." << std::endl;
                        svr.stop();   // 停止 Web 服务
                        std::exit(0); // 强行终止整个 C++ 进程
                    }).detach();
                }
            } catch (...) {
                res.status = 400;
                res.set_content("{\"status\":\"error\"}", "application/json");
            }
        });

        // 4. STATS 接口
        svr.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
            auto stats = ctx.monitor->getStats();
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

        // 5. 配置接口
        svr.Get("/api/config", [&](const httplib::Request&, httplib::Response& res) {
            json j;
            j["sensitive_words"] = ctx.processor->getSensitiveWords();
            j["allow_all"] = ctx.processor->isAllowAll();
            res.set_content(j.dump(), "application/json");
        });

        svr.Post("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
            try {
                auto j = json::parse(req.body);
                
                if (j.contains("tags") || j.contains("allow_all")) {
                    std::vector<std::string> tags;
                    bool allowAll = false;
                    if (j.contains("tags")) tags = j["tags"].get<std::vector<std::string>>();
                    if (j.contains("allow_all")) allowAll = j["allow_all"];
                    else allowAll = ctx.processor->isAllowAll();
                    ctx.processor->setPosConfig(tags, allowAll);
                }

                if (j.contains("add_sensitive")) ctx.processor->addSensitiveWord(j["add_sensitive"]);
                if (j.contains("remove_sensitive")) ctx.processor->removeSensitiveWord(j["remove_sensitive"]);

                res.set_content("{\"status\":\"ok\"}", "application/json");
            } catch(...) { res.status = 400; }
        });

        // 6. 历史查询
        svr.Get("/api/history_view", [&](const httplib::Request& req, httplib::Response& res) {
            long long tStart = GlobalUtils::parseTimeSeconds(req.get_param_value("start"));
            long long tEnd = GlobalUtils::parseTimeSeconds(req.get_param_value("end"));

            auto list = ctx.persistence->queryHistoryTopK(tStart, tEnd, 10);
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
            int k = 10;
            if (req.has_param("k")) k = std::stoi(req.get_param_value("k"));
            auto trends = ctx.window->getTrendingWords(k);
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
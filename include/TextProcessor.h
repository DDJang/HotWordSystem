#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <utility>
#include "cppjieba/Jieba.hpp"
#include "utf8.h"

class TextProcessor {
private:
    std::unique_ptr<cppjieba::Jieba> jieba;
    std::unordered_set<std::string> stopWords;
    std::unordered_set<std::string> sensitiveWords; // 敏感词集合
    std::unordered_set<std::string> allowedTags;    // 允许的词性集合

    bool allowAllTags = false; // 是否允许所有词性
    bool allowAllSensitive = false; // 【新增】敏感词开关

    mutable std::mutex mtx; // 互斥锁，保护配置修改

    std::string sensitiveFilePath; // 【新增】保存文件路径

public:
    TextProcessor(const std::string& dict_path, const std::string& hmm_path, 
                  const std::string& user_dict_path, const std::string& idf_path, 
                  const std::string& stop_word_path, const std::string& sensitive_path = "")
                  : sensitiveFilePath(sensitive_path) {
        
        jieba = std::make_unique<cppjieba::Jieba>(dict_path, hmm_path, user_dict_path, idf_path, stop_word_path);
        loadSet(stopWords, stop_word_path);
        
        // 如果提供了敏感词路径，则加载
        if (!sensitive_path.empty()) {
            loadSet(sensitiveWords, sensitive_path);
        }

        // 定义允许的词性（默认配置）
        allowedTags = { "n", "ns", "nr", "nt", "nz", "v", "vn", "a", "eng" }; 
        allowAllTags = false;
        allowAllSensitive = false; // 【新增】初始化 
    }

    // 设置词性配置 (日志已区分)
    void setPosConfig(const std::vector<std::string>& tags, bool allowAll) {
        std::lock_guard<std::mutex> lock(mtx);
        allowedTags.clear();
        for(const auto& t : tags) allowedTags.insert(t);
        allowAllTags = allowAll;

        std::cout << "[Config] POS tags updated. AllowAllPos=" << std::boolalpha << allowAllTags << std::endl;
    }

    // 【新增】单独设置敏感词开关 (解决日志混淆)
    void setSensitiveConfig(bool allowAll) {
        std::lock_guard<std::mutex> lock(mtx);
        allowAllSensitive = allowAll;
        std::cout << "[Config] Sensitive config updated. AllowAllSensitive=" << std::boolalpha << allowAllSensitive << std::endl;
    }
    
    bool isAllowAllPos() {
        std::lock_guard<std::mutex> lock(mtx);
        return allowAllTags;
    }

    bool isAllowAllSensitive() {
        std::lock_guard<std::mutex> lock(mtx);
        return allowAllSensitive;
    }

    std::vector<std::string> getAllowedTags() {
        std::lock_guard<std::mutex> lock(mtx);
        return std::vector<std::string>(allowedTags.begin(), allowedTags.end());
    }

    // 辅助：获取当前敏感词列表，返回值补全 std::
    std::vector<std::string> getSensitiveWords() {
        std::lock_guard<std::mutex> lock(mtx);
        return std::vector<std::string>(sensitiveWords.begin(), sensitiveWords.end());
    }

    // 【新增需求2】动态更新允许的词性
    void setAllowedTags(const std::vector<std::string>& tags) {
        std::lock_guard<std::mutex> lock(mtx);
        allowedTags.clear();
        for(const auto& t : tags) allowedTags.insert(t);
        std::cout << "[Config] Allowed POS tags updated." << std::endl;
    }

    // 【修改】动态增加敏感词 (支持持久化 + 线程安全)
    void addSensitiveWord(const std::string& word) {
        std::lock_guard<std::mutex> lock(mtx); // 1. 上锁，保护内存也保护文件
        
        // 如果内存里已经有了，就不操作文件了，减少 I/O
        if (sensitiveWords.find(word) != sensitiveWords.end()) {
            return;
        }

        sensitiveWords.insert(word);

        // 写入文件 (追加模式)
        if (!sensitiveFilePath.empty()) {
            std::ofstream ofs(sensitiveFilePath, std::ios::app);
            if (ofs.is_open()) {
                ofs << word << "\n";
            } else {
                std::cerr << "[Error] Cannot write to sensitive file: " << sensitiveFilePath << std::endl;
            }
        }
    }


    // 【修改】动态删除敏感词 (支持持久化 + 线程安全)
    void removeSensitiveWord(const std::string& word) {
        std::lock_guard<std::mutex> lock(mtx); // 1. 上锁
        
        auto it = sensitiveWords.find(word);
        if (it == sensitiveWords.end()) {
            return;
        }
        
        sensitiveWords.erase(it);

        // 删除比较麻烦，必须重写整个文件
        // 因为文件操作较慢，但这属于低频管理操作，可以接受
        if (!sensitiveFilePath.empty()) {
            std::ofstream ofs(sensitiveFilePath, std::ios::trunc); // 截断模式，清空重写
            if (ofs.is_open()) {
                for (const auto& w : sensitiveWords) {
                    ofs << w << "\n";
                }
            }
        }
    }   

    // 核心处理函数，
    std::vector<std::string> process(const std::string& sentence) {
        // 【修复】限制单次处理的最大长度 (例如 1000KB)
        if (sentence.length() > 1024000) {
            std::cerr << "[Warn] Input sentence too long, truncated." << std::endl;
            // 可以选择截断或者直接返回空
            return {}; 
        }

        // 复制配置（避免长时间持有锁，提升并发性能）
        std::unordered_set<std::string> currentSensitive;
        std::unordered_set<std::string> currentTags;
        bool useAllTags = false;
        bool useAllSensitive = false; // 【新增】
        {
            std::lock_guard<std::mutex> lock(mtx);
            currentSensitive = sensitiveWords;
            currentTags = allowedTags;
            useAllTags = allowAllTags; // 读取标志位
            useAllSensitive = allowAllSensitive; // 读取配置
        }

        // 在分词前，先进行清洗和标准化
        std::string clean_sentence = sanitizeAndNormalize(sentence);

        // 使用 Tag 进行分词和词性标注
        std::vector<std::pair<std::string, std::string>> tag_words;
        jieba->Tag(clean_sentence, tag_words); // 结果存为 {词, 词性}

        std::vector<std::string> final_words;
        for (const auto& pair : tag_words) {
            const std::string& word = pair.first;
            const std::string& tag = pair.second;

            // 1. 词性判断
            bool isPosAllowed = useAllTags || (currentTags.count(tag) > 0);
            
            // 2. 敏感词判断 (逻辑修改)
            // 如果是敏感词 AND (不允许保留所有敏感词) -> 也就是被屏蔽
            bool isSensitive = (currentSensitive.count(word) > 0);
            bool shouldFilterSensitive = isSensitive && !useAllSensitive;

            if (stopWords.find(word) == stopWords.end() && 
                !shouldFilterSensitive && // 【修改】如果不过滤敏感词，这里就放行
                isPosAllowed && 
                word.size() > 1) { 
                final_words.push_back(word);
            }
        }
        return final_words;
    }

private:
    // 使用 utfcpp 的迭代功能实现清洗函数，参数和返回值补全 std::
    std::string sanitizeAndNormalize(const std::string& input) {
        if (input.empty()) {
            return "";
        }

        std::string final_output;
        // 预分配内存以提高效率
        final_output.reserve(input.length());

        // 使用 utfcpp 的迭代器安全地遍历 UTF-8 字符串
        auto it = input.begin();
        auto end = input.end();

        while (it != end) {
            try {
                // 读取一个 Unicode 码点，迭代器 it 会自动前进
                uint32_t code_point = utf8::next(it, end);

                // 1. 特定字符映射 (康熙部首 -> 标准汉字)
                if (code_point == 0x2F08) code_point = 0x4EBA; // 部首人 -> 标准人
                if (code_point == 0x2F2F) code_point = 0x5DE5; // 部首工 -> 标准工
                
                // 2. 全角 -> 半角
                if (code_point >= 0xFF01 && code_point <= 0xFF5E) {
                    code_point -= 0xFEE0; // 计算半角对应码点
                } else if (code_point == 0x3000) { // 全角空格
                    code_point = 0x0020; // 替换为半角空格
                }

                // 3. 过滤不可见/控制字符
                bool is_control = (code_point <= 0x1F) || (code_point >= 0x7F && code_point <= 0x9F);
                // 零宽空格(200B), 零宽不连字(200C), 零宽连字(200D), BOM头/零宽不换行空格(FEFF)
                bool is_format = (code_point >= 0x200B && code_point <= 0x200D) || (code_point == 0xFEFF);

                // 如果字符是有效且可见的，则将其添加到输出中
                if (!is_control && !is_format) {
                    utf8::append(code_point, std::back_inserter(final_output)); // back_inserter -> std::back_inserter
                }

            } catch (const utf8::exception& e) {
                // 如果遇到无效的 UTF-8 序列，跳过该字节
                if (it != end) {
                    ++it;
                }
            }
        }
        return final_output;
    }

    // 通用加载函数
    void loadSet(std::unordered_set<std::string>& targetSet, const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return; // 文件不存在则忽略
        std::string line;
        while (std::getline(ifs, line)) { 
            // 处理 Windows 换行符（\r\n），移除末尾的 \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                targetSet.insert(line);
            }
        }
    }

};
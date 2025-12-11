#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <mutex> // 新增锁
#include "cppjieba/Jieba.hpp"
#include "utf8.h"

using namespace std;

class TextProcessor {
private:
    unique_ptr<cppjieba::Jieba> jieba;
    unordered_set<string> stopWords;
    unordered_set<string> sensitiveWords; // 新增：敏感词集合
    unordered_set<string> allowedTags;    // 新增：允许的词性集合

    bool allowAllTags = false; // 【新增】是否允许所有词性

    mutable mutex mtx; // 【新增】互斥锁，保护配置修改

public:
    TextProcessor(const string& dict_path, const string& hmm_path, 
                  const string& user_dict_path, const string& idf_path, 
                  const string& stop_word_path, const string& sensitive_path = "") { // 新增参数
        
        jieba = make_unique<cppjieba::Jieba>(dict_path, hmm_path, user_dict_path, idf_path, stop_word_path);
        loadSet(stopWords, stop_word_path);
        
        // 如果提供了敏感词路径，则加载
        if (!sensitive_path.empty()) {
            loadSet(sensitiveWords, sensitive_path);
        }

        // 定义允许的词性
        // 默认配置
        allowedTags = { "n", "ns", "nr", "nt", "nz", "v", "vn", "a", "eng" }; 
        allowAllTags = false; 
    }

    // 【修改】统一设置词性配置 (支持 allowAll)
    void setPosConfig(const vector<string>& tags, bool allowAll) {
        lock_guard<mutex> lock(mtx);
        allowedTags.clear();
        for(const auto& t : tags) allowedTags.insert(t);
        allowAllTags = allowAll;
        cout << "[Config] POS tags updated. AllowAll=" << allowAllTags << endl;
    }
    
    // 【新增】获取当前 allowAll 状态 (供 API 回显)
    bool isAllowAll() {
        lock_guard<mutex> lock(mtx);
        return allowAllTags;
    }

    // 辅助：获取当前敏感词列表
    vector<string> getSensitiveWords() {
        lock_guard<mutex> lock(mtx);
        return vector<string>(sensitiveWords.begin(), sensitiveWords.end());
    }

    // 【新增需求2】动态更新允许的词性
    void setAllowedTags(const vector<string>& tags) {
        lock_guard<mutex> lock(mtx);
        allowedTags.clear();
        for(const auto& t : tags) allowedTags.insert(t);
        cout << "[Config] Allowed POS tags updated." << endl;
    }

    // 【新增需求2】动态管理敏感词
    void addSensitiveWord(const string& word) {
        lock_guard<mutex> lock(mtx);
        sensitiveWords.insert(word);
    }

    void removeSensitiveWord(const string& word) {
        lock_guard<mutex> lock(mtx);
        sensitiveWords.erase(word);
    }    

    vector<string> process(const string& sentence) {
        // process 函数会被频繁调用，需要加锁保护读取配置
        // 注意：分词本身耗时较长，最好只在读取 set 时加锁，或者复制一份 set 使用
        // 这里为了代码简单，直接在关键判断处用锁不太现实，建议复制配置
        
        unordered_set<string> currentSensitive;
        unordered_set<string> currentTags;
        bool useAllTags = false;
        {
            lock_guard<mutex> lock(mtx);
            currentSensitive = sensitiveWords;
            currentTags = allowedTags;
            useAllTags = allowAllTags; // 读取标志位
        }

        // 在分词前，先进行清洗和标准化
        string clean_sentence = sanitizeAndNormalize(sentence);

        // 使用 Tag 进行分词和词性标注
        vector<pair<string, string>> tag_words;
        jieba->Tag(clean_sentence, tag_words); // 结果存为 {词, 词性}

        // 【Debug】看看它到底是什么词性！
        // cout << "[DEBUG] ";
        // for(auto& p : tag_words) {
        //     cout << p.first << "[" << p.second << "] ";
        // }
        // cout << endl;

        vector<string> final_words;
        for (const auto& pair : tag_words) {
            const string& word = pair.first;
            const string& tag = pair.second;

            // 过滤逻辑：
            // 1. 不在停用词表 AND 不在敏感词表
            // 2. AND (允许所有词性 OR 词性在白名单中)
            // 3. AND 长度 > 1
            bool isPosAllowed = useAllTags || (currentTags.count(tag) > 0);

            if (stopWords.find(word) == stopWords.end() && 
                currentSensitive.find(word) == currentSensitive.end() && 
                isPosAllowed && 
                word.size() > 1) { 
                final_words.push_back(word);
            }
        }
        return final_words;
    }

private:
    // 使用 utfcpp 的迭代功能实现清洗函数
    string sanitizeAndNormalize(const string& input) {
        if (input.empty()) {
            return "";
        }

        string final_output;
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
                
                // 2. (可选) 全角 -> 半角
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
                    utf8::append(code_point, back_inserter(final_output));
                }

            } catch (const utf8::exception& e) {
                // 如果遇到无效的 UTF-8 序列，我们可以选择跳过它
                // 最简单的处理是让迭代器前进一个字节，但这可能不完美
                if (it != end) {
                    ++it;
                }
            }
        }
        return final_output;
    }


    // 通用加载函数
    void loadSet(unordered_set<string>& targetSet, const string& path) {
        ifstream ifs(path);
        if (!ifs.is_open()) return; // 文件不存在则忽略
        string line;
        while (getline(ifs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) targetSet.insert(line);
        }
    }

};
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <fstream>
#include <iostream>
#include <algorithm>
#include "cppjieba/Jieba.hpp"
#include "utf8.h"

using namespace std;

class TextProcessor {
private:
    unique_ptr<cppjieba::Jieba> jieba;
    unordered_set<string> stopWords;
    unordered_set<string> sensitiveWords; // 新增：敏感词集合
    unordered_set<string> allowedTags;    // 新增：允许的词性集合

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

        // 【新增】定义允许的词性
        // n: 名词 (各种变体)
        // v: 动词 (vn: 名动词, vd: 副动词)
        // a: 形容词 (an: 名形词)
        // nz: 其他专名 (这是重点！很多组合词都在这里)
        // l: 习语/成语
        // i: 成语
        // eng: 英文
        allowedTags = {
            "n", "ns", "nr", "nt", "nz", "nl", "ng",  // 各种名词
            "v", "vd", "vn",                          // 各种动词
            "a", "an",                                // 形容词
            "l", "i",                                 // 习语、成语
            "eng", "x"                                // 英文、其他
        }; 
    }

    vector<string> process(const string& sentence) {
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
            // 1. 不在停用词表
            // 2. 不在敏感词表
            // 3. 词性必须在白名单内 (例如只保留名词)
            // 4. 长度大于1 (过滤单个字)
            if (stopWords.find(word) == stopWords.end() && 
                sensitiveWords.find(word) == sensitiveWords.end() && 
                allowedTags.count(tag) > 0 && 
                word.size() > 1) { // 简单过滤单个字符
                
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

    // bool isSpecialChar(const string& w) {
    //     if (w.size() == 1) {
    //         char c = w[0];
    //         if (!isalnum(c)) return true;
    //     }
    //     return false;
    // }
};
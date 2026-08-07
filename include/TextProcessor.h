#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <future>
#include <thread>
#include <functional>
#include <atomic>
#include <cstdint>

#include "cppjieba/Jieba.hpp"
#include "utf8.h"
#include "ThreadPool.h"

class TextProcessor {
private:
    std::unique_ptr<cppjieba::Jieba> jieba;
    std::unordered_set<std::string> stopWords;
    std::unordered_set<std::string> sensitiveWords; // 敏感词集合
    std::unordered_set<std::string> allowedTags;    // 允许的词性集合

    bool allowAllTags = false; // 是否允许所有词性
    bool allowAllSensitive = false; // 敏感词开关

    mutable std::mutex mtx; // 互斥锁，保护配置修改

    std::string sensitiveFilePath; // 保存文件路径

    // 缓存机制
    mutable std::mutex cacheMtx; // 缓存锁
    struct CacheEntry {
        std::uint64_t configVersion;
        std::vector<std::string> words;
    };
    std::unordered_map<std::string, CacheEntry> processCache; // 处理结果缓存
    // 每次过滤配置变化都递增，防止旧计算结果在清空缓存后重新写回。
    std::atomic<std::uint64_t> configVersion{0};

    // 线程池用于并行处理
    std::unique_ptr<ThreadPool> threadPool;

    static constexpr size_t CACHE_MAX_SIZE = 10000; // 最大缓存大小
    static constexpr size_t CACHE_CLEAN_THRESHOLD = 8000; // 缓存清理阈值

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
        allowAllSensitive = false; // 初始化 

        // 初始化线程池，线程数为CPU核心数
        size_t threadCount = std::thread::hardware_concurrency();
        threadPool = std::make_unique<ThreadPool>(threadCount);
    }

    // 设置词性配置
    void setPosConfig(const std::vector<std::string>& tags, bool allowAll) {
        bool updatedAllowAll = allowAll;
        {
            std::lock_guard<std::mutex> lock(mtx);
            allowedTags.clear();
            for(const auto& t : tags) allowedTags.insert(t);
            allowAllTags = allowAll;
            configVersion.fetch_add(1, std::memory_order_release);
        }
        clearCache();

        std::cout << "[Config] POS tags updated. AllowAllPos=" << std::boolalpha << updatedAllowAll << std::endl;
    }

    // 单独设置敏感词开关
    void setSensitiveConfig(bool allowAll) {
        bool updatedAllowAll = allowAll;
        {
            std::lock_guard<std::mutex> lock(mtx);
            allowAllSensitive = allowAll;
            configVersion.fetch_add(1, std::memory_order_release);
        }
        clearCache();
        std::cout << "[Config] Sensitive config updated. AllowAllSensitive=" << std::boolalpha << updatedAllowAll << std::endl;
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

    // 辅助：获取当前敏感词列表
    std::vector<std::string> getSensitiveWords() {
        std::lock_guard<std::mutex> lock(mtx);
        return std::vector<std::string>(sensitiveWords.begin(), sensitiveWords.end());
    }

    // 动态更新允许的词性
    void setAllowedTags(const std::vector<std::string>& tags) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            allowedTags.clear();
            for(const auto& t : tags) allowedTags.insert(t);
            configVersion.fetch_add(1, std::memory_order_release);
        }
        clearCache();
        std::cout << "[Config] Allowed POS tags updated." << std::endl;
    }

    // 动态增加敏感词 (支持持久化 + 线程安全)
    void addSensitiveWord(const std::string& word) {
        if (word.empty()) return;

        {
            std::lock_guard<std::mutex> lock(mtx); // 保护内存和文件

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
            configVersion.fetch_add(1, std::memory_order_release);
        }
        clearCache();
    }


    // 动态删除敏感词 (支持持久化 + 线程安全)
    void removeSensitiveWord(const std::string& word) {
        {
            std::lock_guard<std::mutex> lock(mtx);

            auto it = sensitiveWords.find(word);
            if (it == sensitiveWords.end()) {
                return;
            }

            sensitiveWords.erase(it);

            // 删除比较麻烦，必须重写整个文件
            if (!sensitiveFilePath.empty()) {
                std::ofstream ofs(sensitiveFilePath, std::ios::trunc);
                if (ofs.is_open()) {
                    for (const auto& w : sensitiveWords) {
                        ofs << w << "\n";
                    }
                }
            }
            configVersion.fetch_add(1, std::memory_order_release);
        }
        clearCache();
    }

    // 核心处理函数，
    std::vector<std::string> process(const std::string& sentence) {
        // 限制单次处理的最大长度 (例如 1000KB)
        if (sentence.length() > 1024000) {
            std::cerr << "[Error] Input sentence too long." << std::endl;
            // 直接返回空
            return {}; 
        }

        // 先获取配置快照，再按配置版本读取缓存。这样配置更新后同一句子
        // 不会复用旧过滤结果。
        bool useAllTags = false;
        bool useAllSensitive = false;
        std::unordered_set<std::string> currentAllowedTags;
        std::unordered_set<std::string> currentSensitiveWords;
        std::uint64_t currentConfigVersion = 0;
        {
            std::lock_guard<std::mutex> lock(mtx);
            useAllTags = allowAllTags;
            useAllSensitive = allowAllSensitive;
            currentAllowedTags = allowedTags;
            currentSensitiveWords = sensitiveWords;
            currentConfigVersion = configVersion.load(std::memory_order_acquire);
        }

        {
            std::lock_guard<std::mutex> lock(cacheMtx);
            auto it = processCache.find(sentence);
            if (it != processCache.end() && it->second.configVersion == currentConfigVersion) {
                return it->second.words;
            }
        }

        // 在分词前，先进行清洗和标准化
        std::string clean_sentence = sanitizeAndNormalize(sentence);

        // 使用 Tag 进行分词和词性标注
        std::vector<std::pair<std::string, std::string>> tag_words;
        jieba->Tag(clean_sentence, tag_words); // 结果存为 {词, 词性}

        std::vector<std::string> final_words;
        final_words.reserve(tag_words.size()); // 预分配空间

        for (const auto& pair : tag_words) {
            const std::string& word = pair.first;
            const std::string& tag = pair.second;

            // 1. 停用词判断（不需要锁，stopWords不常修改）
            if (stopWords.find(word) != stopWords.end() || word.size() <= 1) {
                continue;
            }

            // 2. 词性判断
            bool isPosAllowed = useAllTags || (currentAllowedTags.count(tag) > 0);
            if (!isPosAllowed) {
                continue;
            }
            
            // 3. 敏感词判断
            bool isSensitive = (currentSensitiveWords.count(word) > 0);
            bool shouldFilterSensitive = isSensitive && !useAllSensitive;
            if (shouldFilterSensitive) {
                continue;
            }

            final_words.push_back(word);
        }

        // 存入缓存
        {
            std::lock_guard<std::mutex> lock(cacheMtx);
            // 清理过期缓存
            if (processCache.size() > CACHE_CLEAN_THRESHOLD) {
                cleanCacheUnlocked();
            }
            // 添加新缓存
            if (processCache.size() < CACHE_MAX_SIZE &&
                configVersion.load(std::memory_order_acquire) == currentConfigVersion) {
                processCache[sentence] = CacheEntry{currentConfigVersion, final_words};
            }
        }

        return final_words;
    }

    // 批量处理函数
    std::vector<std::vector<std::string>> processBatch(const std::vector<std::string>& sentences) {
        std::vector<std::vector<std::string>> results;
        results.reserve(sentences.size());

        // 并行处理
        std::vector<std::future<std::vector<std::string>>> futures;
        futures.reserve(sentences.size());

        for (const auto& sentence : sentences) {
            futures.push_back(threadPool->enqueue([this, sentence]() {
                return process(sentence);
            }));
        }

        // 收集结果
        for (auto& future : futures) {
            results.push_back(future.get());
        }

        return results;
    }

    // 清理缓存
    void cleanCache() {
        std::lock_guard<std::mutex> lock(cacheMtx);
        cleanCacheUnlocked();
    }

private:
    // 调用方必须已经持有 cacheMtx。
    void cleanCacheUnlocked() {
        // 简单清理：保留一半缓存
        size_t targetSize = CACHE_MAX_SIZE / 2;
        if (processCache.size() <= targetSize) {
            return;
        }

        // 迭代删除一半的缓存项
        size_t count = 0;
        auto it = processCache.begin();
        while (it != processCache.end() && count < processCache.size() - targetSize) {
            it = processCache.erase(it);
            count++;
        }
    }

public:
    // 清空缓存
    void clearCache() {
        std::lock_guard<std::mutex> lock(cacheMtx);
        processCache.clear();
    }

private:
    // 使用 utfcpp 的迭代功能实现清洗函数
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

            } catch (const utf8::exception&) {
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

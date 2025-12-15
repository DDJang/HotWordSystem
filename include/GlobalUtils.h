#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <random>
#include <sstream>

class GlobalUtils {
public:
    // 解析时间字符串 (HH:MM:SS) -> 秒数
    static long long parseTimeSeconds(const std::string& timeStr) {
        int h, m, s;
        if (std::sscanf(timeStr.c_str(), "%d:%d:%d", &h, &m, &s) == 3) {
            return static_cast<long long>(h) * 3600 + static_cast<long long>(m) * 60 + static_cast<long long>(s);
        }
        return 0;
    }

    // 秒数 -> 时间字符串 (HH:MM:SS)
    static std::string formatTime(long long totalSeconds) {
        if (totalSeconds < 0) totalSeconds = 0;

        // 1. 计算总天数
        long long days = totalSeconds / 86400; // 86400 = 24 * 3600

        // 2. 计算当天的小时、分钟、秒
        int h = static_cast<int>((totalSeconds / 3600) % 24);
        int m = static_cast<int>((totalSeconds / 60) % 60);
        int s = static_cast<int>(totalSeconds % 60);

        // 3. 使用 stringstream 格式化输出
        std::stringstream ss;
        
        // 如果天数大于0，则添加 [Day X] 前缀
        if (days > 0) {
            ss << "[Day " << (days + 1) << "] ";
        }

        // 格式化 HH:MM:SS 部分
        char buf[16];
        std::sprintf(buf, "%02d:%02d:%02d", h, m, s);
        ss << buf;

        return ss.str();
    }

    // 生成随机句子 (用于压测)
    static std::string generateRandomSentence() {
        // 使用静态变量，确保随机数引擎只被初始化一次
        static std::mt19937 gen(std::random_device{}()); 

        // 词典
        static std::vector<std::string> dictionary = {
            "华为", "小米", "苹果", "OpenAI", "英伟达", "比特币", "特斯拉",
            "显卡", "暴跌", "上涨", "发布会", "财报", "AI", "大模型"
        };
        static std::vector<std::string> verbs = {"发布", "震惊", "颠覆", "宣布", "推出", "超越"};
        
        // 使用更精确的均匀分布来选择词语
        std::uniform_int_distribution<> dict_dist(0, dictionary.size() - 1);
        std::uniform_int_distribution<> verb_dist(0, verbs.size() - 1);
        std::uniform_int_distribution<> len_dist(3, 8);

        std::string s = "";
        int len = len_dist(gen);
        for(int i=0; i<len; i++) {
            s += dictionary[dict_dist(gen)];
            if(i < len-1) {
                s += verbs[verb_dist(gen)];
            }
        }
        return s;
    }

    // 安全转 Int，失败返回 defaultValue
    static int safeStoi(const std::string& str, int minVal, int maxVal, int defaultValue) {
        try {
            size_t idx;
            int val = std::stoi(str, &idx);
            // 检查是否整个字符串都是数字 (防止 "123abc" 被解析为 123)
            if (idx != str.length()) return defaultValue; 
            if (val < minVal) return minVal;
            if (val > maxVal) return maxVal;
            return val;
        } catch (...) {
            return defaultValue;
        }
    }

    // 安全转 Long Long，失败返回 defaultValue
    static long long safeStoll(const std::string& str, long long minVal, long long maxVal, long long defaultValue) {
        try {
            size_t idx;
            long long val = std::stoll(str, &idx);
            if (idx != str.length()) return defaultValue;
            if (val < minVal) return minVal;
            if (val > maxVal) return maxVal;
            return val;
        } catch (...) {
            return defaultValue;
        }
    }
};
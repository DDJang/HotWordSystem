#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <limits>   // for numeric_limits
#include <stdexcept> // for try-catch

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
        int h = static_cast<int>((totalSeconds / 3600) % 24);
        int m = static_cast<int>((totalSeconds / 60) % 60);
        int s = static_cast<int>(totalSeconds % 60);
        char buf[16];
        std::sprintf(buf, "%02d:%02d:%02d", h, m, s);
        return std::string(buf);
    }

    // 生成随机句子 (用于压测)
    static std::string generateRandomSentence() {
        static std::vector<std::string> dictionary = {"华为", "小米", "苹果", "OpenAI", "显卡", "暴跌", "上涨", "发布会"};
        static std::vector<std::string> verbs = {"发布", "震惊", "颠覆", "宣布"};
        std::string s = "";
        int len = rand() % 5 + 3;
        for(int i=0; i<len; i++) {
            s += dictionary[rand() % dictionary.size()];
            if(i < len-1) {
                s += verbs[rand() % verbs.size()];
            }
        }
        return s;
    }

    // 【新增】安全转 Int，失败返回 defaultValue
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

    // 【新增】安全转 Long Long
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
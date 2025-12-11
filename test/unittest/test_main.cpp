#include <iostream>
#include <cassert>
#include "SlidingWindow.h"
#include "TextProcessor.h"

void test_sliding_window() {
    SlidingWindow sw(10); // 10秒窗口
    
    // 模拟数据：第1秒
    sw.addData(100, {"apple", "banana"});
    
    // 第5秒
    sw.addData(105, {"apple", "orange"});
    
    auto top = sw.getTopK(5);
    // apple=2, banana=1, orange=1
    assert(top[0].first == "apple");
    assert(top[0].second == 2);
    
    // 第15秒 (第1秒的数据应该过期了)
    sw.addData(115, {"melon"});
    
    top = sw.getTopK(5);
    // apple=1 (105那次), orange=1, melon=1. banana应该没了
    bool hasBanana = false;
    for(auto& p : top) if(p.first == "banana") hasBanana = true;
    
    assert(!hasBanana); // banana 应该被淘汰
    
    std::cout << "[Test] SlidingWindow Passed!" << std::endl;
}

int main() {
    test_sliding_window();
    return 0;
}
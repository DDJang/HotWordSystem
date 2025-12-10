cmake_minimum_required(VERSION 3.10)
project(HotWordSystem)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 包含所有需要的头文件路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/cppjieba/include
    ${CMAKE_SOURCE_DIR}/third_party/cppjieba/deps/limonp/include
    ${CMAKE_SOURCE_DIR}/third_party/httplib
    ${CMAKE_SOURCE_DIR}/third_party/json
    # --- 新增：添加 utfcpp 的头文件路径 (替换了 ICU) ---
    # 请确保您的 utfcpp 库放在了 "third_party/utfcpp/source" 目录下
    ${CMAKE_SOURCE_DIR}/third_party/utfcpp/source
)

# 2. 定义源文件
set(MAIN_SOURCES "src/main.cpp")
set(TEST_SOURCES "src/test_main.cpp")

# 3. 生成主程序
add_executable(HotWordSystem ${MAIN_SOURCES})

# 4. 生成单元测试程序
add_executable(RunTests ${TEST_SOURCES})

# --- 针对 Windows 平台的特定配置 ---
if(WIN32)
    # 为两个目标添加编译宏定义，这将全局性地解决 NOMINMAX 重定义问题
    target_compile_definitions(HotWordSystem PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(RunTests    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    # 为两个目标链接 Windows 必需的库
    target_link_libraries(HotWordSystem PRIVATE ws2_32 crypt32 psapi)
    target_link_libraries(RunTests    PRIVATE ws2_32 crypt32 psapi)
endif()

# 5. 复制依赖文件
# 复制 cppjieba 字典 (此部分保持不变)
file(COPY ${CMAKE_SOURCE_DIR}/third_party/cppjieba/dict DESTINATION ${CMAKE_BINARY_DIR})

cmake_minimum_required(VERSION 3.10)
project(HotWordSystem)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 包含所有需要的头文件路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/cppjieba/include
    ${CMAKE_SOURCE_DIR}/third_party/cppjieba/deps/limonp/include
    ${CMAKE_SOURCE_DIR}/third_party/httplib
    ${CMAKE_SOURCE_DIR}/third_party/json
    ${CMAKE_SOURCE_DIR}/third_party/utfcpp/source
    ${CMAKE_SOURCE_DIR}/third_party/compile-time-regular-expressions/include
)

# 2. 定义源文件
# 主程序源文件
set(MAIN_SOURCES "src/main.cpp")
# 定义共享的 Context 源文件
set(CONTEXT_SOURCE "src/SystemContext.cpp")
# 测试程序源文件
set(TEST_SOURCES "test/unittest/test_main.cpp")

# 3. 生成主程序
add_executable(HotWordSystem ${MAIN_SOURCES} ${CONTEXT_SOURCE})

# 4. 生成单元测试程序 (目前开发中，后续可以启用)
# add_executable(RunTests ${TEST_SOURCES} ${CONTEXT_SOURCE})

# --- 针对 Windows 平台的特定配置---
if(WIN32)
    # 5.1 基础宏定义 (解决头文件冲突等)
    target_compile_definitions(HotWordSystem PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # target_compile_definitions(RunTests    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    # 5.2 核心修复：针对 MinGW 开启静态链接
    # 这会将 libwinpthread-1.dll, libstdc++-6.dll 等打包进 exe
    if(MINGW)
        message(STATUS "Detect MinGW: Enabling Static Linking to avoid missing DLLs...")
        # -static: 尽可能静态链接所有库
        # -static-libgcc -static-libstdc++: 强制静态链接 C++ 运行时
        target_link_libraries(HotWordSystem PRIVATE -static -static-libgcc -static-libstdc++)
        # target_link_libraries(RunTests    PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    # 5.3 链接 Windows 系统库 (网络、加密、进程信息)
    # 这些库是 Windows 自带的，必须链接，但不需要(也不能)静态打包
    target_link_libraries(HotWordSystem PRIVATE ws2_32 crypt32 psapi)
    # target_link_libraries(RunTests    PRIVATE ws2_32 crypt32 psapi)
endif()

# 6. 复制依赖文件

# 复制 cppjieba 字典
file(COPY ${CMAKE_SOURCE_DIR}/third_party/cppjieba/dict DESTINATION ${CMAKE_BINARY_DIR})

# 7. 复制 Web 前端资源文件夹 ---
file(COPY ${CMAKE_SOURCE_DIR}/web DESTINATION ${CMAKE_BINARY_DIR})
message(STATUS "已配置 Web 资源复制路径")

# 8. 复制 testdata 到构建目录 ---
if(EXISTS "${CMAKE_SOURCE_DIR}/test/testdata")
    file(COPY ${CMAKE_SOURCE_DIR}/test/testdata DESTINATION ${CMAKE_BINARY_DIR})
    message(STATUS "已复制 testdata 到构建目录")
else()
    message(WARNING "未找到 test/testdata 目录，跳过复制")
endif()
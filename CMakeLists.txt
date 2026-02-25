cmake_minimum_required(VERSION 3.10)
project(HotWordSystem)

# 设置 C++ 标准为 C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含所有需要的头文件路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/cppjieba/include
    ${CMAKE_SOURCE_DIR}/third_party/cppjieba/deps/limonp/include
    ${CMAKE_SOURCE_DIR}/third_party/httplib
    ${CMAKE_SOURCE_DIR}/third_party/json
    ${CMAKE_SOURCE_DIR}/third_party/utfcpp/source
    ${CMAKE_SOURCE_DIR}/third_party/compile-time-regular-expressions/include
    ${CMAKE_SOURCE_DIR}/third_party/spdlog/include
    ${CMAKE_SOURCE_DIR}/third_party/mimalloc/include
)

# 添加第三方库的 CMake 配置
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/mimalloc)  # 高性能内存分配器
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/spdlog)    # 高性能日志库

# 定义源文件
set(MAIN_SOURCES "src/main.cpp")             # 主程序源文件
set(CONTEXT_SOURCE "src/SystemContext.cpp")  # 共享的 Context 源文件
set(TEST_SOURCES "test/unittest/test_main.cpp")  # 测试程序源文件

# 生成主程序
add_executable(HotWordSystem ${MAIN_SOURCES} ${CONTEXT_SOURCE})

# 生成单元测试程序 (目前开发中，后续可以启用)
# add_executable(RunTests ${TEST_SOURCES} ${CONTEXT_SOURCE})

# 针对 Windows 平台的特定配置
if(WIN32)
    # 基础宏定义，解决头文件冲突等问题
    target_compile_definitions(HotWordSystem PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # target_compile_definitions(RunTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    # 针对 MinGW 开启静态链接，避免缺少 DLL 文件
    if(MINGW)
        message(STATUS "Detect MinGW: Enabling Static Linking to avoid missing DLLs...")
        target_link_libraries(HotWordSystem PRIVATE -static -static-libgcc -static-libstdc++)
        # target_link_libraries(RunTests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    # 链接 Windows 系统库 (网络、加密、进程信息)
    target_link_libraries(HotWordSystem PRIVATE ws2_32 crypt32 psapi)
    # target_link_libraries(RunTests PRIVATE ws2_32 crypt32 psapi)
endif()

# 链接第三方库
target_link_libraries(HotWordSystem PRIVATE
    mimalloc-static  # 高性能内存分配器
    spdlog::spdlog   # 高性能日志库
)

# 复制依赖文件
file(COPY ${CMAKE_SOURCE_DIR}/third_party/cppjieba/dict DESTINATION ${CMAKE_BINARY_DIR})  # 复制分词字典
file(COPY ${CMAKE_SOURCE_DIR}/web DESTINATION ${CMAKE_BINARY_DIR})  # 复制 Web 前端资源
message(STATUS "已配置 Web 资源复制路径")
cmake_minimum_required(VERSION 3.10)
project(HotWordSystem)

# 如果检测到是 MinGW 编译器，开启静态链接
if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    # 有时候还需要显式链接 pthread，视版本而定
    link_libraries(-static) 
endif()

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
    ${CMAKE_SOURCE_DIR}/third_party/utfcpp/source
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

# 4. 生成单元测试程序
add_executable(RunTests ${TEST_SOURCES} ${CONTEXT_SOURCE})

# --- 针对 Windows 平台的特定配置 ---
if(WIN32)
    # 宏定义
    target_compile_definitions(HotWordSystem PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(RunTests    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    # 链接库
    target_link_libraries(HotWordSystem PRIVATE ws2_32 crypt32 psapi)
    target_link_libraries(RunTests    PRIVATE ws2_32 crypt32 psapi)
endif()

# 5. 复制依赖文件

# 复制 cppjieba 字典
file(COPY ${CMAKE_SOURCE_DIR}/third_party/cppjieba/dict DESTINATION ${CMAKE_BINARY_DIR})

# 6. 复制 Web 前端资源文件夹 ---
# 这会将源码目录下的 web 文件夹完整复制到 build/web
file(COPY ${CMAKE_SOURCE_DIR}/web DESTINATION ${CMAKE_BINARY_DIR})

message(STATUS "已配置 Web 资源复制路径")

# 7. 复制 testdata 到构建目录 ---
# 这样在代码中读取文件时，可以使用相对路径 "./testdata/xxx.txt"
# 如果 testdata 文件夹不存在，CMake 可能会报警告，这里加个判断
if(EXISTS "${CMAKE_SOURCE_DIR}/test/testdata")
    file(COPY ${CMAKE_SOURCE_DIR}/test/testdata DESTINATION ${CMAKE_BINARY_DIR})
    message(STATUS "已复制 testdata 到构建目录")
else()
    message(WARNING "未找到 test/testdata 目录，跳过复制")
endif()


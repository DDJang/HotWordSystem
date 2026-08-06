cmake_minimum_required(VERSION 3.18)

project(HotWordSystem
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "A C++20 real-time hot-word analysis service"
)

set(CMAKE_CXX_EXTENSIONS OFF)

# The project only consumes headers from these dependencies.  Keep the
# dependency subdirectories out of the default build and disable options that
# are supported by the pinned versions but are not needed here.
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog EXCLUDE_FROM_ALL)

set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# The pinned mimalloc version has no option to disable install rules; the
# library and its tests remain outside the default build instead.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/mimalloc EXCLUDE_FROM_ALL)

find_package(Threads REQUIRED)

add_executable(HotWordSystem
    src/main.cpp
    src/SystemContext.cpp
)

target_compile_features(HotWordSystem PRIVATE cxx_std_20)

target_include_directories(HotWordSystem PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(HotWordSystem SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cppjieba/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cppjieba/deps/limonp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/single_include/nlohmann
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/utfcpp/source
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/compile-time-regular-expressions/include
)

target_link_libraries(HotWordSystem PRIVATE
    mimalloc-static
    spdlog::spdlog
    Threads::Threads
)

if(MSVC)
    target_compile_options(HotWordSystem PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(HotWordSystem PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_compile_definitions(HotWordSystem PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    if(MINGW)
        message(STATUS "Detect MinGW: enabling static runtime linking")
        target_link_options(HotWordSystem PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    target_link_libraries(HotWordSystem PRIVATE ws2_32 crypt32 psapi)
endif()

# Keep CTest ready for a future project test directory without inventing tests
# when this checkout does not contain one.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt")
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(test)
    endif()
endif()

# These resources are intentionally copied at configure time to preserve the
# existing runtime layout expected by SystemContext and APIServer.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cppjieba/dict DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/web DESTINATION ${CMAKE_BINARY_DIR})
message(STATUS "Configured dictionary and Web resource copying")

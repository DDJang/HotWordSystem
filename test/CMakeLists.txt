add_executable(HotWordSystemCorrectness
    test_correctness.cpp
    ${CMAKE_SOURCE_DIR}/src/SystemContext.cpp
)

target_compile_features(HotWordSystemCorrectness PRIVATE cxx_std_20)

target_include_directories(HotWordSystemCorrectness PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_include_directories(HotWordSystemCorrectness SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/cppjieba/include
    ${CMAKE_SOURCE_DIR}/third_party/cppjieba/deps/limonp/include
    ${CMAKE_SOURCE_DIR}/third_party/utfcpp/source
    ${CMAKE_SOURCE_DIR}/third_party/compile-time-regular-expressions/include
)

target_link_libraries(HotWordSystemCorrectness PRIVATE
    mimalloc-static
    spdlog::spdlog
    Threads::Threads
)

if(MSVC)
    target_compile_options(HotWordSystemCorrectness PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(HotWordSystemCorrectness PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_compile_definitions(HotWordSystemCorrectness PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    if(MINGW)
        target_link_options(HotWordSystemCorrectness PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    target_link_libraries(HotWordSystemCorrectness PRIVATE ws2_32 crypt32 psapi)
endif()

set(HOTWORD_CORRECTNESS_TESTS
    PersistenceFlushAtBatchBoundaryDoesNotDeadlock
    PersistenceResetDoesNotRestoreOldPendingData
    PersistenceQuerySeesPendingData
    TextProcessorCacheDoesNotDeadlockAtCleanup
    TextProcessorCacheReflectsConfigChanges
    SlidingWindowExpiresOldData
    SlidingWindowDoesNotCountExpiredOutOfOrderData
    SlidingWindowResizeRebuildsCorrectly
    SlidingWindowActiveCapacityKeepsCountsConsistent
    RepeatedBenchmarkStartIsRejected
    ShutdownOrDestructionFlushesPendingPersistence
)

foreach(test_name IN LISTS HOTWORD_CORRECTNESS_TESTS)
    add_test(
        NAME ${test_name}
        COMMAND HotWordSystemCorrectness ${test_name}
    )
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endforeach()

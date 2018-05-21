cmake_minimum_required(VERSION 3.6)
project(basiclib_ut)
set(CMAKE_CXX_FLAGS "-O0 -ggdb -Wall -Wextra -std=c++14 -pthread")

set (QUEUE_SOURCES
    main.cc
    mt_ring_queue_ut.cc
)

set (MEMLOG_SOURCES
    main.cc
    memorylog.cc
    memorylog_ut.cc
)

set(SOURCES
    mt_ring_queue_ut.cc
    memorylog.cc
)

add_executable(mt_ring_queue_ut ${QUEUE_SOURCES})
target_link_libraries(mt_ring_queue_ut CppUTest CppUTestExt)

add_executable(memlog_ut ${MEMLOG_SOURCES})
target_link_libraries(memlog_ut CppUTest CppUTestExt)

add_library(memorylog ${SOURCES})

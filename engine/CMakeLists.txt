cmake_minimum_required(VERSION 3.15)
project(ProcessExecutionEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src vendor)

set(SOURCES
    src/main.cpp
    src/db_client.cpp
    src/worker.cpp
    src/scheduler.cpp
)

add_executable(engine ${SOURCES})

if (WIN32)
    target_link_libraries(engine ws2_32 crypt32 psapi)
endif()

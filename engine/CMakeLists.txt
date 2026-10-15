cmake_minimum_required(VERSION 3.15)
project(ProcessExecutionEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src vendor)

set(SOURCES
    src/main.cpp
    src/http_client.cpp
    src/ipc.cpp
    src/job_command.cpp
    src/worker_proc.cpp
    src/dispatcher.cpp
    src/pg_client.cpp
    src/config.cpp
)

# Platform layer: one implementation per OS.
if (WIN32)
    list(APPEND SOURCES src/platform_win.cpp src/metrics_win.cpp)
else()
    list(APPEND SOURCES src/platform_posix.cpp src/metrics_posix.cpp)
endif()

add_executable(engine ${SOURCES})

# ---- libpq (dispatcher's direct PostgreSQL access) ----
find_path(PQ_INCLUDE_DIR libpq-fe.h
    PATH_SUFFIXES postgresql pgsql)
find_library(PQ_LIBRARY NAMES pq libpq)

if (NOT PQ_INCLUDE_DIR OR NOT PQ_LIBRARY)
    message(FATAL_ERROR
        "libpq not found.\n"
        "  MSYS2:         pacman -S mingw-w64-ucrt-x86_64-postgresql\n"
        "  Debian/Ubuntu: sudo apt install libpq-dev\n"
        "  Fedora:        sudo dnf install libpq-devel")
endif()

target_include_directories(engine PRIVATE ${PQ_INCLUDE_DIR})
target_link_libraries(engine ${PQ_LIBRARY})

# Keep the GCC runtime inside the binary so it starts without MSYS2 on PATH.
# libpq stays dynamic: only an import library is shipped on Windows.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_link_options(engine PRIVATE -static-libstdc++ -static-libgcc)
endif()

if (WIN32)
    target_link_libraries(engine ws2_32 crypt32 psapi)

    # Windows resolves DLLs from the executable's directory first.
    get_filename_component(PQ_BIN_DIR "${PQ_LIBRARY}" DIRECTORY)
    get_filename_component(PQ_BIN_DIR "${PQ_BIN_DIR}/../bin" ABSOLUTE)
    foreach(DLL libpq.dll libcrypto-3-x64.dll libssl-3-x64.dll
                libintl-8.dll libiconv-2.dll libwinpthread-1.dll)
        if (EXISTS "${PQ_BIN_DIR}/${DLL}")
            add_custom_command(TARGET engine POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${PQ_BIN_DIR}/${DLL}" "$<TARGET_FILE_DIR:engine>")
        endif()
    endforeach()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(engine Threads::Threads)
endif()

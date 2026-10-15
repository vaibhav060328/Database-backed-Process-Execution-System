#!/usr/bin/env bash
#
# Linux / POSIX build for the C++ Process Execution Engine.
# Windows uses compile.bat; CMakeLists.txt builds either.
set -euo pipefail

cd "$(dirname "$0")"

echo "====================================================="
echo " Compiling C++ Process Execution Engine (POSIX)"
echo "====================================================="

SOURCES=(
    src/main.cpp
    src/http_client.cpp
    src/ipc.cpp
    src/job_command.cpp
    src/worker_proc.cpp
    src/dispatcher.cpp
    src/pg_client.cpp
    src/config.cpp
    src/platform_posix.cpp
    src/metrics_posix.cpp
)

# libpq: prefer pkg-config, fall back to the usual Debian/Ubuntu location.
if pkg-config --exists libpq 2>/dev/null; then
    PQ_CFLAGS=$(pkg-config --cflags libpq)
    PQ_LIBS=$(pkg-config --libs libpq)
elif [ -f /usr/include/postgresql/libpq-fe.h ]; then
    PQ_CFLAGS="-I/usr/include/postgresql"
    PQ_LIBS="-lpq"
else
    echo "[ERROR] libpq development headers not found."
    echo "        Debian/Ubuntu: sudo apt install libpq-dev"
    echo "        Fedora:        sudo dnf install libpq-devel"
    exit 1
fi

# -pthread is required by cpp-httplib and std::thread.
# No -lssl/-lcrypto: the engine only speaks plain http:// to the local backend;
# libpq brings its own TLS for the database connection.
# libstdc++/libgcc are linked statically so the binary runs on hosts without a
# matching GCC runtime; glibc stays dynamic (static glibc breaks getaddrinfo).
# shellcheck disable=SC2086
g++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -static-libstdc++ -static-libgcc \
    "${SOURCES[@]}" \
    -Ivendor -Isrc $PQ_CFLAGS \
    $PQ_LIBS \
    -o engine

echo "[SUCCESS] engine built successfully!"

@echo off
echo =====================================================
echo  Compiling C++ Process Execution Engine
echo =====================================================

g++ -std=c++17 -D_WIN32_WINNT=0x0A00 -O2 src\main.cpp src\db_client.cpp src\worker.cpp src\scheduler.cpp -Ivendor -Isrc -lws2_32 -lcrypt32 -lpsapi -o engine.exe

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] engine.exe built successfully!
) else (
    echo [ERROR] Compilation failed with error code %ERRORLEVEL%
)

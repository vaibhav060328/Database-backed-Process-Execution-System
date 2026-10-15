@echo off
echo =====================================================
echo  Compiling C++ Process Execution Engine
echo =====================================================

REM MSYS2 UCRT64 prefix. Override by setting PG_PREFIX before running.
if "%PG_PREFIX%"=="" set PG_PREFIX=C:\msys64\ucrt64

if not exist "%PG_PREFIX%\include\libpq-fe.h" (
    echo [ERROR] libpq-fe.h not found under %PG_PREFIX%
    echo         Install it with:
    echo           pacman -S mingw-w64-ucrt-x86_64-postgresql
    exit /b 1
)

REM libstdc++ and libgcc are linked statically so the binary runs without the
REM MSYS2 runtime on PATH. libpq cannot be: only an import library ships, so it
REM is linked dynamically and its DLLs are copied next to the exe below.
g++ -std=c++17 -D_WIN32_WINNT=0x0A00 -O2 -Wall -Wextra ^
    -static-libstdc++ -static-libgcc ^
    src\main.cpp src\http_client.cpp src\ipc.cpp src\job_command.cpp ^
    src\worker_proc.cpp src\dispatcher.cpp src\pg_client.cpp src\config.cpp ^
    src\platform_win.cpp src\metrics_win.cpp ^
    -Ivendor -Isrc -I"%PG_PREFIX%\include" ^
    -L"%PG_PREFIX%\lib" -lpq -lws2_32 -lcrypt32 -lpsapi ^
    -o engine.exe

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

REM Windows resolves DLLs from the executable's own directory first, so copying
REM them here makes the build runnable on a machine without MSYS2 on PATH.
for %%D in (libpq.dll libcrypto-3-x64.dll libssl-3-x64.dll libintl-8.dll libiconv-2.dll libwinpthread-1.dll) do (
    if exist "%PG_PREFIX%\bin\%%D" copy /Y "%PG_PREFIX%\bin\%%D" . >nul
)

echo [SUCCESS] engine.exe built successfully!

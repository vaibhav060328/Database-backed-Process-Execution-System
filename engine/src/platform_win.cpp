#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN

#include "platform.h"
#include <windows.h>
#include <filesystem>
#include <cstring>
#include <algorithm>

namespace fs = std::filesystem;

namespace {

bool iequals(const std::string& a, const char* b) {
    return _stricmp(a.c_str(), b) == 0;
}

bool istartsWith(const std::string& s, const char* p) {
    size_t n = std::strlen(p);
    return s.size() >= n && _strnicmp(s.c_str(), p, n) == 0;
}

/**
 * Keys never passed to a child process. Jobs run arbitrary uploaded code, so
 * anything resembling a credential must not appear in their environment.
 */
bool isSensitiveKey(const std::string& key) {
    static const char* exact[] = {
        "DATABASE_URL", "DATABASE_URL_DIRECT", "ENGINE_DATABASE_URL",
        "PGPASSWORD", "PGPASSFILE", "PGSERVICE",
    };
    for (const char* e : exact) {
        if (iequals(key, e)) return true;
    }
    return istartsWith(key, "AWS_") || istartsWith(key, "NEON_");
}

/**
 * Quote one argument for a Windows command line.
 *
 * Windows passes a single string and each program parses it itself, so the
 * caller must apply the CommandLineToArgvW rules: backslashes are literal
 * except when they precede a quote, where they must be doubled.
 */
std::string quoteArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }
    std::string out = "\"";
    for (size_t i = 0; i < arg.size(); ++i) {
        size_t slashes = 0;
        while (i < arg.size() && arg[i] == '\\') { ++slashes; ++i; }
        if (i == arg.size()) {
            out.append(slashes * 2, '\\'); // before the closing quote
            break;
        }
        if (arg[i] == '"') {
            out.append(slashes * 2 + 1, '\\');
        } else {
            out.append(slashes, '\\');
        }
        out.push_back(arg[i]);
    }
    out.push_back('"');
    return out;
}

} // namespace

namespace platform {

std::string exePath() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return std::string();
    return std::string(buf, n);
}

std::string exeDir() {
    const std::string p = exePath();
    if (p.empty()) return std::string();
    return fs::path(p).parent_path().string();
}

std::vector<char> buildChildEnvironment(const std::map<std::string, std::string>& extra) {
    // Collect inherited variables, skipping credentials and anything overridden.
    std::map<std::string, std::string> merged;

    LPCH block = GetEnvironmentStringsA();
    if (block != nullptr) {
        for (LPCH p = block; *p != '\0'; ) {
            std::string entry(p);
            p += entry.size() + 1;

            // Windows exposes drive-letter entries such as "=C:=C:\path". Skip them.
            if (entry.empty() || entry[0] == '=') continue;

            size_t eq = entry.find('=');
            if (eq == std::string::npos) continue;

            std::string key = entry.substr(0, eq);
            if (isSensitiveKey(key)) continue;

            merged[key] = entry.substr(eq + 1);
        }
        FreeEnvironmentStringsA(block);
    }

    // Job-supplied variables win over inherited ones.
    for (const auto& kv : extra) {
        if (kv.first.empty()) continue;
        merged[kv.first] = kv.second;
    }

    // CreateProcess expects the block sorted case-insensitively by key.
    std::vector<std::pair<std::string, std::string>> sorted(merged.begin(), merged.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return _stricmp(a.first.c_str(), b.first.c_str()) < 0; });

    std::vector<char> out;
    for (const auto& kv : sorted) {
        std::string entry = kv.first + "=" + kv.second;
        out.insert(out.end(), entry.begin(), entry.end());
        out.push_back('\0');
    }
    out.push_back('\0'); // terminating null for the block itself
    return out;
}

SpawnResult spawnProcess(const std::string& exePath,
                         const std::vector<std::string>& args,
                         const std::map<std::string, std::string>& envExtra,
                         const std::string& workingDir,
                         const SpawnLimits& limits) {
    SpawnResult r;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE; // pipe ends must be inheritable to reach the child

    HANDLE inR = NULL, inW = NULL, outR = NULL, outW = NULL, errR = NULL, errW = NULL;
    auto closeAll = [&]() {
        for (HANDLE* h : { &inR, &inW, &outR, &outW, &errR, &errW }) {
            if (*h) { CloseHandle(*h); *h = NULL; }
        }
    };

    if (!CreatePipe(&inR,  &inW,  &sa, 0) ||
        !CreatePipe(&outR, &outW, &sa, 0) ||
        !CreatePipe(&errR, &errW, &sa, 0)) {
        r.error = "CreatePipe failed: " + std::to_string(GetLastError());
        closeAll();
        return r;
    }

    // The parent's own ends must NOT be inherited, or the child would hold a
    // copy of the write end and the parent would never see EOF on read.
    SetHandleInformation(inW,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

    std::string cmdline = quoteArg(exePath);
    for (const auto& a : args) cmdline += " " + quoteArg(a);
    std::vector<char> cmdv(cmdline.begin(), cmdline.end());
    cmdv.push_back('\0');

    std::vector<char> env = buildChildEnvironment(envExtra);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = inR;
    si.hStdOutput = outW;
    si.hStdError  = errW;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    // With a memory cap the child starts SUSPENDED so it can be placed in the
    // Job Object before it executes a single instruction. Assigning afterwards
    // would leave a window in which it could already have allocated freely.
    const bool wantLimits = limits.memoryMb > 0;
    DWORD flags = CREATE_NO_WINDOW;
    if (wantLimits) flags |= CREATE_SUSPENDED;

    BOOL ok = CreateProcessA(
        NULL, cmdv.data(), NULL, NULL,
        TRUE,                 // inherit the pipe ends marked inheritable above
        flags,
        env.data(),
        workingDir.empty() ? NULL : workingDir.c_str(),
        &si, &pi);

    if (!ok) {
        r.error = "CreateProcess failed: " + std::to_string(GetLastError());
        closeAll();
        return r;
    }

    if (wantLimits) {
        HANDLE job = CreateJobObjectA(NULL, NULL);
        if (job != NULL) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
            ZeroMemory(&info, sizeof(info));
            info.BasicLimitInformation.LimitFlags =
                JOB_OBJECT_LIMIT_PROCESS_MEMORY |
                JOB_OBJECT_LIMIT_JOB_MEMORY |
                // Closing the job handle kills everything in it, so a job that
                // spawns its own children cannot leave orphans behind.
                JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            const SIZE_T bytes = static_cast<SIZE_T>(limits.memoryMb) * 1024u * 1024u;
            info.ProcessMemoryLimit = bytes;
            info.JobMemoryLimit     = bytes;

            if (SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                        &info, sizeof(info)) &&
                AssignProcessToJobObject(job, pi.hProcess)) {
                r.limits = static_cast<LimitHandle>(job);
            } else {
                CloseHandle(job);
            }
        }
        ResumeThread(pi.hThread);
    }

    // The child owns its ends now; the parent must drop them so that EOF works.
    CloseHandle(inR);  inR  = NULL;
    CloseHandle(outW); outW = NULL;
    CloseHandle(errW); errW = NULL;
    CloseHandle(pi.hThread);

    r.proc    = static_cast<ProcHandle>(pi.hProcess);
    r.stdinW  = static_cast<PipeHandle>(inW);
    r.stdoutR = static_cast<PipeHandle>(outR);
    r.stderrR = static_cast<PipeHandle>(errR);
    r.ok      = true;
    return r;
}

bool isAlive(ProcHandle p) {
    if (p == invalidProc()) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(static_cast<HANDLE>(p), &code)) return false;
    return code == STILL_ACTIVE;
}

bool tryGetExitCode(ProcHandle p, int& outCode) {
    if (p == invalidProc()) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(static_cast<HANDLE>(p), &code)) return false;
    if (code == STILL_ACTIVE) return false;
    outCode = static_cast<int>(code);
    return true;
}

int waitForExit(ProcHandle p) {
    if (p == invalidProc()) return -1;
    WaitForSingleObject(static_cast<HANDLE>(p), INFINITE);
    DWORD code = 0;
    if (!GetExitCodeProcess(static_cast<HANDLE>(p), &code)) return -1;
    return static_cast<int>(code);
}

void terminateProc(ProcHandle p) {
    if (p == invalidProc()) return;
    TerminateProcess(static_cast<HANDLE>(p), 1);
}

void closeProc(ProcHandle& p) {
    if (p == invalidProc()) return;
    CloseHandle(static_cast<HANDLE>(p));
    p = invalidProc();
}

int readPipe(PipeHandle h, char* buf, std::size_t len) {
    if (h == invalidPipe() || buf == nullptr || len == 0) return -1;

    // PeekNamedPipe first: anonymous pipes have no non-blocking mode, so an
    // unconditional ReadFile would block until the child writes something.
    DWORD avail = 0;
    if (!PeekNamedPipe(static_cast<HANDLE>(h), NULL, 0, NULL, &avail, NULL)) {
        return -1; // broken pipe: the child closed its end or exited
    }
    if (avail == 0) return 0;

    DWORD toRead = static_cast<DWORD>(len < avail ? len : avail);
    DWORD got = 0;
    if (!ReadFile(static_cast<HANDLE>(h), buf, toRead, &got, NULL)) return -1;
    return static_cast<int>(got);
}

bool writeAll(PipeHandle h, const std::string& data) {
    if (h == invalidPipe()) return false;
    const char* p = data.data();
    size_t left = data.size();
    while (left > 0) {
        DWORD wrote = 0;
        if (!WriteFile(static_cast<HANDLE>(h), p, static_cast<DWORD>(left), &wrote, NULL)) {
            return false;
        }
        if (wrote == 0) return false;
        p += wrote;
        left -= wrote;
    }
    return true;
}

void closePipe(PipeHandle& h) {
    if (h == invalidPipe()) return;
    CloseHandle(static_cast<HANDLE>(h));
    h = invalidPipe();
}

void closeLimits(LimitHandle& h) {
    if (h == invalidLimit()) return;
    // KILL_ON_JOB_CLOSE: this terminates anything still in the job, which is
    // how a job's own grandchildren get cleaned up.
    CloseHandle(static_cast<HANDLE>(h));
    h = invalidLimit();
}

PipeHandle stdinPipe() {
    return static_cast<PipeHandle>(GetStdHandle(STD_INPUT_HANDLE));
}

bool writeStdout(const std::string& data) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == NULL) return false;

    // Written through the Win32 handle rather than std::cout: the C++ stream
    // would apply text-mode translation and its own buffering, and the
    // dispatcher needs each protocol line to leave immediately and intact.
    const char* p = data.data();
    size_t left = data.size();
    while (left > 0) {
        DWORD wrote = 0;
        if (!WriteFile(h, p, static_cast<DWORD>(left), &wrote, NULL) || wrote == 0) return false;
        p += wrote;
        left -= wrote;
    }
    return true;
}

long long currentPid() {
    return static_cast<long long>(GetCurrentProcessId());
}

std::string hostName() {
    char buf[256];
    DWORD n = sizeof(buf);
    if (GetComputerNameA(buf, &n) && n > 0) return std::string(buf, n);
    return "unknown";
}

} // namespace platform

#endif // _WIN32

#ifndef _WIN32

#include "platform.h"
#include <unistd.h>
#include <limits.h>
#include <strings.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/resource.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/types.h>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <mutex>

extern char** environ;

namespace fs = std::filesystem;

namespace {

bool iequals(const std::string& a, const char* b) {
    return strcasecmp(a.c_str(), b) == 0;
}

bool istartsWith(const std::string& s, const char* p) {
    size_t n = std::strlen(p);
    return s.size() >= n && strncasecmp(s.c_str(), p, n) == 0;
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

} // namespace

namespace platform {

std::string exePath() {
    char buf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    buf[n] = '\0';
    return std::string(buf);
}

std::string exeDir() {
    const std::string p = exePath();
    if (p.empty()) return std::string();
    return fs::path(p).parent_path().string();
}

std::vector<char> buildChildEnvironment(const std::map<std::string, std::string>& extra) {
    std::map<std::string, std::string> merged;

    for (char** p = environ; p != nullptr && *p != nullptr; ++p) {
        std::string entry(*p);
        size_t eq = entry.find('=');
        if (eq == std::string::npos || eq == 0) continue;

        std::string key = entry.substr(0, eq);
        if (isSensitiveKey(key)) continue;

        merged[key] = entry.substr(eq + 1);
    }

    for (const auto& kv : extra) {
        if (kv.first.empty()) continue;
        merged[kv.first] = kv.second;
    }

    // Same "K=V\0...\0" layout as the Windows build. Phase 3 converts this to the
    // char*[] that execve wants; keeping one format avoids two code paths here.
    std::vector<char> out;
    for (const auto& kv : merged) {
        std::string entry = kv.first + "=" + kv.second;
        out.insert(out.end(), entry.begin(), entry.end());
        out.push_back('\0');
    }
    out.push_back('\0');
    return out;
}

SpawnResult spawnProcess(const std::string& exePath,
                         const std::vector<std::string>& args,
                         const std::map<std::string, std::string>& envExtra,
                         const std::string& workingDir,
                         const SpawnLimits& limits) {
    SpawnResult r;

    int inP[2] = {-1, -1}, outP[2] = {-1, -1}, errP[2] = {-1, -1};
    auto closeIf = [](int& fd) { if (fd >= 0) { ::close(fd); fd = -1; } };
    auto closeAll = [&]() {
        closeIf(inP[0]);  closeIf(inP[1]);
        closeIf(outP[0]); closeIf(outP[1]);
        closeIf(errP[0]); closeIf(errP[1]);
    };

    if (::pipe(inP) != 0 || ::pipe(outP) != 0 || ::pipe(errP) != 0) {
        r.error = std::string("pipe() failed: ") + std::strerror(errno);
        closeAll();
        return r;
    }

    // Build argv and envp BEFORE forking: between fork and exec only
    // async-signal-safe calls are legal, so no allocation is allowed there.
    std::vector<std::string> argvStore;
    argvStore.push_back(exePath);
    for (const auto& a : args) argvStore.push_back(a);

    std::vector<char*> argv;
    for (auto& s : argvStore) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    std::vector<char> envBlock = buildChildEnvironment(envExtra);
    std::vector<char*> envp;
    for (size_t i = 0; i < envBlock.size() && envBlock[i] != '\0'; ) {
        envp.push_back(&envBlock[i]);
        i += std::strlen(&envBlock[i]) + 1;
    }
    envp.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        r.error = std::string("fork() failed: ") + std::strerror(errno);
        closeAll();
        return r;
    }

    if (pid == 0) {
        // ---- child ----
        // Attach the pipes to the standard streams, then close every original
        // descriptor. Leaving a write end open here means the parent never sees
        // EOF on the matching read end.
        if (::dup2(inP[0],  STDIN_FILENO)  < 0) ::_exit(127);
        if (::dup2(outP[1], STDOUT_FILENO) < 0) ::_exit(127);
        if (::dup2(errP[1], STDERR_FILENO) < 0) ::_exit(127);

        ::close(inP[0]);  ::close(inP[1]);
        ::close(outP[0]); ::close(outP[1]);
        ::close(errP[0]); ::close(errP[1]);

        if (!workingDir.empty() && ::chdir(workingDir.c_str()) != 0) ::_exit(127);

        // Resource ceilings, applied after fork and before exec so they bind to
        // the job and not to the engine. RLIMIT_AS caps the address space, so
        // an allocation beyond it fails in the job rather than exhausting the
        // machine. setrlimit is async-signal-safe, so it is legal here.
        if (limits.memoryMb > 0) {
            struct rlimit rl;
            rl.rlim_cur = static_cast<rlim_t>(limits.memoryMb) * 1024u * 1024u;
            rl.rlim_max = rl.rlim_cur;
            ::setrlimit(RLIMIT_AS, &rl);
        }

        // Die with the parent.
        //
        // The Linux counterpart of a Windows Job Object with KILL_ON_JOB_CLOSE:
        // if the worker is killed, the kernel sends SIGKILL here instead of
        // leaving the job running forever as an orphan reparented to init.
#ifdef PR_SET_PDEATHSIG
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        // Guard against the race where the parent died between fork and here,
        // in which case the signal has already been missed.
        if (::getppid() == 1) ::_exit(127);
#endif

        // Restore default signal handling: the parent may have ignored signals
        // that the job should still receive.
        ::signal(SIGPIPE, SIG_DFL);
        ::signal(SIGINT,  SIG_DFL);
        ::signal(SIGTERM, SIG_DFL);

        // Publish the constructed environment, then exec with a PATH search.
        //
        // execve would NOT search PATH, so a bare "python3" or "node" could
        // never resolve. execvp does search, and it reads PATH from `environ`,
        // which is why environ is swapped first rather than passing envp.
        // Assigning a pointer is async-signal-safe, so it is legal here.
        environ = envp.data();
        ::execvp(exePath.c_str(), argv.data());

        // _exit, not exit: exit() would flush stdio buffers inherited from the
        // parent and duplicate whatever the parent had pending.
        ::_exit(127);
    }

    // ---- parent ----
    closeIf(inP[0]);
    closeIf(outP[1]);
    closeIf(errP[1]);

    // Reads must never block the dispatcher's drain loop.
    ::fcntl(outP[0], F_SETFL, ::fcntl(outP[0], F_GETFL, 0) | O_NONBLOCK);
    ::fcntl(errP[0], F_SETFL, ::fcntl(errP[0], F_GETFL, 0) | O_NONBLOCK);

    r.proc    = static_cast<ProcHandle>(pid);
    r.stdinW  = inP[1];
    r.stdoutR = outP[0];
    r.stderrR = errP[0];
    r.ok      = true;
    return r;
}

namespace {

/**
 * Exit statuses of children already reaped.
 *
 * A pid can only be waited on ONCE: waitpid reaps the zombie and the status is
 * gone. Since isAlive() and tryGetExitCode() may both be called - and in either
 * order - whichever reaps first records the result here so the other still sees
 * it. Without this, polling isAlive() would silently destroy the exit code.
 */
std::mutex g_reapedMutex;
std::map<pid_t, int> g_reaped;

int decodeStatus(int status) {
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

/** Reap if possible. Returns true when the child is finished. */
bool reapIfExited(pid_t pid, int& outCode) {
    {
        std::lock_guard<std::mutex> lock(g_reapedMutex);
        auto it = g_reaped.find(pid);
        if (it != g_reaped.end()) { outCode = it->second; return true; }
    }

    int status = 0;
    pid_t res;
    do { res = ::waitpid(pid, &status, WNOHANG); }
    while (res < 0 && errno == EINTR);

    if (res == 0) return false; // still running

    const int code = (res < 0) ? -1 : decodeStatus(status);
    {
        std::lock_guard<std::mutex> lock(g_reapedMutex);
        g_reaped[pid] = code;
    }
    outCode = code;
    return true;
}

} // namespace

bool isAlive(ProcHandle p) {
    if (p == invalidProc()) return false;
    int ignored = 0;
    return !reapIfExited(static_cast<pid_t>(p), ignored);
}

bool tryGetExitCode(ProcHandle p, int& outCode) {
    if (p == invalidProc()) return false;
    return reapIfExited(static_cast<pid_t>(p), outCode);
}

int waitForExit(ProcHandle p) {
    if (p == invalidProc()) return -1;
    const pid_t pid = static_cast<pid_t>(p);

    {
        std::lock_guard<std::mutex> lock(g_reapedMutex);
        auto it = g_reaped.find(pid);
        if (it != g_reaped.end()) return it->second;
    }

    int status = 0;
    pid_t res;
    do { res = ::waitpid(pid, &status, 0); }
    while (res < 0 && errno == EINTR);

    const int code = (res < 0) ? -1 : decodeStatus(status);
    {
        std::lock_guard<std::mutex> lock(g_reapedMutex);
        g_reaped[pid] = code;
    }
    return code;
}

void terminateProc(ProcHandle p) {
    if (p == invalidProc()) return;
    ::kill(static_cast<pid_t>(p), SIGKILL);
}

void closeProc(ProcHandle& p) {
    if (p == invalidProc()) return;
    const pid_t pid = static_cast<pid_t>(p);

    // No handle to release on POSIX, but the zombie must be reaped and the
    // cached status dropped so the table does not grow for the process lifetime.
    int ignored = 0;
    reapIfExited(pid, ignored);
    {
        std::lock_guard<std::mutex> lock(g_reapedMutex);
        g_reaped.erase(pid);
    }
    p = invalidProc();
}

int readPipe(PipeHandle h, char* buf, std::size_t len) {
    if (h == invalidPipe() || buf == nullptr || len == 0) return -1;
    for (;;) {
        const ssize_t n = ::read(static_cast<int>(h), buf, len);
        if (n > 0) return static_cast<int>(n);
        if (n == 0) return -1;                       // EOF: writer closed
        if (errno == EINTR) continue;                // interrupted: retry
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; // nothing yet
        return -1;
    }
}

bool writeAll(PipeHandle h, const std::string& data) {
    if (h == invalidPipe()) return false;
    const char* p = data.data();
    size_t left = data.size();
    while (left > 0) {
        const ssize_t n = ::write(static_cast<int>(h), p, left);
        if (n > 0) { p += n; left -= static_cast<size_t>(n); continue; }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

void closeLimits(LimitHandle& h) {
    // Nothing to release: POSIX rlimits are a property of the process itself,
    // set in the child before exec, not a handle the parent holds.
    h = invalidLimit();
}

void closePipe(PipeHandle& h) {
    if (h == invalidPipe()) return;
    ::close(static_cast<int>(h));
    h = invalidPipe();
}

PipeHandle stdinPipe() {
    // Switch to non-blocking once: the worker polls stdin between drains of its
    // job's output and must never block here.
    static bool configured = false;
    if (!configured) {
        ::fcntl(STDIN_FILENO, F_SETFL, ::fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
        configured = true;
    }
    return static_cast<PipeHandle>(STDIN_FILENO);
}

bool writeStdout(const std::string& data) {
    const char* p = data.data();
    size_t left = data.size();
    while (left > 0) {
        const ssize_t n = ::write(STDOUT_FILENO, p, left);
        if (n > 0) { p += n; left -= static_cast<size_t>(n); continue; }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

long long currentPid() {
    return static_cast<long long>(::getpid());
}

std::string hostName() {
    char buf[256];
    if (::gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return std::string(buf);
    }
    return "unknown";
}

} // namespace platform

#endif // !_WIN32

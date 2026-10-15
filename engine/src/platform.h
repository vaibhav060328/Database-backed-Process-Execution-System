#ifndef PLATFORM_H
#define PLATFORM_H

#include <string>
#include <map>
#include <vector>

/**
 * platform.h
 * Thin cross-platform layer. Grows over the multi-process migration; for now it
 * carries the two pieces needed by Phase 0 (executable location, child environment).
 */
namespace platform {

/**
 * Opaque handle to an OS process.
 *  - Windows: a HANDLE returned by CreateProcess (stored as void* so that
 *    windows.h is not pulled into every translation unit).
 *  - POSIX:   a pid_t.
 * invalidProc() means "no target"; callers treat that as the current process.
 */
#ifdef _WIN32
using ProcHandle = void*;
using PipeHandle = void*;
inline ProcHandle invalidProc() { return nullptr; }
inline PipeHandle invalidPipe() { return nullptr; }
#else
using ProcHandle = int;
using PipeHandle = int;
inline ProcHandle invalidProc() { return -1; }
inline PipeHandle invalidPipe() { return -1; }
#endif

/**
 * Resource ceilings applied to a child before it runs.
 *
 * This is the payoff of running jobs as separate processes: a thread cannot be
 * given its own memory cap, but a process can. A job that allocates without
 * bound is killed by the OS instead of taking the machine down.
 */
struct SpawnLimits {
    int memoryMb = 0; // 0 = no limit
};

#ifdef _WIN32
using LimitHandle = void*;  // Job Object HANDLE
#else
using LimitHandle = int;    // unused on POSIX; rlimits are set in the child
#endif
inline LimitHandle invalidLimit() {
#ifdef _WIN32
    return nullptr;
#else
    return -1;
#endif
}

/** A spawned child plus the parent-side ends of its three standard streams. */
struct SpawnResult {
    ProcHandle  proc    = invalidProc();
    PipeHandle  stdinW  = invalidPipe();  // parent writes -> child stdin
    PipeHandle  stdoutR = invalidPipe();  // parent reads  <- child stdout
    PipeHandle  stderrR = invalidPipe();  // parent reads  <- child stderr
    LimitHandle limits  = invalidLimit(); // Windows Job Object, if limits applied
    bool        ok      = false;
    std::string error;
};

/**
 * Release a limit handle.
 *
 * On Windows the Job Object is created with KILL_ON_JOB_CLOSE, so closing this
 * while the child still runs terminates it. Close it only after the child has
 * exited, or deliberately to kill the whole tree.
 */
void closeLimits(LimitHandle& h);

/**
 * Spawn a child with all three streams piped to the parent.
 *
 * The environment is built by buildChildEnvironment(envExtra): inherited minus
 * credentials, plus envExtra. It is never simply inherited.
 *
 * On failure, ok is false, error explains why, and no handles are returned.
 */
SpawnResult spawnProcess(const std::string& exePath,
                         const std::vector<std::string>& args,
                         const std::map<std::string, std::string>& envExtra,
                         const std::string& workingDir,
                         const SpawnLimits& limits = SpawnLimits{});

/** False once the child has exited. */
bool isAlive(ProcHandle p);

/** Non-blocking exit status. Returns false while the child is still running. */
bool tryGetExitCode(ProcHandle p, int& outCode);

/** Block until the child exits and return its status (-1 on error). */
int waitForExit(ProcHandle p);

/** Kill immediately. Safe to call on an already-dead child. */
void terminateProc(ProcHandle p);

/** Release the handle (Windows) / reap the zombie (POSIX). */
void closeProc(ProcHandle& p);

/**
 * Non-blocking read.
 *   >0  bytes read
 *    0  nothing available right now
 *   -1  pipe closed or errored
 *
 * Non-blocking is essential: the dispatcher drains every worker on every tick
 * and must never block on one quiet pipe while another fills up.
 */
int readPipe(PipeHandle h, char* buf, std::size_t len);

/** Write everything or fail. Returns false if the pipe is closed. */
bool writeAll(PipeHandle h, const std::string& data);

/** Close and invalidate. Safe on an already-invalid handle. */
void closePipe(PipeHandle& h);

/**
 * This process's own stdin, as a pipe handle usable with readPipe().
 *
 * A worker process must poll its stdin for cancel and shutdown messages while
 * simultaneously draining its job's output, so a blocking read is unusable.
 * Call once at startup; on POSIX this switches fd 0 to non-blocking mode.
 * Do not closePipe() the result.
 */
PipeHandle stdinPipe();

/** Write a protocol line to this process's stdout and flush it immediately. */
bool writeStdout(const std::string& data);

/** This process's id, for building unique worker names. */
long long currentPid();

/** This machine's hostname, or "unknown". Used to keep worker ids unique. */
std::string hostName();

/**
 * Absolute path of the directory containing the running executable.
 *
 * Deliberately NOT derived from argv[0] or the current working directory: the
 * engine must put its sandboxes in the same place regardless of where it was
 * launched from. Returns an empty string if the path cannot be determined.
 */
std::string exeDir();

/**
 * Absolute path of the running executable itself.
 *
 * The dispatcher spawns workers by re-running this same binary with
 * --role worker, so it must know its own path without trusting argv[0]
 * (which is whatever the caller passed, and may not be a path at all).
 */
std::string exePath();

/**
 * Build an environment block for a child process.
 *
 * Starts from the current process environment, removes anything that looks like
 * a credential (see isSensitiveKey in the platform sources), then applies `extra`
 * on top. Keys in `extra` override inherited ones.
 *
 * The returned buffer is in the native Windows format - "K=V\0K=V\0\0" - and is
 * passed straight to CreateProcess as lpEnvironment. The POSIX build returns the
 * same layout; platform_posix.cpp converts it to a char*[] for execve in Phase 3.
 * The vector owns the storage, so it must outlive the spawn call.
 */
std::vector<char> buildChildEnvironment(const std::map<std::string, std::string>& extra);

} // namespace platform

#endif // PLATFORM_H

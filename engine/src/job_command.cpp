#include "job_command.h"
#include "platform.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/**
 * Run a build command to completion, collecting stdout and stderr.
 *
 * Both pipes are drained in the same loop. Draining one fully and then the
 * other would deadlock: the compiler blocks writing to a full pipe the parent
 * is not reading, while the parent blocks waiting on the other stream.
 */
int runBuild(const std::string& exe, const std::vector<std::string>& args,
             const std::string& workingDir, std::string& output) {
    auto sp = platform::spawnProcess(exe, args, {}, workingDir);
    if (!sp.ok) {
        output += "Failed to start " + exe + ": " + sp.error + "\n";
        return -1;
    }

    char buf[4096];
    bool outOpen = true, errOpen = true;
    int exitCode = -1;
    bool exited = false;

    while (outOpen || errOpen || !exited) {
        bool progress = false;

        if (outOpen) {
            int n = platform::readPipe(sp.stdoutR, buf, sizeof(buf));
            if (n > 0)      { output.append(buf, n); progress = true; }
            else if (n < 0) { outOpen = false; }
        }
        if (errOpen) {
            int n = platform::readPipe(sp.stderrR, buf, sizeof(buf));
            if (n > 0)      { output.append(buf, n); progress = true; }
            else if (n < 0) { errOpen = false; }
        }
        if (!exited && platform::tryGetExitCode(sp.proc, exitCode)) {
            exited = true;
        }
        // Both pipes closed and the process is gone: nothing more can arrive.
        if (!outOpen && !errOpen && exited) break;
        if (!progress) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (!exited) exitCode = platform::waitForExit(sp.proc);

    platform::closePipe(sp.stdinW);
    platform::closePipe(sp.stdoutR);
    platform::closePipe(sp.stderrR);
    platform::closeProc(sp.proc);
    return exitCode;
}

/** Compile a single source file and return the path of the produced binary. */
bool compileTo(const std::string& compiler, const std::string& srcPath,
               const std::string& sandboxDir, std::string& buildLog,
               std::string& outBinary) {
#ifdef _WIN32
    const std::string binName = "binary.exe";
#else
    const std::string binName = "binary";
#endif
    const std::string binPath = (fs::path(sandboxDir) / binName).string();

    buildLog += "Compiling with " + compiler + "...\n";
    const int rc = runBuild(compiler, {"-O2", "-o", binPath, srcPath}, sandboxDir, buildLog);

    std::error_code ec;
    if (rc != 0 || !fs::exists(binPath, ec)) {
        buildLog += "Compilation failed (exit code " + std::to_string(rc) + ").\n";
        return false;
    }
    buildLog += "Compilation succeeded.\n";
    outBinary = binPath;
    return true;
}

} // namespace

std::vector<std::string> splitArgs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inSingle = false, inDouble = false, has = false;

    for (char c : line) {
        if (c == '\'' && !inDouble)      { inSingle = !inSingle; has = true; }
        else if (c == '"' && !inSingle)  { inDouble = !inDouble; has = true; }
        else if (!inSingle && !inDouble && (c == ' ' || c == '\t')) {
            if (has) { out.push_back(cur); cur.clear(); has = false; }
        } else {
            cur.push_back(c);
            has = true;
        }
    }
    if (has) out.push_back(cur);
    return out;
}

JobCommand buildJobCommand(const std::string& filePath,
                           const std::string& fileType,
                           const std::string& argsLine,
                           const std::string& sandboxDir,
                           std::string& buildLog) {
    JobCommand cmd;

    std::error_code ec;
    const std::string absPath = fs::absolute(filePath, ec).string();
    if (ec) {
        cmd.error = "Cannot resolve job file path: " + ec.message();
        return cmd;
    }

    std::string ext = lower(fileType);
    if (ext.empty()) ext = lower(fs::path(absPath).extension().string());

    const std::vector<std::string> userArgs = splitArgs(argsLine);
    auto withUserArgs = [&](std::vector<std::string> head) {
        head.insert(head.end(), userArgs.begin(), userArgs.end());
        return head;
    };

    if (ext == ".py") {
#ifdef _WIN32
        cmd.exe = "python";
#else
        cmd.exe = "python3";
#endif
        cmd.args = withUserArgs({absPath});
        cmd.ok = true;
    } else if (ext == ".js") {
        cmd.exe  = "node";
        cmd.args = withUserArgs({absPath});
        cmd.ok   = true;
    } else if (ext == ".sh") {
        cmd.exe  = "sh";
        cmd.args = withUserArgs({absPath});
        cmd.ok   = true;
    } else if (ext == ".bat" || ext == ".cmd") {
#ifdef _WIN32
        cmd.exe  = "cmd.exe";
        cmd.args = withUserArgs({"/c", absPath});
        cmd.ok   = true;
#else
        cmd.error = "Batch files are not supported on this platform";
#endif
    } else if (ext == ".ps1") {
#ifdef _WIN32
        cmd.exe  = "powershell.exe";
        cmd.args = withUserArgs({"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", absPath});
        cmd.ok   = true;
#else
        cmd.error = "PowerShell scripts are not supported on this platform";
#endif
    } else if (ext == ".c" || ext == ".cpp" || ext == ".cc") {
        const std::string compiler = (ext == ".c") ? "gcc" : "g++";
        std::string binary;
        if (compileTo(compiler, absPath, sandboxDir, buildLog, binary)) {
            cmd.exe  = binary;
            cmd.args = userArgs;
            cmd.ok   = true;
        } else {
            cmd.error = "Compilation failed";
        }
    } else {
        // No known interpreter: treat it as a directly executable file.
        cmd.exe  = absPath;
        cmd.args = userArgs;
        cmd.ok   = true;
    }

    return cmd;
}

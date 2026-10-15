#ifndef JOB_COMMAND_H
#define JOB_COMMAND_H

#include <string>
#include <vector>

/**
 * job_command.h
 * Decides how to run an uploaded file, and compiles it first when needed.
 *
 * Returns an executable plus an argument vector rather than a single command
 * string: the OS spawn APIs take argv, so building a string here would mean
 * quoting it only for the platform layer to parse it apart again - a known
 * source of bugs with paths containing spaces.
 */
struct JobCommand {
    std::string              exe;   // program to run (PATH is searched)
    std::vector<std::string> args;  // arguments, already split
    bool                     ok = false;
    std::string              error;
};

/**
 * Split a user-supplied argument line into separate arguments.
 *
 * Honours double and single quotes so that --msg "hello world" stays one
 * argument. Not a full shell parser: no expansion, globbing or substitution,
 * which is deliberate - job arguments must not be interpreted by a shell.
 */
std::vector<std::string> splitArgs(const std::string& line);

/**
 * Build the command for a job.
 *
 * `filePath`   absolute path to the downloaded file inside the sandbox
 * `fileType`   extension recorded on the job, e.g. ".py"; falls back to the
 *              extension of filePath when empty
 * `argsLine`   the job's raw argument string
 * `sandboxDir` working directory, and where a compiled binary is written
 * `buildLog`   receives compiler output when a compile step runs
 *
 * For .c and .cpp this invokes gcc/g++ and returns the compiled binary. A
 * compilation failure returns ok == false with the compiler output in buildLog,
 * so the caller can report it as job output rather than a generic error.
 */
JobCommand buildJobCommand(const std::string& filePath,
                           const std::string& fileType,
                           const std::string& argsLine,
                           const std::string& sandboxDir,
                           std::string& buildLog);

#endif // JOB_COMMAND_H

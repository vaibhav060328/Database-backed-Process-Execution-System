#ifndef IPC_H
#define IPC_H

#include <string>
#include <map>
#include <vector>
#include <cstddef>

/**
 * ipc.h
 * Newline-delimited JSON framing for the dispatcher <-> worker pipes.
 *
 * One message per line. The dispatcher writes to a worker's stdin and reads its
 * stdout; a worker's stderr carries plain diagnostics, never protocol.
 *
 * Two invariants the rest of the engine depends on:
 *  - A worker's stdout carries protocol ONLY. Any stray std::cout corrupts it.
 *  - A job's own output never reaches the worker's stdout directly; it is
 *    captured from the job's pipe and re-emitted as "log" messages.
 */
namespace ipc {

// Dispatcher -> worker
constexpr const char* kAssign   = "assign";
constexpr const char* kCancel   = "cancel";
constexpr const char* kShutdown = "shutdown";

// Worker -> dispatcher
constexpr const char* kReady    = "ready";
constexpr const char* kStarted  = "started";
constexpr const char* kLog      = "log";
constexpr const char* kMetrics  = "metrics";
constexpr const char* kFinished = "finished";
constexpr const char* kError    = "error";

/** A job handed to a worker. The worker is told where to fetch the file, not given it. */
struct Assignment {
    int         jobId = 0;
    std::string fileName;
    std::string fileType;
    std::string args;
    std::string fileUrl;
    std::map<std::string, std::string> env;
};

/** A parsed inbound message. `type` is empty when the line could not be parsed. */
struct Message {
    std::string type;
    int         jobId    = 0;
    std::string line;      // kLog
    std::string level;     // kLog
    std::string workerId;  // kReady
    long long   pid   = 0; // kReady
    double      cpu   = 0; // kMetrics
    double      memMb = 0; // kMetrics
    int         exitCode = 0;   // kFinished
    std::string timestamp;      // kStarted / kFinished
    std::string message;        // kError
    Assignment  assignment;     // kAssign
    std::string rawError;       // set when parsing failed
};

/**
 * Reassembles complete lines from arbitrarily chunked pipe reads.
 *
 * A single read can land mid-line, or carry several lines at once, so partial
 * data is held until a '\n' arrives. Lines longer than maxLineBytes are force
 * emitted rather than growing the buffer without bound - a job that prints
 * megabytes without a newline must not exhaust the worker's memory.
 */
class LineReader {
public:
    explicit LineReader(std::size_t maxLineBytes = 1024 * 1024);

    /** Append raw bytes; returns every complete line now available. */
    std::vector<std::string> feed(const char* data, std::size_t len);

    /** Whatever remains after the stream closes (an unterminated final line). */
    std::string flush();

private:
    std::string buffer_;
    std::size_t maxLineBytes_;
};

/** Parse one NDJSON line. Never throws; check `type` for emptiness. */
Message parse(const std::string& line);

// Builders. Each returns one complete line including the trailing newline.
// Text from a job is sanitised: invalid UTF-8 is replaced rather than throwing,
// because a child process can emit arbitrary bytes on stdout.
std::string makeAssign(const Assignment& a);
std::string makeCancel(int jobId);
std::string makeShutdown();
std::string makeReady(const std::string& workerId, long long pid);
std::string makeStarted(int jobId, const std::string& startedAt);
std::string makeLog(int jobId, const std::string& line, const std::string& level);
std::string makeMetrics(int jobId, double cpu, double memMb);
std::string makeFinished(int jobId, int exitCode, const std::string& finishedAt);
std::string makeError(int jobId, const std::string& message);

} // namespace ipc

#endif // IPC_H

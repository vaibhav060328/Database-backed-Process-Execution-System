#include "ipc.h"
#include "../vendor/json.hpp"

using json = nlohmann::json;

namespace {

/**
 * Serialise to one line.
 *
 * error_handler_t::replace matters: job output is arbitrary bytes, and a child
 * printing CP-1252 or raw binary produces invalid UTF-8. nlohmann's default
 * handler THROWS on that, which would kill the worker over a job's output.
 * Replacing the bad sequence with U+FFFD keeps the pipe alive.
 */
std::string line(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
}

std::string getStr(const json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) return std::string();
    if (j[key].is_string()) return j[key].get<std::string>();
    return j[key].dump();
}

int getInt(const json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) return 0;
    if (j[key].is_number()) return j[key].get<int>();
    if (j[key].is_string()) { try { return std::stoi(j[key].get<std::string>()); } catch (...) {} }
    return 0;
}

double getDbl(const json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null() || !j[key].is_number()) return 0.0;
    return j[key].get<double>();
}

} // namespace

namespace ipc {

LineReader::LineReader(std::size_t maxLineBytes) : maxLineBytes_(maxLineBytes) {}

std::vector<std::string> LineReader::feed(const char* data, std::size_t len) {
    std::vector<std::string> out;
    if (data == nullptr || len == 0) return out;

    buffer_.append(data, len);

    std::size_t pos;
    while ((pos = buffer_.find('\n')) != std::string::npos) {
        std::string one = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 1);
        if (!one.empty() && one.back() == '\r') one.pop_back(); // tolerate CRLF
        out.push_back(std::move(one));
    }

    // A producer that never emits '\n' must not grow this buffer without bound.
    if (buffer_.size() > maxLineBytes_) {
        out.push_back(buffer_.substr(0, maxLineBytes_));
        buffer_.erase(0, maxLineBytes_);
    }
    return out;
}

std::string LineReader::flush() {
    std::string rest;
    rest.swap(buffer_);
    if (!rest.empty() && rest.back() == '\r') rest.pop_back();
    return rest;
}

Message parse(const std::string& text) {
    Message m;
    if (text.empty()) return m;

    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        m.rawError = e.what();
        return m; // type stays empty: caller treats this as unparseable
    }
    if (!j.is_object()) {
        m.rawError = "message is not a JSON object";
        return m;
    }

    m.type      = getStr(j, "type");
    m.jobId     = getInt(j, "jobId");
    m.line      = getStr(j, "line");
    m.level     = getStr(j, "level");
    m.workerId  = getStr(j, "workerId");
    m.cpu       = getDbl(j, "cpu");
    m.memMb     = getDbl(j, "memMb");
    m.exitCode  = getInt(j, "exitCode");
    m.message   = getStr(j, "message");

    if (j.contains("pid") && j["pid"].is_number()) m.pid = j["pid"].get<long long>();

    // kStarted uses startedAt, kFinished uses finishedAt; both land in timestamp.
    if (j.contains("startedAt"))  m.timestamp = getStr(j, "startedAt");
    if (j.contains("finishedAt")) m.timestamp = getStr(j, "finishedAt");

    if (m.type == kAssign) {
        m.assignment.jobId    = m.jobId;
        m.assignment.fileName = getStr(j, "fileName");
        m.assignment.fileType = getStr(j, "fileType");
        m.assignment.args     = getStr(j, "args");
        m.assignment.fileUrl  = getStr(j, "fileUrl");
        if (j.contains("env") && j["env"].is_object()) {
            for (auto& kv : j["env"].items()) {
                m.assignment.env[kv.key()] =
                    kv.value().is_string() ? kv.value().get<std::string>() : kv.value().dump();
            }
        }
    }
    return m;
}

std::string makeAssign(const Assignment& a) {
    json j;
    j["type"]     = kAssign;
    j["jobId"]    = a.jobId;
    j["fileName"] = a.fileName;
    j["fileType"] = a.fileType;
    j["args"]     = a.args;
    j["fileUrl"]  = a.fileUrl;
    j["env"]      = json::object();
    for (const auto& kv : a.env) j["env"][kv.first] = kv.second;
    return line(j);
}

std::string makeCancel(int jobId) {
    json j; j["type"] = kCancel; j["jobId"] = jobId; return line(j);
}

std::string makeShutdown() {
    json j; j["type"] = kShutdown; return line(j);
}

std::string makeReady(const std::string& workerId, long long pid) {
    json j; j["type"] = kReady; j["workerId"] = workerId; j["pid"] = pid; return line(j);
}

std::string makeStarted(int jobId, const std::string& startedAt) {
    json j; j["type"] = kStarted; j["jobId"] = jobId; j["startedAt"] = startedAt; return line(j);
}

std::string makeLog(int jobId, const std::string& text, const std::string& level) {
    json j;
    j["type"]  = kLog;
    j["jobId"] = jobId;
    j["line"]  = text;   // escaped by the serialiser; stays one physical line
    j["level"] = level.empty() ? "info" : level;
    return line(j);
}

std::string makeMetrics(int jobId, double cpu, double memMb) {
    json j; j["type"] = kMetrics; j["jobId"] = jobId; j["cpu"] = cpu; j["memMb"] = memMb;
    return line(j);
}

std::string makeFinished(int jobId, int exitCode, const std::string& finishedAt) {
    json j;
    j["type"]       = kFinished;
    j["jobId"]      = jobId;
    j["exitCode"]   = exitCode;
    j["finishedAt"] = finishedAt;
    return line(j);
}

std::string makeError(int jobId, const std::string& message) {
    json j; j["type"] = kError; j["jobId"] = jobId; j["message"] = message; return line(j);
}

} // namespace ipc

#include "pg_client.h"
#include "../vendor/json.hpp"

#include <libpq-fe.h>
#include <cstring>
#include <iostream>
#include <utility>

using json = nlohmann::json;

struct PgClient::Impl {
    PGconn* conn = nullptr;
};

namespace {

/** RAII for PGresult: every exec path must clear, including the error paths. */
class Result {
public:
    explicit Result(PGresult* r) : r_(r) {}
    ~Result() { if (r_) PQclear(r_); }
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    PGresult* get() const { return r_; }
    operator bool() const { return r_ != nullptr; }
private:
    PGresult* r_;
};

/**
 * Locate a column by name, tolerating camelCase.
 *
 * PQfnumber treats its argument like an SQL identifier: an unquoted name is
 * DOWNCASED before matching. Sequelize created "fileName", "fileType" and
 * "storageKey" as quoted camelCase columns, so PQfnumber("fileName") looks for
 * "filename", finds nothing, and returns -1. Every such field then silently
 * read as an empty string - which is how a .js job ended up with no extension
 * and was executed as a native binary.
 */
int fieldIndex(PGresult* r, const char* name) {
    const int c = PQfnumber(r, name);
    if (c >= 0) return c;
    const std::string quoted = std::string("\"") + name + "\"";
    return PQfnumber(r, quoted.c_str());
}

std::string colText(PGresult* r, int row, const char* name) {
    const int c = fieldIndex(r, name);
    if (c < 0 || PQgetisnull(r, row, c)) return std::string();
    return std::string(PQgetvalue(r, row, c));
}

int colInt(PGresult* r, int row, const char* name, int fallback = 0) {
    const std::string s = colText(r, row, name);
    if (s.empty()) return fallback;
    try { return std::stoi(s); } catch (...) { return fallback; }
}

/** Build a Job from a claim result row. */
Job jobFromRow(PGresult* r, int row) {
    Job j;
    j.id         = colInt(r, row, "id");
    j.name       = colText(r, row, "name");
    j.fileName   = colText(r, row, "fileName");
    j.fileType   = colText(r, row, "fileType");
    j.args       = colText(r, row, "args");
    j.storageKey = colText(r, row, "storageKey");
    j.status     = "running";

    // env is a TEXT column holding a JSON object. Bad JSON must not fail the
    // job; an empty environment is the safe fallback.
    const std::string env = colText(r, row, "env");
    if (!env.empty()) {
        try {
            auto parsed = json::parse(env);
            if (parsed.is_object()) {
                for (auto& kv : parsed.items()) {
                    j.env[kv.key()] = kv.value().is_string()
                                          ? kv.value().get<std::string>()
                                          : kv.value().dump();
                }
            }
        } catch (...) {
            // ignore malformed env
        }
    }
    return j;
}

} // namespace

PgClient::PgClient(std::string connInfo)
    : impl_(new Impl()), connInfo_(std::move(connInfo)) {}

PgClient::~PgClient() {
    if (impl_) {
        if (impl_->conn) PQfinish(impl_->conn);
        delete impl_;
    }
}

bool PgClient::connect() {
    if (impl_->conn) { PQfinish(impl_->conn); impl_->conn = nullptr; }

    if (connInfo_.empty()) {
        lastError_ = "ENGINE_DATABASE_URL is not set";
        return false;
    }

    impl_->conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(impl_->conn) != CONNECTION_OK) {
        lastError_ = PQerrorMessage(impl_->conn) ? PQerrorMessage(impl_->conn) : "connection failed";
        PQfinish(impl_->conn);
        impl_->conn = nullptr;
        return false;
    }
    lastError_.clear();
    return true;
}

bool PgClient::isConnected() const {
    return impl_->conn != nullptr && PQstatus(impl_->conn) == CONNECTION_OK;
}

bool PgClient::ensureConnected() {
    if (isConnected()) return true;
    if (impl_->conn) {
        PQreset(impl_->conn);
        if (PQstatus(impl_->conn) == CONNECTION_OK) { lastError_.clear(); return true; }
    }
    return connect();
}

std::optional<Job> PgClient::claimNextJob(const std::string& workerId) {
    lastClaimFailed_ = false;
    if (!ensureConnected()) { lastClaimFailed_ = true; return std::nullopt; }

    // The whole claim is ONE statement, so it is one implicit transaction.
    //  - FOR UPDATE takes a row lock, so no other claimer can take this row.
    //  - SKIP LOCKED makes a competing claimer step over locked rows rather
    //    than blocking behind them, which is what makes this parallel.
    //  - The subquery is required: FOR UPDATE is not allowed directly in UPDATE.
    //  - RETURNING makes claim-and-read atomic, leaving no window in between.
    static const char* kSql =
        "UPDATE jobs "
        "SET status = 'running', \"workerId\" = $1, \"startedAt\" = NOW(), \"updatedAt\" = NOW() "
        "WHERE id = ( "
        "  SELECT id FROM jobs "
        "  WHERE status = 'queued' "
        "  ORDER BY \"createdAt\" "
        "  FOR UPDATE SKIP LOCKED "
        "  LIMIT 1 "
        ") "
        "RETURNING id, name, \"fileName\", \"fileType\", args, env, \"storageKey\"";

    const char* params[1] = { workerId.c_str() };
    Result res(PQexecParams(impl_->conn, kSql, 1, nullptr, params, nullptr, nullptr, 0));

    if (!res || PQresultStatus(res.get()) != PGRES_TUPLES_OK) {
        lastError_ = PQerrorMessage(impl_->conn);
        lastClaimFailed_ = true;
        return std::nullopt;
    }
    if (PQntuples(res.get()) == 0) return std::nullopt; // queue empty: normal

    return jobFromRow(res.get(), 0);
}

bool PgClient::updateJobStatus(int jobId,
                               const std::string& status,
                               std::optional<int> exitCode,
                               const std::string& startedAt,
                               const std::string& finishedAt) {
    if (!ensureConnected()) return false;

    // COALESCE leaves a column untouched when its parameter is NULL, so one
    // statement serves every combination the caller might supply.
    static const char* kSql =
        "UPDATE jobs SET "
        "  status       = COALESCE($2, status), "
        "  \"exitCode\"   = COALESCE($3::int, \"exitCode\"), "
        "  \"startedAt\"  = COALESCE($4::timestamptz, \"startedAt\"), "
        "  \"finishedAt\" = COALESCE($5::timestamptz, \"finishedAt\"), "
        "  \"updatedAt\"  = NOW() "
        "WHERE id = $1";

    const std::string idStr   = std::to_string(jobId);
    const std::string codeStr = exitCode.has_value() ? std::to_string(*exitCode) : std::string();

    const char* params[5] = {
        idStr.c_str(),
        status.empty()     ? nullptr : status.c_str(),
        exitCode.has_value() ? codeStr.c_str() : nullptr,
        startedAt.empty()  ? nullptr : startedAt.c_str(),
        finishedAt.empty() ? nullptr : finishedAt.c_str(),
    };

    Result res(PQexecParams(impl_->conn, kSql, 5, nullptr, params, nullptr, nullptr, 0));
    if (!res || PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        lastError_ = PQerrorMessage(impl_->conn);
        return false;
    }
    return true;
}

bool PgClient::insertJobLog(int jobId, const std::string& line, const std::string& level) {
    if (!ensureConnected()) return false;

    static const char* kSql =
        "INSERT INTO job_logs (\"jobId\", line, level, \"timestamp\") "
        "VALUES ($1::int, $2, $3::\"enum_job_logs_level\", NOW())";

    const std::string idStr = std::to_string(jobId);
    const std::string lvl   = level.empty() ? "info" : level;

    const char* params[3] = { idStr.c_str(), line.c_str(), lvl.c_str() };
    Result res(PQexecParams(impl_->conn, kSql, 3, nullptr, params, nullptr, nullptr, 0));
    if (!res || PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        lastError_ = PQerrorMessage(impl_->conn);
        return false;
    }
    return true;
}

bool PgClient::upsertWorker(const std::string& id,
                            const std::string& status,
                            double cpu,
                            double memoryMb,
                            std::optional<int> currentJobId) {
    if (!ensureConnected()) return false;

    static const char* kSql =
        "INSERT INTO workers (id, name, host, status, cpu, memory, \"currentJobId\", "
        "                     \"lastHeartbeat\", \"createdAt\", \"updatedAt\") "
        "VALUES ($1, $1, 'localhost', $2::\"enum_workers_status\", $3::float, $4::float, "
        "        $5::int, NOW(), NOW(), NOW()) "
        "ON CONFLICT (id) DO UPDATE SET "
        "  status = EXCLUDED.status, "
        "  cpu = EXCLUDED.cpu, "
        "  memory = EXCLUDED.memory, "
        "  \"currentJobId\" = EXCLUDED.\"currentJobId\", "
        "  \"lastHeartbeat\" = NOW(), "
        "  \"updatedAt\" = NOW()";

    const std::string cpuStr = std::to_string(cpu);
    const std::string memStr = std::to_string(memoryMb);
    const std::string jobStr = currentJobId.has_value() ? std::to_string(*currentJobId) : std::string();

    const char* params[5] = {
        id.c_str(),
        status.c_str(),
        cpuStr.c_str(),
        memStr.c_str(),
        currentJobId.has_value() ? jobStr.c_str() : nullptr,
    };

    Result res(PQexecParams(impl_->conn, kSql, 5, nullptr, params, nullptr, nullptr, 0));
    if (!res || PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        lastError_ = PQerrorMessage(impl_->conn);
        return false;
    }
    return true;
}

std::vector<int> PgClient::fetchCancelledJobs(const std::vector<std::string>& workerIds) {
    std::vector<int> out;
    if (workerIds.empty() || !ensureConnected()) return out;

    // Build a text array literal for = ANY($1). Worker ids are generated by the
    // dispatcher, but this still goes through a bound parameter rather than
    // being spliced into the statement.
    std::string arr = "{";
    for (size_t i = 0; i < workerIds.size(); ++i) {
        if (i) arr += ",";
        arr += "\"" + workerIds[i] + "\"";
    }
    arr += "}";

    static const char* kSql =
        "SELECT id FROM jobs WHERE status = 'cancelled' AND \"workerId\" = ANY($1::text[])";

    const char* params[1] = { arr.c_str() };
    Result res(PQexecParams(impl_->conn, kSql, 1, nullptr, params, nullptr, nullptr, 0));
    if (!res || PQresultStatus(res.get()) != PGRES_TUPLES_OK) {
        lastError_ = PQerrorMessage(impl_->conn);
        return out;
    }
    const int n = PQntuples(res.get());
    for (int i = 0; i < n; ++i) out.push_back(colInt(res.get(), i, "id"));
    return out;
}

int PgClient::reclaimStaleJobs(int staleSeconds, const std::string& reason) {
    if (!ensureConnected()) return -1;
    if (staleSeconds <= 0) staleSeconds = 60;

    // Join to workers so the decision rests on the heartbeat, not on the id.
    // A job whose worker has not reported in staleSeconds has no process behind
    // it: the dispatcher that owned it is gone.
    static const char* kSql =
        "UPDATE jobs j SET status = 'failed', \"exitCode\" = -1, "
        "                  \"finishedAt\" = NOW(), \"updatedAt\" = NOW() "
        "FROM workers w "
        "WHERE j.\"workerId\" = w.id "
        "  AND j.status = 'running' "
        "  AND w.\"lastHeartbeat\" < NOW() - ($1 || ' seconds')::interval "
        "RETURNING j.id";

    const std::string secs = std::to_string(staleSeconds);
    const char* params[1] = { secs.c_str() };
    Result res(PQexecParams(impl_->conn, kSql, 1, nullptr, params, nullptr, nullptr, 0));
    if (!res || PQresultStatus(res.get()) != PGRES_TUPLES_OK) {
        lastError_ = PQerrorMessage(impl_->conn);
        return -1;
    }

    const int n = PQntuples(res.get());
    for (int i = 0; i < n; ++i) {
        insertJobLog(colInt(res.get(), i, "id"), reason, "error");
    }
    return n;
}

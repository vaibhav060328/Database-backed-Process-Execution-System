#ifndef PG_CLIENT_H
#define PG_CLIENT_H

#include <string>
#include <vector>
#include <optional>
#include "job.h"

/**
 * pg_client.h
 * Direct PostgreSQL access for the DISPATCHER only.
 *
 * Worker processes never link against this and never receive the connection
 * string: they run untrusted uploaded code, so a credential in their
 * environment would be readable by that code.
 *
 * Every statement goes through PQexecParams with bound parameters. No SQL is
 * ever built by string concatenation.
 */
class PgClient {
public:
    explicit PgClient(std::string connInfo);
    ~PgClient();

    PgClient(const PgClient&) = delete;
    PgClient& operator=(const PgClient&) = delete;

    /** Open the connection. Safe to call again after a failure. */
    bool connect();

    bool isConnected() const;

    /**
     * Reconnect if the link has dropped. Neon suspends idle connections, so the
     * dispatcher calls this before each cycle rather than assuming liveness.
     */
    bool ensureConnected();

    /**
     * Atomically claim the oldest queued job for `workerId`.
     *
     * Runs a single UPDATE ... WHERE id = (SELECT ... FOR UPDATE SKIP LOCKED)
     * so two dispatchers can never take the same row: the row lock is held for
     * the duration of the statement and other claimers step over it rather than
     * blocking. Returns nullopt when the queue is empty, which is the normal
     * idle case and not an error.
     *
     * Requires a `workers` row for workerId to exist already - jobs."workerId"
     * is a foreign key to workers(id).
     */
    std::optional<Job> claimNextJob(const std::string& workerId);

    /**
     * True when the last claimNextJob() returned nullopt because of an error
     * rather than an empty queue.
     *
     * These two outcomes look identical to the caller but mean opposite things:
     * an empty queue is normal, a failed query means nothing will ever run.
     * Conflating them hid a missing column-level UPDATE grant behind a system
     * that simply sat idle.
     */
    bool lastClaimFailed() const { return lastClaimFailed_; }

    /** Update a job's terminal state. Empty optionals are left unchanged. */
    bool updateJobStatus(int jobId,
                         const std::string& status,
                         std::optional<int> exitCode,
                         const std::string& startedAt,
                         const std::string& finishedAt);

    bool insertJobLog(int jobId, const std::string& line, const std::string& level);

    /** Register or refresh a worker. Must run before that worker's first claim. */
    bool upsertWorker(const std::string& id,
                      const std::string& status,
                      double cpu,
                      double memoryMb,
                      std::optional<int> currentJobId);

    /** Job ids currently 'cancelled' in the database but still assigned to a worker. */
    std::vector<int> fetchCancelledJobs(const std::vector<std::string>& workerIds);

    /**
     * Release jobs whose worker has stopped reporting.
     *
     * Any job still 'running' against a worker whose lastHeartbeat is older
     * than `staleSeconds` has no process behind it and would otherwise sit in
     * 'running' forever.
     *
     * Keyed on the heartbeat rather than on a worker id: worker ids embed the
     * dispatcher's pid (so that two dispatchers on one host stay distinct), so
     * a restarted dispatcher never sees its predecessor's ids again and an
     * id-based sweep would never match. A live dispatcher heartbeats every 4
     * seconds, so a 60 second threshold cannot misfire on a healthy worker -
     * and because the rule is "stale", ANY running dispatcher cleans up after a
     * crashed one, not just a replacement on the same machine.
     *
     * Returns the number of jobs released, or -1 on error.
     */
    int reclaimStaleJobs(int staleSeconds, const std::string& reason);

    const std::string& lastError() const { return lastError_; }

private:
    struct Impl;
    Impl*       impl_;
    std::string connInfo_;
    std::string lastError_;
    bool        lastClaimFailed_ = false;
};

#endif // PG_CLIENT_H

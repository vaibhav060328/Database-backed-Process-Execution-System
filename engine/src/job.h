#ifndef JOB_H
#define JOB_H

#include <string>
#include <map>
#include <vector>

/**
 * Plain data for one job row.
 *
 * Built by pg_client from a claim result. The old fromJson() factory went with
 * db_client: nothing parses jobs out of JSON any more, because the dispatcher
 * reads them straight from PostgreSQL.
 */
struct Job {
    int id = 0;
    std::string name;
    std::string status;       // "queued", "running", "done", "failed", "cancelled"
    std::string storageKey;
    std::string fileUrl;
    std::string fileName;
    std::string fileType;
    std::string args;
    std::map<std::string, std::string> env;
    int exitCode = -1;
    std::string workerId;
    std::string startedAt;
    std::string finishedAt;
    std::string createdAt;
};

#endif // JOB_H

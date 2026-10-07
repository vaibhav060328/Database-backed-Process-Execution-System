#ifndef DB_CLIENT_H
#define DB_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include "job.h"

class DbClient {
public:
    DbClient(const std::string& host = "localhost", int port = 3000);
    ~DbClient();

    // Health / connectivity
    bool testConnection();

    // Job management
    std::vector<Job> fetchQueuedJobs(int limit = 10);
    bool getJobDetails(int jobId, Job& outJob);
    bool downloadJobFile(int jobId, const std::string& destFilePath);
    bool updateJobStatus(int jobId, const std::string& status, int exitCode = -1,
                         const std::string& workerId = "",
                         const std::string& startedAt = "",
                         const std::string& finishedAt = "");
    bool postJobLog(int jobId, const std::string& line, const std::string& level = "info");

    // Worker heartbeat & metrics
    bool sendHeartbeat(const std::string& workerId, const std::string& status,
                       double cpu, double memory, int currentJobId = 0);

private:
    std::string host_;
    int port_;
    std::string baseUrl_;
};

#endif // DB_CLIENT_H

#ifndef WORKER_H
#define WORKER_H

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include "job.h"
#include "db_client.h"

class Worker {
public:
    Worker(const std::string& workerId, std::shared_ptr<DbClient> dbClient, const std::string& baseDir = "worker_dirs");
    ~Worker();

    std::string getId() const { return workerId_; }
    std::string getStatus() const { return status_; }
    int getCurrentJobId() const { return currentJobId_; }
    bool isBusy() const { return busy_; }

    // Execution entry point
    bool executeJob(Job job);

    // Cancel currently running job
    void cancelCurrentJob();

    // Metrics
    double getCpuUsage();
    double getMemoryUsage();

private:
    std::string workerId_;
    std::shared_ptr<DbClient> dbClient_;
    std::string baseDir_;
    std::string status_; // "online", "busy", "offline"
    std::atomic<int> currentJobId_{0};
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancelled_{false};

    // Windows Process handle for active job
    void* activeProcessHandle_{nullptr};

    // Helper functions
    std::string prepareSandbox(int jobId);
    std::string buildCommandLine(const std::string& filePath, const std::string& fileType, const std::string& args, const std::string& sandboxDir);
    std::string getCurrentIsoTimestamp();
};

#endif // WORKER_H

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include "db_client.h"
#include "worker.h"

class Scheduler {
public:
    Scheduler(std::shared_ptr<DbClient> dbClient, int numWorkers = 2, const std::string& baseDir = "worker_dirs");
    ~Scheduler();

    void start();
    void stop();
    bool isRunning() const { return running_; }

private:
    std::shared_ptr<DbClient> dbClient_;
    int numWorkers_;
    std::string baseDir_;
    std::vector<std::shared_ptr<Worker>> workers_;

    std::atomic<bool> running_{false};
    std::thread dispatchThread_;
    std::thread heartbeatThread_;
    std::mutex workerMutex_;

    void runDispatchLoop();
    void runHeartbeatLoop();
};

#endif // SCHEDULER_H

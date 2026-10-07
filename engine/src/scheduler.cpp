#include "scheduler.h"
#include <iostream>
#include <chrono>
#include <set>

Scheduler::Scheduler(std::shared_ptr<DbClient> dbClient, int numWorkers, const std::string& baseDir)
    : dbClient_(dbClient), numWorkers_(numWorkers), baseDir_(baseDir) {
    for (int i = 1; i <= numWorkers_; ++i) {
        std::string workerId = "worker-" + std::to_string(i);
        workers_.push_back(std::make_shared<Worker>(workerId, dbClient_, baseDir_));
    }
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start() {
    if (running_) return;
    running_ = true;

    std::cout << "🚀 Scheduler started with " << workers_.size() << " worker threads." << std::endl;

    // Start heartbeat thread
    heartbeatThread_ = std::thread(&Scheduler::runHeartbeatLoop, this);

    // Start dispatch loop
    dispatchThread_ = std::thread(&Scheduler::runDispatchLoop, this);
}

void Scheduler::stop() {
    if (!running_) return;
    running_ = false;

    std::cout << "🛑 Stopping Scheduler..." << std::endl;

    if (dispatchThread_.joinable()) {
        dispatchThread_.join();
    }
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }

    // Cancel all running jobs
    for (auto& worker : workers_) {
        worker->cancelCurrentJob();
    }

    std::cout << "✅ Scheduler stopped cleanly." << std::endl;
}

void Scheduler::runHeartbeatLoop() {
    while (running_) {
        for (auto& worker : workers_) {
            double cpu = worker->getCpuUsage();
            double mem = worker->getMemoryUsage();
            std::string status = worker->getStatus();
            int currentJob = worker->getCurrentJobId();

            dbClient_->sendHeartbeat(worker->getId(), status, cpu, mem, currentJob);
        }

        // Sleep 4 seconds between heartbeats
        for (int i = 0; i < 40 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void Scheduler::runDispatchLoop() {
    std::set<int> claimedJobIds;

    while (running_) {
        try {
            // Find available idle workers
            std::vector<std::shared_ptr<Worker>> idleWorkers;
            {
                std::lock_guard<std::mutex> lock(workerMutex_);
                for (auto& worker : workers_) {
                    if (!worker->isBusy()) {
                        idleWorkers.push_back(worker);
                    }
                }
            }

            if (!idleWorkers.empty()) {
                // Fetch queued jobs
                std::vector<Job> queuedJobs = dbClient_->fetchQueuedJobs((int)idleWorkers.size() * 2);

                size_t workerIdx = 0;
                for (const auto& job : queuedJobs) {
                    if (workerIdx >= idleWorkers.size()) break;

                    if (claimedJobIds.find(job.id) != claimedJobIds.end()) {
                        continue; // Already being processed
                    }

                    auto targetWorker = idleWorkers[workerIdx++];
                    claimedJobIds.insert(job.id);

                    std::cout << "⚡ Assigning Job #" << job.id << " (" << job.name
                              << ") to [" << targetWorker->getId() << "]" << std::endl;

                    // Launch execution in detached background thread
                    std::thread([this, targetWorker, job, &claimedJobIds]() {
                        targetWorker->executeJob(job);
                        claimedJobIds.erase(job.id);
                    }).detach();
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[Scheduler Exception] " << e.what() << std::endl;
        }

        // Poll interval (1.5 seconds)
        for (int i = 0; i < 15 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

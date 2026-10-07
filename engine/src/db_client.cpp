#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#endif

#include "db_client.h"
#include "../vendor/httplib.h"
#include "../vendor/json.hpp"
#include <iostream>
#include <fstream>

using json = nlohmann::json;

DbClient::DbClient(const std::string& host, int port)
    : host_(host), port_(port) {
    baseUrl_ = "http://" + host_ + ":" + std::to_string(port_);
}

DbClient::~DbClient() {}

bool DbClient::testConnection() {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(5, 0);

        auto res = cli.Get("/api/health");
        if (res && res->status == 200) {
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[DbClient Error] testConnection failed: " << e.what() << std::endl;
    }
    return false;
}

std::vector<Job> DbClient::fetchQueuedJobs(int limit) {
    std::vector<Job> jobs;
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);

        std::string path = "/api/jobs?status=queued&limit=" + std::to_string(limit);
        auto res = cli.Get(path.c_str());

        if (res && res->status == 200) {
            auto data = json::parse(res->body);
            json items = json::array();

            if (data.is_array()) {
                items = data;
            } else if (data.contains("jobs") && data["jobs"].is_array()) {
                items = data["jobs"];
            }

            for (const auto& item : items) {
                jobs.push_back(Job::fromJson(item));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[DbClient Error] fetchQueuedJobs: " << e.what() << std::endl;
    }
    return jobs;
}

bool DbClient::getJobDetails(int jobId, Job& outJob) {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);

        std::string path = "/api/jobs/" + std::to_string(jobId);
        auto res = cli.Get(path.c_str());

        if (res && res->status == 200) {
            auto data = json::parse(res->body);
            outJob = Job::fromJson(data);
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[DbClient Error] getJobDetails: " << e.what() << std::endl;
    }
    return false;
}

bool DbClient::downloadJobFile(int jobId, const std::string& destFilePath) {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(60, 0);

        std::string path = "/api/jobs/" + std::to_string(jobId) + "/file";

        std::ofstream outFile(destFilePath, std::ios::binary);
        if (!outFile.is_open()) {
            std::cerr << "[DbClient Error] Cannot open destination file: " << destFilePath << std::endl;
            return false;
        }

        auto res = cli.Get(path.c_str(), [&](const char *data, size_t data_length) {
            outFile.write(data, data_length);
            return true;
        });

        outFile.close();

        if (res && res->status == 200) {
            return true;
        } else {
            std::cerr << "[DbClient Error] Download failed with status: "
                      << (res ? std::to_string(res->status) : "No Response") << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[DbClient Error] downloadJobFile: " << e.what() << std::endl;
    }
    return false;
}

bool DbClient::updateJobStatus(int jobId, const std::string& status, int exitCode,
                               const std::string& workerId,
                               const std::string& startedAt,
                               const std::string& finishedAt) {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);

        json payload;
        payload["status"] = status;
        if (exitCode != -1) {
            payload["exitCode"] = exitCode;
        }
        if (!workerId.empty()) {
            payload["workerId"] = workerId;
        }
        if (!startedAt.empty()) {
            payload["startedAt"] = startedAt;
        }
        if (!finishedAt.empty()) {
            payload["finishedAt"] = finishedAt;
        }

        std::string path = "/api/jobs/" + std::to_string(jobId) + "/status";
        auto res = cli.Patch(path.c_str(), payload.dump(), "application/json");

        if (res && (res->status == 200 || res->status == 204)) {
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[DbClient Error] updateJobStatus: " << e.what() << std::endl;
    }
    return false;
}

bool DbClient::postJobLog(int jobId, const std::string& line, const std::string& level) {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(5, 0);

        json payload;
        payload["line"] = line;
        payload["level"] = level;

        std::string path = "/api/jobs/" + std::to_string(jobId) + "/logs";
        auto res = cli.Post(path.c_str(), payload.dump(), "application/json");

        if (res && (res->status == 200 || res->status == 201)) {
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[DbClient Error] postJobLog: " << e.what() << std::endl;
    }
    return false;
}

bool DbClient::sendHeartbeat(const std::string& workerId, const std::string& status,
                             double cpu, double memory, int currentJobId) {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(5, 0);

        json payload;
        payload["id"] = workerId;
        payload["status"] = status;
        payload["cpu"] = cpu;
        payload["memory"] = memory;
        if (currentJobId > 0) {
            payload["currentJobId"] = currentJobId;
        } else {
            payload["currentJobId"] = nullptr;
        }

        auto res = cli.Post("/api/workers/heartbeat", payload.dump(), "application/json");
        if (res && res->status == 200) {
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[DbClient Error] sendHeartbeat: " << e.what() << std::endl;
    }
    return false;
}

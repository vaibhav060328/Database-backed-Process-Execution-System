#ifndef JOB_H
#define JOB_H

#include <string>
#include <map>
#include <vector>
#include "../vendor/json.hpp"

using json = nlohmann::json;

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

    static Job fromJson(const json& j) {
        Job job;
        if (j.contains("id") && !j["id"].is_null()) {
            job.id = j["id"].is_number() ? j["id"].get<int>() : std::stoi(j["id"].get<std::string>());
        }
        if (j.contains("name") && !j["name"].is_null()) {
            job.name = j["name"].get<std::string>();
        }
        if (j.contains("status") && !j["status"].is_null()) {
            job.status = j["status"].get<std::string>();
        }
        if (j.contains("storageKey") && !j["storageKey"].is_null()) {
            job.storageKey = j["storageKey"].get<std::string>();
        }
        if (j.contains("fileUrl") && !j["fileUrl"].is_null()) {
            job.fileUrl = j["fileUrl"].get<std::string>();
        }
        if (j.contains("fileName") && !j["fileName"].is_null()) {
            job.fileName = j["fileName"].get<std::string>();
        }
        if (j.contains("fileType") && !j["fileType"].is_null()) {
            job.fileType = j["fileType"].get<std::string>();
        }
        if (j.contains("args") && !j["args"].is_null()) {
            job.args = j["args"].get<std::string>();
        }
        if (j.contains("exitCode") && !j["exitCode"].is_null()) {
            job.exitCode = j["exitCode"].get<int>();
        }
        if (j.contains("workerId") && !j["workerId"].is_null()) {
            job.workerId = j["workerId"].get<std::string>();
        }
        if (j.contains("startedAt") && !j["startedAt"].is_null()) {
            job.startedAt = j["startedAt"].get<std::string>();
        }
        if (j.contains("finishedAt") && !j["finishedAt"].is_null()) {
            job.finishedAt = j["finishedAt"].get<std::string>();
        }
        if (j.contains("createdAt") && !j["createdAt"].is_null()) {
            job.createdAt = j["createdAt"].get<std::string>();
        }

        // Parse environment variables
        if (j.contains("env") && !j["env"].is_null()) {
            try {
                if (j["env"].is_string()) {
                    auto envJson = json::parse(j["env"].get<std::string>());
                    for (auto& [k, v] : envJson.items()) {
                        job.env[k] = v.is_string() ? v.get<std::string>() : v.dump();
                    }
                } else if (j["env"].is_object()) {
                    for (auto& [k, v] : j["env"].items()) {
                        job.env[k] = v.is_string() ? v.get<std::string>() : v.dump();
                    }
                }
            } catch (...) {
                // Ignore env parse failure
            }
        }

        return job;
    }
};

#endif // JOB_H

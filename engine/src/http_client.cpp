#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#endif

#include "http_client.h"
#include "../vendor/httplib.h"

#include <fstream>
#include <iostream>
#include <utility>

HttpClient::HttpClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

bool HttpClient::testConnection() {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(5, 0);

        auto res = cli.Get("/api/health");
        return res && res->status == 200;
    } catch (const std::exception& e) {
        std::cerr << "[HttpClient] testConnection failed: " << e.what() << std::endl;
    }
    return false;
}

bool HttpClient::downloadJobFile(int jobId, const std::string& destFilePath) {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(60, 0);

        std::ofstream outFile(destFilePath, std::ios::binary);
        if (!outFile.is_open()) {
            std::cerr << "[HttpClient] cannot open " << destFilePath << std::endl;
            return false;
        }

        const std::string path = "/api/jobs/" + std::to_string(jobId) + "/file";
        auto res = cli.Get(path.c_str(),
                           [&](const char* data, size_t len) {
                               outFile.write(data, len);
                               return true;
                           });
        outFile.close();

        if (res && res->status == 200) return true;

        std::cerr << "[HttpClient] download failed with status "
                  << (res ? std::to_string(res->status) : std::string("no response"))
                  << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[HttpClient] downloadJobFile failed: " << e.what() << std::endl;
    }
    return false;
}

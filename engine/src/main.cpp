#include <iostream>
#include <string>
#include <memory>
#include <csignal>
#include <thread>
#include <chrono>
#include "db_client.h"
#include "scheduler.h"

static std::atomic<bool> g_shutdownRequested{false};

void signalHandler(int signum) {
    std::cout << "\nCaught signal (" << signum << "). Initiating graceful engine shutdown..." << std::endl;
    g_shutdownRequested = true;
}

void printBanner(const std::string& host, int port, int workers, const std::string& dir) {
    std::cout << "=====================================================" << std::endl;
    std::cout << "   ⚙️  PROCESS EXECUTION SYSTEM - C++ ENGINE        " << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << " Target Backend : http://" << host << ":" << port << std::endl;
    std::cout << " Worker Threads : " << workers << std::endl;
    std::cout << " Sandbox Folder : " << dir << std::endl;
    std::cout << " Platform       : Windows x64 (MSYS2 / UCRT64)" << std::endl;
    std::cout << "=====================================================" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string host = "localhost";
    int port = 3000;
    int numWorkers = 2;
    std::string baseDir = "worker_dirs";

    // Parse CLI arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--workers" && i + 1 < argc) {
            numWorkers = std::stoi(argv[++i]);
        } else if (arg == "--dir" && i + 1 < argc) {
            baseDir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: engine.exe [options]\n"
                      << "Options:\n"
                      << "  --host <string>     Backend host (default: localhost)\n"
                      << "  --port <int>        Backend port (default: 3000)\n"
                      << "  --workers <int>     Number of concurrent workers (default: 2)\n"
                      << "  --dir <string>      Working sandbox directory (default: worker_dirs)\n";
            return 0;
        }
    }

    printBanner(host, port, numWorkers, baseDir);

    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    auto dbClient = std::make_shared<DbClient>(host, port);

    // Verify backend connectivity
    std::cout << "🔍 Connecting to Backend API..." << std::endl;
    while (!dbClient->testConnection() && !g_shutdownRequested) {
        std::cout << "⏳ Waiting for Backend API at http://" << host << ":" << port << " to be ready..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    if (g_shutdownRequested) {
        return 0;
    }

    std::cout << "✅ Connected to Backend API successfully!" << std::endl;

    // Start Scheduler
    Scheduler scheduler(dbClient, numWorkers, baseDir);
    scheduler.start();

    // Main loop waiting for shutdown
    while (!g_shutdownRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    scheduler.stop();
    std::cout << "👋 Engine terminated successfully." << std::endl;
    return 0;
}

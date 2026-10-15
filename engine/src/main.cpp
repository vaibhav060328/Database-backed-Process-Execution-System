#include <iostream>
#include <string>
#include <memory>
#include <csignal>
#include <thread>
#include <chrono>
#include <filesystem>
#include "http_client.h"
#include "platform.h"
#include "worker_proc.h"
#include "dispatcher.h"
#include "pg_client.h"
#include "config.h"

namespace fs = std::filesystem;

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
    std::string baseDir; // empty means "use the default next to the executable"
    std::string role = "dispatcher";
    std::string workerId;
    int jobTimeout = 0; // seconds; 0 disables
    bool keepSandbox = false;
    int maxMemoryMb = 0;

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
        } else if (arg == "--role" && i + 1 < argc) {
            role = argv[++i];
        } else if (arg == "--id" && i + 1 < argc) {
            workerId = argv[++i];
        } else if (arg == "--timeout" && i + 1 < argc) {
            jobTimeout = std::stoi(argv[++i]);
        } else if (arg == "--keep-sandbox") {
            keepSandbox = true;
        } else if (arg == "--max-memory" && i + 1 < argc) {
            maxMemoryMb = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: engine.exe [options]\n"
                      << "Options:\n"
                      << "  --role <string>     dispatcher (default) or worker\n"
                      << "  --host <string>     Backend host (default: localhost)\n"
                      << "  --port <int>        Backend port (default: 3000)\n"
                      << "  --workers <int>     Number of concurrent workers (default: 2)\n"
                      << "  --dir <string>      Working sandbox directory\n"
                      << "                      (default: worker_dirs next to engine.exe)\n"
                      << "  --id <string>       Worker id (--role worker only)\n"
                      << "  --timeout <int>     Kill a job after N seconds (0 = no limit)\n"
                      << "  --max-memory <int>  Per-job memory cap in MB (0 = no limit)\n"
                      << "  --keep-sandbox      Keep each job's directory after it finishes\n"
                      << "\n"
                      << "A worker process is normally spawned by the dispatcher, which\n"
                      << "talks to it over its stdin and stdout. Running one by hand is\n"
                      << "only useful for debugging.\n";
            return 0;
        }
    }

    if (role != "dispatcher" && role != "worker") {
        std::cerr << "Unknown --role '" << role << "'. Expected dispatcher or worker."
                  << std::endl;
        return 1;
    }

    // Resolve the sandbox root to an absolute path. The default sits beside the
    // executable rather than in the current directory, so launching from the repo
    // root and from engine/ no longer produces two separate sandbox trees.
    {
        std::error_code ec;
        if (baseDir.empty()) {
            std::string dir = platform::exeDir();
            if (dir.empty()) {
                std::cerr << "Could not determine the executable directory; "
                          << "pass --dir explicitly." << std::endl;
                return 1;
            }
            baseDir = (fs::path(dir) / "worker_dirs").string();
        } else {
            baseDir = fs::absolute(baseDir, ec).string();
            if (ec) {
                std::cerr << "Invalid --dir path: " << ec.message() << std::endl;
                return 1;
            }
        }
        fs::create_directories(baseDir, ec);
        if (ec) {
            std::cerr << "Could not create sandbox directory '" << baseDir
                      << "': " << ec.message() << std::endl;
            return 1;
        }
    }

    // A worker process never prints a banner or connects to anything on its own:
    // its stdout is the protocol stream, so any stray text would corrupt it.
    if (role == "worker") {
        if (workerId.empty()) {
            workerId = platform::hostName() + "-w" + std::to_string(platform::currentPid());
        }
        WorkerOptions wopts;
        wopts.workerId    = workerId;
        wopts.baseDir     = baseDir;
        wopts.backendHost = host;
        wopts.backendPort = port;
        wopts.keepSandbox = keepSandbox;
        wopts.maxMemoryMb = maxMemoryMb;
        return runWorkerProcess(wopts);
    }

    printBanner(host, port, numWorkers, baseDir);

    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Workers fetch job files over HTTP, so the backend must be reachable
    // before any of them is spawned.
    HttpClient httpProbe(host, port);
    std::cout << "🔍 Connecting to Backend API..." << std::endl;
    while (!httpProbe.testConnection() && !g_shutdownRequested) {
        std::cout << "⏳ Waiting for Backend API at http://" << host << ":" << port
                  << " to be ready..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    if (g_shutdownRequested) return 0;
    std::cout << "✅ Backend API reachable." << std::endl;

    // The dispatcher is the only component that touches PostgreSQL directly.
    const std::string connInfo = config::engineDatabaseUrl();
    if (connInfo.empty()) {
        std::cerr << "ENGINE_DATABASE_URL is not set.\n"
                  << "Copy engine/.env.example to engine/.env and fill it in "
                  << "(see PLAN_MULTIPROCESS.md section 3.1)." << std::endl;
        return 1;
    }

    PgClient db(connInfo);
    std::cout << "🔌 Connecting to PostgreSQL..." << std::endl;
    while (!db.connect() && !g_shutdownRequested) {
        std::cerr << "   " << db.lastError();
        std::cout << "   retrying in 3s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    if (g_shutdownRequested) return 0;
    std::cout << "✅ Connected to PostgreSQL as the engine role." << std::endl;

    DispatcherOptions dopts;
    dopts.numWorkers        = numWorkers;
    dopts.baseDir           = baseDir;
    dopts.backendHost       = host;
    dopts.backendPort       = port;
    dopts.jobTimeoutSeconds = jobTimeout;
    dopts.keepSandbox       = keepSandbox;
    dopts.maxMemoryMb       = maxMemoryMb;

    Dispatcher dispatcher(db, dopts);
    if (!dispatcher.start()) {
        std::cerr << "No worker processes could be started." << std::endl;
        return 1;
    }

    dispatcher.run(g_shutdownRequested);
    dispatcher.stop();

    std::cout << "👋 Engine terminated successfully." << std::endl;
    return 0;
}

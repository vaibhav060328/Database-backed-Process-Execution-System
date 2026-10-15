#include "config.h"
#include "platform.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

namespace config {

std::map<std::string, std::string> loadEnvFile(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream f(path);
    if (!f.is_open()) return out;

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (val.size() >= 2 &&
            ((val.front() == '"' && val.back() == '"') ||
             (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }
        if (!key.empty()) out[key] = val;
    }
    return out;
}

std::string engineDatabaseUrl() {
    if (const char* fromEnv = std::getenv("ENGINE_DATABASE_URL")) {
        if (*fromEnv) return fromEnv;
    }

    const std::string dir = platform::exeDir();
    if (dir.empty()) return std::string();

    const fs::path candidates[] = {
        fs::path(dir) / ".env",
        fs::path(dir) / ".." / "engine" / ".env",
        fs::path(dir) / ".." / ".env",
    };

    for (const auto& p : candidates) {
        auto vars = loadEnvFile(p.string());
        auto it = vars.find("ENGINE_DATABASE_URL");
        if (it != vars.end() && !it->second.empty()) return it->second;
    }
    return std::string();
}

} // namespace config

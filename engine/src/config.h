#ifndef CONFIG_H
#define CONFIG_H

#include <map>
#include <string>

/**
 * config.h
 * Minimal .env loading. The engine has no dependency manager, so rather than
 * pulling in a library this parses the handful of KEY=VALUE lines it needs.
 */
namespace config {

/**
 * Parse a .env file into a map. Blank lines and lines starting with '#' are
 * skipped, surrounding quotes are stripped, and a missing file yields an empty
 * map rather than an error.
 */
std::map<std::string, std::string> loadEnvFile(const std::string& path);

/**
 * Resolve the dispatcher's database URL.
 *
 * Order: the ENGINE_DATABASE_URL environment variable, then
 * ENGINE_DATABASE_URL in <exeDir>/.env, then the same key in <exeDir>/../engine/.env
 * so the binary also works when run from a build subdirectory.
 * Returns an empty string when nothing is configured.
 */
std::string engineDatabaseUrl();

} // namespace config

#endif // CONFIG_H

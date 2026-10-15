#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>

/**
 * http_client.h
 * The engine's only HTTP client, and all it does is fetch files.
 *
 * This replaces the old DbClient, which was named for a role it never had: it
 * opened no database connection, and once the dispatcher started speaking to
 * PostgreSQL directly through libpq, its job-queue, logging and heartbeat
 * methods became dead code. What remains is the one thing workers genuinely
 * need from the backend - downloading an uploaded job file, which is the only
 * component holding the object-storage credentials.
 *
 * Worker processes use downloadJobFile; the dispatcher uses testConnection at
 * startup to avoid spawning workers against a backend that is not up yet.
 * Neither endpoint requires authentication, so this does not weaken the rule
 * that workers hold no credentials.
 */
class HttpClient {
public:
    HttpClient(std::string host = "localhost", int port = 3000);

    /** GET /api/health - is the backend up? */
    bool testConnection();

    /**
     * GET /api/jobs/:id/file, streamed straight to disk.
     *
     * Streamed rather than buffered: uploads are capped at 100 MB and holding
     * one in memory per worker would be wasteful for no benefit.
     */
    bool downloadJobFile(int jobId, const std::string& destFilePath);

private:
    std::string host_;
    int         port_;
};

#endif // HTTP_CLIENT_H

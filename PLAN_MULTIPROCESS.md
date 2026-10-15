# Implementation plan — multi-process engine

Rewrite the C++ engine from *one process with a thread per job* to *a dispatcher process
supervising N worker processes*, with the dispatcher claiming jobs directly from PostgreSQL.

**Decisions taken:**

| Decision | Choice |
|---|---|
| Job claiming | Direct PostgreSQL from the dispatcher via `libpq`, using `FOR UPDATE SKIP LOCKED` |
| Dispatcher ↔ worker IPC | NDJSON over the worker's stdin/stdout pipes |
| Platforms | Windows **and** Linux |
| Credentials | Dispatcher only. Worker processes get no `DATABASE_URL`, ever |

---

## 0. Progress

| Phase | Status |
|---|---|
| 0 — de-risk | **done and verified** |
| 1 — pg_client + atomic claim | **done and verified** |
| 2 — real process metrics | **done and verified** |
| 3 — IPC + worker process | **done and verified end to end** |
| 4 — dispatcher | **done and verified** |
| 5 — live updates | **done and verified** |
| 6 — hardening | **done**, except a restricted OS user per worker |

### Acceptance results

| Test | Result |
|---|---|
| Two dispatchers, 30 jobs (§3.4) | 30 claims, **zero duplicates**, work spread over 4 workers |
| Full job through the real API | done, exit 0, output/args/env all delivered |
| Live updates | `running` and `done` seen over Socket.io; 12 log events streamed |
| Real metrics | 11.9% CPU (one core of eight) and 49.1 MB working set |
| Credential isolation | `DATABASE_URL` invisible in the job process |
| Cancellation | process terminated; job did **not** run to completion |
| Wall-clock timeout | job killed at the 10s limit |
| Memory cap | job killed at 200 MB against a 256 MB cap |
| Sandbox cleanup | `job_*` directories removed after completion |

Verified so far:

- Sandbox path now resolves from the executable, not the working directory —
  confirmed by running a copy from an unrelated folder (`engine/src/main.cpp`,
  `platform::exeDir`).
- Child environment is built explicitly: credentials (`DATABASE_URL`, `AWS_*`,
  `NEON_*`, `PG*`) are stripped, `PATH` survives, job variables override inherited
  ones, and the engine's own environment is no longer mutated.
- CPU and memory are real. A single-threaded child saturating one core on an
  8-core machine reported 12.4% (12.5% theoretical) and a 150 MB allocation
  reported 155.3 MB working set.
- **Added beyond the plan:** the engine is now linked with `-static`. It
  previously needed `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and
  `libwinpthread-1.dll` from the MSYS2 UCRT64 directory and failed to start with
  exit 127 anywhere those were not on `PATH`. It now depends only on
  `KERNEL32.dll` and `WS2_32.dll`.
- **Frontend contract change:** `workers.memory` is now the process working set
  in **MB**, not a system-wide percentage. `WorkerCard.jsx` was updated to match.
- IPC framing (`src/ipc.{h,cpp}`): 21 checks pass, covering round trips, split
  and CRLF line reassembly, malformed input, and two failure modes that would
  otherwise take a worker down — invalid UTF-8 in job output (nlohmann's default
  handler *throws*; the serialiser now replaces), and a job printing without ever
  emitting a newline (force-flushed at a cap instead of growing the buffer).
- Process/pipe layer (`platform::spawnProcess` and friends): 13 checks pass,
  including a real child round trip, non-blocking reads returning immediately,
  stderr staying off the protocol stream, and exit codes surviving repeated
  polling. **`DATABASE_URL` set in the parent was confirmed invisible to the
  child** — the credential boundary in §1 is now demonstrated, not asserted.
- Fixed while writing the POSIX layer: `waitpid(WNOHANG)` reaps, so a polling
  `isAlive()` would have silently destroyed the exit status before
  `tryGetExitCode()` could read it. Statuses are now cached on first reap.
- Second POSIX fix: the child exec'd with `execve`, which does **not** search
  `PATH`, so a bare `python3` or `node` could never have resolved. The child now
  publishes its constructed environment and uses `execvp`.
- Worker process mode (`engine --role worker`) runs a real job end to end:
  20 checks pass against a stub backend, covering the ready handshake, job
  download, execution, stdout and stderr relayed as log messages, quoted
  arguments surviving as single arguments, job environment variables arriving,
  exit code and timestamp reporting, and clean shutdown.
  **`DATABASE_URL` was confirmed absent two levels down** — set in the
  dispatcher-side parent, invisible in the job process itself.
- `buildCommandLine` moved out of `worker.cpp` into `src/job_command.{h,cpp}`
  and now returns an executable plus an argv vector instead of a command
  string. Compilation of `.c`/`.cpp` no longer shells out through `system()`:
  it spawns the compiler directly and captures its output, so build errors reach
  the job log instead of vanishing to a console nobody sees.
- **0.3 `engine_role` created and audited.** It holds `SELECT` on `jobs`,
  column-level `UPDATE` on exactly `status`, `workerId`, `startedAt`,
  `finishedAt`, `exitCode`, `INSERT` on `job_logs`, and read/write on `workers`.
  Verified refused: `DELETE` on either table, `UPDATE` of any other `jobs`
  column, `DROP TABLE`, `CREATE TABLE`, and reading `pg_authid`. No superuser,
  createrole, createdb, bypassrls or replication attributes, and no role
  memberships. Credentials live in `engine/.env` (gitignored);
  `engine/.env.example` is the committed template.
- **The claim statement in §3.3 was executed against the live database as
  `engine_role` and succeeded** (inside a rolled-back transaction, so nothing
  persisted).

### Bugs found by the tests, not by reading the code

1. **`PQfnumber` downcases unquoted column names.** Sequelize created
   `"fileName"`, `"fileType"` and `"storageKey"` as quoted camelCase, so every
   lookup returned -1 and read as an empty string. The worker then saw a `.js`
   job with no extension, treated it as a native binary, and `CreateProcess`
   failed with error 193. Fixed by retrying the lookup with a quoted identifier.
2. **The claim needed `UPDATE` on `"updatedAt"`**, which the original grant
   omitted. Worse, `claimNextJob` returned the same `nullopt` for "queue empty"
   and "query failed", so the dispatcher ran for minutes looking perfectly
   healthy while claiming nothing. `lastClaimFailed()` now separates them and
   the dispatcher logs the error.
3. **`waitpid(WNOHANG)` reaps**, so a polling `isAlive()` would have destroyed
   the exit status before `tryGetExitCode()` could read it — Linux only, silent.
   Statuses are now cached on first reap.
4. **`execve` does not search `PATH`**, so every `.py` and `.js` job would have
   failed on Linux with exit 127. The child now uses `execvp`.
5. **Short jobs reported no metrics at all**: a job finishing inside the 4-second
   heartbeat window never had its CPU or memory written. Metrics are now flushed
   as they arrive, bounded to one write per second per busy worker.
6. **The notify triggers broke `sync({ alter: true })`** — PostgreSQL refuses to
   alter a column a trigger depends on. `syncDatabase` no longer passes `alter`,
   and `db/apply_triggers.js --drop` exists for schema changes.

### Constraint discovered during 0.3 — affects Phase 4

`jobs."workerId"` carries a foreign key:

```
jobs_workerId_fkey: FOREIGN KEY ("workerId") REFERENCES workers(id)
                    ON UPDATE CASCADE ON DELETE SET NULL
```

So **the claim fails unless a `workers` row for that id already exists**. The
dispatcher must therefore upsert each worker's row *before* its first claim, not
merely on the 4-second heartbeat tick. Ordering in §6 is: spawn worker → receive
`ready` → upsert the `workers` row → only then start claiming for that slot.

---

## 1. Target architecture

```
                    ┌──────────────────────────────────┐
                    │  Dispatcher process              │
                    │  - holds DATABASE_URL            │
                    │  - claims jobs (SKIP LOCKED)     │
                    │  - writes status, logs, heartbeat│
                    │  - spawns and supervises workers │
                    └───┬──────────────┬───────────────┘
                 stdin/stdout      stdin/stdout
                        │              │
              ┌─────────▼────┐   ┌─────▼────────┐
              │ Worker proc  │   │ Worker proc  │     no DB credentials
              │ worker-1     │   │ worker-2     │     scrubbed environment
              └─────┬────────┘   └─────┬────────┘
                    │                  │
              ┌─────▼────────┐   ┌─────▼────────┐
              │ Job child    │   │ Job child    │     the user's code
              │ python / exe │   │ python / exe │
              └──────────────┘   └──────────────┘
```

Three process layers, each a real OS process. The worker layer exists so that a crash while
supervising a job kills one worker rather than the whole engine, and so that per-worker
resource limits (Job Objects / cgroups) have something to attach to.

**What each component may talk to:**

| Component | PostgreSQL | Backend REST | Child processes |
|---|---|---|---|
| Dispatcher | yes (least-privilege role) | no | spawns workers |
| Worker | **no** | `GET /api/jobs/:id/file` only | spawns the job |
| Job child | no | no (future: network-isolated) | — |

---

## 2. Phase 0 — De-risk before writing code

Each item is small and independently useful. Do these first; they are also the three bugs
already identified in the current engine.

- **0.1 Verify `libpq` is installable on both targets.**
  Windows/MSYS2: `pacman -S mingw-w64-ucrt-x86_64-postgresql`.
  Linux: `apt install libpq-dev`. Confirm `libpq-fe.h` is found and `-lpq` links.
- **0.2 Verify Neon supports `LISTEN`/`NOTIFY`. — DONE, 2026-10-14.**

  | Endpoint | Result |
  |---|---|
  | Pooled (current `DATABASE_URL`, hostname contains `-pooler`) | **Silently fails.** `LISTEN` is accepted without error; no notification is ever delivered. |
  | Direct (same hostname with `-pooler` removed) | **Works**, including a real `AFTER UPDATE` trigger calling `pg_notify`, and delivery after an idle gap. |

  Server is PostgreSQL 18.6 and the role can create `plpgsql` functions and triggers.
  **Phase 5.1 is viable**, but the backend must open a *second*, direct connection purely
  for `LISTEN` — add `DATABASE_URL_DIRECT` to `backend/.env`. Sequelize keeps using the
  pooled URL for everything else.

  The silent-failure mode is the dangerous part: nothing errors, notifications just vanish.
  Any future "live updates stopped working" bug should check this first.
- **0.3 Create the least-privilege database role** (see §3.1).
- **0.4 Fix the cwd-relative sandbox path** — resolve `worker_dirs` from the executable
  location, not the current directory. Removes the duplicate-directory problem.
- **0.5 Fix the environment leak** — `worker.cpp:158` calls `SetEnvironmentVariableA`, which
  mutates the *engine's own* environment. Job A's variables leak into job B and concurrent
  jobs race. Replace with an explicitly constructed child environment block (required by
  Phase 3 anyway).

---

## 3. Phase 1 — PostgreSQL access layer

New file: `src/pg_client.h` / `src/pg_client.cpp`. Used by the dispatcher only.

### 3.1 Database role

```sql
CREATE ROLE engine_role LOGIN PASSWORD '<secret>';

GRANT SELECT ON jobs TO engine_role;
-- "updatedAt" is required: the claim and every status write touch it, and
-- omitting it makes the claim fail with a permission error that is easy to
-- mistake for an empty queue.
GRANT UPDATE (status, "workerId", "startedAt", "finishedAt", "exitCode", "updatedAt")
  ON jobs TO engine_role;
GRANT INSERT ON job_logs TO engine_role;
GRANT USAGE, SELECT ON SEQUENCE job_logs_id_seq TO engine_role;
GRANT SELECT, INSERT, UPDATE ON workers TO engine_role;
-- deliberately NOT granted: DELETE on anything, access to any other table
```

Store as `ENGINE_DATABASE_URL` in `engine/.env`, separate from the backend's `DATABASE_URL`.

### 3.2 Class surface

```cpp
class PgClient {
public:
    explicit PgClient(const std::string& connInfo);   // PQconnectdb, throws on failure
    ~PgClient();                                      // PQfinish

    bool isConnected();                               // PQstatus
    bool reconnect();                                 // PQreset, with backoff

    std::optional<Job> claimNextJob(const std::string& workerId);
    bool updateJobStatus(int jobId, const std::string& status,
                         std::optional<int> exitCode,
                         const std::string& finishedAt);
    bool insertJobLog(int jobId, const std::string& line, const std::string& level);
    bool upsertWorker(const std::string& workerId, const std::string& status,
                      double cpu, double memoryMb, std::optional<int> currentJobId);
private:
    PGconn* conn_;
    PGresult* exec(const char* sql, const std::vector<std::string>& params);
};
```

**Every query uses `PQexecParams`, never string concatenation.** This is both the SQL-injection
defence and the correct way to pass typed values.

### 3.3 The atomic claim

This is the centre of the whole change. Note the double-quoted camelCase column names —
Sequelize created them that way, so unquoted identifiers will not resolve.

```sql
UPDATE jobs
SET    status      = 'running',
       "workerId"  = $1,
       "startedAt" = NOW(),
       "updatedAt" = NOW()
WHERE  id = (
    SELECT id FROM jobs
    WHERE  status = 'queued'
    ORDER  BY "createdAt"
    FOR UPDATE SKIP LOCKED
    LIMIT  1
)
RETURNING id, name, "fileName", "fileType", args, env, "storageKey";
```

Why each clause matters — worth being able to explain in a viva:

- `FOR UPDATE` takes a row-level write lock, so no other transaction can claim the same row.
- `SKIP LOCKED` makes a second dispatcher step over locked rows instead of blocking on them,
  which is what turns this from a bottleneck into a parallel queue.
- The subquery exists because `FOR UPDATE` is not allowed directly in an `UPDATE` statement.
- `RETURNING` makes claim-and-read a single round trip, so there is no window between them.
- `ORDER BY "createdAt"` gives FIFO fairness.

Returns zero rows when the queue is empty — that is the normal idle case, not an error.

### 3.4 Acceptance test for this phase

Run two dispatcher instances against 50 queued jobs. Assert afterwards:

```sql
SELECT COUNT(*) FROM jobs WHERE status = 'queued';          -- 0
SELECT COUNT(*) FROM jobs WHERE "workerId" IS NULL;         -- 0
SELECT id, COUNT(DISTINCT "workerId") FROM jobs
  GROUP BY id HAVING COUNT(DISTINCT "workerId") > 1;        -- no rows
```

The third query is the real proof that the double-execution race is gone.

---

## 4. Phase 2 — Real process metrics

New file: `src/metrics.h` / `src/metrics_win.cpp` / `src/metrics_posix.cpp`.
Replaces `Worker::getCpuUsage()` (which currently returns `rand() % 15 + 5`) and
`getMemoryUsage()` (which returns system-wide load, not the process's).

```cpp
struct ProcMetrics { double cpuPercent; double memoryMb; };

class ProcSampler {                 // one per observed process; holds previous sample
public:
    explicit ProcSampler(ProcHandle h);
    ProcMetrics sample();           // CPU % since the previous call
};
```

CPU percentage is a **rate**, so it needs two samples. Formula:

```
cpu% = (Δcpu_time / (Δwall_time × num_cores)) × 100
```

**Windows** — `GetProcessTimes` returns kernel and user `FILETIME` in 100 ns units; take the
delta of their sum. Memory from `GetProcessMemoryInfo().WorkingSetSize` (links `psapi`,
already in `CMakeLists.txt`).

**Linux** — CPU from `/proc/<pid>/stat` fields 14 (`utime`) and 15 (`stime`), in clock ticks;
divide by `sysconf(_SC_CLK_TCK)`. Memory from `VmRSS` in `/proc/<pid>/status`, in kB.

Sample every 1 s on a timer in the worker. When a job is running, sample the **job child
process**; when idle, sample the worker itself and report ~0.

**Known limitation to state in the report:** this measures the direct child only. A Python
script that spawns its own subprocesses will under-report. Measuring the whole tree needs a
Windows Job Object (`QueryInformationJobObject`) or a Linux cgroup — both arrive in Phase 6.

---

## 5. Phase 3 — IPC protocol and worker process

### 5.1 Protocol

Newline-delimited JSON, one message per line, over the worker's stdin and stdout.

**Dispatcher → worker (stdin):**

```json
{"type":"assign","jobId":42,"fileName":"add.c","fileType":".c","args":"1 2",
 "env":{"MODE":"test"},"fileUrl":"http://localhost:3000/api/jobs/42/file"}
{"type":"cancel","jobId":42}
{"type":"shutdown"}
```

**Worker → dispatcher (stdout):**

```json
{"type":"ready","workerId":"host-w1","pid":4821}
{"type":"started","jobId":42,"startedAt":"2026-10-14T09:00:00Z"}
{"type":"log","jobId":42,"line":"hello","level":"info"}
{"type":"metrics","cpu":37.2,"memoryMb":51.4,"jobId":42}
{"type":"finished","jobId":42,"exitCode":0,"finishedAt":"2026-10-14T09:00:04Z"}
{"type":"error","jobId":42,"message":"download failed"}
```

Two rules that keep this from breaking:

- **The worker's stdout carries protocol only.** Any `std::cout` debug print corrupts the
  stream. All worker diagnostics go to **stderr**, which the dispatcher logs separately.
- **The job child's output never touches the worker's own stdout.** It is captured from the
  job's pipe and re-emitted as `log` messages.

### 5.2 Cross-platform process and pipe abstraction

New: `src/platform.h`, `src/platform_win.cpp`, `src/platform_posix.cpp`.

```cpp
struct SpawnResult { ProcHandle handle; PipeHandle stdinW, stdoutR, stderrR; };

SpawnResult spawnProcess(const std::string& exePath,
                         const std::vector<std::string>& args,
                         const std::map<std::string,std::string>& env,  // explicit, never inherited
                         const std::string& workingDir);
bool   isAlive(ProcHandle);
int    waitForExit(ProcHandle);
void   terminate(ProcHandle);
size_t readPipe(PipeHandle, char* buf, size_t len);
bool   writeLine(PipeHandle, const std::string& line);
```

Windows uses `CreatePipe` + `CreateProcessW` with an explicit environment block.
POSIX uses `pipe()` + `fork()` + `execvp()`, and must **close unused pipe ends in both parent
and child**, set `FD_CLOEXEC`, retry on `EINTR`, and reap children via `waitpid`.

### 5.3 Worker mode

`engine --role worker --id host-w1 --dir <sandbox> --backend http://localhost:3000`

Main loop: read a line from stdin → parse → on `assign`, run the job → emit messages.
Job execution reuses the existing logic from `worker.cpp` (sandbox prep, `buildCommandLine`,
spawn, pipe read, exit code) with every `dbClient_->...` call replaced by an IPC emit.

The worker keeps **one** HTTP call: downloading the job file. That endpoint needs no
credentials, so this does not weaken the credential boundary.

---

## 6. Phase 4 — Dispatcher

New: `src/dispatcher.h` / `src/dispatcher.cpp`. Deletes `src/scheduler.{h,cpp}`.

```cpp
struct WorkerSlot {
    std::string id;              // "<hostname>-w1", stable across respawns
    ProcHandle  proc;
    PipeHandle  stdinW, stdoutR, stderrR;
    std::string lineBuffer;      // partial-line accumulator
    bool        busy = false;
    int         currentJobId = 0;
    ProcMetrics lastMetrics;
};
```

**Worker identity must be globally unique** — `hostname + "-w" + index`, so two machines never
collide on the same `workers` row. (Today every instance calls itself `worker-1`.)

Main loop, single-threaded, roughly 100 ms per tick:

1. **Drain worker output.** Non-blocking read from every worker's stdout and stderr, append to
   that worker's `lineBuffer`, split on `\n`, handle each complete message.
   **This must happen every tick.** A pipe has a fixed OS buffer; if the dispatcher stops
   reading while a chatty job floods stdout, the worker blocks on write and the system
   deadlocks. This is the single most likely bug in the whole design.
2. **Handle messages** — `log` → `insertJobLog`; `started`/`finished` → `updateJobStatus`;
   `metrics` → cache.
3. **Claim and assign.** For each idle worker, call `claimNextJob(workerId)`. On a row, write
   the `assign` line to that worker's stdin and mark it busy. On no row, stop early.
4. **Heartbeat** every 4 s — `upsertWorker(...)` per slot from cached metrics.
5. **Supervise.** If `isAlive(proc)` is false: mark its in-flight job `failed` with a log line
   explaining the worker died, respawn the worker with the same id, reset the slot.
6. **Shutdown** on SIGINT/SIGTERM: send `shutdown` to each worker, wait up to 10 s, then
   `terminate`. Mark any still-running jobs `failed` so they are not orphaned in `running`.

Note what disappears: no `std::thread`, no `detach()`, no mutex, no `claimedJobIds` set. The
dispatcher is single-threaded, and the three data races in the current engine stop existing.

---

## 7. Phase 5 — Keeping the dashboard live

**Problem:** once the engine writes to PostgreSQL directly, the backend no longer sees status
changes, so `io.emit('job:status', ...)` never fires and the dashboard goes dark. This is a
required part of the change, not an optional extra.

**5.1 — Preferred: `LISTEN` / `NOTIFY`.** A trigger fires on every change, the backend holds a
listening connection and re-broadcasts over Socket.io. The engine needs no knowledge of the
backend at all.

```sql
CREATE OR REPLACE FUNCTION notify_job_change() RETURNS trigger AS $$
BEGIN
  PERFORM pg_notify('job_events', json_build_object(
    'id', NEW.id, 'status', NEW.status,
    'workerId', NEW."workerId", 'exitCode', NEW."exitCode")::text);
  RETURN NEW;
END; $$ LANGUAGE plpgsql;

CREATE TRIGGER jobs_notify AFTER INSERT OR UPDATE OF status ON jobs
  FOR EACH ROW EXECUTE FUNCTION notify_job_change();
```

Backend side, using the raw `pg` client (Sequelize cannot hold a `LISTEN` connection):

```js
const client = new Client({ connectionString: process.env.DATABASE_URL_DIRECT })
await client.connect()
await client.query('LISTEN job_events')
client.on('notification', (msg) => {
  const payload = JSON.parse(msg.payload)
  emitJobStatus(payload.id, payload.status, payload)
})
```

**Gated on task 0.2.** This needs a direct, non-pooled connection and must survive Neon
autosuspend — add reconnect-with-backoff and re-issue `LISTEN` on reconnect.

**5.2 — A second trigger on `job_logs`** does the same for live log streaming. Consider
throttling: a job printing thousands of lines produces thousands of notifications.

**5.3 — Fallback if 0.2 fails.** Dispatcher POSTs a fire-and-forget
`POST /api/internal/events` after each status change, guarded by a shared secret header. Less
elegant, no database dependency, ~30 lines.

---

## 8. Phase 6 — Hardening (after the above works)

Only now does process isolation start paying for itself:

- **Windows Job Objects** — `AssignProcessToJobObject` with `JOB_OBJECT_LIMIT_PROCESS_MEMORY`,
  `JOB_OBJECT_LIMIT_JOB_TIME` and `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. Also gives whole-tree
  CPU accounting, which fixes the Phase 2 limitation.
- **Linux** — `setrlimit(RLIMIT_AS, RLIMIT_CPU, RLIMIT_NPROC, RLIMIT_FSIZE)` after `fork`,
  before `exec`; cgroup v2 for tree-wide accounting.
- **Wall-clock timeout per job**, configurable, status `timeout`. Needs a new enum value in
  the Sequelize model.
- **Working cancellation** — `DELETE /api/jobs/:id` currently sets `cancelled` in the database
  and nothing reads it. The dispatcher should poll for cancelled in-flight jobs and send
  `{"type":"cancel"}` to the owning worker.
- **Sandbox cleanup** — delete `job_<id>/` after completion, or sweep on a retention policy.
- **Restricted user account** for worker processes.

---

## 9. File-level change summary

| File | Action |
|---|---|
| `src/pg_client.{h,cpp}` | **new** — libpq wrapper, atomic claim, all writes |
| `src/platform.h`, `platform_win.cpp`, `platform_posix.cpp` | **new** — process/pipe abstraction |
| `src/metrics.h`, `metrics_win.cpp`, `metrics_posix.cpp` | **new** — real CPU and memory |
| `src/ipc.{h,cpp}` | **new** — NDJSON framing, line buffering |
| `src/dispatcher.{h,cpp}` | **new** — replaces scheduler |
| `src/scheduler.{h,cpp}` | **delete** |
| `src/worker.{h,cpp}` | **rewrite** — now a standalone process main loop |
| `src/db_client.{h,cpp}` | **shrink + rename** to `http_client` — file download only |
| `src/main.cpp` | **modify** — `--role dispatcher\|worker` switch |
| `src/job.h` | mostly unchanged; add `fromPgRow()` alongside `fromJson()` |
| `CMakeLists.txt` | **modify** — find and link `libpq`, platform-conditional sources |
| `compile.sh` | **write** — currently empty; needed for the Linux target |
| `backend/src/services/listener.js` | **new** — `LISTEN` connection → Socket.io |
| Database | **new** — `engine_role`, notify function, two triggers |

---

## 10. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Pipe deadlock when a job floods stdout | **high** | drain every worker pipe every tick; never block on a write |
| POSIX `fork`/`exec`/fd handling subtleties | **high** | isolate in `platform_posix.cpp`; test early with a trivial child |
| Neon does not support `LISTEN`/`NOTIFY` on the available endpoint | medium | verify in task 0.2 before building Phase 5; fallback 5.3 ready |
| `libpq` build setup on MSYS2 | medium | verify in task 0.1 before any code is written |
| Partial JSON lines across pipe reads | medium | per-worker `lineBuffer`; only parse on `\n` |
| Dispatcher is now a single point of failure | low | it holds no job state; restarting re-claims from the database |
| Neon connection limits | low | one dispatcher connection plus one backend `LISTEN` connection |

## 11. Sequencing and rough effort

Part-time student pace; treat as relative weights, not commitments.

| Phase | Depends on | Rough effort |
|---|---|---|
| 0 — de-risk | — | 0.5 day |
| 1 — pg_client + claim | 0.1, 0.3 | 2 days |
| 2 — metrics | — (parallelisable) | 1 day |
| 3 — IPC + worker process | 0.5 | 3 days |
| 4 — dispatcher | 1, 3 | 3 days |
| 5 — live updates | 0.2, 4 | 1 day |
| 6 — hardening | 4 | open-ended |

Phases 1 and 2 are independent and can be built in either order. Nothing before Phase 4 breaks
the existing engine, so work on a branch and keep `main` runnable for demos.

## 12. Definition of done

- Two dispatchers against one database execute 50 jobs with zero duplicates (query in §3.4).
- Killing a worker mid-job: the job is marked `failed` with an explanatory log line, the worker
  is respawned with the same id, and the remaining jobs continue.
- Killing the dispatcher: all workers and job children exit; no orphans in Task Manager / `ps`.
- A CPU-bound test script reports > 50% CPU and a memory-allocating script reports a plausible
  RSS — both verified against Task Manager or `top`.
- The dashboard updates live, as it does today.
- Both `cmake --build` on Linux and `compile.bat` on Windows produce a working binary.

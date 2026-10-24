# ProcExec — Database-backed Process Execution System

Submit a program through a web dashboard; a C++ engine picks it up, runs it in an isolated
OS process under memory and time limits, and streams its output back live.

The database is not just storage here — it is the **coordination mechanism**. Workers claim
work with a single atomic SQL statement, so multiple engines can share one queue on different
machines without ever running the same job twice.

![Dashboard][img-dashboard]

---

## Table of contents

- [What it does](#what-it-does)
- [Screenshots](#screenshots)
- [How it works](#how-it-works)
- [System architecture](#system-architecture)
- [Data model](#data-model)
- [Tools and frameworks, and why](#tools-and-frameworks-and-why)
- [Getting started](#getting-started)
- [Configuration](#configuration)
- [API reference](#api-reference)
- [Project layout](#project-layout)
- [Testing](#testing)

---

## What it does

Upload a `.py`, `.js`, `.sh`, `.c`, `.cpp`, `.bat` or `.exe` file with optional arguments and
environment variables. The system queues it, runs it on a free worker, and shows you its
output as it is produced.

- **Atomic job claiming** — `FOR UPDATE SKIP LOCKED` guarantees exactly-once execution even
  with several engines running concurrently.
- **Process isolation** — every job runs as its own OS process with a memory cap and an
  optional wall-clock timeout, so a runaway job cannot take the system down.
- **Live output** — database triggers publish every status change and log line; the browser
  receives them over WebSocket with no polling.
- **Real resource metrics** — actual CPU and memory of the job process, sampled from the OS.
- **Crash recovery** — a dead worker costs one job, not the engine; jobs orphaned by a
  crashed engine are reclaimed automatically.
- **Compilation support** — `.c` and `.cpp` files are compiled with `gcc`/`g++` before
  running, with compiler errors captured into the job log.

---

## Screenshots

### Dashboard — live counters, time-series chart and activity feed

Counters and chart update over WebSocket as jobs move through the queue; the activity panel
shows each transition as it happens.

![Dashboard with live activity][https://github.com/user-attachments/assets/24704656-2e16-43ba-a508-e6851cf9d75b]

### Job detail — full output log, timeline and worker assignment

Every line of a job's stdout and stderr is stored and replayed, so logs survive a page reload
long after the job finished.

![Job detail][https://github.com/user-attachments/assets/c4c6dd20-f413-4009-ab7c-9869c2810b41]

### Job list — searchable, filterable, paginated

![Job list][https://github.com/user-attachments/assets/325ce8c4-b027-44ee-971c-c2d736f7e439]

### Workers — per-worker CPU, memory and current job

CPU is a share of total machine capacity; memory is the job process's working set.

![Workers][https://github.com/user-attachments/assets/b51c2ddb-68e0-4bd6-adde-aba941bdd3a2]

### Submit a job

![Submit job][https://github.com/user-attachments/assets/529b2435-e1ba-4420-bf2f-cd549348f13c]

---

## How it works

Following one job from click to result:

1. **Submit.** The browser POSTs the file as `multipart/form-data`. The backend streams it
   into object storage and inserts a row in `jobs` with `status = 'queued'`.

2. **Claim.** Each idle worker makes the dispatcher run one statement:

   ```sql
   UPDATE jobs
   SET    status = 'running', "workerId" = $1, "startedAt" = NOW()
   WHERE  id = (
       SELECT id FROM jobs
       WHERE  status = 'queued'
       ORDER  BY "createdAt"
       FOR UPDATE SKIP LOCKED
       LIMIT  1
   )
   RETURNING id, name, "fileName", "fileType", args, env, "storageKey";
   ```

   This is the heart of the system. `FOR UPDATE` locks the candidate row; `SKIP LOCKED` makes
   a competing dispatcher step over it instead of blocking; the subquery is required because
   `FOR UPDATE` is not allowed directly in an `UPDATE`; and `RETURNING` makes claim-and-read a
   single round trip, leaving no window in which two engines could both act on the row.

3. **Dispatch.** The dispatcher writes a JSON line to the chosen worker's stdin. Worker
   processes hold **no database credentials** — they only learn what the dispatcher tells them.

4. **Fetch and run.** The worker downloads the file into a per-job sandbox directory, compiles
   it if needed, and spawns it as a child process inside a Windows Job Object (or under
   `RLIMIT_AS` on Linux) with the configured memory cap.

5. **Stream.** The child's stdout and stderr are read through a pipe, split into lines, and
   sent back to the dispatcher as `log` messages, which inserts them into `job_logs`.

6. **Publish.** A database trigger fires `pg_notify` on every insert and status change. The
   backend holds a `LISTEN` connection and re-broadcasts each event over Socket.io, so the
   dashboard updates without polling.

7. **Finish.** On exit the worker reports the exit code; the dispatcher writes the final
   status (`done`, `failed` or `cancelled`) and deletes the sandbox.

### Why events come from the database

The engine writes directly to PostgreSQL and never calls the REST API for status or logs. If
the backend only emitted events from its own routes, it would be blind to everything the
engine does. Making the **database** the single source of events means every change is
published exactly once, no matter which component made it.

---

## System architecture

### The whole system on one page

Three independent programs meet at a shared database. **The engine talks to PostgreSQL
directly** — it does not go through the backend — which is what lets several engines on
different machines share one queue.

```mermaid
flowchart LR
    UI["React Dashboard<br/>browser · 5173"]
    API["Node.js Backend<br/>REST + Socket.io · 3000"]
    PG[("PostgreSQL<br/>job queue · logs")]
    ENG["C++ Engine<br/>dispatcher + N workers"]
    JOB["Your program<br/>isolated process<br/>capped RAM · timeout"]
    S3[("Object Storage<br/>uploaded files")]

    UI   ==>|"1 · submit"| API
    API  ==>|"2 · queue it"| PG
    PG   ==>|"3 · a worker claims it"| ENG
    ENG  ==>|"4 · run it"| JOB
    API  --> S3

    ENG  -. "status + output" .-> PG
    PG   -. "NOTIFY" .-> API
    API  -. "live updates" .-> UI

    classDef ui   fill:#EEEDFE,stroke:#534AB7,color:#26215C
    classDef svc  fill:#E1F5EE,stroke:#0F6E56,color:#04342C
    classDef eng  fill:#FAECE7,stroke:#993C1D,color:#4A1B0C
    classDef data fill:#F1EFE8,stroke:#5F5E5A,color:#2C2C2A
    class UI ui
    class API svc
    class ENG,JOB eng
    class PG,S3 data
```

**Solid arrows** are the job travelling left to right; **dotted arrows** are the results
coming back. Notice that the backend and the engine never call each other to coordinate work —
they meet at the database. That is what lets you start a second engine, on another machine,
with no change to either side.

### Three process layers, and who is trusted

Uploaded programs are treated as hostile. Each layer below has strictly less access than the
one above it.

```mermaid
flowchart TB
    D["Dispatcher — 1 process<br/>talks to the database<br/>✔ holds credentials"]
    W["Worker — N processes<br/>runs one job at a time<br/>✘ no credentials"]
    J["Job — 1 process per job<br/>your uploaded program<br/>✘ no credentials · capped RAM · timeout"]

    D == "assigns work over a pipe" ==> W
    W == "spawns" ==> J

    classDef trusted   fill:#EAF3DE,stroke:#3B6D11,color:#173404
    classDef limited   fill:#FAEEDA,stroke:#854F0B,color:#412402
    classDef untrusted fill:#FCEBEB,stroke:#A32D2D,color:#501313
    class D trusted
    class W limited
    class J untrusted
```

| Layer | PostgreSQL | Backend REST | Credentials in its environment |
|---|---|---|---|
| Dispatcher | yes (`engine_role`) | health check only | yes |
| Worker | no | file download only | **no** |
| Job | no | no | **no** |

Workers are launched with an environment built from scratch, with `DATABASE_URL`, `AWS_*`,
`NEON_*` and `PG*` removed. A job therefore cannot read a credential out of its own
environment — verified by an automated test, not just intended.

`engine_role` is itself least-privilege: `SELECT` on `jobs`, `UPDATE` on six named columns,
`INSERT` on `job_logs`, read/write on `workers`. It cannot `DELETE`, cannot touch any other
table, and cannot run DDL.

### Why processes instead of threads

An earlier version ran a thread per job inside one process. Threads cannot be given individual
memory caps or privileges, and one bad job took the whole engine down. The supervisor/worker
process model used here is the same one nginx, PostgreSQL and Chrome use, for the same reason:
fault and resource isolation.

## Data model

Three tables. Each one answers a single question:

| Table | Answers | Grows |
|---|---|---|
| **`jobs`** | What work exists, and what state is it in? | one row per submission |
| **`job_logs`** | What did this job print? | one row per output line |
| **`workers`** | Which workers are alive, and what are they doing? | one row per worker |

### How a job moves through its states

The `status` column is the heart of the system — it is both what the dashboard displays and
what the engine queries to find work.

```mermaid
stateDiagram-v2
    direction LR
    [*] --> queued : user submits a file

    queued --> running : a worker claims it
    running --> done : exited with code 0
    running --> failed : non-zero exit, crash,<br/>timeout, or dead worker
    running --> cancelled : user pressed cancel

    done --> [*]
    failed --> [*]
    cancelled --> [*]

    note right of queued
        Only rows in this state
        are visible to the claim query
    end note
```

A job is only ever claimed **out of `queued`**, and the claim flips it to `running` in the same
statement — which is precisely why two engines can never pick up the same job.

### How the tables relate

```mermaid
erDiagram
    WORKERS ||--o{ JOBS : "runs"
    JOBS    ||--o{ JOB_LOGS : "produces"

    WORKERS {
        string id PK "host-pid-wN"
        string status "online, busy, offline"
        float  cpu "percent of machine"
        float  memory "working set in MB"
        date   lastHeartbeat "staleness = crashed"
    }
    JOBS {
        int    id PK
        string name
        enum   status "the state machine above"
        string fileType "picks the interpreter"
        text   args
        text   env "JSON"
        int    exitCode
        string workerId FK "which worker ran it"
        date   startedAt
        date   finishedAt
    }
    JOB_LOGS {
        int    id PK
        int    jobId FK
        text   line "one line of output"
        string level "info, warn, error, success"
        date   timestamp
    }
```

Two relationships worth knowing:

- **`job_logs` cascade.** Delete a job and its log lines go with it, so no orphaned output
  accumulates.
- **`jobs.workerId` is a real foreign key** to `workers(id)`. A worker row must therefore
  exist *before* that worker's first claim — the dispatcher registers each worker the moment
  it reports ready.

---

## Tools and frameworks, and why

### Engine — C++17

| Choice | Why |
|---|---|
| **C++17** | The engine's job is process creation, pipe handling and OS resource limits. These are native OS APIs (`CreateProcess`, `fork`/`execvp`, Job Objects, `setrlimit`), and C++ reaches them without a binding layer. `std::filesystem` and `std::optional` keep the code readable. |
| **libpq** | The official PostgreSQL C client. Used directly rather than through an ORM because the atomic claim is the one query that must be exactly right — writing it by hand makes its semantics explicit. Every statement uses `PQexecParams` with bound parameters, never string concatenation. |
| **cpp-httplib** | Header-only HTTP client, vendored into the repo. The engine makes exactly two HTTP calls (health check and file download), so a full library with a build step would be disproportionate. |
| **nlohmann/json** | Header-only JSON, vendored. Used for the dispatcher↔worker protocol. Configured to replace invalid UTF-8 rather than throw, because job output is arbitrary bytes and a job must never be able to crash its worker by printing. |

### Backend — Node.js

| Choice | Why |
|---|---|
| **Express 5** | Small, familiar routing layer. The backend's job is file handling and queries, not heavy computation, so a minimal framework is the right size. |
| **Sequelize** | Collapses schema definition and relational reads into application code: `Job.findByPk(id, { include: [...] })` replaces a hand-written join plus nesting logic. Used for ordinary CRUD only — the concurrency-critical claim lives in the engine as raw SQL, deliberately. |
| **Socket.io** | Plain HTTP cannot push. Socket.io adds automatic reconnection, a long-polling fallback and named events on top of WebSocket, which matters on networks that block raw WebSocket. |
| **`pg` (raw)** | Used directly for the `LISTEN` connection, because Sequelize's pooled connection cannot hold a long-lived listener. |
| **Multer** | Express cannot parse `multipart/form-data`. Configured with `memoryStorage` so uploaded bytes can be piped straight to object storage without a temp file. |
| **AWS SDK v3** | S3-compatible client for Neon Object Storage; v3's modular imports keep the dependency small. |

### Frontend — React

| Choice | Why |
|---|---|
| **React 19 + Vite** | Component model suits a dashboard of independently updating panels; Vite gives near-instant HMR during development. |
| **axios** | One configured instance with a base URL, timeout and a response interceptor that normalises the backend's error shape, so every page handles errors identically. |
| **socket.io-client** | Client half of the live update channel. |
| **Recharts** | Declarative React charting for the jobs-over-time graph; no imperative chart lifecycle to manage. |
| **lucide-react** | Consistent icon set, tree-shakeable. |

### Infrastructure

| Choice | Why |
|---|---|
| **PostgreSQL** | Chosen specifically for `FOR UPDATE SKIP LOCKED`, which is what makes a correct database-backed queue possible. Also brings real transactions, `ENUM` types enforcing valid statuses, foreign keys, and `LISTEN`/`NOTIFY` for push updates. |
| **Neon** | Serverless Postgres: a connection string instead of a local install, so every team member and the engine can share one database. |
| **Neon Object Storage** | S3-compatible storage for uploaded files, keeping binaries out of the database. |

---

## Getting started

### Prerequisites

- **Node.js** 18+
- **A C++17 compiler** — MSYS2 UCRT64 on Windows, GCC on Linux
- **libpq** development files
  - Windows: `pacman -S mingw-w64-ucrt-x86_64-postgresql`
  - Debian/Ubuntu: `sudo apt install libpq-dev`
- **A PostgreSQL database** (Neon or local)

### 1. Database

Create the least-privilege role the engine uses:

```sql
CREATE ROLE engine_role LOGIN PASSWORD '<choose-one>';
GRANT CONNECT ON DATABASE <dbname> TO engine_role;
GRANT USAGE ON SCHEMA public TO engine_role;
GRANT SELECT ON jobs TO engine_role;
GRANT UPDATE (status, "workerId", "startedAt", "finishedAt", "exitCode", "updatedAt")
  ON jobs TO engine_role;
GRANT INSERT ON job_logs TO engine_role;
GRANT USAGE, SELECT ON SEQUENCE job_logs_id_seq TO engine_role;
GRANT SELECT, INSERT, UPDATE ON workers TO engine_role;
```

### 2. Backend

```bash
cd backend && npm install
```

Create `backend/.env`:

```ini
PORT=3000
DATABASE_URL=postgresql://user:pass@host/db?sslmode=require
DATABASE_URL_DIRECT=postgresql://user:pass@host-without-pooler/db?sslmode=require
AWS_ACCESS_KEY_ID=...
AWS_SECRET_ACCESS_KEY=...
AWS_REGION=auto
AWS_BUCKET_NAME=executablefiles
```

> `DATABASE_URL_DIRECT` must be the **non-pooled** endpoint. A connection pooler in
> transaction mode accepts `LISTEN` and then silently discards every notification — live
> updates would simply never arrive, with no error anywhere.

Start it once to create the tables, then install the notification triggers:

```bash
npm start           # creates tables, then Ctrl+C
node ../db/apply_triggers.js
npm start
```

### 3. Engine

```bash
cd engine
cp .env.example .env     # set ENGINE_DATABASE_URL to the engine_role connection string
./compile.bat            # Windows   (./compile.sh on Linux)
./engine.exe --workers 4 --timeout 300 --max-memory 512
```

### 4. Frontend

```bash
cd frontend && npm install && npm run dev
```

Open <http://localhost:5173>.

---

## Configuration

### Engine flags

| Flag | Default | Meaning |
|---|---|---|
| `--role` | `dispatcher` | `dispatcher` or `worker` (workers are spawned for you) |
| `--host` / `--port` | `localhost` / `3000` | Backend address |
| `--workers <n>` | `2` | Concurrent worker processes |
| `--dir <path>` | next to the exe | Sandbox root |
| `--id <name>` | auto | Worker id; set by the dispatcher when it spawns a worker |
| `--timeout <sec>` | `0` (off) | Kill a job after N seconds |
| `--max-memory <mb>` | `0` (off) | Per-job memory cap |
| `--keep-sandbox` | off | Keep job directories for inspection |

> **Run with `--timeout` in any real use.** It defaults to off, which means a job containing
> an infinite loop will run forever.

### Schema changes

PostgreSQL will not alter a column that a trigger depends on, so:

```bash
node db/apply_triggers.js --drop   # make the schema change, then re-apply
node db/apply_triggers.js
```

---

## API reference

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/api/health` | Liveness check |
| `GET` | `/api/jobs` | List jobs — `page`, `limit`, `status`, `search` |
| `POST` | `/api/jobs` | Submit a job (`multipart/form-data`: `file`, `name`, `args`, `env`) |
| `GET` | `/api/jobs/:id` | One job with its logs and worker |
| `GET` | `/api/jobs/:id/file` | Download the uploaded file (used by workers) |
| `DELETE` | `/api/jobs/:id` | Cancel a job |
| `GET` | `/api/workers` | List workers |
| `POST` | `/api/workers/heartbeat` | Worker heartbeat |
| `GET` | `/api/dashboard` | Summary counts and time-series — `range` |
| `POST` | `/api/jobs/:id/logs` | *Legacy.* Append a log line. Unused since the engine writes to PostgreSQL directly. |
| `PATCH` | `/api/jobs/:id/status` | *Legacy.* Update status. Unused for the same reason. |

### Socket.io events

| Event | Payload |
|---|---|
| `job:status` | `{ id, status, workerId, exitCode }` |
| `job:log` | `{ id, line, level, timestamp }` |
| `worker:update` | `{ id, status, cpu, memory, currentJobId, lastHeartbeat }` |

---

## Project layout

```
backend/src
  routes/        jobs, workers, dashboard
  models/        Sequelize models and associations
  services/      socketService, listener (LISTEN→Socket.io), storageService, scheduler
  middlewares/   multer upload config

engine/src
  main.cpp         CLI parsing, dispatcher/worker role switch
  dispatcher.cpp   claims jobs, supervises workers, heartbeats, timeouts
  worker_proc.cpp  worker process: runs one job, streams output
  pg_client.cpp    libpq wrapper, atomic claim, all database writes
  ipc.cpp          NDJSON framing over pipes
  job_command.cpp  extension → interpreter, compiles C/C++
  platform_*.cpp   process spawning, pipes, resource limits (Windows / POSIX)
  metrics_*.cpp    real CPU and memory sampling
  http_client.cpp  the engine's only HTTP use: downloading job files

frontend/src
  pages/         Dashboard, JobList, JobDetail, JobSubmit, Workers
  components/    JobCard, LogViewer, StatsChart, StatusBadge, WorkerCard, Navbar
  hooks/         useSocket and friends
  api/client.js  axios instance and endpoint functions

db/
  notify_triggers.sql   trigger functions publishing job/log/worker events
  apply_triggers.js     apply or --drop
```

---

## Testing

The concurrency guarantee is verified rather than assumed. With two independent dispatchers
running against one database and 30 queued jobs, the acceptance check confirms **30 claims,
zero duplicates**:

```sql
SELECT j.id, COUNT(*) FROM jobs j JOIN job_logs l ON l."jobId" = j.id
WHERE l.line LIKE 'Assigned to worker%'
GROUP BY j.id HAVING COUNT(*) > 1;     -- must return no rows
```

Also verified end to end: credential isolation (a job cannot see `DATABASE_URL`), live updates
over `LISTEN`/`NOTIFY`, real CPU and memory readings, cancellation actually terminating a
running process, wall-clock timeouts, memory caps killing a runaway allocation, worker crash
isolation with respawn, and reclamation of jobs orphaned by a crashed engine.

<!-- ──────────────────────────────────────────────────────────────────────────
  SCREENSHOT LINKS — replace each placeholder below with a real image URL.
  Every image in this README points here, so this block is the only thing to edit.

  Easiest way to host them on GitHub itself:
    1. Open a new issue in this repository (you do not have to submit it).
    2. Drag a PNG from the local screenshots/ folder into the comment box.
    3. GitHub uploads it and inserts markdown — copy just the URL part.
    4. Paste that URL below, replacing the placeholder.

  The resulting links look like:
    https://github.com/user-attachments/assets/<id>
─────────────────────────────────────────────────────────────────────────── -->

[img-dashboard]:  https://REPLACE-ME/dashboard.png
[img-job-detail]: https://REPLACE-ME/job-detail.png
[img-job-list]:   https://REPLACE-ME/job-list.png
[img-workers]:    https://REPLACE-ME/workers.png
[img-submit-job]: https://REPLACE-ME/submit-job.png

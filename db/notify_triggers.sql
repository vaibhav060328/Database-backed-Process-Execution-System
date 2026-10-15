-- Live update plumbing for the multi-process engine.
--
-- The dispatcher writes job status and logs straight to PostgreSQL, so the
-- backend no longer sees those changes through its own routes and the dashboard
-- would go dark. These triggers make the database itself announce every change;
-- the backend holds a LISTEN connection and re-broadcasts over Socket.io.
--
-- IMPORTANT: LISTEN/NOTIFY does not work over a connection pooler in
-- transaction mode. The pooled Neon endpoint accepts LISTEN and then silently
-- drops every notification. The backend must listen on the DIRECT endpoint
-- (the same hostname with "-pooler" removed) - see DATABASE_URL_DIRECT.
--
-- Apply with:  node db/apply_triggers.js

-- ---------------------------------------------------------------- job status
CREATE OR REPLACE FUNCTION notify_job_change() RETURNS trigger AS $$
BEGIN
  PERFORM pg_notify('job_events', json_build_object(
    'id',       NEW.id,
    'status',   NEW.status,
    'workerId', NEW."workerId",
    'exitCode', NEW."exitCode",
    'name',     NEW.name,
    'fileName', NEW."fileName"
  )::text);
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS jobs_notify_insert ON jobs;
CREATE TRIGGER jobs_notify_insert
  AFTER INSERT ON jobs
  FOR EACH ROW EXECUTE FUNCTION notify_job_change();

-- UPDATE OF status: a heartbeat touching updatedAt must not wake every browser.
DROP TRIGGER IF EXISTS jobs_notify_update ON jobs;
CREATE TRIGGER jobs_notify_update
  AFTER UPDATE OF status ON jobs
  FOR EACH ROW
  WHEN (OLD.status IS DISTINCT FROM NEW.status)
  EXECUTE FUNCTION notify_job_change();

-- ------------------------------------------------------------------ job logs
CREATE OR REPLACE FUNCTION notify_job_log() RETURNS trigger AS $$
BEGIN
  -- pg_notify payloads are capped at 8000 bytes and the whole INSERT fails if
  -- that is exceeded, so a long output line must be truncated here rather than
  -- being allowed to break the job's logging.
  PERFORM pg_notify('job_log_events', json_build_object(
    'id',        NEW."jobId",
    'line',      left(NEW.line, 3000),
    'level',     NEW.level,
    'timestamp', NEW."timestamp"
  )::text);
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS job_logs_notify ON job_logs;
CREATE TRIGGER job_logs_notify
  AFTER INSERT ON job_logs
  FOR EACH ROW EXECUTE FUNCTION notify_job_log();

-- -------------------------------------------------------------------- workers
CREATE OR REPLACE FUNCTION notify_worker_change() RETURNS trigger AS $$
BEGIN
  PERFORM pg_notify('worker_events', json_build_object(
    'id',            NEW.id,
    'status',        NEW.status,
    'cpu',           NEW.cpu,
    'memory',        NEW.memory,
    'currentJobId',  NEW."currentJobId",
    'lastHeartbeat', NEW."lastHeartbeat"
  )::text);
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS workers_notify ON workers;
CREATE TRIGGER workers_notify
  AFTER INSERT OR UPDATE ON workers
  FOR EACH ROW EXECUTE FUNCTION notify_worker_change();

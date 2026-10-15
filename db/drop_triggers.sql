-- Remove the notify triggers.
--
-- PostgreSQL refuses to alter the type of a column that a trigger depends on,
-- so these must be dropped before any schema change that touches jobs.status,
-- jobs."workerId", job_logs.level or the workers columns. Re-apply afterwards.
--
--   node db/apply_triggers.js --drop     # drop
--   <make the schema change>
--   node db/apply_triggers.js            # restore
--
-- Live updates stop working while the triggers are absent.

DROP TRIGGER IF EXISTS jobs_notify_insert ON jobs;
DROP TRIGGER IF EXISTS jobs_notify_update ON jobs;
DROP TRIGGER IF EXISTS job_logs_notify    ON job_logs;
DROP TRIGGER IF EXISTS workers_notify     ON workers;

DROP FUNCTION IF EXISTS notify_job_change();
DROP FUNCTION IF EXISTS notify_job_log();
DROP FUNCTION IF EXISTS notify_worker_change();

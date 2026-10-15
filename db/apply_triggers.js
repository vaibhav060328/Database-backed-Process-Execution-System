/**
 * Apply db/notify_triggers.sql to the database in backend/.env.
 *
 *   node db/apply_triggers.js
 *
 * Idempotent: every statement uses CREATE OR REPLACE / DROP IF EXISTS, so it is
 * safe to re-run. Prints no credential values.
 */
const fs = require('fs')
const path = require('path')

const ROOT = path.join(__dirname, '..')
require(path.join(ROOT, 'backend/node_modules/dotenv')).config({
  path: path.join(ROOT, 'backend/.env'),
})
const { Client } = require(path.join(ROOT, 'backend/node_modules/pg'))

const url = process.env.DATABASE_URL
if (!url) {
  console.error('DATABASE_URL is not set in backend/.env')
  process.exit(1)
}

const dropping = process.argv.includes('--drop')
const sqlFile = dropping ? 'drop_triggers.sql' : 'notify_triggers.sql'
const sql = fs.readFileSync(path.join(__dirname, sqlFile), 'utf8')

async function main() {
  const client = new Client({ connectionString: url })
  await client.connect()
  console.log(dropping ? 'Dropping notify triggers...' : 'Applying notify triggers...')

  // One transaction: either every trigger exists or none of them do.
  await client.query('BEGIN')
  try {
    await client.query(sql)
    await client.query('COMMIT')
  } catch (err) {
    await client.query('ROLLBACK')
    throw err
  }

  const { rows } = await client.query(`
    SELECT c.relname AS table_name, t.tgname AS trigger_name
    FROM pg_trigger t
    JOIN pg_class c ON c.oid = t.tgrelid
    WHERE NOT t.tgisinternal AND t.tgname LIKE '%notify%'
    ORDER BY c.relname, t.tgname`)

  if (rows.length === 0) {
    console.log('No notify triggers present.')
  } else {
    console.log('Triggers installed:')
    for (const r of rows) console.log(`  ${r.table_name.padEnd(10)} ${r.trigger_name}`)
  }

  await client.end()
  console.log(dropping
    ? '\nDropped. Live updates are off until you re-run without --drop.'
    : '\nDone. The backend listens on DATABASE_URL_DIRECT (non-pooled).')
}

main().catch((e) => {
  console.error('FAILED:', e.message)
  process.exit(1)
})

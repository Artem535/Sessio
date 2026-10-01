#include "db/migrations.h"

namespace pcm::tokenbackend {

void runMigrations(SqliteConnection &conn) {
  conn.exec(R"sql(
    CREATE TABLE IF NOT EXISTS accounts (
      id INTEGER PRIMARY KEY,
      credential_hash TEXT NOT NULL UNIQUE,
      created_at TEXT NOT NULL
    );
  )sql");

  conn.exec(R"sql(
    CREATE TABLE IF NOT EXISTS meetings (
      id INTEGER PRIMARY KEY,
      meeting_ref TEXT NOT NULL UNIQUE,
      account_id INTEGER NOT NULL REFERENCES accounts(id),
      room_name TEXT NOT NULL UNIQUE,
      scheduled_start TEXT NOT NULL,
      scheduled_end TEXT NOT NULL,
      status TEXT NOT NULL DEFAULT 'active',
      created_at TEXT NOT NULL
    );
  )sql");

  conn.exec(R"sql(
    CREATE TABLE IF NOT EXISTS invitations (
      id INTEGER PRIMARY KEY,
      meeting_id INTEGER NOT NULL REFERENCES meetings(id),
      account_id INTEGER NOT NULL REFERENCES accounts(id),
      invitation_code_hash TEXT NOT NULL UNIQUE,
      passcode_hash TEXT NOT NULL,
      passcode_attempts INTEGER NOT NULL DEFAULT 0,
      status TEXT NOT NULL DEFAULT 'active',
      created_at TEXT NOT NULL
    );
  )sql");

  // Additive migration: existing single-meeting rows and invitations survive.
  conn.exec(R"sql(
    CREATE TABLE IF NOT EXISTS schedule_series (
      series_uid TEXT PRIMARY KEY,
      account_id INTEGER NOT NULL REFERENCES accounts(id),
      revision INTEGER NOT NULL CHECK(revision > 0),
      timezone TEXT NOT NULL,
      dtstart_local TEXT NOT NULL,
      duration_seconds INTEGER NOT NULL,
      rrule TEXT NOT NULL,
      until_ms INTEGER,
      active INTEGER NOT NULL,
      join_enabled INTEGER NOT NULL,
      content_hash TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS schedule_overrides (
      series_uid TEXT NOT NULL REFERENCES schedule_series(series_uid),
      original_start_ms INTEGER NOT NULL,
      start_ms INTEGER NOT NULL,
      end_ms INTEGER NOT NULL,
      join_enabled INTEGER NOT NULL,
      PRIMARY KEY(series_uid, original_start_ms)
    );
    CREATE TABLE IF NOT EXISTS schedule_exceptions (
      series_uid TEXT NOT NULL REFERENCES schedule_series(series_uid),
      original_start_ms INTEGER NOT NULL,
      PRIMARY KEY(series_uid, original_start_ms)
    );
  )sql");
}

} // namespace pcm::tokenbackend

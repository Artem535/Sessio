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
}

} // namespace pcm::tokenbackend

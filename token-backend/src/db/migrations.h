#pragma once

#include "db/sqlite_connection.h"

namespace pcm::tokenbackend {
void runMigrations(SqliteConnection &conn);
} // namespace pcm::tokenbackend

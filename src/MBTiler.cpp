/*****************************************************************************
 * MBTiler
 *
 * Writes and read MBTiles files.
 *
 * Inspired by code from:
 * - Sean Gillies https://github.com/mapbox/mbtiler
 * - Mark Erikson https://github.com/markerikson
 *
 ****************************************************************************/

#include <sqlite3.h>
#include <stdlib.h>
#include <stdexcept>

#include "MBTiler.hpp"

using namespace std;

ctb::MBTiler::MBTiler(const char *filepath) {
	sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
	int ret = sqlite3_open(
		filepath,
		&db
	);
	if (ret != SQLITE_OK) {
		throw new runtime_error("Could not open the SQLite database");
	}

	exec("PRAGMA synchronous=0");
	exec("PRAGMA journal_mode=OFF");
	exec("PRAGMA locking_mode=EXCLUSIVE");
	exec("CREATE TABLE IF NOT EXISTS metadata (name text, value text)");
	exec("CREATE UNIQUE INDEX IF NOT EXISTS name_index on metadata (name)");
	exec("CREATE TABLE IF NOT EXISTS tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob)");
	// do not create index on table tiles, it will affect the performance of the inserts

	sqlite3_prepare_v2(db, "INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data) values (?, ?, ?, ?)", -1, &insertTileStmt, NULL);
	sqlite3_prepare_v2(db, "REPLACE INTO metadata (name, value) values (?, ?)", -1, &replaceMetaStmt, NULL);

	loadTiles();
}

ctb::MBTiler::~MBTiler() {
	if (insertTileStmt != NULL) {
		sqlite3_finalize(insertTileStmt);
	}
	if (replaceMetaStmt != NULL) {
		sqlite3_finalize(insertTileStmt);
	}
	if (db != NULL) {
		sqlite3_close(db);
	}
}

void ctb::MBTiler::loadTiles() {
	sqlite3_stmt *stmt;
	sqlite3_prepare_v2(db, "SELECT zoom_level, tile_column, tile_row FROM tiles;", -1, &stmt, NULL);
	int rc;
	while (true) {
		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			uint64_t z = sqlite3_column_int(stmt, 0);
			uint64_t x = sqlite3_column_int(stmt, 1);
			uint64_t y = sqlite3_column_int(stmt, 2);
			tiles.insert((z << 58) | (x << 29) | y);
		}
		else if (rc == SQLITE_DONE) {
			break;
		}
		else {
			std::string err = "Could not fetch rendered rows from database";
			throw std::runtime_error(err);
		}
	}
	sqlite3_finalize(stmt);
}

void ctb::MBTiler::insertBlob(
	const uint8_t *blob,
	size_t size,
	int zoom,
	int tileColumn,
	int tileRow
) {
	mtx.lock(); //
	sqlite3_reset(insertTileStmt);
	sqlite3_bind_int(insertTileStmt, 1, zoom);
	sqlite3_bind_int(insertTileStmt, 2, tileColumn);
	sqlite3_bind_int(insertTileStmt, 3, tileRow);
	sqlite3_bind_blob(insertTileStmt, 4, blob, size, SQLITE_STATIC);
	if (sqlite3_step(insertTileStmt) != SQLITE_DONE) {
		std::string err = "SQLite Error: ";
		err.append(sqlite3_errmsg(db));
		throw new runtime_error(err);
	}
	mtx.unlock();
}


void ctb::MBTiler::setMetadata(
	const char *name,
	const char *value
) {
	sqlite3_reset(replaceMetaStmt);
	sqlite3_bind_text(replaceMetaStmt, 1, name, -1, SQLITE_STATIC);
	sqlite3_bind_text(replaceMetaStmt, 2, value, -1, SQLITE_STATIC);
	if (sqlite3_step(replaceMetaStmt) != SQLITE_DONE) {
		std::string err = "SQLite Error: ";
		err.append(sqlite3_errmsg(db));
		throw new runtime_error(err);
	}
}

void
ctb::MBTiler::exec(const char *query) {
	char *errmsg;
	if (sqlite3_exec(db, query, NULL, NULL, &errmsg) != SQLITE_OK) {
		std::string err = "SQLite Error: ";
		err.append(errmsg);
		throw new runtime_error(err);
	}
}

bool
ctb::MBTiler::testTileExists(
	unsigned int zoom,
	unsigned int tileColumn,
	unsigned int tileRow
) {
	uint64_t tile = ((uint64_t) zoom << 58) | ((uint64_t) tileColumn << 29) | (uint64_t) tileRow;
	return tiles.find(tile) != tiles.end();
}
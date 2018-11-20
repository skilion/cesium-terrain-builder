#pragma once

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

#include "config.hpp"

#include <mutex>
#include <sqlite3.h>
#include <unordered_set>

namespace ctb {
  class MBTiler;
}

class CTB_DLL ctb::MBTiler {
	sqlite3 *db;
	sqlite3_stmt *insertTileStmt;
	sqlite3_stmt *replaceMetaStmt;
	std::unordered_set<uint64_t> tiles;
	std::mutex mtx;

	void exec(const char *query);
	void loadTiles();

public:
	MBTiler(const char *filepath);
	~MBTiler();

	/// Inserts a tile, thread-safe
	void insertBlob(
		const uint8_t *blob,
		size_t size,
		int zoom,
		int tileColumn,
		int tileRow
	);

	void setMetadata(
		const char *name,
		const char *value
	);

	bool testTileExists(
		unsigned int zoom,
		unsigned int tileColumn,
		unsigned int tileRow
	);

	int numTiles() { return tiles.size(); }
};

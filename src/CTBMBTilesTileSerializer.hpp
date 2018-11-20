#pragma once

/**
 * @file CTBMBTilesTileSerializer.hpp
 * @brief This declares and defines the `CTBMBTilesTileSerializer` class
 */

#include <sqlite3.h>

#include "CTBZOutputStream.hpp"
#include "MBTiler.hpp"
#include "MeshSerializer.hpp"
#include "TileCoordinate.hpp"

namespace ctb {
  class CTBMBTilesTileSerializer;
}

/// Implements a serializer of `Tile`s based in a directory of files
class CTB_DLL ctb::CTBMBTilesTileSerializer:
  public ctb::MeshSerializer {
public:
  CTBMBTilesTileSerializer(MBTiler *mbtiler, bool resume):
    mbtiler(mbtiler),
    resume(resume) {}

  /// Start a new serialization task
  virtual void startSerialization() {};

  /// Returns if the specified Tile Coordinate should be serialized
  virtual bool mustSerializeCoordinate(const ctb::TileCoordinate *coordinate);

  /// Serialize a MeshTile to the store
  virtual bool serializeTile(const ctb::MeshTile *tile, bool writeVertexNormals = false);

  /// Serialization finished, releases any resources loaded
  virtual void endSerialization() {};

protected:
  /// Pointer to the SQLite database
  MBTiler *mbtiler;
  /// Do not overwrite existing files
  bool resume;
  // gzip compressor
  CTBZOutputStream gzipStream;
};

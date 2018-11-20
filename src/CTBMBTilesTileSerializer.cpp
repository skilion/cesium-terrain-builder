/**
 * @file CTBMBTilesTileSerializer.cpp
 * @brief This defines the `CTBMBTilesTileSerializer` class
 */
#include <assert.h>

#include "CTBException.hpp"
#include "CTBMBTilesTileSerializer.hpp"
#include "CTBZOutputStream.hpp"

/**
 * @details 
 * Returns if the specified Tile Coordinate should be serialized
 */
bool ctb::CTBMBTilesTileSerializer::mustSerializeCoordinate(const ctb::TileCoordinate *coordinate) {
  if (!resume) return true;
  return !mbtiler->testTileExists(
    coordinate->zoom,
    coordinate->x,
    coordinate->y
  );
}

/**
 * @details 
 * Serialize a MeshTile to the Directory store
 */
bool
ctb::CTBMBTilesTileSerializer::serializeTile(const ctb::MeshTile *tile, bool writeVertexNormals) {
  assert(mbtiler);

  // geneare the tile gzipped content
  gzipStream.reset();
  tile->writeFile(gzipStream, writeVertexNormals);
  gzipStream.finish();

  mbtiler->insertBlob(
    gzipStream.data(),
    gzipStream.size(),
    tile->zoom,
    tile->x,
    tile->y
  );

  return true;
}

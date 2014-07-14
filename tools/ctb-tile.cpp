/*******************************************************************************
 * Copyright 2014 GeoData <geodata@soton.ac.uk>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *******************************************************************************/

/**
 * @file ctb-tile.cpp
 * @brief Convert a GDAL raster to a tile format
 *
 * This tool takes a GDAL raster and by default converts it to gzip compressed
 * terrain tiles which are written to an output directory on the filesystem.
 *
 * In the case of a multiband raster, only the first band is used to create the
 * terrain heights.  No water mask is currently set and all tiles are flagged
 * as being 'all land'.
 *
 * It is recommended that the input raster is in the EPSG 4326 spatial
 * reference system. If this is not the case then the tiles will be reprojected
 * to EPSG 4326 as required by the terrain tile format.
 *
 * Using the `--output-format` flag this tool can also be used to create tiles
 * in other raster formats that are supported by GDAL.
 */

#include <iostream>
#include <sstream>
#include <string.h>             // for strcmp
#include <stdlib.h>             // for atoi
#include <thread>
#include <mutex>
#include <future>

#include "cpl_multiproc.h"      // for CPLGetNumCPUs
#include "gdal_priv.h"
#include "commander.hpp"

#include "config.hpp"
#include "GlobalGeodetic.hpp"
#include "GlobalMercator.hpp"
#include "CTBException.hpp"
#include "TerrainTiler.hpp"
#include "RasterIterator.hpp"
#include "TerrainIterator.hpp"

using namespace std;
using namespace ctb;

#ifdef _WIN32
static const char *osDirSep = "\\";
#else
static const char *osDirSep = "/";
#endif

/// Handle the terrain build CLI options
class TerrainBuild : public Command {
public:
  TerrainBuild(const char *name, const char *version) :
    Command(name, version),
    outputDir("."),
    outputFormat("Terrain"),
    profile("geodetic"),
    threadCount(-1),
    tileSize(0),
    startZoom(-1),
    endZoom(-1)
  {}

  void
  check() const {
    switch(command->argc) {
    case 1:
      return;
    case 0:
      cerr << "  Error: The gdal datasource must be specified" << endl;
      break;
    default:
      cerr << "  Error: Only one command line argument must be specified" << endl;
      break;
    }

    help();                   // print help and exit
  }

  static void
  setOutputDir(command_t *command) {
    static_cast<TerrainBuild *>(Command::self(command))->outputDir = command->arg;
  }

  static void
  setOutputFormat(command_t *command) {
    static_cast<TerrainBuild *>(Command::self(command))->outputFormat = command->arg;
  }

  static void
  setProfile(command_t *command) {
    static_cast<TerrainBuild *>(Command::self(command))->profile = command->arg;
  }

  static void
  setThreadCount(command_t *command) {
    static_cast<TerrainBuild *>(Command::self(command))->threadCount = atoi(command->arg);
  }

  static void
  setTileSize(command_t *command) {
    static_cast<TerrainBuild *>(Command::self(command))->tileSize = atoi(command->arg);
  }


  static void
  setStartZoom(command_t *command) {
    static_cast<TerrainBuild *>(Command::self(command))->startZoom = atoi(command->arg);
  }

  static void
  setEndZoom(command_t *command) {
    static_cast<TerrainBuild *>(Command::self(command))->endZoom = atoi(command->arg);
  }

  const char *
  getInputFilename() const {
    return  (command->argc == 1) ? command->argv[0] : NULL;
  }

  const char *outputDir,
    *outputFormat,
    *profile;

  int threadCount,
    tileSize,
    startZoom,
    endZoom;
};

/// Create a filename for a tile coordinate
static string
getTileFilename(const TileCoordinate &coord, const string dirname, const char *extension) {
  string filename = dirname + static_cast<ostringstream*>
    (
     &(ostringstream()
       << coord.zoom
       << "-"
       << coord.x
       << "-"
       << coord.y)
     )->str();

  if (extension != NULL) {
    filename += ".";
    filename += extension;
  }

  return filename;
}

/**
 * Increment a TilerIterator whilst cooperating between threads
 *
 * This function maintains an global index on an iterator and when called
 * ensures the iterator is incremented to point to the next global index.  This
 * can therefore be called with different tiler iterators by different threads
 * to ensure all tiles are iterated over consecutively.  It assumes individual
 * tile iterators point to the same source GDAL dataset.
 */
template<typename T> int
incrementIterator(T &iter, int currentIndex) {
  static int globalIteratorIndex = 0; // keep track of where we are globally
  static mutex mutex;        // ensure iterations occur serially between threads

  mutex.lock();

  while (currentIndex < globalIteratorIndex) {
    ++iter;
    ++currentIndex;
  }
  ++globalIteratorIndex;

  mutex.unlock();

  return currentIndex;
}

/// In a thread safe manner describe the file currently being created
static void
outputFilename(string filename) {
  stringstream stream;
  stream << "creating " << filename << " in thread " << this_thread::get_id() << endl;
  cout << stream.str();
}

/// Output GDAL tiles represented by a tiler to a directory
static void
buildGDAL(const GDALTiler &tiler, TerrainBuild *command) {
  GDALDriver *poDriver = GetGDALDriverManager()->GetDriverByName(command->outputFormat);

  if (poDriver == NULL) {
    throw CTBException("Could not retrieve GDAL driver");
  }

  if (poDriver->pfnCreateCopy == NULL) {
    throw CTBException("The GDAL driver must be write enabled, specifically supporting 'CreateCopy'");
  }

  const char *extension = poDriver->GetMetadataItem(GDAL_DMD_EXTENSION);
  const string dirname = string(command->outputDir) + osDirSep;
  i_zoom startZoom = (command->startZoom < 0) ? tiler.maxZoomLevel() : command->startZoom,
    endZoom = (command->endZoom < 0) ? 0 : command->endZoom;

  RasterIterator iter(tiler, startZoom, endZoom);
  int currentIndex = incrementIterator(iter, 0);

  while (!iter.exhausted()) {
    std::pair<const TileCoordinate &, GDALDataset *> result = *iter;
    const TileCoordinate &coord = result.first;
    GDALDataset *poSrcDS = result.second;
    GDALDataset *poDstDS;
    const string filename = getTileFilename(coord, dirname, extension);

    outputFilename(filename);
    poDstDS = poDriver->CreateCopy(filename.c_str(), poSrcDS, FALSE,
                                   NULL, NULL, NULL );
    GDALClose(poSrcDS);

    // Close the datasets, flushing data to destination
    if (poDstDS == NULL) {
      throw CTBException("Could not create GDAL tile");
    }

    GDALClose(poDstDS);

    currentIndex = incrementIterator(iter, currentIndex);
  }
}

/// Output terrain tiles represented by a tiler to a directory
static void
buildTerrain(const TerrainTiler &tiler, TerrainBuild *command) {
  const string dirname = string(command->outputDir) + osDirSep;
  i_zoom startZoom = (command->startZoom < 0) ? tiler.maxZoomLevel() : command->startZoom,
    endZoom = (command->endZoom < 0) ? 0 : command->endZoom;

  TerrainIterator iter(tiler, startZoom, endZoom);
  int currentIndex = incrementIterator(iter, 0);

  while (!iter.exhausted()) {
    const TerrainTile terrainTile = *iter;
    const TileCoordinate &coord = terrainTile.getCoordinate();
    const string filename = getTileFilename(coord, dirname, "terrain");

    outputFilename(filename);
    terrainTile.writeFile(filename.c_str());

    currentIndex = incrementIterator(iter, currentIndex);
  }
}

/**
 * Perform a tile building operation
 *
 * This function is designed to be run in a separate thread.
 */
static int
runTiler(TerrainBuild *command, Grid *grid) {
  GDALDataset  *poDataset = (GDALDataset *) GDALOpen(command->getInputFilename(), GA_ReadOnly);
  if (poDataset == NULL) {
    cerr << "Error: could not open GDAL dataset" << endl;
    return 1;
  }

  try {
    if (strcmp(command->outputFormat, "Terrain") == 0) {
      const TerrainTiler tiler(poDataset, *grid);
      buildTerrain(tiler, command);
    } else {                    // it's a GDAL format
      const GDALTiler tiler(poDataset, *grid);
      buildGDAL(tiler, command);
    }

  } catch (CTBException &e) {
    cerr << "Error: " << e.what() << endl;
  }

  GDALClose(poDataset);

  return 0;
}

int
main(int argc, char *argv[]) {
  // Specify the command line interface
  TerrainBuild command = TerrainBuild(argv[0], version.cstr);
  command.setUsage("[options] GDAL_DATASOURCE");
  command.option("-o", "--output-dir <dir>", "specify the output directory for the tiles (defaults to working directory)", TerrainBuild::setOutputDir);
  command.option("-f", "--output-format <format>", "specify the output format for the tiles. This is either `Terrain` (the default) or any format listed by `gdalinfo --formats`", TerrainBuild::setOutputFormat);
  command.option("-p", "--profile <profile>", "specify the TMS profile for the tiles. This is either `geodetic` (the default) or `mercator`", TerrainBuild::setProfile);
  command.option("-c", "--thread-count <count>", "specify the number of threads to use for tile generation. On multicore machines this defaults to the number of CPUs", TerrainBuild::setThreadCount);
  command.option("-t", "--tile-size <size>", "specify the size of the tiles in pixels. This defaults to 65 for terrain tiles and 256 for other GDAL formats", TerrainBuild::setTileSize);
  command.option("-s", "--start-zoom <zoom>", "specify the zoom level to start at. This should be greater than the end zoom level", TerrainBuild::setStartZoom);
  command.option("-e", "--end-zoom <zoom>", "specify the zoom level to end at. This should be less than the start zoom level and >= 0", TerrainBuild::setEndZoom);

  // Parse and check the arguments
  command.parse(argc, argv);
  command.check();

  GDALAllRegister();

  // Define the grid we are going to use
  Grid grid;
  if (strcmp(command.profile, "geodetic") == 0) {
    int tileSize = (command.tileSize < 1) ? 65 : command.tileSize;
    grid = GlobalGeodetic(tileSize);
  } else if (strcmp(command.profile, "mercator") == 0) {
    int tileSize = (command.tileSize < 1) ? 256 : command.tileSize;
    grid = GlobalMercator(tileSize);
  } else {
    cerr << "Error: Unknown profile: " << command.profile << endl;
    return 1;
  }

  // Run the tilers in separate threads
  vector<future<int>> tasks;
  int threadCount = (command.threadCount > 0) ? command.threadCount : CPLGetNumCPUs();

  // Instantiate the threads using futures from a packaged_task
  for (int i = 0; i < threadCount ; ++i) {
    packaged_task<int(TerrainBuild *, Grid *)> task(runTiler); // wrap the function
    tasks.push_back(task.get_future());                        // get a future
    thread(move(task), &command, &grid).detach(); // launch on a thread
  }

  // Synchronise the completion of the threads
  for (auto &task : tasks) {
    task.wait();
  }

  // Get the value from the futures
  for (auto &task : tasks) {
    int retval = task.get();

    // return on the first encountered problem
    if (retval)
      return retval;
  }

  return 0;
}
#ifndef CTBZOUTPUTSTREAM_HPP
#define CTBZOUTPUTSTREAM_HPP

/*******************************************************************************
 * Copyright 2018 GeoData <geodata@soton.ac.uk>
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
 * @file CTBZOutputStream.hpp
 * @brief This declares and defines the `CTBZOutputStream` class
 */

#include <vector>

#include "zlib.h"
#include "CTBOutputStream.hpp"

namespace ctb {
  class CTBZFileOutputStream;
  class CTBZOutputStream;
}

/// Implements CTBOutputStream for compressing a stream in memory
class CTB_DLL ctb::CTBZOutputStream : public ctb::CTBOutputStream {
public:
  CTBZOutputStream();
  ~CTBZOutputStream();

  /// Writes a sequence of memory pointed by ptr into the stream
  virtual uint32_t write(const void *ptr, uint32_t size);

  void finish();
  void reset();

  // Use after calling finish
  const uint8_t *data() { return buffer.data(); };
  size_t size() { return buffer.size(); };

protected:
  /// Internal stream state
  z_stream stream;
  /// Buffer for the compressed data
  std::vector<uint8_t> buffer;

  /// Initializes the zlib stream
  void init();
  /// Cleans the memory allocated by zlib
  void end();
  /// Performs a round of deflate()
  void deflateRound(const void *ptr, size_t size, int flush);
};

/// Implements CTBOutputStream for gzipped files
class CTB_DLL ctb::CTBZFileOutputStream : public ctb::CTBOutputStream {
public:
  CTBZFileOutputStream(const char *fileName);
 ~CTBZFileOutputStream();

  /// Writes a sequence of memory pointed by ptr into the stream
  virtual uint32_t write(const void *ptr, uint32_t size);

  void close();

protected:
  /// The underlying GZFILE*
  gzFile fp;
};

#endif /* CTBZOUTPUTSTREAM_HPP */

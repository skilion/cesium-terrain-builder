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
 * @file CTBZOutputStream.cpp
 * @brief This defines the `CTBZOutputStream` and `CTBZFileOutputStream` classes
 */

#include <assert.h>

#include "CTBException.hpp"
#include "CTBZOutputStream.hpp"

using namespace ctb;

ctb::CTBZOutputStream::CTBZOutputStream() {
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.opaque = Z_NULL;
  init();
}

ctb::CTBZOutputStream::~CTBZOutputStream() {
  end();
}

/**
 * @details 
 * Compresses a sequence of memory pointed by ptr.
 */
uint32_t
ctb::CTBZOutputStream::write(const void *ptr, uint32_t size) {
  deflateRound(ptr, size, Z_NO_FLUSH);
}

/**
 * @details 
 * Initializes the stream.
 */
void
ctb::CTBZOutputStream::init() {
  int ret = deflateInit2(
    &stream,
    Z_DEFAULT_COMPRESSION,
    Z_DEFLATED,
    16 + MAX_WBITS,
    8,
    Z_DEFAULT_STRATEGY
  );
  if (ret != Z_OK) {
    throw CTBException("Could not initialize zlib");
  }
}

/**
 * @details 
 * Cleans the memory allocated by zlib.
 */
void
ctb::CTBZOutputStream::end() {
  deflateEnd(&stream);
}

/**
 * @details 
 * Finalizes the stream.
 */
void
ctb::CTBZOutputStream::finish() {
  deflateRound(Z_NULL, 0, Z_FINISH);
}

/**
 * @details 
 * Resets the stream, making it ready to accept new data.
 */
void
ctb::CTBZOutputStream::reset() {
  buffer.clear();
  deflateReset(&stream);
}

/**
 * @details 
 * Performs a round of deflate().
 */
void
ctb::CTBZOutputStream::deflateRound(const void *data, size_t size, int flush) {
  const size_t TMPSIZE = 4 * 1024;
  uint8_t temp[TMPSIZE];
  stream.next_in = (Bytef*) data;
  stream.avail_in = size;
  do {
    stream.next_out = temp;
    stream.avail_out = TMPSIZE;
    if (deflate(&stream, flush) == Z_STREAM_ERROR) {
      throw CTBException("Compression failed");
    }
    size_t have = TMPSIZE - stream.avail_out;
    buffer.insert(buffer.end(), temp, temp + have);
  } while (stream.avail_out == 0);
  assert(stream.avail_in == 0);
}

/**
 * @details 
 * Writes a sequence of memory pointed by ptr into the GZFILE*.
 */
uint32_t
ctb::CTBZFileOutputStream::write(const void *ptr, uint32_t size) {
  if (size == 1) {
    int c = *((const char *)ptr);
    return gzputc(fp, c) == -1 ? 0 : 1;
  }
  else {
    return gzwrite(fp, ptr, size) == 0 ? 0 : size;
  }
}

ctb::CTBZFileOutputStream::CTBZFileOutputStream(const char *fileName) {
  gzFile file = gzopen(fileName, "wb");

  if (file == NULL) {
    throw CTBException("Failed to open file");
  }
  fp = file;
}

ctb::CTBZFileOutputStream::~CTBZFileOutputStream() {
  close();
}

void
ctb::CTBZFileOutputStream::close() {

  // Try and close the file
  if (fp) {
    switch (gzclose(fp)) {
    case Z_OK:
      break;
    case Z_STREAM_ERROR:
    case Z_ERRNO:
    case Z_MEM_ERROR:
    case Z_BUF_ERROR:
    default:
      throw CTBException("Failed to close file");
    }
    fp = NULL;
  }
}

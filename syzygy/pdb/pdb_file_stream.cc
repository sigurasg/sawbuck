// Copyright 2011 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "syzygy/pdb/pdb_file_stream.h"

#include "base/logging.h"

namespace pdb {

PdbFileStream::PdbFileStream(FILE* file,
                             int length,
                             const uint32* pages,
                             int page_size)
    : PdbStream(length),
      file_(file),
      pages_(pages),
      page_size_(page_size) {
}

PdbFileStream::~PdbFileStream() {
}

int PdbFileStream::ReadBytes(void* dest, int count) {
  // Return 0 once we've reached the end of the stream.
  if (pos() == length())
    return 0;

  // Don't read beyond the end of the known stream length.
  if (pos() + count > length())
    count = length() - pos();
  size_t bytes_read = count;

  // Read the stream.
  while (count > 0) {
    int page_index = pos() / page_size_;
    int offset = pos() % page_size_;
    int chunk_size = std::min(count, page_size_ - (pos() % page_size_));
    if (!ReadFromPage(dest, pages_[page_index], offset, chunk_size))
      return -1;

    count -= chunk_size;
    Seek(pos() + chunk_size);
    dest = reinterpret_cast<uint8*>(dest) + chunk_size;
  }

  return bytes_read;
}

bool PdbFileStream::ReadFromPage(void* dest, uint32 page_num, int offset,
                                 int count) {
  DCHECK(dest != NULL);
  DCHECK(offset + count <= page_size_);

  size_t page_offset = page_size_ * page_num;
  if (fseek(file_, page_offset + offset, SEEK_SET) != 0) {
    LOG(ERROR) << "Page seek failed";
    return false;
  }

  if (fread(dest, 1, count, file_) != static_cast<size_t>(count)) {
    LOG(ERROR) << "Page read failed";
    return false;
  }

  return true;
}

}  // namespace pdb

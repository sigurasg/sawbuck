// Copyright 2012 Google Inc. All Rights Reserved.
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
//
// Declares some unittest helper functions.

#ifndef SYZYGY_CORE_UNITTEST_UTIL_H_
#define SYZYGY_CORE_UNITTEST_UTIL_H_

#include "base/file_path.h"
#include "base/file_util.h"
#include "syzygy/core/serialization.h"

namespace testing {

// A simple utility class for creating and cleaning up a temporary file.
class ScopedTempFile {
 public:
  ScopedTempFile() {
    file_util::CreateTemporaryFile(&path_);
  }

  ~ScopedTempFile() {
    file_util::Delete(path_, false);
  }

  const FilePath& path() const { return path_; }

 private:
  FilePath path_;
};

// This defines a simple test of serialization for a given object. Returns
// true on success, false otherwise. The data object must be default
// constructible and comparable.
template<class Data> bool TestSerialization(const Data& data) {
  core::ByteVector bytes;

  core::ScopedOutStreamPtr out_stream;
  out_stream.reset(core::CreateByteOutStream(std::back_inserter(bytes)));
  core::NativeBinaryOutArchive out_archive(out_stream.get());
  if (!out_archive.Save(data))
    return false;
  if (!out_archive.Flush())
    return false;

  core::ScopedInStreamPtr in_stream;
  in_stream.reset(core::CreateByteInStream(bytes.begin(), bytes.end()));
  core::NativeBinaryInArchive in_archive(in_stream.get());
  Data data_copy;
  if (!in_archive.Load(&data_copy))
    return false;

  // Ensure the two elements are the same after a roundtrip through the
  // serialization engine.
  return (data == data_copy);
};

// Same as above, but serializes to the given file, which has to be opened
// in read-write mode.
template<class Data> bool TestSerialization(const Data& data, FILE* file) {
  core::FileOutStream out_stream(file);
  core::NativeBinaryOutArchive out_archive(&out_stream);
  if (!out_archive.Save(data))
    return false;

  // Flush the output and rewind the file.
  fflush(file);
  fseek(file, 0, SEEK_SET);

  core::FileInStream in_stream(file);
  core::NativeBinaryInArchive in_archive(&in_stream);
  Data data_copy;
  if (!in_archive.Load(&data_copy))
    return false;

  // Ensure the two elements are the same after a roundtrip through the
  // serialization engine.
  return (data == data_copy);
};

// Converts a relative path to absolute using the src directory as base.
//
// @path rel_path the relative path to convert.
// @returns an absolute path.
FilePath GetSrcRelativePath(const wchar_t* rel_path);

// Converts a relative path to absolute using the executable directory as base.
//
// @path rel_path the relative path to convert.
// @returns an absolute path.
FilePath GetExeRelativePath(const wchar_t* rel_path);

// Converts a relative path to absolute using the output directory as base.
//
// @path rel_path the relative path to convert.
// @returns an absolute path.
FilePath GetOutputRelativePath(const wchar_t* rel_path);

// Converts a relative path to absolute using the test_data directory as base.
//
// @path rel_path the relative path to convert.
// @returns an absolute path.
FilePath GetExeTestDataRelativePath(const wchar_t* rel_path);

// Converts an absolute path to a relative path using the given root directory
// as a base.
//
// @param abs_path the absolute path to convert.
// @param root_path the root path to use.
// @returns the relative path to abs_path, starting from root. If there is no
//     relative path, it returns the empty path.
// @pre Both abs_path and root_path must be absolute paths.
FilePath GetRelativePath(const FilePath& abs_path, const FilePath& root_path);

// Converts an absolute path to a relative path using the current working
// directory as a base.
//
// @param abs_path the absolute path to convert.
// @returns the relative path to abs_path, starting from the current workign
//     directory. If there is no relative path, it returns the empty path.
FilePath GetRelativePath(const FilePath& abs_path);

}  // namespace testing

#endif  // SYZYGY_CORE_UNITTEST_UTIL_H_

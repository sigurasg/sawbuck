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

#ifndef SYZYGY_RELINK_RELINKER_H_
#define SYZYGY_RELINK_RELINKER_H_

#include "base/scoped_ptr.h"
#include "syzygy/core/block_graph.h"
#include "syzygy/pe/decomposer.h"
#include "syzygy/pe/image_layout.h"
#include "syzygy/pe/pe_file.h"
#include "syzygy/pe/pe_file_builder.h"
#include "syzygy/pe/pe_file_parser.h"
#include "syzygy/reorder/reorderer.h"

namespace relink {

// This base class is used to help track data required for relinking a binary.
// TODO(ericdingle): Find a better place and/or name for this.
class RelinkerBase {
 public:
  typedef core::BlockGraph BlockGraph;
  typedef BlockGraph::AddressSpace AddressSpace;
  typedef core::RelativeAddress RelativeAddress;
  typedef pe::ImageLayout ImageLayout;
  typedef pe::PEFileBuilder PEFileBuilder;
  typedef pe::PEFileParser PEFileParser;
  typedef pe::Decomposer Decomposer;
  typedef reorder::Reorderer Reorderer;

  RelinkerBase();
  virtual ~RelinkerBase();

 protected:
  // Sets up the basic relinker state for the given decomposed image.
  virtual bool Initialize(const ImageLayout& image_layout,
                          BlockGraph* block_graph);

  // Calculates header values for the relinked image, in prep for writing.
  bool FinalizeImageHeaders();

  // Commits the relinked image to disk at the given output path.
  bool WriteImage(const FilePath& output_path);

  // Copies a section from the old image into the new one.
  bool CopySection(const ImageLayout::SectionInfo& section);

  // Copies the blocks identified by iter_pair from the old image into
  // the new one, inserting them in order from insert_at.
  bool CopyBlocks(const AddressSpace::RangeMapConstIterPair& iter_pair,
                  RelativeAddress insert_at, size_t* bytes_copied);

  // TODO(siggi): Remove this accessor in favor of one to the image_layout_.
  const std::vector<ImageLayout::SectionInfo>& original_sections() const {
    CHECK(image_layout_ != NULL);
    return image_layout_->sections;
  }

  // TODO(siggi): This accessor isn't safe under mutation of the underlying
  //     block graph. This usage will vanish under the "new order of things",
  //     but until then, this accessor should be considered deprecated and
  //     dangerous.
  const BlockGraph::AddressSpace& original_addr_space() const {
    CHECK(image_layout_ != NULL);
    return image_layout_->blocks;
  }

  // Accesses the PE file builder.
  PEFileBuilder& builder() { return *builder_; }

 private:
  // Information from the original image.
  const ImageLayout* image_layout_;
  BlockGraph* block_graph_;

  // The builder that we use to construct the new image.
  scoped_ptr<PEFileBuilder> builder_;

  DISALLOW_COPY_AND_ASSIGN(RelinkerBase);
};

// This class keeps track of data we need around during reordering
// and after reordering for PDB rewriting.
class Relinker : public RelinkerBase {
 public:
  typedef Reorderer::Order Order;
  typedef pe::PEFile PEFile;

  // Default constructor.
  Relinker();

  // Sets the amount of padding to insert between blocks.
  void set_padding_length(size_t length);
  static size_t max_padding_length();
  static const uint8* padding_data();

  // Drives the basic relinking process.  This takes input image and pdb
  // paths and creates correponsing output files at the given output
  // paths, reordering sections as defined by a subclass' ReorderSection
  // method.
  virtual bool Relink(const FilePath& input_dll_path,
                      const FilePath& input_pdb_path,
                      const FilePath& output_dll_path,
                      const FilePath& output_pdb_path,
                      bool output_metadata);

 protected:
  // Sets up internal state based on the decomposed image.
  bool Initialize(const ImageLayout& image_layout,
                  BlockGraph* block_graph) OVERRIDE;

  // Performs whatever custom initialization of the order that it required.
  virtual bool SetupOrdering(const PEFile& pe_file,
                             const ImageLayout& image,
                             Order* order) = 0;

  // Function to be overridden by subclasses so that each subclass can have its
  // own reordering implementation.
  virtual bool ReorderSection(size_t section_index,
                              const ImageLayout::SectionInfo& section,
                              const Reorderer::Order& order) = 0;

  // Updates the debug information in the debug directory with our new GUID.
  bool UpdateDebugInformation(BlockGraph::Block* debug_directory_block,
                              const FilePath& output_pdb_path);

  // Call after relinking and finalizing image to create a PDB file that
  // matches the reordered image.
  bool WritePDBFile(const FilePath& input_path,
                    const FilePath& output_path);

  // Returns the GUID for the new image.
  const GUID& new_image_guid() { return new_image_guid_; }

  // Inserts the given length of padding. Increments the insert_at address.
  bool InsertPaddingBlock(BlockGraph::BlockType block_type,
                          size_t size,
                          RelativeAddress* insert_at);

  // Adds toolchain version information, and signature information for the
  // original DLL. This will be placed in its own section.
  bool WriteMetadataSection(const pe::PEFile& input_dll);

  // Copies the resource section from the original binary to the new one.
  bool CopyResourceSection();

  size_t padding_length() const { return padding_length_; }

 private:
  // The GUID we stamp into the new image and Pdb file.
  GUID new_image_guid_;

  // The amount of padding bytes to add between blocks.
  size_t padding_length_;

  // Stores the index of the resource section, if the original module has one.
  size_t resource_section_id_;

  DISALLOW_COPY_AND_ASSIGN(Relinker);
};

}  // namespace relink

#endif  // SYZYGY_RELINK_RELINKER_H_

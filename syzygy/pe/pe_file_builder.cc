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
#include "syzygy/pe/pe_file_builder.h"

#include <ctime>
#include <delayimp.h>

#include "base/string_util.h"
#include "syzygy/common/align.h"

namespace {

// Reference to the associated .asm file that constructs the DOS stub.
extern "C" void begin_dos_stub();
extern "C" void end_dos_stub();

using core::BlockGraph;
using core::RelativeAddress;
typedef std::vector<uint8> ByteVector;

// A utility class to help with formatting the relocations section.
class RelocWriter {
 public:
  RelocWriter() : curr_page_(0), curr_header_offset_(0) {
  }

  void WriteReloc(RelativeAddress addr) {
    DWORD page = PageFromAddr(addr);

    // Initialization case, open the first page.
    if (buf_.size() == 0)
      OpenPage(addr);

    // Close the current page, and open the next if we're outside it.
    if (page != curr_page_) {
      ClosePage();
      OpenPage(addr);
    }

    DCHECK_EQ(curr_page_, page);
    WORD type_offset = (IMAGE_REL_BASED_HIGHLOW << 12) | OffsetFromAddr(addr);
    Append(&type_offset, sizeof(type_offset));
  }

  void Close(ByteVector* relocs_out) {
    DCHECK(relocs_out != NULL);

    // Close the page in progress.
    if (buf_.size() != 0)
      ClosePage();

    relocs_out->swap(buf_);
  }

 private:
  static const DWORD kPageMask = 0x00000FFF;
  DWORD PageFromAddr(RelativeAddress addr) {
    return addr.value() & ~kPageMask;
  }

  WORD OffsetFromAddr(RelativeAddress addr) {
    return static_cast<WORD>(addr.value() & kPageMask);
  }

  void ClosePage() {
    size_t block_len = buf_.size() - curr_header_offset_;
    if (block_len % 4 != 0) {
      DCHECK_EQ(0U, block_len % 2);
      WORD filler = IMAGE_REL_BASED_ABSOLUTE << 12;
      Append(&filler, sizeof(filler));
      block_len += sizeof(filler);
    }
    DCHECK_EQ(0U, block_len % 4);

    IMAGE_BASE_RELOCATION* header =
        reinterpret_cast<IMAGE_BASE_RELOCATION*>(&buf_.at(curr_header_offset_));

    header->SizeOfBlock = block_len;
  }

  void OpenPage(RelativeAddress addr) {
    curr_page_ = PageFromAddr(addr);
    curr_header_offset_ = buf_.size();

    IMAGE_BASE_RELOCATION header = { curr_page_, sizeof(header) };
    Append(&header, sizeof(header));
  }

  void Append(const void* data, size_t size) {
    const uint8* buf = reinterpret_cast<const uint8*>(data);
    buf_.insert(buf_.end(), buf, buf + size);
  }

  // The buffer where we write the data.
  ByteVector buf_;

  // The current page our header is for.
  DWORD curr_page_;

  // The offset of the last IMAGE_BASE_RELOCATION header we wrote.
  size_t curr_header_offset_;
};

// Returns true iff ref is a valid reference in addr_space.
bool IsValidReference(const BlockGraph::AddressSpace& addr_space,
                      const BlockGraph::Reference& ref) {
  // Check that there is a referenced block.
  if (ref.referenced() == NULL)
    return false;

  // Check that the block is in the image.
  RelativeAddress addr;
  if (!addr_space.GetAddressOf(ref.referenced(), &addr))
    return false;

  return true;
}

}  // namespace

namespace pe {

PEFileBuilder::PEFileBuilder(BlockGraph* block_graph)
    : image_layout_(block_graph),
      section_alignment_(kDefaultSectionAlignment),
      file_alignment_(kDefaultFileAlignment),
      next_section_address_(kDefaultSectionAlignment),
      dos_header_block_(NULL),
      nt_headers_block_(NULL) {
}

bool PEFileBuilder::SetImageHeaders(BlockGraph::Block* dos_header_block) {
  DCHECK(dos_header_block != NULL);
  if (dos_header_block->data() == NULL ||
      dos_header_block->data_size() < sizeof(IMAGE_DOS_HEADER)) {
    LOG(ERROR) << "DOS header missing or too short.";
    return false;
  }

  const IMAGE_DOS_HEADER* dos_header =
      reinterpret_cast<const IMAGE_DOS_HEADER*>(dos_header_block->data());
  if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
    LOG(ERROR) << "DOS header has incorrect signature.";
    return false;
  }
  // TODO(siggi): Validate more DOS header info.

  BlockGraph::Reference nt_headers_ref;
  if (!dos_header_block->GetReference(offsetof(IMAGE_DOS_HEADER, e_lfanew),
                                      &nt_headers_ref)) {
    LOG(ERROR) << "DOS header does not reference NT header.";
    return false;
  }
  if (nt_headers_ref.offset() != 0) {
    LOG(ERROR) << "DOS header reference does not refer to the start of block.";
    return false;
  }

  BlockGraph::Block* nt_headers_block = nt_headers_ref.referenced();
  DCHECK(nt_headers_block != NULL);
  if (nt_headers_block->data() == NULL ||
      nt_headers_block->data_size() < sizeof(IMAGE_NT_HEADERS)) {
    LOG(ERROR) << "NT headers missing or too short.";
    return false;
  }

  const IMAGE_NT_HEADERS* nt_headers =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(nt_headers_block->data());
  if (nt_headers->Signature != IMAGE_NT_SIGNATURE ||
      nt_headers->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
    LOG(ERROR) << "NT headers have incorrect signature(s).";
    return false;
  }
  // TODO(siggi): Validate more header info.

  dos_header_block_ = dos_header_block;
  nt_headers_block_ = nt_headers_block;

  return true;
}

void PEFileBuilder::SetAllocationParameters(size_t header_size,
                                            size_t section_alignment,
                                            size_t file_alignment) {
  DCHECK(header_size > 0);
  DCHECK(section_alignment > 0 && common::IsPowerOfTwo(section_alignment));
  DCHECK(file_alignment > 0 && common::IsPowerOfTwo(file_alignment));
  DCHECK_EQ(0U, image_layout_.segments.size());

  section_alignment_ = section_alignment;
  file_alignment_ = file_alignment;
  next_section_address_ =
      RelativeAddress(common::AlignUp(header_size, section_alignment_));
}

RelativeAddress PEFileBuilder::AddSegment(const char* name,
                                          size_t size,
                                          size_t data_size,
                                          uint32 characteristics) {
  DCHECK_NE(0U, size);

  data_size = common::AlignUp(data_size, file_alignment_);
  RelativeAddress section_base = next_section_address_;

  ImageLayout::SegmentInfo segment;
  segment.addr = next_section_address_;
  segment.name = name;
  segment.size = size;
  segment.data_size = data_size;
  segment.characteristics = characteristics;

  image_layout_.segments.push_back(segment);

  next_section_address_ += common::AlignUp(size, section_alignment_);

  return section_base;
}

bool PEFileBuilder::SetDataDirectoryEntry(size_t entry_index,
                                          BlockGraph::Block* block) {
  DCHECK_LT(entry_index, static_cast<size_t>(IMAGE_NUMBEROF_DIRECTORY_ENTRIES));
  DCHECK(block != NULL);

  BlockGraph::Reference ref(BlockGraph::RELATIVE_REF,
                            sizeof(RelativeAddress),
                            block,
                            0);
  return SetDataDirectoryEntry(entry_index, ref, block->size());
}

bool PEFileBuilder::SetDataDirectoryEntry(size_t entry_index,
                                          const BlockGraph::Reference& entry,
                                          size_t entry_size) {
  DCHECK_LT(entry_index, static_cast<size_t>(IMAGE_NUMBEROF_DIRECTORY_ENTRIES));
  DCHECK(IsValidReference(image_layout_.blocks, entry));
  DCHECK_EQ(BlockGraph::RELATIVE_REF, entry.type());
  DCHECK(entry_size != NULL);
  DCHECK(nt_headers_block_ != NULL);
  DCHECK(nt_headers_block_->data() != NULL);
  DCHECK(nt_headers_block_->data_size() >= sizeof(IMAGE_NT_HEADERS));

  IMAGE_NT_HEADERS* nt_headers =
      reinterpret_cast<IMAGE_NT_HEADERS*>(nt_headers_block_->GetMutableData());

  nt_headers->OptionalHeader.DataDirectory[entry_index].Size = entry_size;
  size_t entry_offset =
      offsetof(IMAGE_NT_HEADERS,
               OptionalHeader.DataDirectory[entry_index].VirtualAddress);
  nt_headers_block_->SetReference(entry_offset, entry);

  return true;
}

bool PEFileBuilder::CreateRelocsSection() {
  RelocWriter writer;

  // Iterate over all blocks in the address space, in the
  // order of increasing addresses.
  BlockGraph::AddressSpace::RangeMap::const_iterator it(
      image_layout_.blocks.address_space_impl().ranges().begin());
  BlockGraph::AddressSpace::RangeMap::const_iterator end(
      image_layout_.blocks.address_space_impl().ranges().end());

  for (; it != end; ++it) {
    const BlockGraph::Block* block = it->second;
    RelativeAddress block_addr;
    CHECK(image_layout_.blocks.GetAddressOf(block, &block_addr));

    // Iterate over all outgoing references in this block in
    // order of increasing offset.
    BlockGraph::Block::ReferenceMap::const_iterator ref_it(
        block->references().begin());
    BlockGraph::Block::ReferenceMap::const_iterator ref_end(
        block->references().end());
    for (; ref_it != ref_end; ++ref_it) {
      // Add each absolute reference to the relocs.
      if (ref_it->second.type() == BlockGraph::ABSOLUTE_REF) {
        writer.WriteReloc(block_addr + ref_it->first);
      }
    }
  }

  // Get the relocations data from the writer.
  ByteVector relocs;
  writer.Close(&relocs);

  // Create a new image segment for the relocs.
  const uint32 kRelocCharacteristics = IMAGE_SCN_CNT_INITIALIZED_DATA |
      IMAGE_SCN_MEM_DISCARDABLE | IMAGE_SCN_MEM_READ;
  size_t relocs_file_size =
      common::AlignUp(relocs.size(), file_alignment_);
  RelativeAddress section_base = AddSegment(".reloc",
                                            relocs.size(),
                                            relocs_file_size,
                                            kRelocCharacteristics);
  DCHECK_NE(RelativeAddress(0), section_base);

  // And add a corresponding block referring the data to the address space.
  BlockGraph::Block* block =
      image_layout_.blocks.AddBlock(BlockGraph::DATA_BLOCK,
                                    section_base,
                                    relocs.size(),
                                    ".relocs");
  if (block == NULL || block->CopyData(relocs.size(), &relocs.at(0)) == NULL) {
    LOG(ERROR) << "Failed to add relocs block to image";
    return false;
  }

  // Store the new data directory entry.
  return SetDataDirectoryEntry(IMAGE_DIRECTORY_ENTRY_BASERELOC, block);
}

bool PEFileBuilder::FinalizeHeaders() {
  // The DOS and NT headers must be set at this point.
  DCHECK(dos_header_block_ != NULL);
  DCHECK(nt_headers_block_ != NULL);

  if (!UpdateDosHeader()) {
    LOG(ERROR) << "Failed update DOS header.";
    return false;
  }

  // Resize the NT headers block if needed to fit the new section headers.
  size_t nt_headers_size = sizeof(IMAGE_NT_HEADERS) +
      sizeof(IMAGE_SECTION_HEADER) * image_layout_.segments.size();
  if (nt_headers_block_->ResizeData(nt_headers_size) == NULL) {
    LOG(ERROR) << "Failed to resize NT headers block.";
    return false;
  }

  // Update the NT header block.
  IMAGE_NT_HEADERS* nt_headers =
      reinterpret_cast<IMAGE_NT_HEADERS*>(nt_headers_block_->GetMutableData());

  nt_headers->OptionalHeader.SectionAlignment = section_alignment_;
  nt_headers->OptionalHeader.FileAlignment = file_alignment_;

  nt_headers->OptionalHeader.CheckSum = 0;
  nt_headers->FileHeader.NumberOfSections = image_layout_.segments.size();

  // Iterate through our sections to initialize the code/data
  // fields in the NT header.
  for (size_t i = 0; i < image_layout_.segments.size(); ++i) {
    const ImageLayout::SegmentInfo& segment = image_layout_.segments[i];

    if (segment.characteristics& IMAGE_SCN_CNT_CODE) {
      nt_headers->OptionalHeader.SizeOfCode += segment.data_size;
      if (nt_headers->OptionalHeader.BaseOfCode == 0) {
        nt_headers->OptionalHeader.BaseOfCode = segment.addr.value();
      }
    }
    if (segment.characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) {
      nt_headers->OptionalHeader.SizeOfInitializedData += segment.data_size;

      if (nt_headers->OptionalHeader.BaseOfData == 0)
        nt_headers->OptionalHeader.BaseOfData = segment.addr.value();
    }
    if (segment.characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) {
      nt_headers->OptionalHeader.SizeOfUninitializedData +=
          segment.data_size;
      if (nt_headers->OptionalHeader.BaseOfData == 0)
        nt_headers->OptionalHeader.BaseOfData = segment.addr.value();
    }
  }

  // Update the image layout from the NT headers.
  // TODO(siggi): This feels awfully backwards and awfully redundant somehow.
  //    Better to remove the header info from ImageLayout and use the data
  //    in the NT headers in the image.
  CopyNtHeaderToImageLayout(nt_headers, &image_layout_.header_info);

  nt_headers->OptionalHeader.SizeOfImage = next_section_address_.value();

  // Get the section headers pointer.
  IMAGE_SECTION_HEADER* section_headers =
      reinterpret_cast<IMAGE_SECTION_HEADER*>(nt_headers + 1);
  core::FileOffsetAddress segment_file_start(
      image_layout_.header_info.size_of_headers);

  for (size_t i = 0; i < image_layout_.segments.size(); ++i) {
    const ImageLayout::SegmentInfo& segment = image_layout_.segments[i];
    IMAGE_SECTION_HEADER& hdr = section_headers[i];

    // Start by zeroing the header to get rid of any old crud in it.
    memset(&hdr, 0, sizeof(hdr));

    strncpy(reinterpret_cast<char*>(hdr.Name),
            segment.name.c_str(),
            arraysize(hdr.Name));
    hdr.Misc.VirtualSize = segment.size;
    hdr.VirtualAddress = segment.addr.value();
    hdr.SizeOfRawData = segment.data_size;
    hdr.PointerToRawData = segment_file_start.value();
    hdr.Characteristics = segment.characteristics;

    segment_file_start += segment.data_size;
  }

  // Now assign the header blocks addresses in the new image layout.
  if (!image_layout_.blocks.InsertBlock(RelativeAddress(0),
                                        dos_header_block_)) {
    LOG(ERROR) << "Unable to assign DOS header to new image layout.";
    return false;
  }
  if (!image_layout_.blocks.InsertBlock(
      RelativeAddress(dos_header_block_->size()), nt_headers_block_)) {
    LOG(ERROR) << "Unable to assign DOS header to new image layout.";
    return false;
  }

  return true;
}

bool PEFileBuilder::UpdateDosHeader() {
  DCHECK(dos_header_block_ != NULL);

  const uint8* begin_dos_stub_ptr =
      reinterpret_cast<const uint8*>(&begin_dos_stub);
  const uint8* end_dos_stub_ptr =
      reinterpret_cast<const uint8*>(&end_dos_stub);

  // The DOS header has to be a multiple of 16 bytes for historic reasons.
  size_t dos_header_size = common::AlignUp(
      sizeof(IMAGE_DOS_HEADER) + end_dos_stub_ptr - begin_dos_stub_ptr, 16);
  if (dos_header_block_->ResizeData(dos_header_size) == NULL) {
    LOG(ERROR) << "Unable to resize DOS header.";
    return false;
  }

  IMAGE_DOS_HEADER* dos_header_ptr =
      reinterpret_cast<IMAGE_DOS_HEADER*>(dos_header_block_->GetMutableData());
  if (dos_header_ptr == NULL) {
    LOG(ERROR) << "Unable to allocate DOS header data.";
    return false;
  }

  memset(dos_header_ptr, 0, sizeof(*dos_header_ptr));
  memcpy(dos_header_ptr + 1,
         begin_dos_stub_ptr,
         end_dos_stub_ptr - begin_dos_stub_ptr);

  dos_header_ptr->e_magic = IMAGE_DOS_SIGNATURE;
  // Calculate the number of bytes used on the last DOS executable "page".
  dos_header_ptr->e_cblp = dos_header_size % 512;
  // Calculate the number of pages used by the DOS executable.
  dos_header_ptr->e_cp = dos_header_size / 512;
  // Count the last page if we didn't have an even multiple
  if (dos_header_ptr->e_cblp != 0)
    dos_header_ptr->e_cp++;

  // Header length in "paragraphs".
  dos_header_ptr->e_cparhdr = sizeof(*dos_header_ptr) / 16;

  // Set this to max allowed, just because.
  dos_header_ptr->e_maxalloc = 0xFFFF;

  // Location of relocs - our header has zero relocs, but we set this anyway.
  dos_header_ptr->e_lfarlc = sizeof(*dos_header_ptr);

  return true;
}

}  // namespace pe

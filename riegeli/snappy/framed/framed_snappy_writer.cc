// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "riegeli/snappy/framed/framed_snappy_writer.h"

#include <stddef.h>
#include <stdint.h>

#include <cstring>
#include <limits>
#include <string>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "crc32c/crc32c.h"
#include "riegeli/base/base.h"
#include "riegeli/base/buffer.h"
#include "riegeli/bytes/writer.h"
#include "riegeli/endian/endian_writing.h"
#include "snappy.h"

namespace riegeli {

namespace {

// https://github.com/google/snappy/blob/e9e11b84e629c3e06fbaa4f0a86de02ceb9d6992/framing_format.txt#L39
inline uint32_t MaskChecksum(uint32_t x) {
  return ((x >> 15) | (x << 17)) + 0xa282ead8;
}

}  // namespace

void FramedSnappyWriterBase::Initialize(Writer* dest) {
  RIEGELI_ASSERT(dest != nullptr)
      << "Failed precondition of FramedSnappyWriter: null Writer pointer";
  if (dest->pos() == 0) {
    // Stream identifier.
    if (ABSL_PREDICT_FALSE(!dest->Write("\xff\x06\x00\x00"
                                        "sNaPpY",
                                        10))) {
      Fail(*dest);
    }
  } else {
    if (ABSL_PREDICT_FALSE(!dest->healthy())) Fail(*dest);
  }
}

void FramedSnappyWriterBase::DefaultAnnotateStatus() {
  RIEGELI_ASSERT(!not_failed())
      << "Failed precondition of Object::DefaultAnnotateStatus(): "
         "Object not failed";
  if (is_open()) AnnotateStatus(absl::StrCat("at uncompressed byte ", pos()));
}

bool FramedSnappyWriterBase::PushBehindScratch() {
  RIEGELI_ASSERT_EQ(available(), 0u)
      << "Failed precondition of PushableWriter::PushBehindScratch(): "
         "some space available, use Push() instead";
  RIEGELI_ASSERT(!scratch_used())
      << "Failed precondition of PushableWriter::PushBehindScratch():"
         "scratch used";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Writer& dest = *dest_writer();
  if (ABSL_PREDICT_FALSE(!PushInternal(dest))) return false;
  if (ABSL_PREDICT_FALSE(start_pos() == std::numeric_limits<Position>::max())) {
    return FailOverflow();
  }
  const size_t length =
      UnsignedMin(BufferLength(1, snappy::kBlockSize, size_hint_, start_pos()),
                  std::numeric_limits<Position>::max() - start_pos());
  uncompressed_.Reset(length);
  set_buffer(uncompressed_.data(), length);
  return true;
}

inline bool FramedSnappyWriterBase::PushInternal(Writer& dest) {
  const size_t uncompressed_length = start_to_cursor();
  RIEGELI_ASSERT_LE(uncompressed_length, snappy::kBlockSize)
      << "Failed invariant of FramedSnappyWriterBase: buffer too large";
  if (uncompressed_length == 0) return true;
  set_cursor(start());
  const char* const uncompressed_data = cursor();
  if (ABSL_PREDICT_FALSE(
          !dest.Push(2 * sizeof(uint32_t) +
                     snappy::MaxCompressedLength(uncompressed_length)))) {
    return Fail(dest);
  }
  char* const compressed_chunk = dest.cursor();
  size_t compressed_length;
  snappy::RawCompress(uncompressed_data, uncompressed_length,
                      compressed_chunk + 2 * sizeof(uint32_t),
                      &compressed_length);
  if (compressed_length < uncompressed_length) {
    WriteLittleEndian32(
        IntCast<uint32_t>(0x00 /* Compressed data */ |
                          ((sizeof(uint32_t) + compressed_length) << 8)),
        compressed_chunk);
  } else {
    std::memcpy(compressed_chunk + 2 * sizeof(uint32_t), uncompressed_data,
                uncompressed_length);
    compressed_length = uncompressed_length;
    WriteLittleEndian32(
        IntCast<uint32_t>(0x01 /* Uncompressed data */ |
                          ((sizeof(uint32_t) + compressed_length) << 8)),
        compressed_chunk);
  }
  WriteLittleEndian32(
      MaskChecksum(crc32c::Crc32c(uncompressed_data, uncompressed_length)),
      compressed_chunk + sizeof(uint32_t));
  dest.move_cursor(2 * sizeof(uint32_t) + compressed_length);
  move_start_pos(uncompressed_length);
  return true;
}

bool FramedSnappyWriterBase::FlushBehindScratch(FlushType flush_type) {
  RIEGELI_ASSERT(!scratch_used())
      << "Failed precondition of PushableWriter::FlushBehindScratch(): "
         "scratch used";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Writer& dest = *dest_writer();
  return PushInternal(dest);
}

}  // namespace riegeli

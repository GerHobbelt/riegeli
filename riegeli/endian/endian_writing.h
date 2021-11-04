// Copyright 2017 Google LLC
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

#ifndef RIEGELI_ENDIAN_ENDIAN_WRITING_H_
#define RIEGELI_ENDIAN_ENDIAN_WRITING_H_

#include <stdint.h>

#include <cstring>

#include "absl/base/optimization.h"
#include "absl/types/span.h"
#include "riegeli/bytes/backward_writer.h"
#include "riegeli/bytes/writer.h"
#include "riegeli/endian/endian_internal.h"

namespace riegeli {

// Writes a number in a fixed width Little/Big Endian encoding.
//
// Return values:
//  * `true`  - success (`dest.healthy()`)
//  * `false` - failure (`!dest.healthy()`)
bool WriteLittleEndian16(uint16_t data, Writer& dest);
bool WriteLittleEndian32(uint32_t data, Writer& dest);
bool WriteLittleEndian64(uint64_t data, Writer& dest);
bool WriteBigEndian16(uint16_t data, Writer& dest);
bool WriteBigEndian32(uint32_t data, Writer& dest);
bool WriteBigEndian64(uint64_t data, Writer& dest);
bool WriteLittleEndian16(uint16_t data, BackwardWriter& dest);
bool WriteLittleEndian32(uint32_t data, BackwardWriter& dest);
bool WriteLittleEndian64(uint64_t data, BackwardWriter& dest);
bool WriteBigEndian16(uint16_t data, BackwardWriter& dest);
bool WriteBigEndian32(uint32_t data, BackwardWriter& dest);
bool WriteBigEndian64(uint64_t data, BackwardWriter& dest);

// Writes an array of numbers in a fixed width Little/Big Endian encoding.
//
// This is faster than writing them individually for native endianness.
//
// Return values:
//  * `true`  - success (`dest.healthy()`)
//  * `false` - failure (`!dest.healthy()`)
bool WriteLittleEndian16s(absl::Span<const uint16_t> data, Writer& dest);
bool WriteLittleEndian32s(absl::Span<const uint32_t> data, Writer& dest);
bool WriteLittleEndian64s(absl::Span<const uint64_t> data, Writer& dest);
bool WriteBigEndian16s(absl::Span<const uint16_t> data, Writer& dest);
bool WriteBigEndian32s(absl::Span<const uint32_t> data, Writer& dest);
bool WriteBigEndian64s(absl::Span<const uint64_t> data, Writer& dest);

// Writes a number in a fixed width Little/Big Endian encoding to an array.
//
// Writes `sizeof(uint{16,32,64}_t)` bytes to `dest[]`.
void WriteLittleEndian16(uint16_t data, char* dest);
void WriteLittleEndian32(uint32_t data, char* dest);
void WriteLittleEndian64(uint64_t data, char* dest);
void WriteBigEndian16(uint16_t data, char* dest);
void WriteBigEndian32(uint32_t data, char* dest);
void WriteBigEndian64(uint64_t data, char* dest);

// Writes an array of numbers in a fixed width Little/Big Endian encoding to an
// array.
//
// This is faster than writing them individually for native endianness.
//
// Writes `data.size() * sizeof(uint{16,32,64}_t)` bytes to `dest[]`.
void WriteLittleEndian16s(absl::Span<const uint16_t> data, char* dest);
void WriteLittleEndian32s(absl::Span<const uint32_t> data, char* dest);
void WriteLittleEndian64s(absl::Span<const uint64_t> data, char* dest);
void WriteBigEndian16s(absl::Span<const uint16_t> data, char* dest);
void WriteBigEndian32s(absl::Span<const uint32_t> data, char* dest);
void WriteBigEndian64s(absl::Span<const uint64_t> data, char* dest);

// Implementation details follow.

inline bool WriteLittleEndian16(uint16_t data, Writer& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint16_t)))) return false;
  WriteLittleEndian16(data, dest.cursor());
  dest.move_cursor(sizeof(uint16_t));
  return true;
}

inline bool WriteLittleEndian32(uint32_t data, Writer& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint32_t)))) return false;
  WriteLittleEndian32(data, dest.cursor());
  dest.move_cursor(sizeof(uint32_t));
  return true;
}

inline bool WriteLittleEndian64(uint64_t data, Writer& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint64_t)))) return false;
  WriteLittleEndian64(data, dest.cursor());
  dest.move_cursor(sizeof(uint64_t));
  return true;
}

inline bool WriteBigEndian16(uint16_t data, Writer& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint16_t)))) return false;
  WriteBigEndian16(data, dest.cursor());
  dest.move_cursor(sizeof(uint16_t));
  return true;
}

inline bool WriteBigEndian32(uint32_t data, Writer& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint32_t)))) return false;
  WriteBigEndian32(data, dest.cursor());
  dest.move_cursor(sizeof(uint32_t));
  return true;
}

inline bool WriteBigEndian64(uint64_t data, Writer& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint64_t)))) return false;
  WriteBigEndian64(data, dest.cursor());
  dest.move_cursor(sizeof(uint64_t));
  return true;
}

inline bool WriteLittleEndian16(uint16_t data, BackwardWriter& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint16_t)))) return false;
  dest.move_cursor(sizeof(uint16_t));
  WriteLittleEndian16(data, dest.cursor());
  return true;
}
inline bool WriteLittleEndian32(uint32_t data, BackwardWriter& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint32_t)))) return false;
  dest.move_cursor(sizeof(uint32_t));
  WriteLittleEndian32(data, dest.cursor());
  return true;
}

inline bool WriteLittleEndian64(uint64_t data, BackwardWriter& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint64_t)))) return false;
  dest.move_cursor(sizeof(uint64_t));
  WriteLittleEndian64(data, dest.cursor());
  return true;
}

inline bool WriteBigEndian16(uint16_t data, BackwardWriter& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint16_t)))) return false;
  dest.move_cursor(sizeof(uint16_t));
  WriteBigEndian16(data, dest.cursor());
  return true;
}

inline bool WriteBigEndian32(uint32_t data, BackwardWriter& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint32_t)))) return false;
  dest.move_cursor(sizeof(uint32_t));
  WriteBigEndian32(data, dest.cursor());
  return true;
}

inline bool WriteBigEndian64(uint64_t data, BackwardWriter& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(sizeof(uint64_t)))) return false;
  dest.move_cursor(sizeof(uint64_t));
  WriteBigEndian64(data, dest.cursor());
  return true;
}

inline bool WriteLittleEndian16s(absl::Span<const uint16_t> data,
                                 Writer& dest) {
  if (internal::IsLittleEndian()) {
    return dest.Write(reinterpret_cast<const char*>(data.data()),
                      data.size() * sizeof(uint16_t));
  } else {
    for (const uint16_t value : data) {
      if (ABSL_PREDICT_FALSE(!WriteLittleEndian16(value, dest))) return false;
    }
    return true;
  }
}

inline bool WriteLittleEndian32s(absl::Span<const uint32_t> data,
                                 Writer& dest) {
  if (internal::IsLittleEndian()) {
    return dest.Write(reinterpret_cast<const char*>(data.data()),
                      data.size() * sizeof(uint32_t));
  } else {
    for (const uint32_t value : data) {
      if (ABSL_PREDICT_FALSE(!WriteLittleEndian32(value, dest))) return false;
    }
    return true;
  }
}

inline bool WriteLittleEndian64s(absl::Span<const uint64_t> data,
                                 Writer& dest) {
  if (internal::IsLittleEndian()) {
    return dest.Write(reinterpret_cast<const char*>(data.data()),
                      data.size() * sizeof(uint64_t));
  } else {
    for (const uint64_t value : data) {
      if (ABSL_PREDICT_FALSE(!WriteLittleEndian64(value, dest))) return false;
    }
    return true;
  }
}

inline bool WriteBigEndian16s(absl::Span<const uint16_t> data, Writer& dest) {
  if (internal::IsBigEndian()) {
    return dest.Write(reinterpret_cast<const char*>(data.data()),
                      data.size() * sizeof(uint16_t));
  } else {
    for (const uint16_t value : data) {
      if (ABSL_PREDICT_FALSE(!WriteBigEndian16(value, dest))) return false;
    }
    return true;
  }
}

inline bool WriteBigEndian32s(absl::Span<const uint32_t> data, Writer& dest) {
  if (internal::IsBigEndian()) {
    return dest.Write(reinterpret_cast<const char*>(data.data()),
                      data.size() * sizeof(uint32_t));
  } else {
    for (const uint32_t value : data) {
      if (ABSL_PREDICT_FALSE(!WriteBigEndian32(value, dest))) return false;
    }
    return true;
  }
}

inline bool WriteBigEndian64s(absl::Span<const uint64_t> data, Writer& dest) {
  if (internal::IsBigEndian()) {
    return dest.Write(reinterpret_cast<const char*>(data.data()),
                      data.size() * sizeof(uint64_t));
  } else {
    for (const uint64_t value : data) {
      if (ABSL_PREDICT_FALSE(!WriteBigEndian64(value, dest))) return false;
    }
    return true;
  }
}

namespace internal {

// If these functions are manually inlined into their callers, clang generates
// poor code (with byte shifting even for native endianness).

inline uint16_t EncodeLittleEndian16(uint16_t data) {
  uint16_t encoded;
  unsigned char* const ptr = reinterpret_cast<unsigned char*>(&encoded);
  ptr[0] = static_cast<unsigned char>(data);
  ptr[1] = static_cast<unsigned char>(data >> 8);
  return encoded;
}

inline uint32_t EncodeLittleEndian32(uint32_t data) {
  uint32_t encoded;
  unsigned char* const ptr = reinterpret_cast<unsigned char*>(&encoded);
  ptr[0] = static_cast<unsigned char>(data);
  ptr[1] = static_cast<unsigned char>(data >> 8);
  ptr[2] = static_cast<unsigned char>(data >> (2 * 8));
  ptr[3] = static_cast<unsigned char>(data >> (3 * 8));
  return encoded;
}

inline uint64_t EncodeLittleEndian64(uint64_t data) {
  uint64_t encoded;
  unsigned char* const ptr = reinterpret_cast<unsigned char*>(&encoded);
  ptr[0] = static_cast<unsigned char>(data);
  ptr[1] = static_cast<unsigned char>(data >> 8);
  ptr[2] = static_cast<unsigned char>(data >> (2 * 8));
  ptr[3] = static_cast<unsigned char>(data >> (3 * 8));
  ptr[4] = static_cast<unsigned char>(data >> (4 * 8));
  ptr[5] = static_cast<unsigned char>(data >> (5 * 8));
  ptr[6] = static_cast<unsigned char>(data >> (6 * 8));
  ptr[7] = static_cast<unsigned char>(data >> (7 * 8));
  return encoded;
}

inline uint16_t EncodeBigEndian16(uint16_t data) {
  uint16_t encoded;
  unsigned char* const ptr = reinterpret_cast<unsigned char*>(&encoded);
  ptr[0] = static_cast<unsigned char>(data >> 8);
  ptr[1] = static_cast<unsigned char>(data);
  return encoded;
}

inline uint32_t EncodeBigEndian32(uint32_t data) {
  uint32_t encoded;
  unsigned char* const ptr = reinterpret_cast<unsigned char*>(&encoded);
  ptr[0] = static_cast<unsigned char>(data >> (3 * 8));
  ptr[1] = static_cast<unsigned char>(data >> (2 * 8));
  ptr[2] = static_cast<unsigned char>(data >> 8);
  ptr[3] = static_cast<unsigned char>(data);
  return encoded;
}

inline uint64_t EncodeBigEndian64(uint64_t data) {
  uint64_t encoded;
  unsigned char* const ptr = reinterpret_cast<unsigned char*>(&encoded);
  ptr[0] = static_cast<unsigned char>(data >> (7 * 8));
  ptr[1] = static_cast<unsigned char>(data >> (6 * 8));
  ptr[2] = static_cast<unsigned char>(data >> (5 * 8));
  ptr[3] = static_cast<unsigned char>(data >> (4 * 8));
  ptr[4] = static_cast<unsigned char>(data >> (3 * 8));
  ptr[5] = static_cast<unsigned char>(data >> (2 * 8));
  ptr[6] = static_cast<unsigned char>(data >> 8);
  ptr[7] = static_cast<unsigned char>(data);
  return encoded;
}

}  // namespace internal

inline void WriteLittleEndian16(uint16_t data, char* dest) {
  const uint16_t encoded = internal::EncodeLittleEndian16(data);
  std::memcpy(dest, &encoded, sizeof(uint16_t));
}

inline void WriteLittleEndian32(uint32_t data, char* dest) {
  const uint32_t encoded = internal::EncodeLittleEndian32(data);
  std::memcpy(dest, &encoded, sizeof(uint32_t));
}

inline void WriteLittleEndian64(uint64_t data, char* dest) {
  const uint64_t encoded = internal::EncodeLittleEndian64(data);
  std::memcpy(dest, &encoded, sizeof(uint64_t));
}

inline void WriteBigEndian16(uint16_t data, char* dest) {
  const uint16_t encoded = internal::EncodeBigEndian16(data);
  std::memcpy(dest, &encoded, sizeof(uint16_t));
}

inline void WriteBigEndian32(uint32_t data, char* dest) {
  const uint32_t encoded = internal::EncodeBigEndian32(data);
  std::memcpy(dest, &encoded, sizeof(uint32_t));
}

inline void WriteBigEndian64(uint64_t data, char* dest) {
  const uint64_t encoded = internal::EncodeBigEndian64(data);
  std::memcpy(dest, &encoded, sizeof(uint64_t));
}

inline void WriteLittleEndian16s(absl::Span<const uint16_t> data, char* dest) {
  if (internal::IsLittleEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !data.empty())) {
      std::memcpy(dest, data.data(), data.size() * sizeof(uint16_t));
    }
  } else {
    for (const uint16_t value : data) {
      WriteLittleEndian16(value, dest);
      dest += sizeof(uint16_t);
    }
  }
}

inline void WriteLittleEndian32s(absl::Span<const uint32_t> data, char* dest) {
  if (internal::IsLittleEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !data.empty())) {
      std::memcpy(dest, data.data(), data.size() * sizeof(uint32_t));
    }
  } else {
    for (const uint32_t value : data) {
      WriteLittleEndian32(value, dest);
      dest += sizeof(uint32_t);
    }
  }
}

inline void WriteLittleEndian64s(absl::Span<const uint64_t> data, char* dest) {
  if (internal::IsLittleEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !data.empty())) {
      std::memcpy(dest, data.data(), data.size() * sizeof(uint64_t));
    }
  } else {
    for (const uint64_t value : data) {
      WriteLittleEndian64(value, dest);
      dest += sizeof(uint64_t);
    }
  }
}

inline void WriteBigEndian16s(absl::Span<const uint16_t> data, char* dest) {
  if (internal::IsBigEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !data.empty())) {
      std::memcpy(dest, data.data(), data.size() * sizeof(uint16_t));
    }
  } else {
    for (const uint16_t value : data) {
      WriteBigEndian16(value, dest);
      dest += sizeof(uint16_t);
    }
  }
}

inline void WriteBigEndian32s(absl::Span<const uint32_t> data, char* dest) {
  if (internal::IsBigEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !data.empty())) {
      std::memcpy(dest, data.data(), data.size() * sizeof(uint32_t));
    }
  } else {
    for (const uint32_t value : data) {
      WriteBigEndian32(value, dest);
      dest += sizeof(uint32_t);
    }
  }
}

inline void WriteBigEndian64s(absl::Span<const uint64_t> data, char* dest) {
  if (internal::IsBigEndian()) {
    if (ABSL_PREDICT_TRUE(
            // `std::memcpy(nullptr, _, 0)` and `std::memcpy(_, nullptr, 0)` are
            // undefined.
            !data.empty())) {
      std::memcpy(dest, data.data(), data.size() * sizeof(uint64_t));
    }
  } else {
    for (const uint64_t value : data) {
      WriteBigEndian64(value, dest);
      dest += sizeof(uint64_t);
    }
  }
}

}  // namespace riegeli

#endif  // RIEGELI_ENDIAN_ENDIAN_WRITING_H_

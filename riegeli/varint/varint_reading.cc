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

#include "riegeli/varint/varint_reading.h"

#include <stddef.h>
#include <stdint.h>

#include "absl/base/optimization.h"
#include "absl/types/optional.h"
#include "riegeli/bytes/reader.h"

namespace riegeli {
namespace internal {

absl::optional<ReadFromStringResult<uint32_t>> ReadVarint32Slow(
    const char* src, const char* limit, uint32_t result) {
  uint8_t byte;
  size_t shift = kReadVarintSlowThreshold;
  do {
    if (ABSL_PREDICT_FALSE(src == limit)) return absl::nullopt;
    byte = static_cast<uint8_t>(*src++);
    result += (uint32_t{byte} - 1) << shift;
    shift += 7;
    if (ABSL_PREDICT_FALSE(shift == kMaxLengthVarint32 * 7)) {
      // Last possible byte.
      if (ABSL_PREDICT_FALSE(
              byte >= uint8_t{1} << (32 - (kMaxLengthVarint32 - 1) * 7))) {
        // The representation is longer than `kMaxLengthVarint32`
        // or the represented value does not fit in `uint32_t`.
        return absl::nullopt;
      }
      break;
    }
  } while (byte >= 0x80);
  return ReadFromStringResult<uint32_t>{result, src};
}

absl::optional<ReadFromStringResult<uint64_t>> ReadVarint64Slow(
    const char* src, const char* limit, uint64_t result) {
  uint8_t byte;
  size_t shift = kReadVarintSlowThreshold;
  do {
    if (ABSL_PREDICT_FALSE(src == limit)) return absl::nullopt;
    byte = static_cast<uint8_t>(*src++);
    result += (uint64_t{byte} - 1) << shift;
    shift += 7;
    if (ABSL_PREDICT_FALSE(shift == kMaxLengthVarint64 * 7)) {
      // Last possible byte.
      if (ABSL_PREDICT_FALSE(
              byte >= uint8_t{1} << (64 - (kMaxLengthVarint64 - 1) * 7))) {
        // The representation is longer than `kMaxLengthVarint64`
        // or the represented value does not fit in `uint64_t`.
        return absl::nullopt;
      }
      break;
    }
  } while (byte >= 0x80);
  return ReadFromStringResult<uint64_t>{result, src};
}

absl::optional<uint32_t> StreamingReadVarint32Slow(Reader& src) {
  uint8_t byte = src.cursor()[0];
  uint32_t result{byte};
  size_t length = 1;
  while (byte >= 0x80) {
    if (ABSL_PREDICT_FALSE(!src.Pull(length + 1, kMaxLengthVarint32))) {
      return absl::nullopt;
    }
    byte = src.cursor()[length];
    result += (uint32_t{byte} - 1) << (length * 7);
    ++length;
    if (ABSL_PREDICT_FALSE(length == kMaxLengthVarint32)) {
      // Last possible byte.
      if (ABSL_PREDICT_FALSE(
              byte >= uint8_t{1} << (32 - (kMaxLengthVarint32 - 1) * 7))) {
        // The representation is longer than `kMaxLengthVarint32`
        // or the represented value does not fit in `uint32_t`.
        return absl::nullopt;
      }
      break;
    }
  }
  src.move_cursor(length);
  return result;
}

absl::optional<uint64_t> StreamingReadVarint64Slow(Reader& src) {
  uint8_t byte = src.cursor()[0];
  uint64_t result{byte};
  size_t length = 1;
  while (byte >= 0x80) {
    if (ABSL_PREDICT_FALSE(!src.Pull(length + 1, kMaxLengthVarint64))) {
      return absl::nullopt;
    }
    byte = src.cursor()[length];
    result += (uint64_t{byte} - 1) << (length * 7);
    ++length;
    if (ABSL_PREDICT_FALSE(length == kMaxLengthVarint64)) {
      // Last possible byte.
      if (ABSL_PREDICT_FALSE(
              byte >= uint8_t{1} << (64 - (kMaxLengthVarint64 - 1) * 7))) {
        // The representation is longer than `kMaxLengthVarint64`
        // or the represented value does not fit in `uint64_t`.
        return absl::nullopt;
      }
      break;
    }
  }
  src.move_cursor(length);
  return result;
}

}  // namespace internal
}  // namespace riegeli

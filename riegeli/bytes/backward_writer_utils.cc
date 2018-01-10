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

#include "riegeli/bytes/backward_writer_utils.h"

#include <stdint.h>

#include "riegeli/base/assert.h"
#include "riegeli/base/string_view.h"
#include "riegeli/bytes/backward_writer.h"
#include "riegeli/bytes/writer_utils.h"

namespace riegeli {
namespace internal {

char* ContinueWritingVarint64Backwards(char* dest, uint64_t data) {
  RIEGELI_ASSERT_GE(data, uint64_t{0x80});
  RIEGELI_ASSERT_LT(data, uint64_t{1} << (64 - 7 * (kMaxLengthVarint32() - 1)));
  dest = WriteVarintBackwards(dest, static_cast<uint32_t>(data >> 7));
  *--dest = static_cast<char>(data | 0x80);
  return dest;
}

bool WriteVarint32Slow(BackwardWriter* dest, uint32_t data) {
  char buffer[kMaxLengthVarint32()];
  char* const end = WriteVarint32(buffer, data);
  return dest->Write(string_view(buffer, end - buffer));
}

bool WriteVarint64Slow(BackwardWriter* dest, uint64_t data) {
  char buffer[kMaxLengthVarint64()];
  char* const end = WriteVarint64(buffer, data);
  return dest->Write(string_view(buffer, end - buffer));
}

}  // namespace internal
}  // namespace riegeli

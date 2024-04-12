// Copyright 2023 Google LLC
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

#ifndef RIEGELI_DIGESTS_WRAPPING_DIGESTER_H_
#define RIEGELI_DIGESTS_WRAPPING_DIGESTER_H_

#include <stddef.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "riegeli/base/chain.h"
#include "riegeli/base/dependency.h"
#include "riegeli/base/maker.h"
#include "riegeli/base/types.h"
#include "riegeli/digests/digest_converter.h"
#include "riegeli/digests/digester_handle.h"

namespace riegeli {

namespace wrapping_digester_internal {

// The type of a function converting a digest, taking it by value, except that
// conversion from `void` takes no parameters.

template <typename From, typename To, typename Enable = void>
struct DigestConverterFunction {
  using type = To (*)(From);
};

template <typename From, typename To>
struct DigestConverterFunction<From, To,
                               std::enable_if_t<std::is_void<From>::value>> {
  using type = To (*)();
};

}  // namespace wrapping_digester_internal

// Wraps an object providing and possibly owning a digester in a concrete
// digester type. Propagates calls to `Close()` if the base digester is owned.
// Possibly converts the type of the digest returned by `Digest()`.
//
// `BaseDigester` must support `Dependency<DigesterBaseHandle, BaseDigester>`.
//
// `DigestType` is the new digest type, by default `DigestOf<BaseDigester>`,
// i.e. unchanged.
//
// `digest_converter` is a function used to convert a digest, by default using
// `DigestConverter`.
template <typename BaseDigester, typename DigestType = DigestOf<BaseDigester>,
          typename wrapping_digester_internal::DigestConverterFunction<
              DigestOf<BaseDigester>, DigestType>::type digest_converter =
              nullptr>
class WrappingDigester {
 public:
  // Default-constructs the `BaseDigester`.
  template <
      typename DependentBaseDigester = BaseDigester,
      std::enable_if_t<
          std::is_default_constructible<DependentBaseDigester>::value, int> = 0>
  WrappingDigester() : base_(riegeli::Maker()) {}

  // Forwards constructor arguments to the `BaseDigester`.
  template <
      typename... Args,
      std::enable_if_t<
          absl::conjunction<
              absl::negation<std::is_same<std::tuple<std::decay_t<Args>...>,
                                          std::tuple<WrappingDigester>>>,
              std::is_constructible<BaseDigester, Args&&...>>::value,
          int> = true>
  explicit WrappingDigester(Args&&... args)
      : base_(riegeli::Maker(std::forward<Args>(args)...)) {}

  WrappingDigester(const WrappingDigester& that) = default;
  WrappingDigester& operator=(const WrappingDigester& that) = default;

  WrappingDigester(WrappingDigester&& that) = default;
  WrappingDigester& operator=(WrappingDigester&& that) = default;

  bool Write(absl::string_view src) { return base_.get().Write(src); }
  bool Write(const Chain& src) { return base_.get().Write(src); }
  bool Write(const absl::Cord& src) { return base_.get().Write(src); }
  bool WriteZeros(riegeli::Position length) {
    return base_.get().WriteZeros(length);
  }
  bool Close() { return !base_.IsOwning() || base_.get().Close(); }

  template <typename DependentBaseDigester = BaseDigester,
            std::enable_if_t<
                absl::conjunction<
                    std::integral_constant<bool, digest_converter == nullptr>,
                    HasDigestConverter<DigestOf<DependentBaseDigester>,
                                       DigestType>>::value,
                int> = 0>
  DigestType Digest() {
    return base_.get().template Digest<DigestType>();
  }
  template <typename DependentBaseDigester = BaseDigester,
            std::enable_if_t<
                absl::conjunction<
                    std::integral_constant<bool, digest_converter != nullptr>,
                    absl::negation<
                        std::is_void<DigestOf<DependentBaseDigester>>>>::value,
                int> = 0>
  DigestType Digest() {
    return digest_converter(base_.get().Digest());
  }
  template <typename DependentBaseDigester = BaseDigester,
            std::enable_if_t<
                absl::conjunction<
                    std::integral_constant<bool, digest_converter != nullptr>,
                    std::is_void<DigestOf<DependentBaseDigester>>>::value,
                int> = 0>
  DigestType Digest() {
    base_.get().Digest();
    return digest_converter();
  }

 private:
  Dependency<DigesterBaseHandle, BaseDigester> base_;
};

}  // namespace riegeli

#endif  // RIEGELI_DIGESTS_WRAPPING_DIGESTER_H_

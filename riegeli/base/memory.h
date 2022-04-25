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

#ifndef RIEGELI_BASE_MEMORY_H_
#define RIEGELI_BASE_MEMORY_H_

#include <stddef.h>

#include <array>
#include <limits>
#include <new>
#include <utility>

#include "absl/numeric/bits.h"
#include "absl/strings/cord.h"
#include "riegeli/base/base.h"

namespace riegeli {

// `NoDestructor<T>` constructs and stores an object of type `T` but does not
// call its destructor.
//
// It can be used as a static variable in a function to lazily initialize an
// object.
template <typename T>
class NoDestructor {
 public:
  // Forwards constructor arguments to `T` constructor.
  template <typename... Args>
  explicit NoDestructor(Args&&... args) {
    new (storage_) T(std::forward<Args>(args)...);
  }

  // Forwards copy and move construction, e.g. a brace initializer.
  explicit NoDestructor(const T& src) { new (storage_) T(src); }
  explicit NoDestructor(T&& src) { new (storage_) T(std::move(src)); }

  NoDestructor(const NoDestructor&) = delete;
  NoDestructor& operator=(const NoDestructor&) = delete;

  // Smart pointer interface with deep constness.
  T* get() { return reinterpret_cast<T*>(storage_); }
  const T* get() const { return reinterpret_cast<const T*>(storage_); }
  T& operator*() { return *get(); }
  const T& operator*() const { return *get(); }
  T* operator->() { return get(); }
  const T* operator->() const { return get(); }

 private:
  alignas(T) char storage_[sizeof(T)];
};

// Returns the estimated size which will be allocated when requesting to
// allocate `requested_size`.
inline size_t EstimatedAllocatedSize(size_t requested_size) {
  return RoundUp<sizeof(size_t) * 2>(requested_size);
}

// `NewAligned()`/`DeleteAligned()` provide memory allocation with the specified
// alignment known at compile time, with the size specified in bytes, and which
// allow deallocation to be faster by knowing the size.
//
// The alignment and size passed to `DeleteAligned()` must be the same as in the
// corresponding `NewAligned()`. Pointer types must be compatible as with new
// and delete expressions.
//
// If the allocated size is given in terms of objects rather than bytes
// and the type is not over-aligned (i.e. its alignment is not larger than
// `alignof(max_align_t))`, it is simpler to use `std::allocator<T>()` instead.
// If the type is over-aligned, `std::allocator<T>()` works correctly only when
// `operator new(size_t, std::align_val_t)` from C++17 is available.

// TODO: Test this with overaligned types.

template <typename T, size_t alignment = alignof(T), typename... Args>
inline T* NewAligned(size_t num_bytes, Args&&... args) {
  static_assert(absl::has_single_bit(alignment),
                "alignment must be a power of 2");
  // Allocate enough space to construct the object, even if the caller does not
  // need the whole tail part of the object.
  num_bytes = UnsignedMax(num_bytes, sizeof(T));
  T* ptr;
#if __cpp_aligned_new
  if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
    ptr = static_cast<T*>(operator new(num_bytes));
  } else {
    ptr = static_cast<T*>(operator new(num_bytes, std::align_val_t(alignment)));
  }
#else
#ifdef __STDCPP_DEFAULT_NEW_ALIGNMENT__
  constexpr size_t kDefaultNewAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
  constexpr size_t kDefaultNewAlignment = alignof(max_align_t);
#endif
  if (alignment <= kDefaultNewAlignment) {
    ptr = static_cast<T*>(operator new(num_bytes));
  } else {
    RIEGELI_CHECK_LE(num_bytes, std::numeric_limits<size_t>::max() -
                                    sizeof(void*) - alignment +
                                    kDefaultNewAlignment)
        << "Out of memory";
    void* const allocated = operator new(sizeof(void*) + num_bytes + alignment -
                                         kDefaultNewAlignment);
    void* const aligned =
        reinterpret_cast<void*>(RoundUp<alignment>(reinterpret_cast<uintptr_t>(
            static_cast<char*>(allocated) + sizeof(void*))));
    reinterpret_cast<void**>(aligned)[-1] = allocated;
    ptr = static_cast<T*>(aligned);
  }
#endif
  new (ptr) T(std::forward<Args>(args)...);
  return ptr;
}

template <typename T, size_t alignment = alignof(T)>
inline void DeleteAligned(T* ptr, size_t num_bytes) {
  static_assert(absl::has_single_bit(alignment),
                "alignment must be a power of 2");
  num_bytes = UnsignedMax(num_bytes, sizeof(T));
  ptr->~T();
#if __cpp_aligned_new
#if __cpp_sized_deallocation || __GXX_DELETE_WITH_SIZE__
  if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
    operator delete(ptr, num_bytes);
  } else {
    operator delete(ptr, num_bytes, std::align_val_t(alignment));
  }
#else
  if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
    operator delete(ptr);
  } else {
    operator delete(ptr, std::align_val_t(alignment));
  }
#endif
#else
#ifdef __STDCPP_DEFAULT_NEW_ALIGNMENT__
  constexpr size_t kDefaultNewAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
  constexpr size_t kDefaultNewAlignment = alignof(max_align_t);
#endif
#if __cpp_sized_deallocation || __GXX_DELETE_WITH_SIZE__
  if (alignment <= kDefaultNewAlignment) {
    operator delete(ptr, num_bytes);
  } else {
    void* const allocated = reinterpret_cast<void**>(ptr)[-1];
    RIEGELI_ASSERT(
        ptr ==
        reinterpret_cast<void*>(RoundUp<alignment>(reinterpret_cast<uintptr_t>(
            static_cast<char*>(allocated) + sizeof(void*)))))
        << "Failed precondition of DeleteAligned(): "
           "the pointer was not obtained from NewAligned(), "
           "or alignment does not match, "
           "or memory before the allocated block got corrupted";
    operator delete(allocated, sizeof(void*) + num_bytes + alignment -
                                   kDefaultNewAlignment);
  }
#else
  if (alignment <= kDefaultNewAlignment) {
    operator delete(ptr);
  } else {
    operator delete(reinterpret_cast<void**>(ptr)[-1]);
  }
#endif
#endif
}

// `SizeReturningNewAligned()` is like `NewAligned()`, but it returns the number
// of bytes actually allocated, which can be greater than the requested number
// of bytes.
//
// The object can be freed with `DeleteAligned()`, passing either
// `min_num_bytes` or `*actual_num_bytes`, or anything between.
//
// `*actual_num_bytes` is already set during the constructor call.
template <typename T, size_t alignment = alignof(T), typename... Args>
inline T* SizeReturningNewAligned(size_t min_num_bytes,
                                  size_t* actual_num_bytes, Args&&... args) {
  static_assert(absl::has_single_bit(alignment),
                "alignment must be a power of 2");
  // Allocate enough space to construct the object, even if the caller does not
  // need the whole tail part of the object.
  min_num_bytes = UnsignedMax(min_num_bytes, sizeof(T));
  T* ptr;
  const size_t capacity = EstimatedAllocatedSize(min_num_bytes);
#if __cpp_aligned_new
  if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
    ptr = static_cast<T*>(operator new(capacity));
  } else {
    ptr = static_cast<T*>(operator new(capacity, std::align_val_t(alignment)));
  }
#else
#ifdef __STDCPP_DEFAULT_NEW_ALIGNMENT__
  constexpr size_t kDefaultNewAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
  constexpr size_t kDefaultNewAlignment = alignof(max_align_t);
#endif
  if (alignment <= kDefaultNewAlignment) {
    ptr = static_cast<T*>(operator new(capacity));
  } else {
    RIEGELI_CHECK_LE(capacity, std::numeric_limits<size_t>::max() -
                                   sizeof(void*) - alignment +
                                   kDefaultNewAlignment)
        << "Out of memory";
    void* const allocated = operator new(sizeof(void*) + capacity + alignment -
                                         kDefaultNewAlignment);
    void* const aligned =
        reinterpret_cast<void*>(RoundUp<alignment>(reinterpret_cast<uintptr_t>(
            static_cast<char*>(allocated) + sizeof(void*))));
    reinterpret_cast<void**>(aligned)[-1] = allocated;
    ptr = static_cast<T*>(aligned);
  }
#endif
  *actual_num_bytes = capacity;
  new (ptr) T(std::forward<Args>(args)...);
  return ptr;
}

// 64K zero bytes.
extern const std::array<char, (size_t{64} << 10)> kArrayOfZeros;

// Returns the given number of zero bytes.
absl::Cord CordOfZeros(size_t length);

}  // namespace riegeli

#endif  // RIEGELI_BASE_MEMORY_H_

// Copyright 2021 Google LLC
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

#ifndef RIEGELI_BASE_INTRUSIVE_REF_COUNT_H_
#define RIEGELI_BASE_INTRUSIVE_REF_COUNT_H_

#include <stddef.h>

#include <atomic>
#include <utility>

#include "riegeli/base/base.h"

namespace riegeli {

// `RefCountedPtr<T>` implements shared ownership of an object of type `T`.
// It can also be `nullptr`.
//
// `RefCountedPtr<T>` has a smaller overhead than `std::shared_ptr<T>`, but
// requires cooperation from `T`.
//
// `T` maintains its own reference count (as a mutable atomic member which can
// be thought of as conceptually being owned by the `RefCountedPtr<T>`), and `T`
// should support:
//
// ```
//   // Increments the reference count.
//   void Ref() const;
//
//   // Decrements the reference count. Deletes `this` when the reference count
//   // reaches 0.
//   void Unref() const;
// ```
template <typename T>
class RefCountedPtr {
 public:
  constexpr RefCountedPtr() noexcept {}

  explicit RefCountedPtr(T* ptr) noexcept : ptr_(ptr) {}

  RefCountedPtr(const RefCountedPtr& that) noexcept;
  RefCountedPtr& operator=(const RefCountedPtr& that) noexcept;

  // The source `RefCountedPtr` is left as nullptr.
  RefCountedPtr(RefCountedPtr&& that) noexcept;
  RefCountedPtr& operator=(RefCountedPtr&& that) noexcept;

  ~RefCountedPtr();

  void reset();
  void reset(T* ptr);

  T* get() const { return ptr_; }
  T& operator*() const { return *get(); }
  T* operator->() const { return get(); }

  T* release() { return std::exchange(ptr_, nullptr); }

  friend bool operator==(const RefCountedPtr& a, const RefCountedPtr& b) {
    return a.get() == b.get();
  }
  friend bool operator!=(const RefCountedPtr& a, const RefCountedPtr& b) {
    return a.get() != b.get();
  }
  friend bool operator==(const RefCountedPtr& a, std::nullptr_t) {
    return a.get() == nullptr;
  }
  friend bool operator!=(const RefCountedPtr& a, std::nullptr_t) {
    return a.get() != nullptr;
  }
  friend bool operator==(std::nullptr_t, const RefCountedPtr& b) {
    return nullptr == b.get();
  }
  friend bool operator!=(std::nullptr_t, const RefCountedPtr& b) {
    return nullptr != b.get();
  }

 private:
  T* ptr_ = nullptr;
};

// A subset of what `std::atomic<RefCountedPtr<T>>` would provide.
template <typename T>
class AtomicRefCountedPtr {
 public:
  constexpr AtomicRefCountedPtr() noexcept {}

  explicit AtomicRefCountedPtr(RefCountedPtr<T> that) noexcept
      : ptr_(that.release()) {}

  AtomicRefCountedPtr(const AtomicRefCountedPtr&) = delete;
  AtomicRefCountedPtr& operator=(const AtomicRefCountedPtr&) = delete;

  ~AtomicRefCountedPtr();

  RefCountedPtr<T> load(
      std::memory_order order = std::memory_order_seq_cst) const;

  void store(RefCountedPtr<T> desired,
             std::memory_order order = std::memory_order_seq_cst);

 private:
  std::atomic<T*> ptr_{nullptr};
};

// Deriving `T` from `RefCountedBase<T>` makes it easier to provide functions
// needed by `RefCountedPtr<T>`.
//
// The destructor of `RefCountedBase<T>` is not virtual. The object will be
// deleted by `delete static_cast<T*>(ptr)`. This means that `T` must be the
// actual object type or `T` must define a virtual destructor, and that multiple
// inheritance is not suported.
//
// `RefCountedBase<T>` also provides `has_unique_owner()`.
template <typename T>
class RefCountedBase {
 public:
  void Ref() const;
  void Unref() const;

  // Returns `true` if there is only one owner of the object.
  bool has_unique_owner() const;

 private:
  mutable std::atomic<size_t> ref_count_{1};
};

// Implementation details follow.

template <typename T>
inline RefCountedPtr<T>::RefCountedPtr(const RefCountedPtr& that) noexcept
    : ptr_(that.ptr_) {
  if (ptr_ != nullptr) ptr_->Ref();
}

template <typename T>
inline RefCountedPtr<T>& RefCountedPtr<T>::operator=(
    const RefCountedPtr& that) noexcept {
  T* const ptr = that.ptr_;
  if (ptr != nullptr) ptr->Ref();
  if (ptr_ != nullptr) ptr_->Unref();
  ptr_ = ptr;
  return *this;
}

template <typename T>
inline RefCountedPtr<T>::RefCountedPtr(RefCountedPtr&& that) noexcept
    : ptr_(that.release()) {}

template <typename T>
inline RefCountedPtr<T>& RefCountedPtr<T>::operator=(
    RefCountedPtr&& that) noexcept {
  // Exchange `that.ptr_` early to support self-assignment.
  T* const ptr = that.release();
  if (ptr_ != nullptr) ptr_->Unref();
  ptr_ = ptr;
  return *this;
}

template <typename T>
inline RefCountedPtr<T>::~RefCountedPtr() {
  if (ptr_ != nullptr) ptr_->Unref();
}

template <typename T>
inline void RefCountedPtr<T>::reset() {
  if (ptr_ != nullptr) {
    ptr_->Unref();
    ptr_ = nullptr;
  }
}

template <typename T>
inline void RefCountedPtr<T>::reset(T* ptr) {
  if (ptr_ != nullptr) ptr_->Unref();
  ptr_ = ptr;
}

template <typename T>
inline AtomicRefCountedPtr<T>::~AtomicRefCountedPtr() {
  T* const ptr = ptr_.load(std::memory_order_acquire);
  if (ptr != nullptr) ptr->Unref();
}

template <typename T>
inline RefCountedPtr<T> AtomicRefCountedPtr<T>::load(
    std::memory_order order) const {
  T* const ptr = ptr_.load(order);
  if (ptr != nullptr) ptr->Ref();
  return RefCountedPtr<T>(ptr);
}

template <typename T>
inline void AtomicRefCountedPtr<T>::store(RefCountedPtr<T> desired,
                                          std::memory_order order) {
  switch (order) {
    case std::memory_order_relaxed:
      order = std::memory_order_acquire;
      break;
    case std::memory_order_release:
      order = std::memory_order_acq_rel;
      break;
    case std::memory_order_seq_cst:
      break;
    default:
      RIEGELI_ASSERT_UNREACHABLE()
          << "Unexpected memory order for store(): " << static_cast<int>(order);
  }
  T* const ptr = ptr_.exchange(desired.release(), order);
  if (ptr != nullptr) ptr->Unref();
}

template <typename T>
inline void RefCountedBase<T>::Ref() const {
  ref_count_.fetch_add(1, std::memory_order_relaxed);
}

template <typename T>
inline void RefCountedBase<T>::Unref() const {
  // Optimization: avoid an expensive atomic read-modify-write operation if the
  // reference count is 1.
  if (ref_count_.load(std::memory_order_acquire) == 1 ||
      ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    delete static_cast<T*>(const_cast<RefCountedBase*>(this));
  }
}

template <typename T>
inline bool RefCountedBase<T>::has_unique_owner() const {
  return ref_count_.load(std::memory_order_acquire) == 1;
}

}  // namespace riegeli

#endif  // RIEGELI_BASE_INTRUSIVE_REF_COUNT_H_

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

#ifndef RIEGELI_CONTAINERS_LINEAR_SORTED_STRING_SET_H_
#define RIEGELI_CONTAINERS_LINEAR_SORTED_STRING_SET_H_

#include <stddef.h>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "riegeli/base/assert.h"
#include "riegeli/base/compact_string.h"
#include "riegeli/base/type_traits.h"
#include "riegeli/bytes/compact_string_writer.h"

namespace riegeli {

// A sorted set of strings, compressed by recognizing shared prefixes.
//
// `LinearSortedStringSet` is optimized for memory usage. It should be used
// only with very small sets (up to tens of elements), otherwise consider
// `ChunkedSortedStringSet`.
class LinearSortedStringSet {
 public:
  class Iterator;
  class Builder;
  class NextInsertIterator;

  using value_type = absl::string_view;
  using reference = value_type;
  using const_reference = reference;
  using iterator = Iterator;
  using const_iterator = iterator;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  // Creates a set consisting of the given elements. They must be sorted.
  // Consecutive duplicates are inserted only once.
  //
  // The type of `src` must support iteration yielding `absl::string_view`:
  // `for (const absl::string_view element : src)`,
  // e.g. `std::vector<std::string>`.
  template <
      typename Src,
      std::enable_if_t<IsIterableOf<Src, absl::string_view>::value, int> = 0>
  static LinearSortedStringSet FromSorted(Src&& src);
  static LinearSortedStringSet FromSorted(
      std::initializer_list<absl::string_view> src);

  // Creates a set consisting of the given elements. They do not need to be
  // sorted. Duplicates are inserted only once.
  //
  // The type of `src` must support iteration yielding `absl::string_view`:
  // `for (const absl::string_view element : src)`,
  // e.g. `std::vector<std::string>`.
  template <
      typename Src,
      std::enable_if_t<IsIterableOf<Src, absl::string_view>::value, int> = 0>
  static LinearSortedStringSet FromUnsorted(Src&& src);
  static LinearSortedStringSet FromUnsorted(
      std::initializer_list<absl::string_view> src);

  // An empty set.
  LinearSortedStringSet() = default;

  LinearSortedStringSet(const LinearSortedStringSet& that) = default;
  LinearSortedStringSet& operator=(const LinearSortedStringSet& that) = default;

  LinearSortedStringSet(LinearSortedStringSet&& that) noexcept = default;
  LinearSortedStringSet& operator=(LinearSortedStringSet&& that) noexcept =
      default;

  // Iteration over the set.
  Iterator begin() const;
  Iterator cbegin() const;
  Iterator end() const;
  Iterator cend() const;

  // Returns `true` if the set is empty.
  bool empty() const { return encoded_.empty(); }

  // Returns the number of elements.
  //
  // Time complexity: `O(size)`.
  size_t size() const;

  // Returns the first element. The set must not be empty.
  absl::string_view first() const;

  // Returns `true` if `element` is present in the set.
  //
  // Time complexity: `O(size)`.
  bool contains(absl::string_view element) const;

  friend bool operator==(const LinearSortedStringSet& a,
                         const LinearSortedStringSet& b) {
    return EqualImpl(a, b);
  }
  friend bool operator!=(const LinearSortedStringSet& a,
                         const LinearSortedStringSet& b) {
    return !EqualImpl(a, b);
  }
  friend bool operator<(const LinearSortedStringSet& a,
                        const LinearSortedStringSet& b) {
    return LessImpl(a, b);
  }
  friend bool operator>(const LinearSortedStringSet& a,
                        const LinearSortedStringSet& b) {
    return LessImpl(b, a);
  }
  friend bool operator<=(const LinearSortedStringSet& a,
                         const LinearSortedStringSet& b) {
    return !LessImpl(b, a);
  }
  friend bool operator>=(const LinearSortedStringSet& a,
                         const LinearSortedStringSet& b) {
    return !LessImpl(a, b);
  }

  template <typename HashState>
  friend HashState AbslHashValue(HashState hash_state,
                                 const LinearSortedStringSet& self) {
    return self.AbslHashValueImpl(std::move(hash_state));
  }

  // Estimates the amount of memory used by this `LinearSortedStringSet`,
  // including `sizeof(LinearSortedStringSet)`.
  size_t EstimateMemory() const;

  // Support `EstimateMemory()`.
  template <typename MemoryEstimator>
  friend void RiegeliRegisterSubobjects(const LinearSortedStringSet& self,
                                        MemoryEstimator& memory_estimator) {
    memory_estimator.RegisterSubobjects(self.encoded_);
  }

 private:
  explicit LinearSortedStringSet(CompactString&& encoded);

  static bool EqualImpl(const LinearSortedStringSet& a,
                        const LinearSortedStringSet& b);
  static bool LessImpl(const LinearSortedStringSet& a,
                       const LinearSortedStringSet& b);
  template <typename HashState>
  HashState AbslHashValueImpl(HashState hash_state);

  // Representation of each other element, which consists of the prefix of the
  // previous element with length shared_length, concatenated with unshared,
  // where tagged_length = (unshared_length << 1) | (shared_length > 0 ? 1 : 0):
  //
  //  * tagged_length : varint64
  //  * shared_length : varint64, if shared_length > 0
  //  * unshared      : char[unshared_length]
  CompactString encoded_;
};

// Iterates over a `LinearSortedStringSet` in the sorted order.
class LinearSortedStringSet::Iterator {
 public:
  // `iterator_concept` is only `std::input_iterator_tag` because the
  // `std::forward_iterator` requirement and above require references to remain
  // valid while the range exists.
  using iterator_concept = std::input_iterator_tag;
  // `iterator_category` is only `std::input_iterator_tag` also because the
  // `LegacyForwardIterator` requirement and above require `reference` to be
  // a true reference type.
  using iterator_category = std::input_iterator_tag;
  using value_type = absl::string_view;
  using reference = value_type;
  using difference_type = ptrdiff_t;

  class pointer {
   public:
    reference* operator->() { return &ref_; }
    const reference* operator->() const { return &ref_; }

   private:
    friend class Iterator;
    explicit pointer(reference ref) : ref_(ref) {}
    reference ref_;
  };

  // A sentinel value, equal to `end()`.
  Iterator() = default;

  Iterator(const Iterator& that) = default;
  Iterator& operator=(const Iterator& that) = default;

  Iterator(Iterator&& that) noexcept = default;
  Iterator& operator=(Iterator&& that) noexcept = default;

  // Returns the current element.
  //
  // The `absl::string_view` is valid until the next non-const operation on this
  // `Iterator` (the string it points to is conditionally owned by `Iterator`).
  reference operator*() const {
    RIEGELI_ASSERT(cursor_ != nullptr)
        << "Failed precondition of "
           "LinearSortedStringSet::Iterator::operator*: "
           "iterator is end()";
    if (length_if_unshared_ > 0) {
      return absl::string_view(cursor_ - length_if_unshared_,
                               length_if_unshared_);
    } else {
      return current_;
    }
  }

  pointer operator->() const {
    RIEGELI_ASSERT(cursor_ != nullptr)
        << "Failed precondition of "
           "LinearSortedStringSet::Iterator::operator->: "
           "iterator is end()";
    return pointer(**this);
  }

  Iterator& operator++() {
    RIEGELI_ASSERT(cursor_ != nullptr)
        << "Failed precondition of "
           "LinearSortedStringSet::Iterator::operator++: "
           "iterator is end()";
    Next();
    return *this;
  }
  Iterator operator++(int) {
    const Iterator tmp = *this;
    ++*this;
    return tmp;
  }

  // Advances to the next element, like `operator++`, but returns a length known
  // to be shared with the previous element, or 0 if `end()` was reached.
  //
  // The shared length is not guaranteed to be maximal, so it should be used
  // only for optimization.
  size_t Next();

  // Iterators can be compared even if they are associated with different
  // `LinearSortedStringSet` objects. All `end()` values are equal, while all
  // other values are not equal.
  friend bool operator==(const Iterator& a, const Iterator& b) {
    return a.cursor_ == b.cursor_;
  }
  friend bool operator!=(const Iterator& a, const Iterator& b) {
    return a.cursor_ != b.cursor_;
  }

 private:
  friend class LinearSortedStringSet;  // For `Iterator::Iterator`.

  explicit Iterator(absl::string_view encoded)
      : cursor_(encoded.data()), limit_(encoded.data() + encoded.size()) {
    Next();
  }

  // `cursor_` points after the encoded current element in
  // `LinearSortedStringSet::encoded_`, or is `nullptr` for `end()` (this is
  // unambiguous because `CompactString::data()` is never `nullptr`).
  const char* cursor_ = nullptr;
  const char* limit_ = nullptr;
  // If `length_if_unshared_ > 0`, the current element is
  // `absl::string_view(cursor_ - length_if_unshared_, length_if_unshared_)`,
  // and `current_` is unused and empty.
  //
  // If `length_if_unshared_ == 0`, the decoded current element is `current_`.
  size_t length_if_unshared_ = 0;
  // If `*this` is `end()`, or if `length_if_unshared_ > 0`, unused and empty.
  // Otherwise stores the decoded current element.
  CompactString current_;
};

// Builds a `LinearSortedStringSet` from a sorted sequence of strings.
class LinearSortedStringSet::Builder {
 public:
  // Begins with an empty set.
  Builder();

  Builder(Builder&& that) noexcept;
  Builder& operator=(Builder&& that) noexcept;

  ~Builder();

  // Makes `*this` equivalent to a newly constructed `Builder`.
  void Reset();

  // Returns an output iterator which inserts elements to this `Builder`.
  // Consecutive duplicates are inserted only once.
  //
  // Each inserted element must be greater than or equal to the last inserted
  // element.
  //
  // Inserting with a `NextInsertIterator` is equivalent to calling
  // `InsertNext()`. In particular if multiple iterators and explicit
  // `InsertNext()` calls are used together, then their combined element
  // sequence must be ordered.
  NextInsertIterator NextInserter();

  // Inserts an element. Consecutive duplicates are inserted only once.
  //
  // Precondition: `element` is greater than or equal to the last inserted
  // element.
  //
  // Returns `true` if `element` was inserted, or `false` if it is equal to the
  // last inserted element.
  //
  // If `std::string&&` is passed, it is moved only if the result is `true`.
  //
  // `std::string&&` is accepted with a template to avoid implicit conversions
  // to `std::string` which can be ambiguous against `absl::string_view`
  // (e.g. `const char*`).
  bool InsertNext(absl::string_view element);
  template <
      typename Element,
      std::enable_if_t<std::is_same<Element, std::string>::value, int> = 0>
  bool InsertNext(Element&& element);

  // Inserts an element. Elements out of order are skipped.
  //
  // Returns `true` if `element` was inserted, `false` if it is equal to the
  // last inserted element, or `absl::FailedPreconditionError()` if it is less
  // than the last inserted element.
  //
  // If `std::string&&` is passed, it is moved only if the result is `true`.
  //
  // `std::string&&` is accepted with a template to avoid implicit conversions
  // to `std::string` which can be ambiguous against `absl::string_view`
  // (e.g. `const char*`).
  absl::StatusOr<bool> TryInsertNext(absl::string_view element);
  template <
      typename Element,
      std::enable_if_t<std::is_same<Element, std::string>::value, int> = 0>
  absl::StatusOr<bool> TryInsertNext(Element&& element);

  // Returns `true` if the set is empty.
  bool empty() const { return writer_.pos() == 0; }

  // Returns the last inserted element. The set must not be empty.
  absl::string_view last() const {
    RIEGELI_ASSERT(!empty())
        << "Failed precondition of LinearSortedStringSet::Builder::last(): "
           "empty set";
    return last_;
  }

  // Builds the `LinearSortedStringSet`. No more elements can be inserted.
  LinearSortedStringSet Build() &&;

 private:
  // This template is defined and used only in linear_sorted_string_set.cc.
  template <typename Element, typename UpdateLast>
  absl::StatusOr<bool> InsertNextImpl(Element&& element,
                                      UpdateLast update_last);

  CompactStringWriter<CompactString> writer_;
  std::string last_;
};

// Inserts elements to a `LinearSortedStringSet::Builder`. Consecutive
// duplicates are inserted only once.
//
// Each inserted element must be greater than or equal to the last inserted
// element.
class LinearSortedStringSet::NextInsertIterator {
 public:
  using iterator_concept = std::output_iterator_tag;
  using iterator_category = std::output_iterator_tag;
  using value_type = absl::string_view;
  using difference_type = ptrdiff_t;
  using pointer = void;

  class reference {
   public:
    // Inserts the next element.
    //
    // `std::string&&` is accepted with a template to avoid implicit conversions
    // to `std::string` which can be ambiguous against `absl::string_view`
    // (e.g. `const char*`).
    reference& operator=(absl::string_view element) {
      builder_->InsertNext(element);
      return *this;
    }
    template <
        typename Element,
        std::enable_if_t<std::is_same<Element, std::string>::value, int> = 0>
    reference& operator=(Element&& element) {
      // `std::move(element)` is correct and `std::forward<Element>(element)` is
      // not necessary: `Element` is always `std::string`, never an lvalue
      // reference.
      builder_->InsertNext(std::move(element));
      return *this;
    }

   private:
    friend class NextInsertIterator;
    explicit reference(Builder* builder) : builder_(builder) {}
    Builder* builder_;
  };

  // A sentinel value.
  NextInsertIterator() = default;

  NextInsertIterator(const NextInsertIterator& that) = default;
  NextInsertIterator& operator=(const NextInsertIterator& that) = default;

  reference operator*() const {
    RIEGELI_ASSERT(builder_ != nullptr)
        << "Failed precondition of NextInsertIterator::operator*: "
           "iterator is sentinel";
    return reference(builder_);
  }

  NextInsertIterator& operator++() { return *this; }
  NextInsertIterator operator++(int) { return ++*this; }

  Builder* builder() const { return builder_; }

 private:
  friend class Builder;  // For `NextInsertIterator::NextInsertIterator()`.

  explicit NextInsertIterator(Builder* builder) : builder_(builder) {}

  Builder* builder_ = nullptr;
};

// Implementation details follow.

template <typename Src,
          std::enable_if_t<IsIterableOf<Src, absl::string_view>::value, int>>
LinearSortedStringSet LinearSortedStringSet::FromSorted(Src&& src) {
  using std::begin;
  auto iter = begin(src);
  using std::end;
  auto end_iter = end(src);
  LinearSortedStringSet::Builder builder;
  for (; iter != end_iter; ++iter) {
    builder.InsertNext(MaybeMoveElement<Src>(*iter));
  }
  return std::move(builder).Build();
}

template <typename Src,
          std::enable_if_t<IsIterableOf<Src, absl::string_view>::value, int>>
inline LinearSortedStringSet LinearSortedStringSet::FromUnsorted(Src&& src) {
  using std::begin;
  auto iter = begin(src);
  using std::end;
  auto end_iter = end(src);
  using SrcIterator = decltype(iter);
  std::vector<SrcIterator> iterators;
  if (std::is_convertible<
          typename std::iterator_traits<SrcIterator>::iterator_category,
          std::random_access_iterator_tag>::value) {
    iterators.reserve(std::distance(iter, end_iter));
  }
  for (; iter != end_iter; ++iter) {
    iterators.push_back(iter);
  }
  std::sort(iterators.begin(), iterators.end(),
            [](const SrcIterator& a, const SrcIterator& b) {
              return absl::string_view(*a) < absl::string_view(*b);
            });

  LinearSortedStringSet::Builder builder;
  for (const SrcIterator& iter : iterators) {
    builder.InsertNext(MaybeMoveElement<Src>(*iter));
  }
  return std::move(builder).Build();
}

inline LinearSortedStringSet::Iterator LinearSortedStringSet::begin() const {
  return Iterator(encoded_);
}

inline LinearSortedStringSet::Iterator LinearSortedStringSet::cbegin() const {
  return begin();
}

inline LinearSortedStringSet::Iterator LinearSortedStringSet::end() const {
  return Iterator();
}

inline LinearSortedStringSet::Iterator LinearSortedStringSet::cend() const {
  return end();
}

template <typename HashState>
HashState LinearSortedStringSet::AbslHashValueImpl(HashState hash_state) {
  size_t size = 0;
  for (const absl::string_view element : *this) {
    hash_state = HashState::combine(std::move(hash_state), element);
    ++size;
  }
  return HashState::combine(std::move(hash_state), size);
}

inline LinearSortedStringSet::NextInsertIterator
LinearSortedStringSet::Builder::NextInserter() {
  return NextInsertIterator(this);
}

extern template bool LinearSortedStringSet::Builder::InsertNext(
    std::string&& element);

extern template absl::StatusOr<bool>
LinearSortedStringSet::Builder::TryInsertNext(std::string&& element);

}  // namespace riegeli

#endif  // RIEGELI_CONTAINERS_LINEAR_SORTED_STRING_SET_H_

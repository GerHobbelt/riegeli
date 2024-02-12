// Copyright 2018 Google LLC
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

#ifndef RIEGELI_BASE_DEPENDENCY_H_
#define RIEGELI_BASE_DEPENDENCY_H_

#include <stddef.h>

#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "riegeli/base/assert.h"
#include "riegeli/base/compare.h"
#include "riegeli/base/reset.h"
#include "riegeli/base/type_id.h"
#include "riegeli/base/type_traits.h"

namespace riegeli {

// `Dependency<Ptr, Manager>` contains or refers to an optionally owned object
// which is stored as type `Manager` and accessed through type `Ptr`.
//
// When a dependent object is said to be owned by a host object or function, the
// host is responsible for closing it when done, and certain other operations
// are propagated to it. The host is usually also responsible for destroying the
// owned object.
//
// Often `Ptr` is some pointer `P*`, and then `Manager` can be e.g.
// `M` (owned), `M*` (not owned), or `std::unique_ptr<M, Deleter>` (owned),
// with `M` derived from `P`.
//
// Often `Dependency<Ptr, Manager>` is a member of a host class template
// parameterized by `Manager`, with `Ptr` fixed by the host class. The member
// is initialized from an argument of a constructor or a resetting function.
// A user of the host class specifies ownership of the dependent object and
// possibly narrows its type by choosing the `Manager` template argument of the
// host class. The `Manager` type can be deduced from a constructor argument
// using CTAD (since C++17), which is usually done by removing any toplevel
// references and `const` qualifiers using `std::decay`.
//
// As an alternative to passing `std::move(m)`, passing `ClosingPtr(&m)` avoids
// moving `m`, but the caller must ensure that the dependent object is valid
// while the host object needs it.
//
// `Manager` can also be `M&` (not owned) or `M&&` (owned). They are primarily
// meant to be used with a host function rather than a host object, because such
// a dependency stores only a reference to the dependent object. By convention a
// reference argument is expected to be valid for the duration of the function
// call but not necessarily after the function returns. The `Manager` type is
// usually deduced from a function argument as a reference type rather than
// using `std::decay`.
//
// `Manager` being `M&` is functionally equivalent to `M*`, but offers a more
// idiomatic API for passing an object which does not need to be valid after the
// function returns.
//
// `Manager` being `M&&` is similar to `ClosingPtrType<M>`. In contrast to a
// host class, a host function does not decay `M&&` to `M` and avoids moving
// `m`, because the dependent object can be expected to be valid for the
// duration of the function call.

// `RiegeliDependencySentinel(T*)` specifies how to initialize a default
// `Manager` (for `Dependency`) or `Ptr` (for `AnyDependency`) of type `T`.
//
// To customize that for a class `T`, define a free function
// `friend T RiegeliDependencySentinel(T*)` as a friend of `T` inside class
// definition or in the same namespace as `T`, so that it can be found via ADL.
//
// `RiegeliDependencySentinel(T*)` returns a value of type `T`, or a tuple of
// its constructor arguments to avoid constructing a temporary `T` and moving
// from it.
//
// The argument of `RiegeliDependencySentinel(T*)` is always a null pointer,
// used to choose the right overload based on the type.

inline std::tuple<> RiegeliDependencySentinel(void*) { return {}; }

// Specialization of `RiegeliDependencySentinel(int*)`, used for file
// descriptors.

inline int RiegeliDependencySentinel(int*) { return -1; }

// `Dependency<Ptr, Manager>` derives from `DependencyImpl<Ptr, Manager>` which
// has specializations for various combinations of `Ptr` and `Manager` types.
// Most operations of `Dependency` are provided by `DependencyImpl`.
//
// Operations of `Dependency<Ptr, Manager>`:
//
// ```
//   // Constructs a dummy `Manager` from
//   // `RiegeliDependencySentinel(static_cast<Manager*>(nullptr))`. Used
//   // when the host object is closed and does not need a dependent object.
//   //
//   // This constructor is optional.
//   Dependency();
//
//   // Copies or moves a `Manager`. Used to specify the initial value of the
//   // dependent object.
//   explicit Dependency(const Manager& manager);
//   explicit Dependency(Manager&& manager);
//
//   // Constructs a `Manager` from elements of `manager_args`. Used to specify
//   // the initial value of the dependent object. This avoids constructing a
//   // temporary `Manager` and moving from it.
//   //
//   // This constructor is optional.
//   template <typename... ManagerArgs>
//   explicit Dependency(std::tuple<ManagerArgs...> manager_args);
//
//   // Copies the dependency.
//   //
//   // These methods are optional.
//   Dependency(const Dependency& that) noexcept;
//   Dependency& operator=(const Dependency& that) noexcept;
//
//   // Moves the dependency.
//   //
//   // These methods are optional.
//   Dependency(Dependency&& that) noexcept;
//   Dependency& operator=(Dependency&& that) noexcept;
//
//   // Makes `*this` equivalent to a newly constructed Dependency. This avoids
//   // constructing a temporary Dependency and moving from it.
//   //
//   // These methods are optional.
//   ABSL_ATTRIBUTE_REINITIALIZES void Reset();
//   ABSL_ATTRIBUTE_REINITIALIZES void Reset(const Manager& manager);
//   ABSL_ATTRIBUTE_REINITIALIZES void Reset(Manager&& manager);
//   template <typename... ManagerArgs>
//   ABSL_ATTRIBUTE_REINITIALIZES void Reset(
//       std::tuple<ManagerArgs...> manager_args);
//
//   // Exposes the contained `Manager`.
//   Manager& manager();
//   const Manager& manager() const;
//
//   // Returns a `Ptr` to the `Manager`.
//   //
//   // If `Ptr` is `P*` then this method might return a pointer to a derived
//   // class.
//   //
//   // The result is non-const even if the `Manager` is stored inside the
//   // `Dependency`.
//   Ptr get() const;
//
//   // If `Ptr` is `P*`, `Dependency<P*, Manager>` can be used as a smart
//   // pointer to `P`, for convenience.
//   //
//   // These methods might return a pointer to a derived class.
//   //
//   // These methods are provided by `Dependency` itself, not `DependencyImpl`.
//   P& operator*() const { return *get(); }
//   P* operator->() const { return get(); }
//
//   // If `Ptr` is `P*`, the dependency can be compared against `nullptr`.
//   //
//   // This method is provided by `Dependency` itself, not `DependencyImpl`.
//   friend bool operator==(const Dependency& a, std::nullptr_t) {
//     return a.get() == nullptr;
//   }
//
//   // If `true`, the `Dependency` owns the dependent object, i.e. closing the
//   // host object should close the dependent object.
//   //
//   // This method is optional.
//   bool is_owning() const;
//
//   // If `true`, `get()` stays unchanged when a `Dependency` is moved.
//   static constexpr bool kIsStable;
//
//   // If the `Manager` has exactly this type or a reference to it, returns a
//   // pointer to the `Manager`. If the `Manager` is an `AnyDependency`
//   // (possibly wrapped in an rvalue reference or `std::unique_ptr`),
//   // propagates `GetIf()` to it. Otherwise returns `nullptr`.
//   //
//   // These methods are provided by `Dependency` itself if `DependencyImpl`
//   // does not provide them. They are implemented in terms of `GetIf(TypeId)`
//   // if only those variants are provided.
//   template <typename OtherManager>
//   OtherManager* GetIf();
//   template <typename OtherManager>
//   const OtherManager* GetIf() const;
//
//   // A variant of `GetIf()` with the expected type passed as a `TypeId`.
//   //
//   // These methods are provided by `Dependency` itself if `DependencyImpl`
//   // does not provide them.
//   void* GetIf(TypeId type_id);
//   const void* GetIf(TypeId type_id) const;
// ```

// Implementation shared between most specializations of `DependencyImpl` which
// store `manager()` in a member variable.
//
// Provides constructors, `Reset()`, and `manager()`.
template <typename Manager>
class DependencyBase {
 public:
  DependencyBase() noexcept
      : DependencyBase(
            RiegeliDependencySentinel(static_cast<Manager*>(nullptr))) {}

  template <typename DependentManager = Manager,
            std::enable_if_t<std::is_convertible<const DependentManager&,
                                                 DependentManager>::value,
                             int> = 0>
  explicit DependencyBase(const Manager& manager) : manager_(manager) {}
  template <typename DependentManager = Manager,
            std::enable_if_t<std::is_convertible<DependentManager&&,
                                                 DependentManager>::value,
                             int> = 0>
  explicit DependencyBase(Manager&& manager) noexcept
      : manager_(std::move(manager)) {}

  template <typename... ManagerArgs>
  explicit DependencyBase(std::tuple<ManagerArgs...> manager_args)
      :
#if __cpp_guaranteed_copy_elision && __cpp_lib_make_from_tuple
        manager_(std::make_from_tuple<Manager>(std::move(manager_args)))
#else
        DependencyBase(std::move(manager_args),
                       std::index_sequence_for<ManagerArgs...>())
#endif
  {
  }

  ABSL_ATTRIBUTE_REINITIALIZES void Reset() {
    Reset(RiegeliDependencySentinel(static_cast<Manager*>(nullptr)));
  }

  template <typename DependentManager = Manager,
            std::enable_if_t<std::is_convertible<const DependentManager&,
                                                 DependentManager>::value,
                             int> = 0>
  ABSL_ATTRIBUTE_REINITIALIZES void Reset(const Manager& manager) {
    manager_ = manager;
  }
  template <typename DependentManager = Manager,
            std::enable_if_t<std::is_convertible<DependentManager&&,
                                                 DependentManager>::value,
                             int> = 0>
  ABSL_ATTRIBUTE_REINITIALIZES void Reset(Manager&& manager) {
    manager_ = std::move(manager);
  }

  template <typename... ManagerArgs>
  ABSL_ATTRIBUTE_REINITIALIZES void Reset(
      std::tuple<ManagerArgs...> manager_args) {
#if __cpp_lib_apply
    std::apply(
        [&](ManagerArgs&&... args) {
          riegeli::Reset(manager_, std::forward<ManagerArgs>(args)...);
        },
        std::move(manager_args));
#else
    ResetInternal(std::move(manager_args),
                  std::index_sequence_for<ManagerArgs...>());
#endif
  }

  Manager& manager() { return manager_; }
  const Manager& manager() const { return manager_; }

 protected:
  DependencyBase(const DependencyBase& that) = default;
  DependencyBase& operator=(const DependencyBase& that) = default;

  DependencyBase(DependencyBase&& that) = default;
  DependencyBase& operator=(DependencyBase&& that) = default;

  ~DependencyBase() = default;

  Manager& mutable_manager() const { return manager_; }

 private:
#if !__cpp_guaranteed_copy_elision || !__cpp_lib_make_from_tuple
  template <typename... ManagerArgs, size_t... indices>
  explicit DependencyBase(
      ABSL_ATTRIBUTE_UNUSED std::tuple<ManagerArgs...>&& manager_args,
      std::index_sequence<indices...>)
      : manager_(
            std::forward<ManagerArgs>(std::get<indices>(manager_args))...) {}
#endif

#if !__cpp_lib_apply
  template <typename... ManagerArgs, size_t... indices>
  void ResetInternal(
      ABSL_ATTRIBUTE_UNUSED std::tuple<ManagerArgs...>&& manager_args,
      std::index_sequence<indices...>) {
    riegeli::Reset(manager_, std::forward<ManagerArgs>(
                                 std::get<indices>(manager_args))...);
  }
#endif

  mutable Manager manager_;
};

// Specialization of `DependencyBase` for lvalue references.
//
// Only a subset of operations are provided: the dependency must be initialized,
// assignment is not supported, and initialization from a tuple of constructor
// arguments is not supported.
template <typename Manager>
class DependencyBase<Manager&> {
 public:
  explicit DependencyBase(Manager& manager) noexcept : manager_(manager) {}

  Manager& manager() const { return manager_; }

 protected:
  DependencyBase(const DependencyBase& that) = default;
  DependencyBase& operator=(const DependencyBase&) = delete;

  ~DependencyBase() = default;

  Manager& mutable_manager() const { return manager_; }

 private:
  Manager& manager_;
};

// Specialization of `DependencyBase` for rvalue references.
//
// Only a subset of operations are provided: the dependency must be initialized,
// assignment is not supported, and initialization from a tuple of constructor
// arguments is not supported.
template <typename Manager>
class DependencyBase<Manager&&> {
 public:
  explicit DependencyBase(Manager&& manager) noexcept : manager_(manager) {}

  Manager& manager() const { return manager_; }

 protected:
  DependencyBase(const DependencyBase& that) = default;
  DependencyBase& operator=(const DependencyBase&) = delete;

  ~DependencyBase() = default;

  Manager& mutable_manager() const { return manager_; }

 private:
  Manager& manager_;
};

// This template is specialized but does not have a primary definition.
template <typename Ptr, typename Manager, typename Enable = void>
class DependencyImpl;

namespace dependency_internal {

// `IsValidDependencyProbe<Ptr, Manager>::value` is `true` when
// `DependencyImpl<Ptr, Manager>` is defined, as determined by instantiating
// the `DependencyImpl` and calling `get()`.

template <typename Ptr, typename Manager, typename Enable = void>
struct IsValidDependencyProbe : std::false_type {};

template <typename Ptr, typename Manager>
struct IsValidDependencyProbe<
    Ptr, Manager,
    absl::void_t<
        decltype(std::declval<const DependencyImpl<Ptr, Manager>&>().get())>>
    : std::true_type {};

}  // namespace dependency_internal

// `IsValidDependencyImpl<Ptr, Manager>::value` is `true` when
// `DependencyImpl<Ptr, Manager>` is defined.
//
// By default this is determined by instantiating the `DependencyImpl` and
// checking if `get()` is available. If that instantiation might have undesired
// side effects in contexts where `IsValidDependencyImpl` is needed,
// `IsValidDependencyImpl` can also be specialized explicitly.
template <typename Ptr, typename Manager, typename Enable = void>
struct IsValidDependencyImpl
    : dependency_internal::IsValidDependencyProbe<Ptr, Manager> {};

namespace dependency_internal {

// `DependencyMaybeRef<Ptr, Manager>` extends `DependencyImpl<Ptr, Manager>`
// with specializations when `Manager` is `M&` or `M&&`.
//
// If `DependencyImpl<Ptr, Manager>` is not specialized, `Manager` is delegated
// to `std::decay_t<M>`. This handles cases where the `Manager` type is deduced
// from a function parameter as a reference type to avoid moving the dependent
// object, but the function argument is not `P&` or `P&&` but e.g. `P*&` or
// `std::unique_ptr<P>&&`, intended to be treated as the pointer value.

// This template is specialized but does not have a primary definition.
template <typename Ptr, typename Manager, typename Enable = void>
class DependencyMaybeRef;

// Specialization of `DependencyMaybeRef<Ptr, Manager>` when
// `DependencyImpl<Ptr, Manager>` is defined.
template <typename Ptr, typename Manager>
class DependencyMaybeRef<
    Ptr, Manager, std::enable_if_t<IsValidDependencyImpl<Ptr, Manager>::value>>
    : public DependencyImpl<Ptr, Manager> {
 public:
  using DependencyMaybeRef::DependencyImpl::DependencyImpl;

 protected:
  DependencyMaybeRef(const DependencyMaybeRef& that) = default;
  DependencyMaybeRef& operator=(const DependencyMaybeRef& that) = default;

  DependencyMaybeRef(DependencyMaybeRef&& that) = default;
  DependencyMaybeRef& operator=(DependencyMaybeRef&& that) = default;

  ~DependencyMaybeRef() = default;
};

// Specialization of `DependencyMaybeRef<Ptr, M&>` when
// `DependencyImpl<Ptr, M&>` is not defined: delegate to
// `DependencyImpl<Ptr, std::decay_t<M>>`.
//
// Assignment is not supported.
template <typename Ptr, typename M>
class DependencyMaybeRef<
    Ptr, M&,
    std::enable_if_t<
        absl::conjunction<absl::negation<IsValidDependencyImpl<Ptr, M&>>,
                          IsValidDependencyImpl<Ptr, std::decay_t<M>>>::value>>
    : public DependencyImpl<Ptr, std::decay_t<M>> {
 public:
  explicit DependencyMaybeRef(M& manager) noexcept
      : DependencyMaybeRef::DependencyImpl(manager) {}

 protected:
  DependencyMaybeRef(const DependencyMaybeRef& that) = default;
  DependencyMaybeRef& operator=(const DependencyMaybeRef&) = delete;

  DependencyMaybeRef(DependencyMaybeRef&& that) = default;
  DependencyMaybeRef& operator=(DependencyMaybeRef&&) = delete;

  ~DependencyMaybeRef() = default;
};

// Specialization of `DependencyMaybeRef<Ptr, M&&>` when
// `DependencyImpl<Ptr, M&&>` is not defined: delegate to
// `DependencyImpl<Ptr, std::decay_t<M>>`.
//
// Assignment is not supported.
template <typename Ptr, typename M>
class DependencyMaybeRef<
    Ptr, M&&,
    std::enable_if_t<
        absl::conjunction<absl::negation<IsValidDependencyImpl<Ptr, M&&>>,
                          IsValidDependencyImpl<Ptr, std::decay_t<M>>>::value>>
    : public DependencyImpl<Ptr, std::decay_t<M>> {
 public:
  explicit DependencyMaybeRef(M&& manager) noexcept
      : DependencyMaybeRef::DependencyImpl(std::move(manager)) {}

 protected:
  DependencyMaybeRef(const DependencyMaybeRef& that) = default;
  DependencyMaybeRef& operator=(const DependencyMaybeRef&) = delete;

  DependencyMaybeRef(DependencyMaybeRef&& that) = default;
  DependencyMaybeRef& operator=(DependencyMaybeRef&&) = delete;

  ~DependencyMaybeRef() = default;
};

}  // namespace dependency_internal

// `IsValidDependency<Ptr, Manager>::value` is `true` when
// `Dependency<Ptr, Manager>` is defined.

template <typename Ptr, typename Manager, typename Enable = void>
struct IsValidDependency : IsValidDependencyImpl<Ptr, Manager> {};

template <typename Ptr, typename M>
struct IsValidDependency<
    Ptr, M&,
    std::enable_if_t<IsValidDependencyImpl<Ptr, std::decay_t<M>>::value>>
    : std::true_type {};

template <typename Ptr, typename M>
struct IsValidDependency<
    Ptr, M&&,
    std::enable_if_t<IsValidDependencyImpl<Ptr, std::decay_t<M>>::value>>
    : std::true_type {};

namespace dependency_internal {

template <typename T, typename OtherManager, typename Enable = void>
struct HasGetIfStatic : std::false_type {};

template <typename T, typename OtherManager>
struct HasGetIfStatic<
    T, OtherManager,
    std::enable_if_t<absl::disjunction<
        std::is_convertible<
            decltype(std::declval<T&>().template GetIf<OtherManager>()),
            OtherManager*>,
        std::is_convertible<
            decltype(std::declval<const T&>().template GetIf<OtherManager>()),
            const OtherManager*>>::value>> : std::true_type {
  static_assert(
      absl::conjunction<
          std::is_convertible<
              decltype(std::declval<T&>().template GetIf<OtherManager>()),
              OtherManager*>,
          std::is_convertible<
              decltype(std::declval<const T&>().template GetIf<OtherManager>()),
              const OtherManager*>>::value,
      "Either both mutable and const or none GetIf() overloads "
      "must be provided by DependencyImpl");
};

template <typename T, typename Enable = void>
struct HasGetIfDynamic : std::false_type {};

template <typename T>
struct HasGetIfDynamic<
    T,
    std::enable_if_t<absl::disjunction<
        std::is_convertible<
            decltype(std::declval<T&>().GetIf(std::declval<TypeId>())), void*>,
        std::is_convertible<decltype(std::declval<const T&>().GetIf(
                                std::declval<TypeId>())),
                            const void*>>::value>> : std::true_type {
  static_assert(absl::conjunction<
                    std::is_convertible<decltype(std::declval<T&>().GetIf(
                                            std::declval<TypeId>())),
                                        void*>,
                    std::is_convertible<decltype(std::declval<const T&>().GetIf(
                                            std::declval<TypeId>())),
                                        const void*>>::value,
                "Either both mutable and const or none GetIf() overloads "
                "must be provided by DependencyImpl");
};

// `DependencyDerived` adds `Dependency` and `StableDependency` operations
// uniformly implemented in terms of other operations: `operator*`,
// `operator->`, comparisons with `nullptr`, and `GetIf()`.
//
// It derives from the template parameter `Base` so that it can be used in
// `Dependency` (applied to `DependencyMaybeRef`) and `StableDependency`
// (applied to `StableDependencyImpl`).
template <typename Base, typename Ptr, typename Manager>
class DependencyDerived
    : public Base,
      public WithEqual<DependencyDerived<Base, Ptr, Manager>>,
      public ConditionallyAbslNullabilityCompatible<
          std::is_pointer<Ptr>::value> {
 public:
  using Base::Base;

  template <typename DependentPtr = Ptr,
            std::enable_if_t<std::is_pointer<DependentPtr>::value, int> = 0>
  std::remove_pointer_t<decltype(std::declval<const Base&>().get())>&
  operator*() const {
    const auto ptr = this->get();
    RIEGELI_ASSERT(ptr != nullptr)
        << "Failed precondition of Dependency::operator*: null pointer";
    return *ptr;
  }

  template <typename DependentPtr = Ptr,
            std::enable_if_t<std::is_pointer<DependentPtr>::value, int> = 0>
  decltype(std::declval<const Base&>().get()) operator->() const {
    const auto ptr = this->get();
    RIEGELI_ASSERT(ptr != nullptr)
        << "Failed precondition of Dependency::operator->: null pointer";
    return ptr;
  }

  template <typename DependentPtr = Ptr,
            std::enable_if_t<std::is_pointer<DependentPtr>::value, int> = 0>
  friend bool operator==(const DependencyDerived& a, std::nullptr_t) {
    return a.get() == nullptr;
  }

  template <
      typename OtherManager,
      std::enable_if_t<IsValidDependency<Ptr, OtherManager>::value, int> = 0>
  OtherManager* GetIf() {
    return GetIfImpl<OtherManager>();
  }
  template <
      typename OtherManager,
      std::enable_if_t<IsValidDependency<Ptr, OtherManager>::value, int> = 0>
  const OtherManager* GetIf() const {
    return GetIfImpl<OtherManager>();
  }

  void* GetIf(TypeId type_id) { return GetIfImpl(type_id); }
  const void* GetIf(TypeId type_id) const { return GetIfImpl(type_id); }

 protected:
  DependencyDerived(const DependencyDerived& that) = default;
  DependencyDerived& operator=(const DependencyDerived& that) = default;

  DependencyDerived(DependencyDerived&& that) = default;
  DependencyDerived& operator=(DependencyDerived&& that) = default;

  ~DependencyDerived() = default;

 private:
  template <typename OtherManager, typename DependentBase = Base,
            std::enable_if_t<HasGetIfStatic<DependentBase, OtherManager>::value,
                             int> = 0>
  OtherManager* GetIfImpl() {
    return Base::template GetIf<OtherManager>();
  }
  template <typename OtherManager, typename DependentBase = Base,
            std::enable_if_t<
                absl::conjunction<
                    absl::negation<HasGetIfStatic<DependentBase, OtherManager>>,
                    HasGetIfDynamic<DependentBase>>::value,
                int> = 0>
  OtherManager* GetIfImpl() {
    return static_cast<OtherManager*>(Base::GetIf(TypeId::For<OtherManager>()));
  }
  template <typename OtherManager, typename DependentBase = Base,
            std::enable_if_t<
                absl::conjunction<
                    absl::negation<HasGetIfStatic<DependentBase, OtherManager>>,
                    absl::negation<HasGetIfDynamic<DependentBase>>,
                    std::is_same<std::decay_t<Manager>, OtherManager>>::value,
                int> = 0>
  OtherManager* GetIfImpl() {
    return &this->manager();
  }
  template <typename OtherManager, typename DependentBase = Base,
            std::enable_if_t<
                absl::conjunction<
                    absl::negation<HasGetIfStatic<DependentBase, OtherManager>>,
                    absl::negation<HasGetIfDynamic<DependentBase>>,
                    absl::negation<std::is_same<std::decay_t<Manager>,
                                                OtherManager>>>::value,
                int> = 0>
  OtherManager* GetIfImpl() {
    return nullptr;
  }
  template <typename OtherManager, typename DependentBase = Base,
            std::enable_if_t<HasGetIfStatic<DependentBase, OtherManager>::value,
                             int> = 0>
  const OtherManager* GetIfImpl() const {
    return Base::template GetIf<OtherManager>();
  }
  template <typename OtherManager, typename DependentBase = Base,
            std::enable_if_t<
                absl::conjunction<
                    absl::negation<HasGetIfStatic<DependentBase, OtherManager>>,
                    HasGetIfDynamic<DependentBase>>::value,
                int> = 0>
  const OtherManager* GetIfImpl() const {
    return static_cast<const OtherManager*>(
        Base::GetIf(TypeId::For<OtherManager>()));
  }
  template <typename OtherManager, typename DependentBase = Base,
            std::enable_if_t<
                absl::conjunction<
                    absl::negation<HasGetIfStatic<DependentBase, OtherManager>>,
                    absl::negation<HasGetIfDynamic<DependentBase>>,
                    std::is_same<std::decay_t<Manager>, OtherManager>>::value,
                int> = 0>
  const OtherManager* GetIfImpl() const {
    return &this->manager();
  }
  template <typename OtherManager, typename DependentBase = Base,
            std::enable_if_t<
                absl::conjunction<
                    absl::negation<HasGetIfStatic<DependentBase, OtherManager>>,
                    absl::negation<HasGetIfDynamic<DependentBase>>,
                    absl::negation<std::is_same<std::decay_t<Manager>,
                                                OtherManager>>>::value,
                int> = 0>
  const OtherManager* GetIfImpl() const {
    return nullptr;
  }

  template <typename DependentBase = Base,
            std::enable_if_t<HasGetIfDynamic<DependentBase>::value, int> = 0>
  void* GetIfImpl(TypeId type_id) {
    return Base::GetIf(type_id);
  }
  template <typename DependentBase = Base,
            std::enable_if_t<!HasGetIfDynamic<DependentBase>::value, int> = 0>
  void* GetIfImpl(TypeId type_id) {
    if (TypeId::For<std::decay_t<Manager>>() == type_id) {
      return &this->manager();
    }
    return nullptr;
  }
  template <typename DependentBase = Base,
            std::enable_if_t<HasGetIfDynamic<DependentBase>::value, int> = 0>
  const void* GetIfImpl(TypeId type_id) const {
    return Base::GetIf(type_id);
  }
  template <typename DependentBase = Base,
            std::enable_if_t<!HasGetIfDynamic<DependentBase>::value, int> = 0>
  const void* GetIfImpl(TypeId type_id) const {
    if (TypeId::For<std::decay_t<Manager>>() == type_id) {
      return &this->manager();
    }
    return nullptr;
  }
};

}  // namespace dependency_internal

template <typename Ptr, typename Manager, typename Enable = void>
class Dependency;

template <typename Ptr, typename Manager>
class Dependency<Ptr, Manager,
                 std::enable_if_t<IsValidDependency<Ptr, Manager>::value>>
    : public dependency_internal::DependencyDerived<
          dependency_internal::DependencyMaybeRef<Ptr, Manager>, Ptr, Manager> {
 public:
  using Dependency::DependencyDerived::DependencyDerived;
};

namespace dependency_internal {

template <typename Manager, typename Enable = void>
struct DereferencedForVoidPtr : std::false_type {};
template <typename M>
struct DereferencedForVoidPtr<M*> : std::true_type {};
template <>
struct DereferencedForVoidPtr<std::nullptr_t> : std::true_type {};
template <typename M, typename Deleter>
struct DereferencedForVoidPtr<std::unique_ptr<M, Deleter>> : std::true_type {};

}  // namespace dependency_internal

// Specialization of `DependencyImpl<P*, M>` when `M*` is convertible to `P*`:
// an owned dependency stored by value.
//
// If `P` is possibly cv-qualified `void`, then `Dependency<P*, Manager>`
// has an ambiguous interpretation for `Manager` being `M*`, `std::nullptr_t`,
// or `std::unique_ptr<M, Deleter>`. The ambiguity is resolved in favor of
// pointing the `void*` to the dereferenced `M`, not to the `Manager` object
// itself.
template <typename P, typename M>
class DependencyImpl<
    P*, M,
    std::enable_if_t<absl::conjunction<
        std::is_convertible<M*, P*>,
        absl::negation<absl::conjunction<
            std::is_void<P>, dependency_internal::DereferencedForVoidPtr<
                                 std::decay_t<M>>>>>::value>>
    : public DependencyBase<M> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  M* get() const { return &this->mutable_manager(); }

  bool is_owning() const { return true; }

  static constexpr bool kIsStable = false;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  DependencyImpl(DependencyImpl&& that) = default;
  DependencyImpl& operator=(DependencyImpl&& that) = default;

  ~DependencyImpl() = default;
};

// `IsValidDependencyImpl<P*, M>` when `M*` is convertible to `P*` is
// specialized explicitly for a subtle reason:
//
// Consider a type `T` like `BrotliReader<AnyDependency<Reader*>>`.
// Checking `IsValidDependency<Reader*, T>` by instantiating
// `DependencyImpl<Reader*, T>` would try to generate the copy constructor of
// `DependencyImpl<Reader*, T>`, which would try to copy `DependencyBase<T>`,
// which would consider not only its copy constructor but also its constructor
// from `const T&` (even though conversion from `DependencyBase<T>` to `T`
// would ultimately fail), which would check whether `T` is copy constructible,
// which would consider not only its deleted copy constructor but also its
// constructor from `const AnyDependency<Reader*>&`, which would check whether
// `T` is implicitly convertible to `AnyDependency<Reader*>`, which would check
// whether `IsValidDependency<Reader*, T>`, which is still in the process of
// being determined.
template <typename P, typename M>
struct IsValidDependencyImpl<
    P*, M,
    std::enable_if_t<absl::conjunction<
        std::is_convertible<M*, P*>,
        absl::negation<absl::conjunction<
            std::is_void<P>, dependency_internal::DereferencedForVoidPtr<
                                 std::decay_t<M>>>>>::value>> : std::true_type {
};

// Specialization of `DependencyImpl<P*, M&>` when `M*` is convertible to `P*`:
// an unowned dependency passed by lvalue reference.
//
// If `P` is possibly cv-qualified `void`, then `Dependency<P*, Manager&>`
// has an ambiguous interpretation for `Manager` being `M*`, `std::nullptr_t`,
// or `std::unique_ptr<M, Deleter>`. The ambiguity is resolved in favor of
// pointing the `void*` to the dereferenced `M`, not to the `Manager` object
// itself.
template <typename P, typename M>
class DependencyImpl<
    P*, M&,
    std::enable_if_t<absl::conjunction<
        std::is_convertible<M*, P*>,
        absl::negation<absl::conjunction<
            std::is_void<P>, dependency_internal::DereferencedForVoidPtr<
                                 std::decay_t<M>>>>>::value>>
    : public DependencyBase<M&> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  M* get() const { return &this->manager(); }

  bool is_owning() const { return false; }

  static constexpr bool kIsStable = true;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = delete;

  ~DependencyImpl() = default;
};

// Specialization of `DependencyImpl<P*, M&&>` when `M*` is convertible to `P*`:
// an owned dependency passed by rvalue reference.
//
// If `P` is possibly cv-qualified `void`, then `Dependency<P*, Manager&&>`
// has an ambiguous interpretation for `Manager` being `M*`, `std::nullptr_t`,
// or `std::unique_ptr<M, Deleter>`. The ambiguity is resolved in favor of
// pointing the `void*` to the dereferenced `M`, not to the `Manager` object
// itself.
template <typename P, typename M>
class DependencyImpl<
    P*, M&&,
    std::enable_if_t<absl::conjunction<
        std::is_convertible<M*, P*>,
        absl::negation<absl::conjunction<
            std::is_void<P>, dependency_internal::DereferencedForVoidPtr<
                                 std::decay_t<M>>>>>::value>>
    : public DependencyBase<M&&> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  M* get() const { return &this->manager(); }

  bool is_owning() const { return true; }

  static constexpr bool kIsStable = true;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = delete;

  ~DependencyImpl() = default;
};

// Specialization of `DependencyImpl<P*, M*>` when `M*` is convertible to `P*`:
// an unowned dependency passed by pointer.
template <typename P, typename M>
class DependencyImpl<P*, M*,
                     std::enable_if_t<std::is_convertible<M*, P*>::value>>
    : public DependencyBase<M*> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  M* get() const { return this->manager(); }

  bool is_owning() const { return false; }

  static constexpr bool kIsStable = true;
};

// Specialization of `DependencyImpl<P*, std::nullptr_t>`: an unowned dependency
// passed by pointer, always missing. This is useful for `AnyDependency` and
// `AnyDependencyRef`.
template <typename P>
class DependencyImpl<P*, std::nullptr_t>
    : public DependencyBase<std::nullptr_t> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  std::nullptr_t get() const { return nullptr; }

  bool is_owning() const { return false; }

  static constexpr bool kIsStable = true;
};

// Specialization of `DependencyImpl<P*, std::unique_ptr<M, Deleter>>` when `M*`
// is convertible to `P*`: an owned dependency stored by `std::unique_ptr`.
template <typename P, typename M, typename Deleter>
class DependencyImpl<P*, std::unique_ptr<M, Deleter>,
                     std::enable_if_t<std::is_convertible<M*, P*>::value>>
    : public DependencyBase<std::unique_ptr<M, Deleter>> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  DependencyImpl(DependencyImpl&& that) = default;
  DependencyImpl& operator=(DependencyImpl&& that) = default;

  M* get() const { return this->manager().get(); }

  bool is_owning() const { return this->manager() != nullptr; }

  static constexpr bool kIsStable = true;
};

// Specializations of `DependencyImpl<Ptr, Manager>` for `Ptr` being a view type
// like `absl::string_view` with an implicit conversion from `M` that `Manager`
// is related to.
//
// `Ptr` being a pointer type is excluded because this was already handled.
//
// `M` being a pointer type is excluded because this interferes with
// `absl::Span<T>` which has a (deprecated) implicit conversion from a pointer
// type.

template <typename P, typename M>
class DependencyImpl<
    P, M,
    std::enable_if_t<absl::conjunction<
        absl::negation<std::is_pointer<P>>, absl::negation<std::is_pointer<M>>,
        std::is_convertible<const M&, P>>::value>> : public DependencyBase<M> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  M& get() const { return this->mutable_manager(); }

  static constexpr bool kIsStable = std::is_same<P, M>::value;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  DependencyImpl(DependencyImpl&& that) = default;
  DependencyImpl& operator=(DependencyImpl&& that) = default;

  ~DependencyImpl() = default;
};

template <typename P, typename M>
class DependencyImpl<
    P, M*,
    std::enable_if_t<absl::conjunction<
        absl::negation<std::is_pointer<P>>, absl::negation<std::is_pointer<M>>,
        std::is_convertible<const M&, P>>::value>> : public DependencyBase<M*> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  M& get() const { return *this->manager(); }

  static constexpr bool kIsStable = true;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  ~DependencyImpl() = default;
};

template <typename P, typename M, typename Deleter>
class DependencyImpl<
    P, std::unique_ptr<M, Deleter>,
    std::enable_if_t<absl::conjunction<
        absl::negation<std::is_pointer<P>>, absl::negation<std::is_pointer<M>>,
        std::is_convertible<const M&, P>>::value>>
    : public DependencyBase<std::unique_ptr<M, Deleter>> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  M& get() const { return *this->manager(); }

  static constexpr bool kIsStable = true;

 protected:
  DependencyImpl(DependencyImpl&& that) = default;
  DependencyImpl& operator=(DependencyImpl&& that) = default;

  ~DependencyImpl() = default;
};

// Additional specializations of `DependencyImpl<absl::string_view, Manager>`.
//
// They accept also types convertible to `absl::Span<const char>`.
//
// Specializations for `const char*` and `char*` are defined separately for
// `kIsStable` to be `true`.

template <>
class DependencyImpl<absl::string_view, const char*>
    : public DependencyBase<const char*> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  absl::string_view get() const { return this->manager(); }

  static constexpr bool kIsStable = true;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  ~DependencyImpl() = default;
};

template <>
class DependencyImpl<absl::string_view, char*> : public DependencyBase<char*> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  absl::string_view get() const { return this->manager(); }

  static constexpr bool kIsStable = true;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  ~DependencyImpl() = default;
};

template <typename M>
class DependencyImpl<
    absl::string_view, M,
    std::enable_if_t<absl::conjunction<
        absl::negation<std::is_pointer<M>>,
        absl::negation<std::is_convertible<const M&, absl::string_view>>,
        std::is_convertible<const M&, absl::Span<const char>>>::value>>
    : public DependencyBase<M> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  absl::string_view get() const {
    const absl::Span<const char> span = this->manager();
    return absl::string_view(span.data(), span.size());
  }

  static constexpr bool kIsStable =
      absl::disjunction<std::is_same<M, absl::Span<const char>>,
                        std::is_same<M, absl::Span<char>>>::value;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  DependencyImpl(DependencyImpl&& that) = default;
  DependencyImpl& operator=(DependencyImpl&& that) = default;

  ~DependencyImpl() = default;
};

template <typename M>
class DependencyImpl<
    absl::string_view, M*,
    std::enable_if_t<absl::conjunction<
        absl::negation<std::is_pointer<M>>,
        absl::negation<std::is_convertible<const M&, absl::string_view>>,
        std::is_convertible<const M&, absl::Span<const char>>>::value>>
    : public DependencyBase<M*> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  absl::string_view get() const {
    const absl::Span<const char> span = *this->manager();
    return absl::string_view(span.data(), span.size());
  }

  static constexpr bool kIsStable = true;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  ~DependencyImpl() = default;
};

template <typename M, typename Deleter>
class DependencyImpl<
    absl::string_view, std::unique_ptr<M, Deleter>,
    std::enable_if_t<absl::conjunction<
        absl::negation<std::is_pointer<M>>,
        absl::negation<std::is_convertible<const M&, absl::string_view>>,
        std::is_convertible<const M&, absl::Span<const char>>>::value>>
    : public DependencyBase<std::unique_ptr<M, Deleter>> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  absl::string_view get() const {
    const absl::Span<const char> span = *this->manager();
    return absl::string_view(span.data(), span.size());
  }

  static constexpr bool kIsStable = true;

 protected:
  DependencyImpl(DependencyImpl&& that) = default;
  DependencyImpl& operator=(DependencyImpl&& that) = default;

  ~DependencyImpl() = default;
};

// Additional specializations of `DependencyImpl<absl::Span<T>, Manager>`.
//
// They accept also types only explicitly convertible to `absl::Span<T>`
// which has an explicit converting constructor for a non-const `T`.

template <typename T, typename M>
class DependencyImpl<
    absl::Span<T>, M,
    std::enable_if_t<absl::conjunction<
        absl::negation<std::is_pointer<M>>,
        absl::negation<std::is_convertible<const M&, absl::Span<T>>>,
        std::is_constructible<absl::Span<T>, M&>>::value>>
    : public DependencyBase<M> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  absl::Span<T> get() const { return absl::Span<T>(this->mutable_manager()); }

  static constexpr bool kIsStable = false;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  DependencyImpl(DependencyImpl&& that) = default;
  DependencyImpl& operator=(DependencyImpl&& that) = default;

  ~DependencyImpl() = default;
};

template <typename T, typename M>
class DependencyImpl<
    absl::Span<T>, M*,
    std::enable_if_t<absl::conjunction<
        absl::negation<std::is_pointer<M>>,
        absl::negation<std::is_convertible<const M&, absl::Span<T>>>,
        std::is_constructible<absl::Span<T>, M&>>::value>>
    : public DependencyBase<M*> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  absl::Span<T> get() const { return absl::Span<T>(*this->manager()); }

  static constexpr bool kIsStable = true;

 protected:
  DependencyImpl(const DependencyImpl& that) = default;
  DependencyImpl& operator=(const DependencyImpl& that) = default;

  ~DependencyImpl() = default;
};

template <typename T, typename M, typename Deleter>
class DependencyImpl<
    absl::Span<T>, std::unique_ptr<M, Deleter>,
    std::enable_if_t<absl::conjunction<
        absl::negation<std::is_pointer<M>>,
        absl::negation<std::is_convertible<const M&, absl::Span<T>>>,
        std::is_constructible<absl::Span<T>, M&>>::value>>
    : public DependencyBase<std::unique_ptr<M, Deleter>> {
 public:
  using DependencyImpl::DependencyBase::DependencyBase;

  absl::Span<T> get() const { return absl::Span<T>(*this->manager()); }

  static constexpr bool kIsStable = true;

 protected:
  DependencyImpl(DependencyImpl&& that) = default;
  DependencyImpl& operator=(DependencyImpl&& that) = default;

  ~DependencyImpl() = default;
};

namespace dependency_internal {

// `AlwaysFalse<T...>::value` is `false`, but formally depends on `T...`.
// This is useful for `static_assert()`.

template <typename... T>
struct AlwaysFalse : std::false_type {};

}  // namespace dependency_internal

// A placeholder `Dependency` manager to be deduced by CTAD, used to delete CTAD
// for particular constructor argument types.
//
// It takes `ConstructorArgTypes` so that an error message from the
// `static_assert()` can show them.

template <typename... ConstructorArgTypes>
struct DeleteCtad {
  DeleteCtad() = delete;
};

template <typename Ptr, typename... ConstructorArgTypes>
class Dependency<Ptr, DeleteCtad<ConstructorArgTypes...>> {
  static_assert(dependency_internal::AlwaysFalse<ConstructorArgTypes...>::value,
                "Template arguments must be written explicitly "
                "with these constructor argument types");
};

}  // namespace riegeli

#endif  // RIEGELI_BASE_DEPENDENCY_H_

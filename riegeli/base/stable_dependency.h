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

#ifndef RIEGELI_BASE_STABLE_DEPENDENCY_H_
#define RIEGELI_BASE_STABLE_DEPENDENCY_H_

#include <atomic>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "riegeli/base/dependency.h"
#include "riegeli/base/reset.h"

namespace riegeli {

// Similar to `Dependency<Handle, Manager>`, but ensures that `Handle` stays
// unchanged when `StableDependency<Handle, Manager>` is moved.
// `StableDependency` can be used instead of `Dependency` if `Handle` stability
// is required, e.g. if background threads access the `Handle`.
//
// This template is specialized but does not have a primary definition.
template <typename Handle, typename Manager, typename Enable = void>
class StableDependency;

// Specialization when `Dependency<Handle, Manager>` is already stable.
template <typename Handle, typename Manager>
class StableDependency<Handle, Manager,
                       std::enable_if_t<Dependency<Handle, Manager>::kIsStable>>
    : public Dependency<Handle, Manager> {
 public:
  using StableDependency::Dependency::Dependency;

  StableDependency(StableDependency&& other) = default;
  StableDependency& operator=(StableDependency&& other) = default;
};

namespace dependency_internal {

template <typename Handle, typename Manager>
class StableDependencyImpl {
 public:
  StableDependencyImpl() = default;

  template <typename DependentManager = Manager,
            std::enable_if_t<std::is_convertible<const DependentManager&,
                                                 DependentManager>::value,
                             int> = 0>
  explicit StableDependencyImpl(const Manager& manager)
      : dep_(new Dependency<Handle, Manager>(manager)) {}
  template <typename DependentManager = Manager,
            std::enable_if_t<std::is_convertible<DependentManager&&,
                                                 DependentManager>::value,
                             int> = 0>
  explicit StableDependencyImpl(Manager&& manager) noexcept
      : dep_(new Dependency<Handle, Manager>(std::move(manager))) {}

  template <typename... ManagerArgs>
  explicit StableDependencyImpl(std::tuple<ManagerArgs...> manager_args)
      : dep_(new Dependency<Handle, Manager>(std::move(manager_args))) {}

  StableDependencyImpl(StableDependencyImpl&& that) noexcept
      : dep_(that.dep_.exchange(nullptr, std::memory_order_relaxed)) {}
  StableDependencyImpl& operator=(StableDependencyImpl&& that) noexcept {
    delete dep_.exchange(that.dep_.exchange(nullptr, std::memory_order_relaxed),
                         std::memory_order_relaxed);
    return *this;
  }

  ~StableDependencyImpl() { delete dep_.load(std::memory_order_relaxed); }

  ABSL_ATTRIBUTE_REINITIALIZES void Reset() {
    Dependency<Handle, Manager>* const dep =
        dep_.load(std::memory_order_relaxed);
    if (dep != nullptr) riegeli::Reset(*dep);
  }

  template <typename DependentManager = Manager,
            std::enable_if_t<std::is_convertible<const DependentManager&,
                                                 DependentManager>::value,
                             int> = 0>
  ABSL_ATTRIBUTE_REINITIALIZES void Reset(const Manager& manager) {
    Dependency<Handle, Manager>* const dep =
        dep_.load(std::memory_order_relaxed);
    if (dep == nullptr) {
      // A race would violate the contract because this is not a const method.
      dep_.store(new Dependency<Handle, Manager>(manager),
                 std::memory_order_relaxed);
    } else {
      riegeli::Reset(*dep, manager);
    }
  }
  template <typename DependentManager = Manager,
            std::enable_if_t<std::is_convertible<DependentManager&&,
                                                 DependentManager>::value,
                             int> = 0>
  ABSL_ATTRIBUTE_REINITIALIZES void Reset(Manager&& manager) {
    Dependency<Handle, Manager>* const dep =
        dep_.load(std::memory_order_relaxed);
    if (dep == nullptr) {
      // A race would violate the contract because this is not a const method.
      dep_.store(new Dependency<Handle, Manager>(std::move(manager)),
                 std::memory_order_relaxed);
    } else {
      riegeli::Reset(*dep, std::move(manager));
    }
  }

  template <typename... ManagerArgs>
  ABSL_ATTRIBUTE_REINITIALIZES void Reset(
      std::tuple<ManagerArgs...> manager_args) {
    Dependency<Handle, Manager>* const dep =
        dep_.load(std::memory_order_relaxed);
    if (dep == nullptr) {
      // A race would violate the contract because this is not a const method.
      dep_.store(new Dependency<Handle, Manager>(std::move(manager_args)),
                 std::memory_order_relaxed);
    } else {
      riegeli::Reset(*dep, std::move(manager_args));
    }
  }

  Manager& manager() { return EnsureAllocated()->manager(); }
  const Manager& manager() const { return EnsureAllocated()->manager(); }

  typename Dependency<Handle, Manager>::Subhandle get() const {
    return EnsureAllocated()->get();
  }

  bool is_owning() const { return EnsureAllocated()->is_owning(); }

  template <typename MemoryEstimator>
  friend void RiegeliRegisterSubobjects(const StableDependencyImpl& self,
                                        MemoryEstimator& memory_estimator) {
    Dependency<Handle, Manager>* const dep =
        self.dep_.load(std::memory_order_acquire);
    if (dep != nullptr) memory_estimator.RegisterDynamicObject(*dep);
  }

 private:
  Dependency<Handle, Manager>* EnsureAllocated() const {
    Dependency<Handle, Manager>* const dep =
        dep_.load(std::memory_order_acquire);
    if (ABSL_PREDICT_TRUE(dep != nullptr)) return dep;
    return EnsureAllocatedSlow();
  }

  Dependency<Handle, Manager>* EnsureAllocatedSlow() const;

  // Owned. `nullptr` is equivalent to a default constructed `Dependency`.
  mutable std::atomic<Dependency<Handle, Manager>*> dep_{nullptr};
};

}  // namespace dependency_internal

// Specialization when `Dependency<Handle, Manager>` is not stable: allocates
// the dependency dynamically.
template <typename Handle, typename Manager>
class StableDependency<
    Handle, Manager, std::enable_if_t<!Dependency<Handle, Manager>::kIsStable>>
    : public dependency_internal::DependencyDerived<
          dependency_internal::StableDependencyImpl<Handle, Manager>, Handle,
          Manager> {
 public:
  using StableDependency::DependencyDerived::DependencyDerived;

  StableDependency(StableDependency&& other) = default;
  StableDependency& operator=(StableDependency&& other) = default;
};

// Implementation details follow.

namespace dependency_internal {

template <typename Handle, typename Manager>
Dependency<Handle, Manager>*
StableDependencyImpl<Handle, Manager>::EnsureAllocatedSlow() const {
  Dependency<Handle, Manager>* const dep = new Dependency<Handle, Manager>();
  Dependency<Handle, Manager>* other_dep = nullptr;
  if (ABSL_PREDICT_FALSE(!dep_.compare_exchange_strong(
          other_dep, dep, std::memory_order_acq_rel))) {
    // We lost the race.
    delete dep;
    return other_dep;
  }
  return dep;
}

}  // namespace dependency_internal

}  // namespace riegeli

#endif  // RIEGELI_BASE_STABLE_DEPENDENCY_H_

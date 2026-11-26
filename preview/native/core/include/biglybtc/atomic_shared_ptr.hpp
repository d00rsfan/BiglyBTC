// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace biglybtc::core {

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
template<class T>
using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;
#else
template<class T>
class AtomicSharedPtr final {
public:
  AtomicSharedPtr() = default;
  AtomicSharedPtr(const AtomicSharedPtr&) = delete;
  AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

  std::shared_ptr<T> load() const noexcept { return std::atomic_load(&value_); }

  void store(std::shared_ptr<T> value) noexcept {
    std::atomic_store(&value_, std::move(value));
  }

private:
  std::shared_ptr<T> value_;
};
#endif

} // namespace biglybtc::core

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifdef _WIN32
#include <bit>
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace biglybtc::platform {

inline void* loadedSymbol(void* module, const char* name) noexcept {
#ifdef _WIN32
  return module ? std::bit_cast<void*>(::GetProcAddress(static_cast<HMODULE>(module), name)) : nullptr;
#else
  return ::dlsym(module, name);
#endif
}

inline void* moduleContaining(const void* address) noexcept {
  if (!address) { return nullptr; }
#ifdef _WIN32
  HMODULE module = nullptr;
  constexpr DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
  return ::GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(address), &module) ? module : nullptr;
#else
  Dl_info info{};
  return ::dladdr(address, &info) ? info.dli_fbase : nullptr;
#endif
}

inline void* visibleSymbol(const void* anchor, const char* name) noexcept {
#ifdef _WIN32
  return loadedSymbol(moduleContaining(anchor), name);
#else
  (void)anchor;
  return loadedSymbol(RTLD_DEFAULT, name);
#endif
}

inline void* sameModuleSymbol(const void* anchor, const char* name) noexcept {
  const auto module = moduleContaining(anchor);
  if (!module) { return nullptr; }
  auto* symbol = visibleSymbol(anchor, name);
  return moduleContaining(symbol) == module ? symbol : nullptr;
}

} // namespace biglybtc::platform

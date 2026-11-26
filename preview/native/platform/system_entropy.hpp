// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <unistd.h>
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
#include <sys/random.h>
#endif

namespace biglybtc::platform {

inline bool fillSystemEntropy(void* data, std::size_t length, bool nonBlocking = false) noexcept {
  auto* bytes = static_cast<unsigned char*>(data);
  while (length != 0) {
#if defined(_WIN32)
    (void)nonBlocking;
    const auto count = static_cast<ULONG>(std::min<std::size_t>(length, MAXDWORD));
    if (::BCryptGenRandom(nullptr, bytes, count, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) { return false; }
#elif defined(__APPLE__)
    (void)nonBlocking;
    // Darwin getentropy accepts at most 256 bytes per call.
    const auto count = std::min<std::size_t>(length, 256);
    if (::getentropy(bytes, count) != 0) {
      if (errno == EINTR) { continue; }
      return false;
    }
#else
    const auto result = ::getrandom(bytes, length, nonBlocking ? GRND_NONBLOCK : 0);
    if (result <= 0) {
      if (result < 0 && errno == EINTR) { continue; }
      return false;
    }
    const auto count = static_cast<std::size_t>(result);
#endif
    bytes += count;
    length -= count;
  }
  return true;
}

} // namespace biglybtc::platform

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <unistd.h>

#ifndef __APPLE__
#include <sys/random.h>
#endif

namespace biglybtc::platform {

inline bool fillSystemEntropy(void* data, std::size_t length, bool nonBlocking = false) noexcept {
  auto* bytes = static_cast<unsigned char*>(data);
  while (length != 0) {
#ifdef __APPLE__
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

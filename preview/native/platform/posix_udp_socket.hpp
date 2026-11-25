// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace biglybtc::platform {

inline int openNonblockingUdpSocket(int family) noexcept {
#ifdef __APPLE__
  const int socket = ::socket(family, SOCK_DGRAM, 0);
  if (socket < 0) { return socket; }
  if (::fcntl(socket, F_SETFD, FD_CLOEXEC) < 0 || ::fcntl(socket, F_SETFL, O_NONBLOCK) < 0) {
    const int code = errno;
    ::close(socket);
    errno = code;
    return -1;
  }
  return socket;
#else
  return ::socket(family, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
#endif
}

} // namespace biglybtc::platform

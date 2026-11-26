// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ip_socket.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

#ifndef _WIN32
#include "posix_udp_socket.hpp"
#endif

namespace biglybtc::platform {

class UdpSocket final {
public:
#ifdef _WIN32
  using Length = int;
  using Native = SOCKET;
  static constexpr Native Invalid = INVALID_SOCKET;
#else
  using Length = socklen_t;
  using Native = int;
  static constexpr Native Invalid = -1;
#endif

  explicit UdpSocket(int family) noexcept {
#ifdef _WIN32
    WSADATA data{};
    started_ = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    if (!started_) { return; }
    socket_ = ::WSASocketW(family, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (socket_ != Invalid) {
      u_long enabled = 1;
      if (::ioctlsocket(socket_, FIONBIO, &enabled) != 0) {
        ::closesocket(std::exchange(socket_, Invalid));
      }
    }
#else
    socket_ = openNonblockingUdpSocket(family);
#endif
  }

  UdpSocket(UdpSocket&& other) noexcept : socket_(std::exchange(other.socket_, Invalid))
#ifdef _WIN32
      , started_(std::exchange(other.started_, false))
#endif
  {}

  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  ~UdpSocket() {
#ifdef _WIN32
    if (*this) { ::closesocket(socket_); }
    if (started_) { ::WSACleanup(); }
#else
    if (*this) { ::close(socket_); }
#endif
  }

  explicit operator bool() const noexcept { return socket_ != Invalid; }

  bool bind(const sockaddr* address, Length size) const noexcept {
    return ::bind(socket_, address, size) == 0;
  }

  bool connect(const sockaddr* address, Length size) const noexcept {
    return ::connect(socket_, address, size) == 0;
  }

  bool localAddress(sockaddr* address, Length& size) const noexcept {
    return ::getsockname(socket_, address, &size) == 0;
  }

  bool send(std::string_view datagram) const noexcept {
    if (datagram.size() > 65535) { return false; }
    return ::send(socket_, datagram.data(), static_cast<Length>(datagram.size()), 0)
        == static_cast<std::ptrdiff_t>(datagram.size());
  }

  std::ptrdiff_t receive(std::span<char> buffer) const noexcept {
    if (buffer.size() > 65535) { return -1; }
    const auto result = ::recv(socket_, buffer.data(), static_cast<Length>(buffer.size()), 0);
#ifdef _WIN32
    // The caller uses an extra byte to distinguish truncated datagrams.
    if (result < 0 && ::WSAGetLastError() == WSAEMSGSIZE) {
      return static_cast<std::ptrdiff_t>(buffer.size());
    }
#endif
    return result;
  }

  static bool receiveWouldBlock() noexcept {
#ifdef _WIN32
    const auto error = ::WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
  }

private:
  Native socket_ = Invalid;
#ifdef _WIN32
  bool started_ = false;
#endif
};

} // namespace biglybtc::platform

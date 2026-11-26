// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/socket.h>
#endif

namespace biglybtc::platform {

#ifdef _WIN32
inline constexpr std::size_t MaximumInterfaceName = 127;
#else
// Native interface names include their terminating NUL within IF_NAMESIZE.
inline constexpr std::size_t MaximumInterfaceName = IF_NAMESIZE - 1;
#endif

}

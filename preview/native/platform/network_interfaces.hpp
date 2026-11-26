// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace biglybtc::platform {

enum class NetworkInterfacesError { Io, Limit };

struct InterfaceAddress {
  std::string name;
  std::array<std::uint8_t, 16> address{};
  unsigned index = 0;
  unsigned scope = 0;
  unsigned length = 0;
  bool up = false;
  bool loopback = false;
};

// Snapshot the addresses assigned to local interfaces without sending packets.
std::expected<std::vector<InterfaceAddress>, NetworkInterfacesError> readInterfaceAddresses(std::size_t maximum);

}

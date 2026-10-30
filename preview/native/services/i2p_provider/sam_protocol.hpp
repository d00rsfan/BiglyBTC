// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "datagram.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace biglybtc::services::i2p::provider {
inline constexpr std::size_t MaximumCommandBytes=2048,MaximumBridgeSockets=6;
enum class CommandKind {Hello,Create,Naming,Connect,Accept};
enum class Purpose {Peer,Tracker};
struct Command {
  Command()=default;
  Command(Command&&)noexcept=default;
  Command& operator=(Command&&)noexcept=default;
  Command(const Command&)=delete;
  ~Command();
  CommandKind kind{};
  std::map<std::string,std::string,std::less<>> fields;
};
// One bounded ASCII line. Duplicate/unknown keys, quoting, CR, embedded NUL,
// empty tokens and a second command are rejected. This is deliberately not a
// general SAM implementation; all supported fields have an owning consumer.
std::optional<Command> parseCommand(std::string_view);
bool constantEqual(View,View)noexcept;
std::optional<Hash> hashName(std::string_view);
std::string hashName(const Hash&);
std::string destinationText(View);
std::optional<Destination> destinationText(std::string_view);
std::optional<std::uint64_t> decimal(std::string_view,std::uint64_t maximum);
std::optional<Bytes> hex(std::string_view,std::size_t bytes);
std::string hex(View);
} // namespace biglybtc::services::i2p::provider

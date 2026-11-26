#pragma once
#include "state_file_io.hpp"
#include <stop_token>

namespace biglybtc::storage::scheduler_detail {
using detail::StateDescriptor;
using detail::StateHandle;
using detail::StateStamp;
using detail::InvalidStateHandle;
struct Failure { Error error; };
[[noreturn]] void fail(ErrorCode, const char*);
void checkStop(std::stop_token);
struct Raw {
  std::optional<std::string> bytes;
  StateStamp identity{};
};
Raw read(StateHandle directory, const char* name, std::size_t maximum);
void unchanged(StateHandle directory, const char* name, const Raw&, std::size_t maximum);
StateDescriptor directory(StateHandle parent, const char* name, bool missingAllowed = false);
void directoryIdentity(StateHandle parent, const char* name, StateHandle held);
// The fixed exclusive temporary is retained on uncertain publication.
Raw replace(StateHandle directory, const char* name, const Raw&, std::string_view, std::size_t maximum);
void remove(StateHandle directory, const char* name, const Raw&, std::size_t maximum);
std::string sha256(StateHandle file, std::uint64_t expected);
}

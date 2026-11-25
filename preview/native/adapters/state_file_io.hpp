// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"
#ifndef _WIN32
#include "file_io.hpp"
#endif

#include <ctime>
#include <utility>

namespace biglybtc::storage::detail {

using StateHandle = decltype(std::declval<const ProfileLease&>().directoryHandle());
#ifdef _WIN32
inline constexpr StateHandle InvalidStateHandle = nullptr;

class StateDescriptor final {
public:
  StateDescriptor() noexcept = default;
  explicit StateDescriptor(StateHandle handle) noexcept : handle_(handle) {}
  StateDescriptor(const StateDescriptor&) = delete;
  StateDescriptor& operator=(const StateDescriptor&) = delete;
  StateDescriptor(StateDescriptor&& other) noexcept : handle_(other.release()) {}
  StateDescriptor& operator=(StateDescriptor&& other) noexcept;
  ~StateDescriptor();
  StateHandle get() const noexcept { return handle_; }
  StateHandle release() noexcept { return std::exchange(handle_, InvalidStateHandle); }
private:
  StateHandle handle_ = InvalidStateHandle;
};
#else
inline constexpr StateHandle InvalidStateHandle = -1;
using StateDescriptor = Descriptor;
#endif

struct StateStamp {
  std::uint64_t device{}, inode{}, links{}, owner{}, group{};
  std::int64_t size{};
  unsigned mode{};
  timespec modified{}, changed{};
  bool regular{}, directory{}, owned{};
};

inline bool sameStateIdentity(const StateStamp& a, const StateStamp& b) noexcept {
  return a.device == b.device && a.inode == b.inode;
}

inline bool sameStateTimestamp(timespec a, timespec b) noexcept {
  return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

inline bool sameStateSnapshot(const StateStamp& a, const StateStamp& b) noexcept {
  return sameStateIdentity(a, b) && a.size == b.size && a.mode == b.mode
      && a.owner == b.owner && a.group == b.group && a.links == b.links && a.owned == b.owned
      && sameStateTimestamp(a.modified, b.modified) && sameStateTimestamp(a.changed, b.changed);
}

std::expected<StateDescriptor, Error> openStateDirectory(StateHandle parent, const std::string& name);
std::expected<StateDescriptor, Error> createStateDirectory(StateHandle parent, const std::string& name);
std::expected<StateDescriptor, Error> duplicateStateDirectory(StateHandle directory);
std::expected<std::filesystem::path, Error> stateDirectoryPath(StateHandle directory);
std::expected<StateDescriptor, Error> openStatePathDirectory(const std::filesystem::path& path);
std::expected<StateDescriptor, Error> openStateFile(StateHandle parent, const std::string& name);
std::expected<StateStamp, Error> stateStamp(StateHandle);
std::expected<std::optional<StateStamp>, Error> stateStampAt(StateHandle parent, const std::string& name);
std::ptrdiff_t readStateAt(StateHandle, char* data, std::size_t bytes, std::uint64_t offset);
std::expected<std::optional<std::string>, Error> readStateFile(
    StateHandle parent, const std::string& name, std::size_t maximum);
std::expected<StateDescriptor, Error> stageStateFile(StateHandle parent, const std::string& name,
    std::string_view data, unsigned mode);
std::expected<void, Error> publishStateFile(StateHandle directory, const std::string& source,
    const std::string& destination, StateHandle staged = InvalidStateHandle, bool replace = true,
    const StateStamp* expected = nullptr);
std::expected<void, Error> retireStateFile(StateHandle directory, const std::string& name, const StateStamp& expected);
std::expected<void, Error> protectPrivateStateFile(StateHandle file);
std::expected<void, Error> syncStateFile(StateHandle);
std::expected<void, Error> syncStateDirectory(StateHandle);
std::expected<void, Error> setStateMtime(StateHandle, timespec);
unsigned stateWriteMode(const StateStamp&) noexcept;

} // namespace biglybtc::storage::detail

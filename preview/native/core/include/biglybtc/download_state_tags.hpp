// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace biglybtc::core::tags::download_state {
// Original externalised IDs. Data only: no download, profile or effect rights.
enum class Tag : unsigned {
  Initializing, Downloading, Seeding, QueuedDownloading, QueuedSeeding, Stopped,
  Error, Active, Paused, Inactive, Complete, Incomplete, Moving, Checking, Deleting
};
struct State {
  std::array<bool,15> members{};
  std::optional<Tag> main;
  std::optional<bool> complete;
  std::optional<std::int64_t> activityPrefix;
  bool contains(Tag tag)const noexcept{return members[static_cast<unsigned>(tag)];}
  bool operator==(const State&)const=default;
};
struct Change {int state=0;bool complete=false,paused=false,destroyed=false;};
struct Activity {
  std::int64_t received=0,sent=0,receiveRate=0,sendRate=0;
  bool destroyed=false;
};
// Call only for observed state events, not a translated UI label. Transient
// checking/ready/etc retain the preceding main tag. Activity is a separate
// original ten-second observation; its return requests the last-active stamp.
void stateChanged(State&,Change)noexcept;
bool updateActive(State&,Activity)noexcept;
void operation(State&,int originalOperation,bool added,bool hasDownload=true)noexcept;
void removeInitiated(State&)noexcept;
void remove(State&)noexcept;
} // namespace biglybtc::core::tags::download_state

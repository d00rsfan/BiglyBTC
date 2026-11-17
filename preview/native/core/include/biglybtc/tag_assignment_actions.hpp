// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/bencode.hpp>
#include <array>
#include <cstdint>
#include <expected>
#include <span>
namespace biglybtc::core::tags::assignment {
inline constexpr std::uint32_t Start=2,Stop=4,Force=8,NotForce=16,Pause=64,Resume=128,Queue=32768;
inline constexpr auto LifecycleMask=Start|Stop|Force|NotForce|Pause|Resume|Queue;
enum class Task { StartOrResume, StopQueueOrPause, ForceStart, CancelForceStart };
enum class Effect { Queue, Resume, Stop, QueueAfterStop, Pause, ForceStart, CancelForceStart, CheckMaximum };
enum class Error { Unsupported, Invalid };
struct SavedActions {
  std::uint32_t supported=0,enabled=0;
  bool lifecycleOnly()const noexcept{return (enabled&~LifecycleMask)==0;}
};
// Actual original getter projection only. Non-Long/missing fields fall back to
// false, and only Long1 enables an individually supported flag. Unknown fields
// are untouched. This does not certify an editable source or actor permission.
SavedActions readSaved(const Node& record,std::int32_t tagType)noexcept;

// A value protocol only. No assignment receipt, source/command permission,
// dispatch queue, callback, scheduler, profile, engine or effect is owned here.
struct Plan {
  std::array<Task,2> values{};unsigned count=0;
  std::span<const Task> tasks()const noexcept{return {values.data(),count};}
};
struct Effects {
  std::array<Effect,2> values{};unsigned count=0;
  std::span<const Effect> effects()const noexcept{return {values.data(),count};}
};
std::expected<Plan,Error> plan(std::uint32_t enabled,std::int32_t state,bool metadata,bool destroyed)noexcept;
// Original async START/STOP/QUEUE tests read CURRENT action flags at execution;
// a scheduled force-start boolean is already fixed. The supplied current flags
// do not certify settings, ownership or permission to execute anything.
std::expected<Effects,Error> resolve(Task,std::uint32_t currentEnabled)noexcept;
} // namespace biglybtc::core::tags::assignment

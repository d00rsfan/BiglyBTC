// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "biglybtc/auto_speed_v1.hpp"
#include "biglybtc/auto_speed_v2.hpp"
#include "biglybtc/passive_speed_mapper.hpp"
#include "persistent_session.hpp"

#include <utility>

namespace biglybtc::app {
class GeneralChats;
class LiveSpeedMapperBinding;

// Shares the actual profile worker, engine and owned DHT probe source.
class AutomaticSpeedControl final {
public:
  void step(const storage::ProfileLease&,const engine::NetworkPolicy&,engine::PersistentSession&,
      GeneralChats*,LiveSpeedMapperBinding*,bool schedulerEnabled,std::int64_t nowMillis);
  bool active() const noexcept { return state_.controller||state_.adaptive; }
  bool adaptive() const noexcept { return state_.adaptive.has_value(); }
  bool hasAdvice() const noexcept { return state_.lastAdvice.has_value(); }

private:
  struct State {
    std::optional<core::AutoSpeedV1Preferences> preferences;
    std::optional<core::AutoSpeedV1> controller;
    std::optional<core::AutoSpeedV2Preferences> adaptivePreferences;
    std::optional<core::AutoSpeedV2> adaptive;
    std::unique_ptr<core::PassiveSpeedMapper> mapper, trial;
    std::optional<std::pair<std::int64_t,float>> uploadCapacity, downloadCapacity;
    std::uint64_t epoch=0, lastProbe=0;
    std::optional<engine::AccountingIdentity> source;
    std::optional<std::int64_t> lastRate, lastAdvice;
    std::int64_t pollAfter=0;
    bool applied=false, seedingUpload=false;
  } state_;
};
} // namespace biglybtc::app

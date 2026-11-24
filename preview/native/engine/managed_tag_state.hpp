// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "managed_download.hpp"
#include <biglybtc/download_state_tags.hpp>
#include <biglybtc/tag_runtime_registry.hpp>
#include <chrono>

namespace biglybtc::engine::detail {
// Passive policy history for one unchanged retained admission. Its owner must
// discard it on missing/ambiguous observations or engine/admission replacement.
class ManagedTagState final {
public:
  using Clock=std::chrono::steady_clock;
  using Members=std::array<core::tags::runtime::Membership,15>;
  void observe(const engine::ManagedDownloadStatus&, Clock::time_point,
               std::optional<std::int64_t> wallMilliseconds = {});
  Members members()const noexcept;
  std::optional<std::int64_t> lastActiveMillis() const noexcept { return lastActiveMillis_; }
  std::optional<std::int64_t> secondsSinceLastActive(std::int64_t wallMilliseconds) const noexcept;
private:
  core::tags::download_state::State state_;
  bool mainKnown_=false,activityKnown_=false,completeKnown_=false;
  bool pausedKnown_=false;
  std::optional<Clock::time_point> last_,activityAt_;
  std::optional<std::int64_t> lastActiveMillis_;
};
} // namespace biglybtc::engine::detail

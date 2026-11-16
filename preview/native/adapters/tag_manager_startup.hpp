// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include <stop_token>
#include <utility>
namespace biglybtc::storage {
// Immutable original startup setting, captured from this retained profile.
// It grants no tag effect rights and has no caller-supplied enabled setter.
class TagManagerStartupMode final {
public:
 static std::expected<TagManagerStartupMode,Error> capture(const ProfileLease&,std::stop_token={});
 bool enabled()const noexcept{return enabled_;}
 bool matches(const ProfileLease& lease)const noexcept{return lease_&&lease_->sharesOwnership(lease);}
private:
 TagManagerStartupMode(std::shared_ptr<const ProfileLease> lease,bool enabled):lease_(std::move(lease)),enabled_(enabled){}
 std::shared_ptr<const ProfileLease> lease_;bool enabled_=false;
};
}

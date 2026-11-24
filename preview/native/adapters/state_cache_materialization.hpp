#pragma once
#include "profile.hpp"
#include <functional>
#include <stop_token>
#include <biglybtc/library.hpp>

namespace biglybtc::storage {
struct StateCacheMaterialization {
  std::size_t examined=0,hydrated=0,stateWrites=0,cacheWrites=0,retained=0;
};
// One pass; resume/info and their source mtime remain unchanged. The original
// cache remains authoritative until all hydrated primaries are durable. One
// final CAS removes their entries. Unrequested, missing and invalid entries remain.
// A selector limits this pass to the current transfer-admission requests.
std::expected<StateCacheMaterialization,Error> materializeStateCache(
    const ProfileLease&,std::stop_token={},std::function<void(std::size_t,std::size_t)> progress={},
    const std::function<bool(const core::TorrentHash&)>& requested={});
}

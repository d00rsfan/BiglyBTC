#pragma once
#include "profile.hpp"
#include <functional>
#include <stop_token>

namespace biglybtc::storage {
struct StateCacheMaterialization {
  std::size_t examined=0,hydrated=0,stateWrites=0,cacheWrites=0,retained=0;
};
// One pass; resume/info and their source mtime remain unchanged. The original
// cache remains authoritative until all hydrated primaries are durable. One
// final CAS removes their entries; missing/recovered/unknown entries remain.
std::expected<StateCacheMaterialization,Error> materializeStateCache(
    const ProfileLease&,std::stop_token={},std::function<void(std::size_t,std::size_t)> progress={});
}

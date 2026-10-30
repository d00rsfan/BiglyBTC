// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
namespace biglybtc::services::i2p::provider {class ProfileSource;class SourcePermit;}
namespace biglybtc::services::i2p::i2cp {
// Optional concrete profile-backed authority. Construction and permit changes
// are unavailable to channel callers; only the held source/worker mint them.
// current() performs atomics only, never filesystem work or a Qt callback.
class EffectAuthority final {
public:
  bool revoked()const noexcept{return revoked_.load();}
  bool current()const noexcept{try{return !revoked_.load()&&current_(revision_.load());}catch(...){return false;}}
  bool granted()const noexcept{return granted_.load()&&current();}
private:
  friend class provider::ProfileSource;
  friend class provider::SourcePermit;
  EffectAuthority(std::function<bool(std::uint64_t)> current,std::uint64_t revision):current_(std::move(current)),revision_(revision){}
  bool enter(std::uint64_t revision)noexcept{bool no=false;if(!granted_.compare_exchange_strong(no,true)){return false;}revision_=revision;if(!current()){granted_=false;return false;}return true;}
  void leave()noexcept{granted_=false;}
  void revoke()noexcept{revoked_=true;granted_=false;}
  const std::function<bool(std::uint64_t)> current_;
  std::atomic<std::uint64_t> revision_;
  std::atomic<bool> granted_{false},revoked_{false};
};
}

// SPDX-License-Identifier: GPL-2.0-or-later
// BuddyPluginBeta private getClone/destroy; SWT AC:KeepAlive runtime clone.
#pragma once
#include <cstdint>
#include <set>
namespace biglybtc::services::msgsync {
class ViewReferences final {
public:
 enum class Released {Unknown,Retained,Retire};
 bool acquire(std::uint64_t id){if(!id){return false;}if(views_.contains(id)){return true;}if(views_.size()>=64){return false;}views_.insert(id);adopted_=true;return true;}
 Released release(std::uint64_t id){if(!views_.erase(id)){return Released::Unknown;}return shouldRetire()?Released::Retire:Released::Retained;}
 bool keepAlive()const{return alive_;}void keepAlive(bool value){alive_=value;}
 bool shouldRetire()const{return adopted_&&views_.empty()&&!alive_;}
 const std::set<std::uint64_t>& views()const{return views_;}
private:
 // Until a GUI view adopts it, the facade caller owns the original reference.
 bool adopted_=false,alive_=false;std::set<std::uint64_t> views_;
};
}

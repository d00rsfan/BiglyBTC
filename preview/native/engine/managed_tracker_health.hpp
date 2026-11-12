// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <libtorrent/socket.hpp>
#include <map>
#include <algorithm>
#include <string>
#include <tuple>
namespace biglybtc::engine::detail {
// Presentation of actual announce alerts, scoped by URL, listener and hash
// protocol. A failed IPv6 attempt cannot revoke a working IPv4 announce, or a
// backup tracker revoke a working primary. No network permission comes from it.
class ManagedTrackerHealth final {
public:
  enum class Outcome { Announcing, Reply, Error, Skipped };
  struct Snapshot { int state=0; std::string detail;int peers=-1; };
  void observe(std::string url,libtorrent::tcp::endpoint local,unsigned protocol,
      Outcome outcome,std::string detail={},int peers=-1) {
    const Key key{std::move(url),std::move(local),protocol};
    auto found=entries_.find(key);
    if(found==entries_.end()) {
      if(entries_.size()>=256){overflow_=true;return;}
      found=entries_.emplace(key,Entry{}).first;
    }
    // A refresh has not yet invalidated the preceding successful announce.
    if(outcome==Outcome::Announcing&&found->second.outcome==Outcome::Reply){return;}
    found->second={outcome,std::move(detail),++sequence_,std::max(-1,peers)};
  }
  Snapshot routeSnapshot(const std::string& url,const libtorrent::tcp::endpoint& local,unsigned protocol)const {
    const auto found=entries_.find(Key{url,local,protocol});if(found==entries_.end()){return {};}
    const auto& entry=found->second;
    return {entry.outcome==Outcome::Reply?2:entry.outcome==Outcome::Announcing?1:3,
      entry.outcome==Outcome::Skipped?"No matching local route; request was not sent":entry.detail,entry.peers};
  }
  Snapshot snapshot()const {
    if(overflow_){return {3,"Tracker status capacity exceeded"};}
    bool pending=false;const Entry* error=nullptr;const Entry* reply=nullptr;bool skipped=false;
    for(const auto& [key,value]:entries_) {
      (void)key;
      if(value.outcome==Outcome::Reply&&(!reply||value.sequence>reply->sequence)){reply=&value;}
      if(value.outcome==Outcome::Announcing){pending=true;}
      if(value.outcome==Outcome::Error&&(!error||value.sequence>error->sequence)){error=&value;}
      skipped|=value.outcome==Outcome::Skipped;
    }
    if(reply){return {2,{},reply->peers};}
    if(pending){return {1,{}};}
    if(error){return {3,error->detail};}
    if(skipped){return {3,"No matching local route for tracker announce; request was not sent"};}
    return {};
  }
private:
  using Key=std::tuple<std::string,libtorrent::tcp::endpoint,unsigned>;
  struct Entry { Outcome outcome=Outcome::Announcing; std::string detail; unsigned long long sequence=0;int peers=-1; };
  std::map<Key,Entry> entries_;unsigned long long sequence_=0;bool overflow_=false;
};
}

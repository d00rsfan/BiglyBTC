// SPDX-License-Identifier: GPL-2.0-or-later
#include "all_trackers_profile.hpp"
#include "file_operations.hpp"
#include "original_identity_digest.hpp"
#include "torrent_state.hpp"
#include <map>
#include <set>

namespace biglybtc::app {
namespace {
namespace tracker=core::tags::tracker;
constexpr std::size_t MaxSubjects=512,MaxOrigins=2048,MaxProperties=64;
storage::Error incomplete(std::string detail){
  return {storage::ErrorCode::Unsupported,std::move(detail)};
}
std::expected<void,storage::Error> readTrackers(const core::Document& metainfo,
    AllTrackerSubject& subject,std::map<std::string,AllTrackerOrigin>& origins){
  std::set<std::string> seen;
  const auto append=[&](const core::Node& url)->std::expected<void,storage::Error>{
    if(url.type!=core::Node::Type::Bytes||subject.trackers.size()==MaxProperties){
      return std::unexpected(incomplete("Tracker declarations exceed the supported shape"));
    }
    auto track=tracker::selectionUrl(url.bytes);
    if(!track){
      return std::unexpected(incomplete("A tracker URL is outside the supported original forms"));
    }
    subject.trackers.push_back(std::move(track->endpoint));
    if(!track->origin||!seen.insert(track->origin->name).second){return {};}
    const auto& key=track->origin->name;
    if(!origins.contains(key)&&origins.size()==MaxOrigins){
      return std::unexpected(incomplete("Too many tracker origins to read a complete selection"));
    }
    auto row=origins.try_emplace(key,AllTrackerOrigin{*track->origin,0}).first;
    ++row->second.torrents;
    return {};
  };
  if(const auto* announce=metainfo.find("announce")){
    auto added=append(*announce);if(!added){return added;}
  }
  if(const auto* tiers=metainfo.find("announce-list")){
    if(tiers->type!=core::Node::Type::List||tiers->children.size()>16){
      return std::unexpected(incomplete("Tracker tiers exceed the supported shape"));
    }
    for(const auto& tier:tiers->children){
      if(tier.type!=core::Node::Type::List){
        return std::unexpected(incomplete("Tracker tiers need original lists"));
      }
      for(const auto& url:tier.children){
        auto added=append(url);if(!added){return added;}
      }
    }
  }
  return {};
}
}

std::expected<AllTrackerLibrary,storage::Error> readAllTrackerLibrary(
    const storage::ProfileLease& lease,std::stop_token stop){
  try{
    auto access=lease.sourceAccess();
    if(!access){return std::unexpected(access.error());}
    if(storage::hasPendingFileOperation(lease)){
      return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Tracker library has a pending file operation"});
    }
    auto source=storage::ConfigFile::load(lease,"downloads.config");
    if(!source){
      if(source.error().code==storage::ErrorCode::Missing){
        return AllTrackerLibrary{access->revision(),{},{}};
      }
      return std::unexpected(source.error());
    }
    const auto inventory=core::readLibrary(source->document());
    if(source->recoverySource()!=storage::RecoverySource::Primary||!inventory||
        !inventory->issues.empty()||inventory->downloads.size()>MaxSubjects){
      return std::unexpected(incomplete("A complete original tracker inventory is unavailable"));
    }
    auto store=storage::TorrentStateStore::open(lease);
    if(!store){return std::unexpected(store.error());}
    AllTrackerLibrary result;
    result.sourceRevision=access->revision();
    result.subjects.reserve(inventory->downloads.size());
    std::set<std::string> hashes;
    std::map<std::string,AllTrackerOrigin> origins;
    for(const auto& row:inventory->downloads){
      if(stop.stop_requested()){
        return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Tracker library read cancelled"});
      }
      if(!row.persistent||!row.hash||row.hash->bytes().size()!=20||!hashes.insert(row.hash->hex()).second){
        return std::unexpected(incomplete("A tracker subject has no unique original identity"));
      }
      auto state=store->load(lease,*row.hash);
      if(!state||state->recoveredBackup()){
        return std::unexpected(incomplete("A tracker subject has no primary original metainfo"));
      }
      const auto& document=state->document();
      const auto identity=storage::digestOriginalTorrentIdentity(document);
      if(!identity||identity->originalKey!=*row.hash){
        return std::unexpected(incomplete("A tracker subject identity differs from the original inventory"));
      }
      const auto* info=document.find("info");
      const auto* attrs=document.find("attributes");
      const auto* name=attrs?attrs->find("displayname"):nullptr;
      if(!name||name->type!=core::Node::Type::Bytes||name->bytes.empty()){
        name=info?info->find("name.utf-8"):nullptr;
        if(!name){name=info?info->find("name"):nullptr;}
      }
      if(!name||name->type!=core::Node::Type::Bytes||name->bytes.size()>4096){
        return std::unexpected(incomplete("A tracker subject has no bounded original name"));
      }
      AllTrackerSubject subject{*row.hash,std::string(name->bytes),{}};
      auto parsed=readTrackers(document,subject,origins);
      if(!parsed){return std::unexpected(parsed.error());}
      result.subjects.push_back(std::move(subject));
    }
    for(auto& [name,source]:origins){
      static_cast<void>(name);
      result.origins.push_back(std::move(source));
    }
    return result;
  }catch(const std::bad_alloc&){
    return std::unexpected(storage::Error{storage::ErrorCode::Io,"Tracker library memory unavailable"});
  }
}

std::expected<AllTrackerMembership,storage::Error> reconcileAllTrackerSelection(
    const AllTrackerLibrary& current,const AllTrackerLibrary* previous,
    const AllTrackerMembership& prior,std::span<const std::string> selectors,bool propertyChanged){
  try{
    if(current.subjects.size()>MaxSubjects||(previous&&previous->subjects.size()>MaxSubjects)||
        selectors.size()>MaxProperties){
      return std::unexpected(incomplete("Tracker selection exceeds its supported inventory"));
    }
    std::map<std::string,const AllTrackerSubject*,std::less<>> known;
    if(previous){for(const auto& subject:previous->subjects){known.emplace(subject.hash.hex(),&subject);}}
    bool reconcile=propertyChanged;
    if(!reconcile&&!selectors.empty()){
      for(const auto& subject:current.subjects){
        const auto old=known.find(subject.hash.hex());
        if(old!=known.end()&&old->second->trackers!=subject.trackers){reconcile=true;break;}
      }
    }

    AllTrackerMembership membership;
    for(const auto& subject:current.subjects){
      const auto hash=subject.hash.hex();
      const bool newDownload=previous&&!known.contains(hash);
      if(reconcile||newDownload){
        auto matched=tracker::matches(subject.trackers,selectors);
        if(!matched){return std::unexpected(incomplete("A tracker selector cannot be compared with the current source"));}
        membership.emplace(hash,*matched);
      }else{
        const auto old=prior.find(hash);
        membership.emplace(hash,old!=prior.end()&&old->second);
      }
    }
    return membership;
  }catch(const std::bad_alloc&){
    return std::unexpected(storage::Error{storage::ErrorCode::Io,"Tracker membership memory unavailable"});
  }
}
}

#include "file_policy_commands.hpp"
#include "torrent_state.hpp"
#include "biglybtc/torrent_layout.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <set>
namespace biglybtc::storage {
namespace {
std::unexpected<Error> bad(ErrorCode code,const char* message) { return std::unexpected(Error{code,message}); }
std::string raw(const core::Document& document,const core::Node& node) {
  return document.originalBytes().substr(node.begin,node.end-node.begin);
}
std::string bytes(std::string_view value) { return std::to_string(value.size())+":"+std::string(value); }
}
std::expected<void,Error> setFilePolicies(const ProfileLease& lease,const core::TorrentHash& hash,
    std::span<const compat::FilePolicy> policies) {
  auto config=ConfigFile::load(lease,"downloads.config"); if(!config) { return std::unexpected(config.error()); }
  auto library=core::readLibrary(config->document());
  if(!library || !library->issues.empty()) { return bad(ErrorCode::InvalidFormat,"Invalid download inventory"); }
  const core::SavedDownload* selected=nullptr;
  for(const auto& saved:library->downloads) {
    if(saved.hash && *saved.hash==hash) {
      if(selected) { return bad(ErrorCode::Conflict,"Duplicate download identity"); } selected=&saved;
    }
  }
  if(!selected || !selected->persistent) { return bad(ErrorCode::Missing,"Persistent download not found"); }
  auto store=TorrentStateStore::open(lease); if(!store) { return std::unexpected(store.error()); }
  auto state=store->load(lease,hash); if(!state) { return std::unexpected(state.error()); }
  auto layout=compat::originalLayout(state->document());
  if(!layout || policies.size()!=layout->files.size()) { return bad(ErrorCode::InvalidFormat,"File selection does not match metadata"); }
  std::set<std::int32_t> levels;
  std::map<std::int64_t,std::string> groups;
  for(std::size_t i=0;i<policies.size();++i) {
    const auto& policy=policies[i];
    if(layout->files[i].padding && !policy.skipped) { return bad(ErrorCode::Unsupported,"Padding files cannot be selected"); }
    if(policy.priority==std::numeric_limits<std::int32_t>::min()) { return bad(ErrorCode::Unsupported,"Priority cannot round trip through original encoding"); }
    if(!policy.skipped && !layout->files[i].padding) { levels.insert(policy.priority); }
    const std::int64_t encoded=policy.skipped?0:policy.priority<=0?static_cast<std::int64_t>(policy.priority)-1:policy.priority;
    auto& ranges=groups[encoded]; if(!ranges.empty()) { ranges+=','; } ranges+=std::to_string(i);
  }
  if(levels.size()>7) { return bad(ErrorCode::Unsupported,"At most seven distinct linear priorities are supported"); }
  const auto* rows=config->document().find("downloads");
  const auto& node=library->legacyMap?config->document().root().children.at(selected->sourceIndex*2+1):rows->children.at(selected->sourceIndex);
  auto entry=core::Document::parse(raw(config->document(),node),compatibleConfigLimits());
  if(!entry) { return bad(ErrorCode::InvalidFormat,"Invalid saved download entry"); }
  auto before=compat::originalFilePolicy(entry->root(),policies.size());
  if(!before) { return bad(ErrorCode::InvalidFormat,"Invalid saved file priorities"); }
  if(std::ranges::equal(*before,policies)) { return {}; }
  std::string encoded="d";
  for(const auto& [priority,ranges]:groups) { encoded+=bytes(std::to_string(priority))+bytes(ranges); } encoded+='e';
  if(!entry->replaceValue("file_priorities_c",encoded)) { return bad(ErrorCode::InvalidFormat,"Cannot encode original file priorities"); }
  const auto checked=compat::originalFilePolicy(entry->root(),policies.size());
  if(!checked || !std::ranges::equal(*checked,policies)) { return bad(ErrorCode::InvalidFormat,"File priorities did not round trip"); }
  if(library->legacyMap) {
    if(!config->document().replaceValue(*selected->legacyKey,entry->serialize())) { return bad(ErrorCode::InvalidFormat,"Cannot edit legacy file priorities"); }
  } else {
    std::string list="l";
    for(std::size_t i=0;i<rows->children.size();++i) { list+=i==selected->sourceIndex?entry->serialize():raw(config->document(),rows->children[i]); }
    if(!config->document().replaceValue("downloads",list+'e')) { return bad(ErrorCode::InvalidFormat,"Cannot edit file priorities"); }
  }
  // Invalidation is safe even if later publication fails; no payload is opened.
  auto prepared=store->beginUpdate(lease,hash); if(!prepared) { return std::unexpected(prepared.error()); }
  return config->save(lease);
}
}

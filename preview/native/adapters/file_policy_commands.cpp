#include "original_filename_policy.hpp"
#include "file_policy_commands.hpp"
#include "linear_to_compact.hpp"
#include "file_operations.hpp"
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
}
std::expected<void,Error> setFilePolicies(const ProfileLease& lease,const core::TorrentHash& hash,
    std::span<const compat::FilePolicy> policies) {
  if(hasPendingOriginalCompaction(lease)){return bad(ErrorCode::Conflict,"Recover pending explicit compaction before editing file preferences");}
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
  auto layout=readOriginalLayout(lease,state->document());
  if(!layout || policies.size()!=layout->files.size()) { return bad(ErrorCode::InvalidFormat,"File selection does not match metadata"); }
  for(std::size_t i=0;i<policies.size();++i) {
    if(layout->files[i].padding && !policies[i].skipped) { return bad(ErrorCode::Unsupported,"Padding files cannot be selected"); }
  }
  auto policyEncoding=compat::encodeOriginalFilePolicy(policies);
  if(!policyEncoding){return std::unexpected(Error{ErrorCode::Unsupported,policyEncoding.error().message});}
  const auto* rows=config->document().find("downloads");
  const auto& node=library->legacyMap?config->document().root().children.at(selected->sourceIndex*2+1):rows->children.at(selected->sourceIndex);
  auto entry=core::Document::parse(raw(config->document(),node),compatibleConfigLimits());
  if(!entry) { return bad(ErrorCode::InvalidFormat,"Invalid saved download entry"); }
  auto before=compat::originalFilePolicy(entry->root(),policies.size());
  if(!before) { return bad(ErrorCode::InvalidFormat,"Invalid saved file priorities"); }
  if(std::ranges::equal(*before,policies)) { return {}; }
  const auto& encoded=*policyEncoding;
  if(!entry->replaceValue("file_priorities_c",encoded)) { return bad(ErrorCode::InvalidFormat,"Cannot encode original file priorities"); }
  const auto checked=compat::originalFilePolicy(entry->root(),policies.size());
  if(!checked || !std::ranges::equal(*checked,policies)) { return bad(ErrorCode::InvalidFormat,"File priorities did not round trip"); }
  bool skipChanged=false;
  for(std::size_t i=0;i<policies.size();++i){skipChanged=skipChanged||(*before)[i].skipped!=policies[i].skipped;}
  if(!skipChanged){
    // Priority-only changes keep the existing bounded metadata transaction;
    // no physical path planning or stricter DND file-count bound is needed.
    if(library->legacyMap){
      if(!config->document().replaceValue(*selected->legacyKey,entry->serialize())){return bad(ErrorCode::InvalidFormat,"Cannot edit legacy file priorities");}
    }else{
      std::string list="l";
      for(std::size_t i=0;i<rows->children.size();++i){list+=i==selected->sourceIndex?entry->serialize():raw(config->document(),rows->children[i]);}
      if(!config->document().replaceValue("downloads",list+'e')){return bad(ErrorCode::InvalidFormat,"Cannot edit file priorities");}
    }
    auto prepared=store->beginUpdate(lease,hash);if(!prepared){return std::unexpected(prepared.error());}
    return config->save(lease);
  }
  // The existing durable file-operation transaction owns cache invalidation,
  // DND moves, original links, state/backup publication and sparse recovery.
  auto plan=prepareFileOperation(lease,{hash,FileOperationKind::SetFilePolicies,{},encoded,raw(config->document(),node)});
  if(!plan){return std::unexpected(plan.error());}
  auto accepted=confirmFileOperation(*plan,plan->preview().approvalScopeDigest,false);
  if(!accepted){return std::unexpected(accepted.error());}
  auto result=applyFileOperation(lease,std::move(*plan),*accepted);
  if(!result){return std::unexpected(result.error());}
  return {};
}
}

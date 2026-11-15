// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_chip_assignment_profile.hpp"
#include "category_store.hpp"
#include "manual_tag_store.hpp"
#include "manual_tag_policy.hpp"
#include "library_view.hpp"
#include <QDateTime>
#include <algorithm>
#include <map>
#include <set>
namespace biglybtc::app {
namespace {
struct Failed{storage::Error error;};
template<class T>T take(std::expected<T,storage::Error> value){if(!value){throw Failed{std::move(value.error())};}return std::move(*value);}
void need(bool value,const char* message){if(!value){throw Failed{{storage::ErrorCode::Conflict,message}};}}
void check(std::stop_token stop){if(stop.stop_requested()){throw Failed{{storage::ErrorCode::Cancelled,"Library assignment cancelled"}};}}
}
ui::TagChipAssignmentResult assignTagChip(const storage::ProfileLease& lease,const ui::TagChipAssignmentCommand& command,std::stop_token stop,CategoryOwnerRebase rebase){
 ui::TagChipAssignmentResult result;result.session=command.session;result.request=command.request;result.total=command.targets.size();bool attempted=false;
 try{
  need(command.session&&command.request&&!command.targets.empty()&&command.targets.size()<=ui::LibraryModel::MaximumRows,"Invalid library assignment request");check(stop);
  std::vector<core::TorrentHash> hashes;hashes.reserve(command.targets.size());std::set<std::string> wanted;
  for(const auto& target:command.targets){const auto encoded=target.hash.toLatin1();need((encoded.size()==40||encoded.size()==64)&&target.hash.size()==encoded.size(),"Invalid assignment identity");const auto bytes=QByteArray::fromHex(encoded);auto hash=core::TorrentHash::parse(std::string_view(bytes.constData(),std::size_t(bytes.size())));need(bool(hash)&&QString::fromStdString(hash->hex()).compare(target.hash,Qt::CaseInsensitive)==0,"Invalid assignment identity");need(wanted.insert(hash->hex()).second,"Duplicate assignment identity");hashes.push_back(*hash);}
  // One actual inventory read for all selections, under the held source gate.
  {auto access=take(lease.sourceAccess());auto library=take(storage::readLibraryView(lease));std::map<std::string,std::size_t> counts;for(const auto& row:library.items){if(row.saved.hash){++counts[row.saved.hash->hex()];}}for(const auto& hash:hashes){need(counts[hash.hex()]==1,"Saved download selection changed; refresh the library");}}
  if(command.kind==ui::TagChipAssignmentKind::Manual){
   need(command.tagId&&*command.tagId>=0&&!command.category,"Invalid manual tag selection");auto store=take(storage::ManualTagStore::open(lease,stop));auto snapshot=take(store->snapshot());const auto* tags=snapshot.tags();need(tags&&tags->source,"Saved tag source is unavailable");const auto row=std::ranges::find_if(tags->tags,[&](const auto& tag){return tag.id==command.tagId&&!tag.duplicateId;});need(row!=tags->tags.end(),"Selected tag no longer exists");bool add=false;std::size_t matching=0;std::set<std::string> existing;for(const auto& hash:row->uniqueHashes){existing.insert(hash.hex());}for(const auto& hash:hashes){const bool present=existing.contains(hash.hex());matching+=present;add|=!present;}
   const auto* type=tags->source->find("3");const auto* record=type?type->find(std::to_string(*command.tagId)):nullptr;need(record,"Selected tag source is unavailable");const auto policy=classifyManualTagMembership(*record);need(!policy.malformed&&!(command.longPress?policy.autoAdd:policy.autoAdd&&(add||policy.autoRemove)),"Automatic or ambiguous tag membership cannot be changed in this direction");
   auto access=take(lease.sourceAccess());check(stop);attempted=true;auto committed=store->apply(snapshot,core::SavedTagMemberBatch{*command.tagId,hashes,add,QDateTime::currentSecsSinceEpoch()},stop);if(!committed){throw Failed{committed.error()};}result.applied=hashes.size();result.changed=committed->changed?(add?hashes.size()-matching:matching):0;
  }else if(command.kind==ui::TagChipAssignmentKind::Category){
   need(!command.tagId,"Invalid category selection");std::optional<std::string> name;if(command.category){const auto bytes=command.category->toUtf8();need(bytes.size()<=4096&&QString::fromUtf8(bytes)==*command.category,"Invalid category name");name=bytes.toStdString();}auto store=take(storage::CategoryStore::open(lease,stop));auto snapshot=take(store->snapshot());
   for(const auto& hash:hashes){check(stop);auto target=take(store->readTarget(snapshot,hash,stop));attempted=true;auto saved=store->assign(snapshot,target,name,stop);if(!saved){if(rebase){static_cast<void>(rebase(hash));}throw Failed{saved.error()};}++result.applied;if(saved->changed){++result.changed;if(rebase){auto refreshed=rebase(hash);if(!refreshed){throw Failed{refreshed.error()};}}}}
  }else{need(false,"Unknown library assignment kind");}
 }catch(const Failed& failure){result.failure=QString::fromUtf8(failure.error.message);result.uncertain=attempted&&(failure.error.code==storage::ErrorCode::Io||failure.error.code==storage::ErrorCode::Conflict);}
 catch(const std::bad_alloc&){result.failure=QStringLiteral("Insufficient memory during library assignment");result.uncertain=attempted;}
 return result;
}
}

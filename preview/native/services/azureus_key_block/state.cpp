// SPDX-License-Identifier: GPL-2.0-or-later
// Original storage policy: Copyright (C) Azureus Software, Inc.
#include "state.hpp"
#include <bit>
#include <algorithm>
#include <set>
namespace biglybtc::services::azureus_key_block {
namespace {
std::int32_t intValue(std::int64_t value){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));}
std::int64_t subtract(std::int64_t a,std::int64_t b){return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)-std::bit_cast<std::uint64_t>(b));}
std::size_t size(const Record& record){return record.request.size()+record.signature.size();}
bool valid(const Record& record){return record.request.size()>8&&record.request.size()<=255&&record.signature.size()<=8192;}
core::Node bytes(std::string_view value){core::Node n(core::Node::Type::Bytes);n.bytes=value;return n;}
core::Node integer(std::int64_t value){core::Node n(core::Node::Type::Integer);n.integer=value;return n;}
void field(core::Node& node,std::string_view name,core::Node value){for(std::size_t i=0;i+1<node.children.size();i+=2){if(node.children[i].bytes==name){node.children[i+1]=std::move(value);return;}}node.children.push_back(bytes(name));node.children.push_back(std::move(value));}
}
std::expected<Admission,StateError> State::admit(std::string request,std::string signature,std::optional<std::string_view> origin,std::int64_t wall){
 try{
  Record candidate{std::move(request),std::move(signature),intValue(wall/1000),origin.has_value()};if(!valid(candidate)||(origin&&origin->size()!=20)){return std::unexpected(StateError::Input);}
  const auto found=records_.find(candidate.key());const auto* previous=found==records_.end()?nullptr:&found->second.record;const auto plan=decide(previous,candidate);
  if(plan.verify){
   if(!failed_||failedAt_>wall||subtract(wall,failedAt_)>1800000){auto bloom=msgsync::AddOnlyBloom::create(4000);if(!bloom){return std::unexpected(StateError::Resources);}failed_=std::move(*bloom);failedAt_=wall;}
   const std::string zeros(20,'\0');const auto sender=origin.value_or(zeros);if(failed_->contains(sender)){return Admission{};}
   const auto result=verify_(candidate.request,candidate.signature);if(result==Verification::Unavailable){return std::unexpected(StateError::Crypto);}if(result==Verification::Malformed){return Admission{};}if(result==Verification::Invalid){failed_->add(sender);return Admission{};}
  }
  if(plan.replace){
   const auto oldBytes=previous?size(*previous):0;if((!previous&&records_.size()>=maximumRecords)||size(candidate)>maximumRecordBytes-(bytes_-oldBytes)||revision_==UINT64_MAX){return std::unexpected(StateError::Limit);}
   Entry entry{std::move(candidate),{}};if(origin){auto bloom=msgsync::AddOnlyBloom::create(500);if(!bloom){return std::unexpected(StateError::Resources);}bloom->add(*origin);entry.sent=std::move(*bloom);}
   const std::string key(entry.record.key());const auto newBytes=size(entry.record);records_.insert_or_assign(key,std::move(entry));bytes_=bytes_-oldBytes+newBytes;++revision_;
  }
  return Admission{plan.returnBlocked,plan.replace};
 }catch(...){return std::unexpected(StateError::Resources);}
}
std::expected<void,StateError> State::restore(std::span<const Record> records){
 try{
  if(records.size()>maximumRecords||revision_==UINT64_MAX){return std::unexpected(StateError::Limit);}std::map<std::string,Entry,std::less<>> next;std::size_t total=0;
  for(const auto& record:records){if(!valid(record)){return std::unexpected(StateError::Input);}const auto result=verify_(record.request,record.signature);if(result==Verification::Unavailable){return std::unexpected(StateError::Crypto);}if(result!=Verification::Valid){return std::unexpected(StateError::Input);}auto old=next.find(record.key());const auto oldBytes=old==next.end()?0:size(old->second.record);if(size(record)>maximumRecordBytes-(total-oldBytes)){return std::unexpected(StateError::Limit);}total=total-oldBytes+size(record);next.insert_or_assign(std::string(record.key()),Entry{record,{}});}
  records_=std::move(next);bytes_=total;failed_.reset();++revision_;return {};
 }catch(...){return std::unexpected(StateError::Resources);}
}
const Record* State::find(std::string_view key)const{const auto it=records_.find(key);return it==records_.end()?nullptr:&it->second.record;}
const Record* State::blocked(std::string_view key)const{const auto it=records_.find(key);return it!=records_.end()&&it->second.record.add()?&it->second.record:nullptr;}
std::vector<Record> State::records(bool directOnly)const{std::vector<Record> out;out.reserve(records_.size());for(const auto& [key,value]:records_){(void)key;if(!directOnly||value.record.direct){out.push_back(value.record);}}return out;}
bool State::hasSent(std::string_view key,std::string_view id)const{if(id.size()!=20){return false;}const auto it=records_.find(key);return it!=records_.end()&&it->second.sent&&it->second.sent->contains(id);}
bool State::markSent(std::string_view key,std::string_view id){if(id.size()!=20){return false;}auto it=records_.find(key);if(it==records_.end()){return false;}auto& sent=it->second.sent;if(!sent||sent->count()>100){auto bloom=msgsync::AddOnlyBloom::create(500);if(!bloom){return false;}sent=std::move(*bloom);}sent->add(id);return true;}
bool State::purgeDirect(){if(revision_==UINT64_MAX){return false;}bool changed=false;for(auto it=records_.begin();it!=records_.end();){if(it->second.record.direct){bytes_-=size(it->second.record);it=records_.erase(it);changed=true;}else{++it;}}if(changed){++revision_;}return changed;}
std::expected<Saved,StateError> decodeSaved(const core::Node& root,std::int64_t wall,Verifier verify){
 try{
  if(root.type!=core::Node::Type::Dictionary){return std::unexpected(StateError::Input);}const auto* entries=root.find("entries");if(!entries){return Saved{};}if(entries->type!=core::Node::Type::List){return std::unexpected(StateError::Input);}if(entries->children.size()>maximumRecords){return std::unexpected(StateError::Limit);}Saved out;std::map<std::string,Record,std::less<>> records;const auto now=intValue(wall/1000);std::size_t total=0;
  for(const auto& entry:entries->children){const auto* req=entry.find("req");const auto* cert=entry.find("cert");const auto* received=entry.find("received");const auto* direct=entry.find("direct");if(entry.type!=core::Node::Type::Dictionary||!req||req->type!=core::Node::Type::Bytes||!cert||cert->type!=core::Node::Type::Bytes||!received||received->type!=core::Node::Type::Integer||!direct||direct->type!=core::Node::Type::Integer){return std::unexpected(StateError::Input);}
   Record record{std::string(req->bytes),std::string(cert->bytes),intValue(received->integer),direct->integer==1};if(!valid(record)){return std::unexpected(StateError::Input);}if(record.received>now){record.received=now;}const auto age=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(now)-std::bit_cast<std::uint32_t>(record.received));if(!(record.direct&&record.add())&&age>=604800){++out.rejected;continue;}const auto verified=verify(record.request,record.signature);if(verified==Verification::Unavailable){return std::unexpected(StateError::Crypto);}if(verified!=Verification::Valid){++out.rejected;continue;}const auto old=records.find(record.key());const auto oldBytes=old==records.end()?0:size(old->second);if(size(record)>maximumRecordBytes-(total-oldBytes)){return std::unexpected(StateError::Limit);}total=total-oldBytes+size(record);const auto key=std::string(record.key());records.insert_or_assign(key,std::move(record));
  }
  out.records.reserve(records.size());for(auto& [key,record]:records){(void)key;out.records.push_back(std::move(record));}return out;
 }catch(...){return std::unexpected(StateError::Resources);}
}
std::expected<std::string,StateError> encodeSaved(const core::Node& original,std::span<const Record> records){
 try{
  if(original.type!=core::Node::Type::Dictionary){return std::unexpected(StateError::Input);}if(records.size()>maximumRecords){return std::unexpected(StateError::Limit);}const auto* old=original.find("entries");if(old&&old->type!=core::Node::Type::List){return std::unexpected(StateError::Input);}core::Node root=original,list(core::Node::Type::List);std::set<std::string_view> keys;std::size_t total=0;
  for(const auto& record:records){if(!valid(record)||!keys.insert(record.key()).second){return std::unexpected(StateError::Input);}if(size(record)>maximumRecordBytes-total){return std::unexpected(StateError::Limit);}total+=size(record);core::Node entry(core::Node::Type::Dictionary);if(old){for(const auto& previous:old->children){const auto* req=previous.find("req");if(previous.type==core::Node::Type::Dictionary&&req&&req->type==core::Node::Type::Bytes&&req->bytes.size()>8&&req->bytes.substr(8)==record.key()){entry=previous;break;}}}field(entry,"req",bytes(record.request));field(entry,"cert",bytes(record.signature));field(entry,"received",integer(record.received));field(entry,"direct",integer(record.direct?1:0));list.children.push_back(std::move(entry));}
  field(root,"entries",std::move(list));auto result=core::encodeValue(root);if(!result){return std::unexpected(StateError::Input);}return std::move(*result);
 }catch(...){return std::unexpected(StateError::Resources);}
}
}

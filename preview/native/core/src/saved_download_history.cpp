// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/saved_download_history.hpp"
#include <algorithm>
#include <bit>
#include <new>
#include <set>
#include <type_traits>

namespace biglybtc::core {
namespace {
using Error=SavedDownloadHistoryError;
struct Failure {Error code;};
void require(bool ok,Error code){if(!ok){throw Failure{code};}}
SavedDownloadHistoryLimits bounded(SavedDownloadHistoryLimits value){
  const SavedDownloadHistoryLimits cap;
  value.document.inputBytes=std::min(value.document.inputBytes,cap.document.inputBytes);
  value.document.stringBytes=std::min(value.document.stringBytes,cap.document.stringBytes);
  value.document.nodes=std::min(value.document.nodes,cap.document.nodes);
  value.document.depth=std::min(value.document.depth,cap.document.depth);
  value.records=std::min(value.records,cap.records);value.textBytes=std::min(value.textBytes,cap.textBytes);
  value.fieldBytes=std::min(value.fieldBytes,cap.fieldBytes);value.tagsPerRecord=std::min(value.tagsPerRecord,cap.tagsPerRecord);
  value.hashBytes=std::min(value.hashBytes,cap.hashBytes);return value;
}
bool utf8(std::string_view value){
  for(std::size_t i=0;i<value.size();){
    const auto first=static_cast<unsigned char>(value[i++]);if(first<0x80){continue;}
    const unsigned count=first>=0xc2&&first<=0xdf?1:first>=0xe0&&first<=0xef?2:first>=0xf0&&first<=0xf4?3:0;
    if(!count||count>value.size()-i){return false;}std::uint32_t code=first&((1U<<(6-count))-1);
    for(unsigned j=0;j<count;++j){const auto next=static_cast<unsigned char>(value[i++]);if((next&0xc0)!=0x80){return false;}code=(code<<6)|(next&0x3f);}
    if(code<(count==1?0x80U:count==2?0x800U:0x10000U)||code>0x10ffff||(code>=0xd800&&code<=0xdfff)){return false;}
  }return true;
}
Document parse(std::string_view input,const SavedDownloadHistoryLimits& limits){
  require(input.size()<=limits.document.inputBytes,Error::Limit);auto value=Document::parse(std::string(input),limits.document);
  if(!value){const auto& why=value.error().code;throw Failure{why=="input_limit"||why=="string_limit"||why=="node_limit"||why=="depth_limit"?Error::Limit:Error::Format};}
  return std::move(*value);
}
const Node& field(const Node& row,std::string_view key,Node::Type type){const auto* node=row.find(key);require(node&&node->type==type,Error::Field);return *node;}
std::int64_t integer(const Node& row,std::string_view key){return field(row,key,Node::Type::Integer).integer;}
void count(std::size_t bytes,std::size_t& total,const SavedDownloadHistoryLimits& limits){require(bytes<=limits.textBytes-total,Error::Limit);total+=bytes;}
std::string text(const Node& value,std::size_t& total,const SavedDownloadHistoryLimits& limits){require(value.type==Node::Type::Bytes,Error::Field);require(value.bytes.size()<=limits.fieldBytes,Error::Limit);require(utf8(value.bytes),Error::Text);count(value.bytes.size(),total,limits);return std::string(value.bytes);}
SavedDownloadHistory read(std::string_view input,const SavedDownloadHistoryLimits& limits){
  SavedDownloadHistory out;out.source=std::make_shared<const Document>(parse(input,limits));require(out.source->root().type==Node::Type::Dictionary,Error::Root);
  const auto* list=out.source->find("records");if(!list){return out;}require(list->type==Node::Type::List,Error::Records);require(list->children.size()<=limits.records,Error::Limit);out.recordsPresent=true;
  std::set<std::int64_t> ids;std::size_t total=0;
  for(std::size_t n=0;n<list->children.size();++n){
    const auto& row=list->children[n];require(row.type==Node::Type::Dictionary,Error::Record);
    SavedDownloadHistoryRecord value;value.sourceIndex=n;value.uid=integer(row,"u");require(ids.insert(value.uid).second,Error::DuplicateUid);
    for(const auto key:{"h","v"}){if(const auto* hash=row.find(key)){require(hash->type==Node::Type::Bytes,Error::Field);require(hash->bytes.size()<=limits.hashBytes,Error::Limit);count(hash->bytes.size(),total,limits);(std::string_view(key)=="h"?value.hash:value.hashV2)=std::string(hash->bytes);}}
    value.name=text(field(row,"n",Node::Type::Bytes),total,limits);value.saveLocation=text(field(row,"s",Node::Type::Bytes),total,limits);
    if(const auto* size=row.find("z")){require(size->type==Node::Type::Integer,Error::Field);value.rawSize=size->integer;value.size=size->integer;}
    value.addTime=integer(row,"a");value.rawCompleteTime=integer(row,"c");value.rawRemoveTime=integer(row,"r");
    value.completeTime=std::max<std::int64_t>(0,value.rawCompleteTime);value.removeTime=std::max<std::int64_t>(0,value.rawRemoveTime);
    if(const auto* tags=row.find("t")){require(tags->type==Node::Type::List,Error::Field);require(tags->children.size()<=limits.tagsPerRecord,Error::Limit);value.tagsPresent=true;for(const auto& tag:tags->children){value.tags.push_back(text(tag,total,limits));}}
    out.records.push_back(std::move(value));
  }return out;
}
void append(std::string& out,std::string_view value,const SavedDownloadHistoryLimits& limits){require(out.size()<=limits.document.inputBytes&&value.size()<=limits.document.inputBytes-out.size(),Error::Limit);out.append(value);}
std::string bytes(std::string_view value,const SavedDownloadHistoryLimits& limits){require(value.size()<=limits.document.stringBytes,Error::Limit);std::string out;append(out,std::to_string(value.size()),limits);append(out,":",limits);append(out,value,limits);return out;}
std::string number(std::int64_t value){return 'i'+std::to_string(value)+'e';}
std::string tags(const std::vector<std::string>& values,const SavedDownloadHistoryLimits& limits){require(values.size()<=limits.tagsPerRecord,Error::Limit);std::string out="l";for(const auto& value:values){require(value.size()<=limits.fieldBytes,Error::Limit);require(utf8(value),Error::Text);append(out,bytes(value,limits),limits);}append(out,"e",limits);return out;}
void replace(Document& document,std::string_view key,std::string value){const auto changed=document.replaceValue(key,std::move(value));if(!changed){throw Failure{changed.error()==EditError::Limit?Error::Limit:Error::Field};}}
std::string raw(const Document& document,const Node& node){return document.originalBytes().substr(node.begin,node.end-node.begin);}
std::string newRecord(const DownloadHistoryNewRecord& value,const SavedDownloadHistoryLimits& limits){
  require(value.name.size()<=limits.fieldBytes&&value.saveLocation.size()<=limits.fieldBytes,Error::Limit);require(utf8(value.name)&&utf8(value.saveLocation),Error::Text);
  std::string out="d";auto put=[&](std::string_view key,std::string encoded){append(out,bytes(key,limits),limits);append(out,encoded,limits);};
  put("u",number(value.uid));if(value.hash){require(value.hash->size()<=limits.hashBytes,Error::Limit);put("h",bytes(*value.hash,limits));}if(value.hashV2){require(value.hashV2->size()<=limits.hashBytes,Error::Limit);put("v",bytes(*value.hashV2,limits));}
  put("n",bytes(value.name,limits));put("z",number(value.size));put("s",bytes(value.saveLocation,limits));put("a",number(value.addTime));put("c",number(std::max<std::int64_t>(0,value.completeTime)));put("r",number(std::max<std::int64_t>(0,value.removeTime)));
  if(!value.tags.empty()){put("t",tags(value.tags,limits));}append(out,"e",limits);return out;
}
SavedDownloadHistoryEditResult edit(std::string_view input,const SavedDownloadHistoryEdit& request,const SavedDownloadHistoryLimits& limits){
  auto current=read(input,limits);const auto* list=current.source->find("records");
  std::optional<std::size_t> erase,change;std::string replacement;bool clear=false,addition=false;
  std::visit([&](const auto& command){using T=std::decay_t<decltype(command)>;
    if constexpr(std::is_same_v<T,DownloadHistoryClear>){clear=!current.records.empty();}
    else{
      const auto uid=[&]{if constexpr(std::is_same_v<T,DownloadHistoryAppend>){return command.record.uid;}else{return command.uid;}}();
      const auto found=std::find_if(current.records.begin(),current.records.end(),[&](const auto& value){return value.uid==uid;});
      if constexpr(std::is_same_v<T,DownloadHistoryAppend>){require(found==current.records.end(),Error::ExistingUid);require(current.records.size()<limits.records,Error::Limit);replacement=newRecord(command.record,limits);addition=true;}
      else if constexpr(std::is_same_v<T,DownloadHistoryErase>){if(found!=current.records.end()){erase=found->sourceIndex;}}
      else{
        require(found!=current.records.end()&&list,Error::MissingUid);auto row=parse(raw(*current.source,list->children[found->sourceIndex]),limits);bool changed=false;
        if constexpr(std::is_same_v<T,DownloadHistorySetName>||std::is_same_v<T,DownloadHistorySetSaveLocation>){require(command.value.size()<=limits.fieldBytes,Error::Limit);require(utf8(command.value),Error::Text);const bool name=std::is_same_v<T,DownloadHistorySetName>;if((name?found->name:found->saveLocation)!=command.value){replace(row,name?"n":"s",bytes(command.value,limits));changed=true;}}
        else if constexpr(std::is_same_v<T,DownloadHistorySetCompleteTime>){const auto value=std::max<std::int64_t>(0,command.value);if(found->completeTime!=value){replace(row,"c",number(value));changed=true;}}
        else if constexpr(std::is_same_v<T,DownloadHistorySetRemoved>){const auto value=std::max<std::int64_t>(0,command.time);if(found->removeTime!=value){replace(row,"r",number(value));changed=true;}if(!command.tags.empty()&&found->tags!=command.tags){replace(row,"t",tags(command.tags,limits));changed=true;}}
        if(changed){change=found->sourceIndex;replacement=row.serialize();}
      }
    }
  },request);
  if(!clear&&!addition&&!erase&&!change){return {std::move(current),false};}
  std::string encoded="l";
  if(!clear&&list){for(std::size_t n=0;n<list->children.size();++n){if(erase&&n==*erase){continue;}append(encoded,change&&n==*change?replacement:raw(*current.source,list->children[n]),limits);}}
  if(addition){append(encoded,replacement,limits);}append(encoded,"e",limits);
  auto document=parse(input,limits);replace(document,"records",std::move(encoded));const auto output=document.serialize();
  return {read(output,limits),true};
}
}
std::expected<SavedDownloadHistory,SavedDownloadHistoryError> readSavedDownloadHistory(std::string_view input,SavedDownloadHistoryLimits limits){try{return read(input,bounded(limits));}catch(const Failure& failure){return std::unexpected(failure.code);}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}}
std::expected<SavedDownloadHistoryEditResult,SavedDownloadHistoryError> editSavedDownloadHistory(std::string_view input,const SavedDownloadHistoryEdit& request,SavedDownloadHistoryLimits limits){try{return edit(input,request,bounded(limits));}catch(const Failure& failure){return std::unexpected(failure.code);}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}}
std::int64_t originalDownloadHistoryUid(std::optional<std::string_view> hash,std::int64_t added)noexcept{
  std::uint32_t prefix=0;if(hash&&hash->size()>=4){for(unsigned n=0;n<4;++n){prefix=(prefix<<8)|static_cast<unsigned char>((*hash)[n]);}}
  const auto bits=(static_cast<std::uint64_t>(prefix)<<32)|std::bit_cast<std::uint64_t>(added/1000);return std::bit_cast<std::int64_t>(bits);
}
std::int64_t originalDownloadHistoryCompleteTime(std::int64_t last,std::int64_t completed)noexcept{return std::max<std::int64_t>(0,last==0?completed:last);}
bool originalDownloadHistoryMonitored(bool persistent,std::uint64_t flags)noexcept{return persistent&&(flags&(0x10U|0x200U))==0;}
} // namespace biglybtc::core

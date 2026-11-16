// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/tag_runtime_registry.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <utility>
namespace biglybtc::core::tags::runtime {
std::int64_t Reference::uid()const noexcept{return static_cast<std::int64_t>((std::uint64_t(std::uint32_t(type))<<32)|std::uint32_t(id));}
struct Registry::Data {
 std::vector<Type> types;Limits limits;bool complete=false;std::size_t tags=0,units=0;
 std::map<Text,std::vector<Reference>,std::less<>> names,groups;
 std::vector<Reference> unknownNames,unknownGroups;
 const Tag& entry(const Reference& ref)const noexcept{return types[ref.typeIndex].tags[ref.tagIndex];}
 Truth witness(std::u16string_view name,std::int32_t target,bool group)const noexcept {
  if(target<0||name.size()>limits.nameUnits){return Truth::Unknown;}bool uncertain=!complete;
  const auto& index=group?groups:names;const auto found=index.find(name);
  if(found!=index.end()){for(const auto& ref:found->second){const auto& tag=entry(ref);
   if(tag.membership==Membership::Absent||(tag.taggableTypes&&*tag.taggableTypes!=target)){continue;}
   if(tag.taggableTypes&&tag.membership==Membership::Present){return Truth::Yes;}
   uncertain=true;
  }}
  for(const auto& ref:group?unknownGroups:unknownNames){const auto& tag=entry(ref);if(tag.membership!=Membership::Absent&&(!tag.taggableTypes||*tag.taggableTypes==target)){uncertain=true;}}
  return uncertain?Truth::Unknown:Truth::No;
 }
};
Registry::Registry(std::shared_ptr<const Data> data):data_(std::move(data)){}
std::expected<Registry,Error> Registry::create(std::vector<Type> types,bool completeTypes,Limits limits){
 try{
  if(!limits.types||!limits.tags||!limits.nameUnits||!limits.totalUnits||types.size()>limits.types){return std::unexpected(Error::Bounds);}
  auto data=std::make_shared<Data>();data->limits=limits;data->complete=completeTypes;data->types=std::move(types);std::set<std::int32_t> ids;
  for(std::size_t i=0;i<data->types.size();++i){const auto& type=data->types[i];
   if(type.id<=0||!ids.insert(type.id).second){return std::unexpected(Error::Invalid);}if(type.tags.size()>limits.tags-data->tags){return std::unexpected(Error::Bounds);}data->tags+=type.tags.size();data->complete=data->complete&&type.complete;std::set<std::int32_t> tagIds;
   for(std::size_t j=0;j<type.tags.size();++j){const auto& tag=type.tags[j];
    if(tag.id<0||!tagIds.insert(tag.id).second||(tag.taggableTypes&&*tag.taggableTypes<0)||(!tag.groupKnown&&tag.group)
        ||(tag.membership!=Membership::Unknown&&tag.membership!=Membership::Absent&&tag.membership!=Membership::Present)){return std::unexpected(Error::Invalid);}
    for(const auto* text:{&tag.name,&tag.group}){if(*text){if((*text)->size()>limits.nameUnits||(*text)->size()>limits.totalUnits-data->units){return std::unexpected(Error::Bounds);}data->units+=(*text)->size();}}
    const Reference ref{i,j,type.id,tag.id};if(tag.name){data->names[*tag.name].push_back(ref);}else{data->unknownNames.push_back(ref);}
    if(tag.groupKnown){if(tag.group){data->groups[*tag.group].push_back(ref);}}else{data->unknownGroups.push_back(ref);}
   }
  }return Registry(std::move(data));
 }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<Selection,Error> Registry::lookupName(std::u16string_view name)const{
 try{
  Selection result;if(!data_){return result;}if(name.size()>data_->limits.nameUnits){return std::unexpected(Error::Bounds);}result.complete=data_->complete;
  const auto found=data_->names.find(name);if(found!=data_->names.end()){
   for(const auto& ref:found->second){if(result.tags.empty()||result.tags.back().typeIndex!=ref.typeIndex){result.tags.push_back(ref);}}
  }
  // An unknown earlier name could be the source's actual first match. Unknown
  // later names cannot supersede an already selected first tag in that type.
  for(const auto& unknown:data_->unknownNames){const auto chosen=std::ranges::find(result.tags,unknown.typeIndex,&Reference::typeIndex);if(chosen==result.tags.end()||unknown.tagIndex<chosen->tagIndex){result.complete=false;}}
  return result;
 }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<Selection,Error> Registry::lookupConstraintName(std::u16string_view name)const{
 try{
  Selection result;if(!data_){return result;}
  if(name.size()>data_->limits.nameUnits){return std::unexpected(Error::Bounds);}
  result.complete=true;unsigned observed=0;
  for(const auto& type:data_->types){
   if(type.id>=1&&type.id<=5){observed|=1U<<unsigned(type.id-1);if(!type.complete){result.complete=false;}}
   else if(type.id!=6&&type.id!=7){result.complete=false;}
  }
  result.complete=result.complete&&observed==31;
  const auto found=data_->names.find(name);
  if(found!=data_->names.end()){for(const auto& ref:found->second){
   if(ref.type<1||ref.type>5){continue;}
   if(!result.tags.empty()&&result.tags.back().typeIndex==ref.typeIndex){result.complete=false;continue;}
   result.tags.push_back(ref);
  }}
  // Even a later unknown name could create an ambiguous native alias. The
  // unique supported result deliberately does not guess source registration
  // order from a saved byte order or original HashSet iteration.
  for(const auto& ref:data_->unknownNames){if(ref.type>=1&&ref.type<=5){result.complete=false;}}
  return result;
 }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<Selection,Error> Registry::tagged(std::int32_t target)const{
 try{
  if(target<0){return std::unexpected(Error::Invalid);}Selection result;if(!data_){return result;}result.complete=data_->complete;
  for(std::size_t i=0;i<data_->types.size();++i){const auto& type=data_->types[i];for(std::size_t j=0;j<type.tags.size();++j){const auto& tag=type.tags[j];
   if(tag.membership==Membership::Absent||(tag.taggableTypes&&*tag.taggableTypes!=target)){continue;}
   if(tag.taggableTypes&&tag.membership==Membership::Present){result.tags.push_back({i,j,type.id,tag.id});}else{result.complete=false;}
  }}return result;
 }catch(...){return std::unexpected(Error::Memory);}
}
Truth Registry::hasName(std::u16string_view name,std::int32_t target)const noexcept{return data_?data_->witness(name,target,false):Truth::Unknown;}
Truth Registry::hasGroup(std::u16string_view name,std::int32_t target)const noexcept{return data_?data_->witness(name,target,true):Truth::Unknown;}
const Tag* Registry::at(const Reference& ref)const noexcept{
 if(!data_||ref.typeIndex>=data_->types.size()){return nullptr;}const auto& type=data_->types[ref.typeIndex];if(ref.type!=type.id||ref.tagIndex>=type.tags.size()||ref.id!=type.tags[ref.tagIndex].id){return nullptr;}return &type.tags[ref.tagIndex];
}
std::size_t Registry::tagCount()const noexcept{return data_?data_->tags:0;}
std::size_t Registry::textUnits()const noexcept{return data_?data_->units:0;}
}

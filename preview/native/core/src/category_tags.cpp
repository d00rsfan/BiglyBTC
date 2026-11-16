// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/category_tags.hpp>
#include <limits>
#include <map>
#include <set>
namespace biglybtc::core::tags::category {
bool member(Kind kind,bool download,bool userMember,std::optional<Kind> assigned)noexcept{
 if(!download){return false;}
 if(kind==Kind::User){return userMember;}
 if(kind==Kind::All){return true;}
 return !assigned||*assigned==Kind::Uncategorized;
}
Roster::Roster():entries_{{1,u"Categories.all",Kind::All},{2,u"Categories.uncategorized",Kind::Uncategorized}}{}
std::expected<void,runtime::Error> Roster::observe(std::span<const runtime::Text> names){
 try{
  if(names.size()>4096){return std::unexpected(runtime::Error::Bounds);}
  std::map<runtime::Text,std::int32_t,std::less<>> old;
  for(const auto& entry:entries_){if(entry.kind==Kind::User){old.emplace(entry.rawName,entry.id);}}
  std::vector<Entry> next{entries_[0],entries_[1]};std::set<runtime::Text,std::less<>> unique;
  std::size_t units=0;auto allocator=next_;
  for(const auto& name:names){
   if(name.empty()||name==u"Categories.all"||name==u"Categories.uncategorized"||!unique.insert(name).second){return std::unexpected(runtime::Error::Invalid);}
   if(name.size()>4096||name.size()>262144-units){return std::unexpected(runtime::Error::Bounds);}units+=name.size();
   std::int32_t id;const auto found=old.find(name);
   if(found!=old.end()){id=found->second;}
   else{if(allocator>std::numeric_limits<std::int32_t>::max()){return std::unexpected(runtime::Error::Bounds);}id=static_cast<std::int32_t>(allocator++);}
   next.push_back({id,name,Kind::User});
  }
  entries_=std::move(next);next_=allocator;return {};
 }catch(...){return std::unexpected(runtime::Error::Memory);}
}
}

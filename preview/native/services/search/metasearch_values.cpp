// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_values.hpp"
#include "biglybtc/saved_search_engines.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <set>
namespace biglybtc::search {
namespace {
using E=MetasearchValueError;using N=core::Node;
struct Failure {E error;std::string_view reason;};
void require(bool ok,E error,std::string_view reason="Malformed saved search definition"){if(!ok){throw Failure{error,reason};}}
bool utf8(std::string_view s){
  for(std::size_t i=0;i<s.size();){const auto lead=static_cast<unsigned char>(s[i++]);if(lead<128){continue;}unsigned count=0;std::uint32_t value=0,min=0;
    if(lead>=0xc2&&lead<=0xdf){count=1;value=lead&31;min=0x80;}else if(lead>=0xe0&&lead<=0xef){count=2;value=lead&15;min=0x800;}else if(lead>=0xf0&&lead<=0xf4){count=3;value=lead&7;min=0x10000;}else{return false;}
    if(count>s.size()-i){return false;}while(count--){const auto c=static_cast<unsigned char>(s[i++]);if((c&0xc0)!=0x80){return false;}value=(value<<6)|(c&63);}if(value<min||value>0x10ffff||(value>=0xd800&&value<=0xdfff)){return false;}
  }return true;
}
bool unique(const N& n){if(n.type==N::Type::Dictionary){std::set<std::string_view> keys;for(std::size_t i=0;i<n.children.size();i+=2){if(i+1>=n.children.size()||n.children[i].type!=N::Type::Bytes||!keys.insert(n.children[i].bytes).second){return false;}}}return std::ranges::all_of(n.children,[](const auto& c){return unique(c);});}
std::optional<std::string> text(const N& row,std::string_view key,std::size_t limit=4096){const auto* n=row.find(key);if(!n){return {};}require(n->type==N::Type::Bytes,E::Malformed);require(n->bytes.size()<=limit,E::Limit);require(utf8(n->bytes)&&n->bytes.find('\0')==n->bytes.npos,E::Malformed);return std::string(n->bytes);}
std::int32_t integer(const N& row,std::string_view key,std::int32_t fallback){const auto* n=row.find(key);if(!n){return fallback;}require(n->type==N::Type::Integer,E::Malformed);return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(n->integer));}
bool emptyText(const N& n,std::string_view key){auto value=text(n,key);return !value||value->empty();}
bool emptyList(const N& n,std::string_view key){const auto* p=n.find(key);return !p||(p->type==N::Type::List&&p->children.empty());}
MetasearchRecipe recipe(const N& n){
  require(n.type==N::Type::Dictionary&&unique(n),E::Malformed);const auto type=integer(n,"type",0);require(type==2||type==4,E::Unsupported,"This engine type is not supported");
  require(integer(n,"az_version",5)>=1&&integer(n,"az_version",5)<=5&&integer(n,"version",1)>=1,E::Unsupported,"This engine version is not supported");
  const auto selected=integer(n,"selected",0);require(selected==1||selected==2,E::Inactive,"This saved engine is not enabled");require(integer(n,"source",0)>=0&&integer(n,"source",0)<=3,E::Unsupported);
  require(!integer(n,"web.needs_auth",0)&&emptyText(n,"web.login_page")&&emptyText(n,"web.full_cookies")&&emptyList(n,"web.required_cookies"),E::Unsupported,"Login or cookies are required");
  require(emptyText(n,"web.dl_link_css")&&emptyList(n,"l1_map")&&emptyList(n,"l2_map"),E::Unsupported,"This engine requires additional result transformations");
  auto auth=text(n,"web.auth_method");require(!auth||*auth=="transparent",E::Unsupported);
  constexpr std::string_view allowed[]{"web.search_url_format","web.maps","web.needs_auth","web.auth_method","web.login_page","web.required_cookies","web.full_cookies","web.auto_date","web.time_zone","web.date_format","web.dl_link_css","web.icon_url"};
  for(std::size_t i=0;i<n.children.size();i+=2){const auto key=n.children[i].bytes;require(!(key.starts_with("web.")&&std::ranges::find(allowed,key)==std::end(allowed))&&key!="headers"&&key!="plugin_id"&&key!="plugin"&&key!="provider",E::Unsupported);}
  auto url=text(n,"web.search_url_format",2048);require(url&&!url->empty(),E::Malformed);auto expanded=expandMetasearchQuery(*url,"value");require(bool(expanded),expanded?E::Malformed:expanded.error(),"This query URL or method is not supported");
  MetasearchRecipe out;out.configuredUrl=std::move(*url);out.kind=type==2?MetasearchRecipeKind::Json:MetasearchRecipeKind::Rss;
  const auto* maps=n.find("web.maps");require(maps&&maps->type==N::Type::List,E::Malformed);require(maps->children.size()<=64,E::Limit);
  if(type==4){require(maps->children.empty(),E::Unsupported);return out;}
  require(emptyText(n,"rank.divisor.path"),E::Unsupported);out.resultPath=text(n,"json.path").value_or("");
  if(!out.resultPath.empty()){std::size_t offset=0;while(offset<out.resultPath.size()){const auto end=out.resultPath.find('.',offset);auto part=std::string_view(out.resultPath).substr(offset,end==std::string::npos?end:end-offset);require(!part.empty(),E::Unsupported);if(part.front()=='['){require(part.size()>2&&part.back()==']'&&std::ranges::all_of(part.substr(1,part.size()-2),[](unsigned char c){return c>='0'&&c<='9';}),E::Unsupported);}offset=end==std::string::npos?out.resultPath.size():end+1;}require(out.resultPath.back()!='.',E::Unsupported);}
  require(!maps->children.empty(),E::Unsupported);
  for(const auto& m:maps->children){require(m.type==N::Type::Dictionary,E::Malformed);auto name=text(m,"name");const auto field=integer(m,"field",0);require(emptyText(m,"post_filter"),E::Unsupported,"This engine requires result post-filters");if(!name){continue;}require(name->find("${")==std::string::npos,E::Unsupported,"This engine requires mapping expressions");constexpr int fields[]{1,2,3,4,5,102,103,105,200};require(std::ranges::find(fields,field)!=std::end(fields),E::Unsupported,"This engine uses an unsupported result field");out.mappings.push_back({std::move(*name),field});}
  return out;
}
}
std::expected<std::string,E> expandMetasearchQuery(std::string_view format,std::string_view query){try{
  require(format.size()<=2048&&query.size()<=1024,E::Limit);require(utf8(query)&&query.find('\0')==query.npos,E::Malformed);require(!query.empty(),E::Unsupported);
  require(format.starts_with("http://")||format.starts_with("https://"),E::Unsupported);require(format.find("azmethod=")==format.npos&&format.find(":COOKIE:")==format.npos&&format.find('#')==format.npos,E::Unsupported);
  std::string encoded;constexpr char Hex[]="0123456789ABCDEF";for(const unsigned char c:query){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='*'){encoded+=static_cast<char>(c);}else if(c==' '){encoded+='+';}else{encoded+='%';encoded+=Hex[c>>4];encoded+=Hex[c&15];}}
  std::string out;const auto authorityEnd=format.find_first_of("/?#",format.find("://")+3);const auto hex=[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');};
  for(std::size_t i=0;i<format.size();++i){const auto c=static_cast<unsigned char>(format[i]);require(c>=33&&c<=126&&c!='\\',E::Unsupported);if(c!='%'){out+=static_cast<char>(c);continue;}if(i+1<format.size()&&format[i+1]=='s'){require(authorityEnd!=format.npos&&i>=authorityEnd,E::Unsupported);out+=encoded;++i;}else{require(i+2<format.size()&&hex(format[i+1])&&hex(format[i+2]),E::Unsupported);out.append(format.substr(i,3));i+=2;}}
  require(out.size()<=4096,E::Limit);return out;
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
std::expected<MetasearchCatalog,E> readMetasearchCatalog(std::string bytes){try{
  auto inventory=core::readSavedSearchEngines(bytes);require(bool(inventory),E::Malformed);MetasearchCatalog out;out.source=inventory->source;std::set<std::string_view> rootKeys;for(std::size_t n=0;n<out.source->root().children.size();n+=2){require(rootKeys.insert(out.source->root().children[n].bytes).second,E::Malformed);}const auto* list=out.source->find("engines");if(!list){return out;}require(list->type==N::Type::List,E::Malformed);
  for(const auto& passive:inventory->engines){MetasearchCatalogRow row;row.selected={passive.sourceIndex,passive.id.value_or(0),passive.uid.value_or("")};row.name=passive.name.value_or("");row.type=passive.type;row.selectionState=passive.selected;
    try{require(passive.id&&passive.uid&&!passive.uid->empty()&&passive.uid->size()<=256,E::Malformed);require(!passive.duplicateId&&!passive.duplicateUid,E::Ambiguous,"The saved engine identity is ambiguous");require(utf8(row.name),E::Malformed);row.recipe=recipe(list->children.at(passive.sourceIndex));}catch(const Failure& e){row.issue=e.error;row.diagnostic=e.reason;}out.rows.push_back(std::move(row));}
  return out;
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
} // namespace biglybtc::search

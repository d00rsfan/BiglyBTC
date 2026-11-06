// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/saved_search_engines.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace c=biglybtc::core;
namespace {
void check(bool v,const char* message){if(!v){throw std::runtime_error(message);}}
std::string b(std::string_view v){return std::to_string(v.size())+':'+std::string(v);}
std::string d(std::initializer_list<std::pair<std::string,std::string>> fields){std::string out="d";for(const auto& [key,value]:fields){out+=b(key)+value;}return out+'e';}
std::string l(std::initializer_list<std::string> rows){std::string out="l";for(const auto& row:rows){out+=row;}return out+'e';}
std::string n(std::int64_t v){return 'i'+std::to_string(v)+'e';}
c::SavedSearchEngines take(std::string_view bytes,c::SavedSearchLimits limits={}){auto v=c::readSavedSearchEngines(bytes,limits);check(bool(v),"Expected saved search catalog");return std::move(*v);}
std::string row(std::int64_t id,std::string uid){return d({{"type",n(4)},{"id",n(id)},{"uid",b(uid)},{"name",b("RSS μ")},{"web.search_url_format",b("https://example.invalid/feed?x=%s")},{"web.full_cookies",b("SECRET")},{"future",d({{"binary",b(std::string("x\0y",3))}})}});}
void owningOriginalProjection(){
  auto bytes=d({{"engines",l({row(std::numeric_limits<std::int64_t>::min(),"uid")})},{"plugin_map",d({{"future",n(-7)}})},{"opaque",b("keep")}});auto v=take(bytes);const auto saved=bytes;bytes.assign("gone");check(v.enginesPresent&&v.pluginMapPresent&&v.engines.size()==1&&v.issues.empty(),"Original inventory lost");const auto& r=v.engines[0];check(r.id==std::numeric_limits<std::int64_t>::min()&&r.kind==c::SavedSearchKind::Rss&&r.name=="RSS μ"&&r.uid=="uid", "Owning identity/name/kind changed");check(!r.rawSelected&&r.selected==0&&!r.rawSource&&r.origin==0&&!r.rawAzVersion&&r.azVersion==5&&r.lastUpdated==0,"Original missing defaults");check(v.source->originalBytes()==saved&&v.source->serialize()==saved,"Raw definition or unknown fields rewritten");check(take("de").enginesPresent==false&&take("d7:engineslee").enginesPresent,"Missing list collapsed into stored empty list");
}
void javaNarrowingAndMalformedRows(){
  auto v=take(d({{"engines",l({d({{"type",n(4294967300LL)},{"id",n(9)},{"selected",n(4294967298LL)},{"source",n(-4294967294LL)},{"az_version",n(4294967301LL)}}),"i4e",d({{"id",n(7)},{"selected",b("bad")}}),d({{"type",n(55)},{"az_version",n(6)}})})}}));check(v.engines.size()==4&&v.engines[0].kind==c::SavedSearchKind::Rss&&v.engines[0].selected==2&&v.engines[0].origin==2&&v.engines[0].azVersion==5,"Java low32 semantics");check(v.engines[1].sourceIndex==1&&!v.engines[1].type&&!v.engines[2].selected&&!v.engines[2].kind&&!v.engines[3].id&&!v.engines[3].kind,"Malformed row was dropped/coerced/allocated");check(!v.issues.empty(),"Missing malformed-row diagnostics");
}
void duplicateIdentityIsNotCollapsed(){
  const auto bytes=d({{"engines",l({row(3,"first"),row(3,"second"),row(8,"second")})}});const auto v=take(bytes);check(v.engines.size()==3&&v.engines[0].duplicateId&&v.engines[1].duplicateId&&!v.engines[2].duplicateId&&!v.engines[0].duplicateUid&&v.engines[1].duplicateUid&&v.engines[2].duplicateUid,"Ambiguous identity hidden");check(v.source->originalBytes()==bytes,"Duplicate source rewritten");
}
void limitsAndMalformedRoots(){
  check(!c::readSavedSearchEngines("le")&&!c::readSavedSearchEngines("d7:enginesi0ee")&&!c::readSavedSearchEngines("garbage"),"Malformed format accepted");auto limits=c::SavedSearchLimits{};limits.engines=1;check(!c::readSavedSearchEngines(d({{"engines",l({row(1,"a"),row(2,"b")})}}),limits),"Row budget ignored");limits={};limits.fieldBytes=2;check(!c::readSavedSearchEngines(d({{"engines",l({row(1,"uid")})}}),limits),"Field budget ignored");limits={};limits.textBytes=1;check(!c::readSavedSearchEngines(d({{"engines",l({row(1,"uid")})}}),limits),"Total copied text budget ignored");limits={};limits.issues=0;check(!c::readSavedSearchEngines("d7:enginesli0eee",limits),"Issue budget ignored");limits={};++limits.engines;check(!c::readSavedSearchEngines("de",limits),"Raised native cap accepted");
}
}
int main(){try{owningOriginalProjection();javaNarrowingAndMalformedRows();duplicateIdentityIsNotCollapsed();limitsAndMalformedRoots();std::cout<<"PASS original saved search inventory\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_download_values.hpp"
#include "biglybtc/bencode.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>
namespace {
namespace d=biglybtc::search::detail;namespace c=biglybtc::core;
using Counts=d::SubscriptionRawScrapeCounts;
const std::string Key(d::SubscriptionDownloadMapAttribute);
void check(bool yes,const char* why){if(!yes){throw std::runtime_error(why);}}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string dict(std::map<std::string,std::string> fields){std::string value="d";for(const auto& [key,item]:fields){value+=b(key)+item;}return value+'e';}
std::string list(const std::vector<std::string>& items){std::string value="l";for(const auto& item:items){value+=item;}return value+'e';}
c::Document parse(std::string bytes){auto value=c::Document::parse(std::move(bytes));check(value.has_value(),"Private value fixture is malformed");return std::move(*value);}
std::string raw(const c::Document& doc,const c::Node& node){return doc.originalBytes().substr(node.begin,node.end-node.begin);}
d::SubscriptionDownloadValues plan(std::string attributes,std::string_view sid,bool callback,Counts counts={}){auto value=d::planSubscriptionDownloadValues(std::move(attributes),sid,callback,counts);check(value.has_value(),"Unexpected pure download value refusal");return std::move(*value);}
std::string sid(){std::string value(10,'\0');value[3]=static_cast<char>(0xff);value[9]='Z';return value;}
void newAssociationCreatesOnlyActualPluginMap(){
  const auto source=dict({{"Plugin.other",dict({{"future","li7e3:rawe"}})},{"unknown",b("opaque")}});auto result=plan(source,sid(),true);check(result.changed&&result.sidChanged&&!result.cacheChanged,"New callback appends one SID only");auto after=parse(result.attributes);const auto* map=after.find(Key);check(map&&map->find("s")&&map->find("s")->children.size()==1&&map->find("s")->children[0].bytes==sid(),"Actual Plugin.<internal> SID bytes retained");check(!map->find("lc")&&!after.find("scsrc")&&!after.find("scrapecache"),"No full lookup or missing-count cache invented");auto before=parse(source);check(raw(after,*after.find("Plugin.other"))==raw(before,*before.find("Plugin.other"))&&raw(after,*after.find("unknown"))==raw(before,*before.find("unknown")),"Unrelated attributes stay byte exact");
}
void appendRetainsOtherMembershipsOrderAndUnknowns(){
  const auto members=list({b("short"),b("other12345"),b("other12345")});const auto info=dict({{"lc","i-7e"},{"s",members},{"unknown","d3:rawli1e2:ZZee"}});const auto source=dict({{Key,info},{"future","li4e3:rawe"}});auto result=plan(source,sid(),true);auto after=parse(result.attributes);auto before=parse(source);const auto* map=after.find(Key);check(map->find("s")->children.size()==4,"New SID appended once");for(std::size_t i=0;i<3;++i){check(raw(after,map->find("s")->children[i])==raw(before,before.find(Key)->find("s")->children[i]),"Other SID bytes/order/duplicates preserved");}check(map->find("s")->children[3].bytes==sid(),"Binary SID exact");check(raw(after,*map->find("unknown"))==raw(before,*before.find(Key)->find("unknown"))&&map->find("lc")->integer==-7,"Unknown map and lc untouched for non-full lookup");
}
void existingMembershipDoesNotRewriteOrDeduplicate(){
  const auto source=dict({{Key,dict({{"s",list({b(sid()),b("other12345"),b(sid())})},{"future",b("keep")}})},{"future","i9e"}});auto result=plan(source,sid(),true);check(!result.changed&&!result.sidChanged&&!result.cacheChanged&&result.attributes==source,"Existing SID preserves exact map, timestamps and duplicates");
}
void duplicateSubscriptionBypassesCallbackWithoutRepair(){
  const auto source=dict({{Key,"i99e"},{"future",b("opaque")}});auto unchanged=plan(source,"unused",false);check(!unchanged.changed&&unchanged.attributes==source,"No callback means malformed plugin map is neither inspected nor repaired");auto cached=plan(source,"unused",false,{1,2});check(cached.changed&&cached.cacheChanged&&!cached.sidChanged,"prepareDownload cache is independent of skipped association callback");auto after=parse(cached.attributes);check(after.find(Key)->integer==99&&after.find("scsrc")->integer==1&&after.find("scrapecache")->integer==4294967298LL,"Only valid raw cache updated");
}
void rawScrapePackingPreservesUnknownOldCache(){
  struct Row {Counts counts;std::optional<std::int64_t> packed;};
  const std::vector<Row> cases{{{},{}},{{0,0},0},{{-1,0},{}},{{0,-1},{}},{{4294967297LL,2147483648LL},4294967296LL},{{std::numeric_limits<std::int64_t>::max(),std::numeric_limits<std::int64_t>::max()},72057589759737855LL},{{16777216,16777217},1},{{-2147483648LL,1},{}},{{1,2},4294967298LL}};
  const auto source=dict({{"scsrc","i0e"},{"scrapecache","i-123e"},{"future","li8e3:rawe"}});
  for(const auto& row:cases){check(d::subscriptionRawScrapeCache(row.counts)==row.packed,"Actual raw Long bit packing");auto result=plan(source,sid(),false,row.counts);if(!row.packed){check(!result.changed&&result.attributes==source,"Missing/negative raw count leaves prior cache untouched");}else{auto after=parse(result.attributes);check(result.cacheChanged&&!result.sidChanged&&after.find("scsrc")->integer==1&&after.find("scrapecache")->integer==*row.packed,"Both nonnegative raw Longs prepare source1/cache");auto before=parse(source);check(raw(after,*after.find("future"))==raw(before,*before.find("future")),"Raw preparation does not normalize unknown data");}}
}
void malformedSupportedShapesRefusePurely(){
  const std::vector<std::string> invalid{"0:","degarbage",dict({{Key,"i3e"}}),dict({{Key,dict({{"s","i3e"}})}}),dict({{Key,dict({{"s","li3ee"}})}}),"d1:ai1e1:ai2ee"};
  for(const auto& source:invalid){auto result=d::planSubscriptionDownloadValues(source,sid(),true,{});check(!result&&result.error()==d::SubscriptionDownloadValueError::Malformed,"Malformed callback value refuses");}
  check(!d::planSubscriptionDownloadValues("de","nine-key!",true,{}),"Callback SID must be actual raw10");
  for(const auto& name:{std::string("scsrc"),std::string("scrapecache")}){check(!d::planSubscriptionDownloadValues(dict({{name,b("wrong")}}),sid(),false,{1,2}),"Malformed known cache attribute refuses before planned effects");}
  const auto untouched=dict({{"scsrc",b("opaque-old")},{Key,"i3e"}});check(plan(untouched,"",false).attributes==untouched,"No effect must not inspect unused known fields");
}
void boundedMembershipNeverEvictsOrDropsUnknowns(){
  std::vector<std::string> entries(4096,b("other12345"));auto full=dict({{Key,dict({{"s",list(entries)}})}});auto refused=d::planSubscriptionDownloadValues(full,sid(),true,{});check(!refused&&refused.error()==d::SubscriptionDownloadValueError::Limit,"Bounded codec never guesses SID eviction");entries.back()=b(sid());auto existing=dict({{Key,dict({{"s",list(entries)}})}});auto unchanged=plan(existing,sid(),true);check(!unchanged.changed&&unchanged.attributes==existing,"Existing SID at bound remains exact");auto oversized=d::planSubscriptionDownloadValues(std::string(4*1024*1024+1,'x'),sid(),false,{});check(!oversized&&oversized.error()==d::SubscriptionDownloadValueError::Limit,"Encoded input is bounded");
}
void combinedPreparationIsIdempotent(){
  const auto source=dict({{Key,dict({{"future",b("map")}})},{"unknown","li2e3:rawe"}});auto first=plan(source,sid(),true,{4294967297LL,2147483648LL});check(first.changed&&first.cacheChanged&&first.sidChanged,"One value plan composes actual raw cache and new SID");auto second=plan(first.attributes,sid(),true,{4294967297LL,2147483648LL});check(!second.changed&&!second.cacheChanged&&!second.sidChanged&&second.attributes==first.attributes,"Repeated values do not rewrite or refresh");
}
}
int main(){try{
  const std::vector<std::pair<const char*,void(*)()>> groups{{"new-association",newAssociationCreatesOnlyActualPluginMap},{"append-unknown-members",appendRetainsOtherMembershipsOrderAndUnknowns},{"existing-sid",existingMembershipDoesNotRewriteOrDeduplicate},{"duplicate-subscription-no-callback",duplicateSubscriptionBypassesCallbackWithoutRepair},{"raw64-preparation",rawScrapePackingPreservesUnknownOldCache},{"malformed-shapes",malformedSupportedShapesRefusePurely},{"bounded-members",boundedMembershipNeverEvictsOrDropsUnknowns},{"combined-idempotence",combinedPreparationIsIdempotent}};
  for(const auto& [name,test]:groups){test();std::cout<<"PASS "<<name<<'\n';}std::cout<<"PASS_SUBSCRIPTION_DOWNLOAD_STATE_VALUES_8_GROUPS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

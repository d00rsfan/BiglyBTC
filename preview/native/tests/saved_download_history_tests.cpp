// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/saved_download_history.hpp"
#include <bit>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace c=biglybtc::core;using E=c::SavedDownloadHistoryError;
namespace {
void check(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,E> value){check(bool(value),"Expected pure download-history result");return std::move(*value);}
template<class T>void error(const std::expected<T,E>& value,E code){check(!value&&value.error()==code,"Wrong pure download-history refusal");}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string dict(std::initializer_list<std::pair<std::string,std::string>> fields){std::string out="d";for(const auto& [key,value]:fields){out+=b(key)+value;}return out+'e';}
std::string list(std::initializer_list<std::string> rows){std::string out="l";for(const auto& row:rows){out+=row;}return out+'e';}
std::string number(std::int64_t value){return 'i'+std::to_string(value)+'e';}
std::string row(std::int64_t uid,std::string name="old",std::string tags="l3:one3:onee"){
  return dict({{"u",number(uid)},{"h",b(std::string("hash\0hint",9))},{"v",b(std::string(32,'v'))},{"n",b(name)},{"s",b("/original/payload")},{"a","i-7e"},{"c","i-1e"},{"r","i-9e"},{"t",std::move(tags)},{"future",dict({{"bin",b(std::string("x\0y",3))}})}});
}
// Canonical integer is required by the existing bounded bencode parser.
std::string fixture(std::string records){return dict({{"future-root",list({"i-3e",b(std::string("z\0w",3))})},{"records",std::move(records)},{"version",b("future-not-a-history-version")}});}
std::string raw(const c::Document& document,const c::Node* value){check(value,"Missing source node");return document.originalBytes().substr(value->begin,value->end-value->begin);}
void owningProjectionKeepsOriginalSignedAndOptionalFields(){
  std::string bytes=fixture(list({row(std::numeric_limits<std::int64_t>::min())}));const auto value=take(c::readSavedDownloadHistory(bytes));bytes.assign("destroyed input");
  check(value.recordsPresent&&value.records.size()==1,"Original records absent");const auto& r=value.records.front();
  check(r.uid==std::numeric_limits<std::int64_t>::min()&&!r.rawSize&&r.size==0&&r.addTime==-7,"Original signed/absent integer semantics");
  check(r.completeTime==0&&r.removeTime==0&&r.rawCompleteTime==-1&&r.rawRemoveTime==-9,"Negative times normalized by rewriting source");
  check(r.tagsPresent&&r.tags==std::vector<std::string>({"one","one"})&&r.hash==std::string("hash\0hint",9)&&r.hashV2==std::string(32,'v'),"Original byte hints/tag list changed");
  check(value.source->find("version")->bytes=="future-not-a-history-version","Invented history version policy");
  const auto empty=take(c::readSavedDownloadHistory("de"));check(!empty.recordsPresent&&empty.records.empty(),"Missing records fabricated a persisted list");
  const auto minimum=dict({{"u","i0e"},{"n",b("")},{"s",b(std::string("a\0b",3))},{"a","i0e"},{"c","i0e"},{"r","i0e"}});
  const auto missing=take(c::readSavedDownloadHistory(fixture(list({minimum}))));check(!missing.records[0].hash&&!missing.records[0].hashV2&&!missing.records[0].tagsPresent&&missing.records[0].tags.empty(),"Optional hints materialized");
}
void editsPreserveUnknownRawAndUntouchedRows(){
  const auto bytes=fixture(list({row(1),row(2,"sibling")}));const auto before=take(c::readSavedDownloadHistory(bytes));
  const auto noOp=take(c::editSavedDownloadHistory(bytes,c::DownloadHistorySetCompleteTime{1,-4}));check(!noOp.changed&&noOp.value.source->originalBytes()==bytes,"Projected zero rewrote raw negative time");
  const auto renamed=take(c::editSavedDownloadHistory(bytes,c::DownloadHistorySetName{1,"\xe6\x96\xb0 name"}));check(renamed.changed&&renamed.value.records[0].name=="\xe6\x96\xb0 name","Valid UTF8 name not set");
  const auto* oldList=before.source->find("records");const auto* newList=renamed.value.source->find("records");check(raw(*before.source,&oldList->children[1])==raw(*renamed.value.source,&newList->children[1]),"Sibling row rewritten");
  for(const auto key:{"u","h","v","s","a","c","r","t","future"}){check(raw(*before.source,oldList->children[0].find(key))==raw(*renamed.value.source,newList->children[0].find(key)),"Unselected field/unknown bytes changed");}
  for(const auto key:{"future-root","version"}){check(raw(*before.source,before.source->find(key))==raw(*renamed.value.source,renamed.value.source->find(key)),"Unknown root rewritten");}
  const auto located=take(c::editSavedDownloadHistory(renamed.value.source->originalBytes(),c::DownloadHistorySetSaveLocation{1,"/new/location"}));check(located.value.records[0].saveLocation=="/new/location"&&!located.value.records[0].rawSize,"Path edit materialized missing size");
  const auto completed=take(c::editSavedDownloadHistory(located.value.source->originalBytes(),c::DownloadHistorySetCompleteTime{1,1234}));check(completed.value.records[0].completeTime==1234&&completed.value.records[0].rawRemoveTime==-9,"Completion edit changed removed time");
  const auto removed=take(c::editSavedDownloadHistory(completed.value.source->originalBytes(),c::DownloadHistorySetRemoved{1,4321,{"z","a","z"}}));check(removed.value.records[0].removeTime==4321&&removed.value.records[0].tags==std::vector<std::string>({"z","a","z"}),"Removal tags implicitly sorted/deduplicated");
  const auto same=take(c::editSavedDownloadHistory(removed.value.source->originalBytes(),c::DownloadHistorySetRemoved{1,4321,{}}));check(!same.changed&&same.value.source->originalBytes()==removed.value.source->originalBytes(),"Empty removal tags cleared historical list");
}
void appendEraseAndClearAreSingleDocumentValues(){
  const auto bytes=fixture(list({row(1)}));c::DownloadHistoryNewRecord added;added.uid=2;added.name="new";added.saveLocation="/path";added.hash=std::string(20,'h');added.hashV2=std::string(32,'v');added.size=456;added.addTime=123;added.completeTime=-1;added.removeTime=-2;
  const auto result=take(c::editSavedDownloadHistory(bytes,c::DownloadHistoryAppend{added}));check(result.changed&&result.value.records.size()==2,"New record missing");const auto& record=result.value.records.back();check(record.uid==2&&record.rawSize==456&&record.addTime==123&&record.completeTime==0&&record.rawCompleteTime==0&&record.rawRemoveTime==0&&!record.tagsPresent,"Original writer field/default policy differs");
  const auto before=take(c::readSavedDownloadHistory(bytes));check(raw(*before.source,&before.source->find("records")->children[0])==raw(*result.value.source,&result.value.source->find("records")->children[0]),"Append rewrote old record");
  error(c::editSavedDownloadHistory(result.value.source->originalBytes(),c::DownloadHistoryAppend{added}),E::ExistingUid);
  const auto absent=take(c::editSavedDownloadHistory(bytes,c::DownloadHistoryErase{999}));check(!absent.changed&&absent.value.source->originalBytes()==bytes,"Missing erase mutated source");
  const auto erased=take(c::editSavedDownloadHistory(result.value.source->originalBytes(),c::DownloadHistoryErase{1}));check(erased.value.records.size()==1&&erased.value.records[0].uid==2,"UID-selected erase differs");
  const auto cleared=take(c::editSavedDownloadHistory(bytes,c::DownloadHistoryClear{}));check(cleared.changed&&cleared.value.recordsPresent&&cleared.value.records.empty()&&cleared.value.source->find("future-root"),"Clear destroyed root metadata");
  check(!take(c::editSavedDownloadHistory("de",c::DownloadHistoryClear{})).changed,"Missing empty list rewritten");
  const auto first=take(c::editSavedDownloadHistory("de",c::DownloadHistoryAppend{added}));check(first.value.recordsPresent&&first.value.records.size()==1,"First append failed");
}
void malformedAndAmbiguousRowsRefuseWithoutPartialSuccess(){
  error(c::readSavedDownloadHistory("le"),E::Root);error(c::readSavedDownloadHistory(fixture("de")),E::Records);error(c::readSavedDownloadHistory(fixture(list({"i1e"}))),E::Record);
  error(c::readSavedDownloadHistory(fixture(list({row(1),row(1)}))),E::DuplicateUid);
  const auto broken=dict({{"u","i1e"},{"n",b("name")},{"s",b("path")},{"a","i1e"},{"c","i0e"}});error(c::readSavedDownloadHistory(fixture(list({row(2),broken}))),E::Field);
  error(c::readSavedDownloadHistory(fixture(list({row(1,std::string("\xc0\x80",2))}))),E::Text);error(c::readSavedDownloadHistory(fixture(list({row(1,"ok","li1ee")}))),E::Field);
  error(c::readSavedDownloadHistory("d7:recordsle7:recordslee"),E::Format);
  const auto before=fixture(list({row(1)}));error(c::editSavedDownloadHistory(before,c::DownloadHistorySetName{2,"absent"}),E::MissingUid);check(take(c::readSavedDownloadHistory(before)).source->originalBytes()==before,"Refusal changed caller bytes");
  c::SavedDownloadHistoryLimits limits;limits.records=0;error(c::readSavedDownloadHistory(before,limits),E::Limit);limits={};limits.hashBytes=2;error(c::readSavedDownloadHistory(before,limits),E::Limit);limits={};limits.textBytes=2;error(c::readSavedDownloadHistory(before,limits),E::Limit);limits={};limits.tagsPerRecord=1;error(c::readSavedDownloadHistory(before,limits),E::Limit);limits={};limits.document.inputBytes=3;error(c::readSavedDownloadHistory(before,limits),E::Limit);
  limits={};limits.fieldBytes=3;error(c::editSavedDownloadHistory("de",c::DownloadHistoryAppend{{1,{}, {},"large","",0,0,0,0,{}}},limits),E::Limit);
}
void originalUidTimeAndEligibilityEdgesAreExplicitInputs(){
  const std::string prefix("\x80\x00\x00\x01",4);check(std::bit_cast<std::uint64_t>(c::originalDownloadHistoryUid(prefix,1000))==0x8000000100000001ULL,"Signed Java hash prefix differs");
  check(c::originalDownloadHistoryUid({},-1999)==-1&&c::originalDownloadHistoryUid(std::string_view("abc"),12000)==12,"Missing/short hash or signed division differs");
  const std::string zero(4,'\0');check(std::bit_cast<std::uint64_t>(c::originalDownloadHistoryUid(zero,4294967296000LL))==0x100000000ULL,"Java RHS was incorrectly masked to32 bits");
  const std::string one("\0\0\0\1",4);check(std::bit_cast<std::uint64_t>(c::originalDownloadHistoryUid(one,8589934592000LL))==0x300000000ULL,"RHS high bits were replaced instead of ORed");
  check(c::originalDownloadHistoryCompleteTime(0,123)==123&&c::originalDownloadHistoryCompleteTime(-1,123)==0&&c::originalDownloadHistoryCompleteTime(12,999)==12,"Original completion precedence differs");
  check(c::originalDownloadHistoryMonitored(true,0)&&c::originalDownloadHistoryMonitored(true,0x80000000)&&!c::originalDownloadHistoryMonitored(false,0)&&!c::originalDownloadHistoryMonitored(true,0x10)&&!c::originalDownloadHistoryMonitored(true,0x200),"Original persistent/flags eligibility differs");
}
}
int main(){try{owningProjectionKeepsOriginalSignedAndOptionalFields();editsPreserveUnknownRawAndUntouchedRows();appendEraseAndClearAreSingleDocumentValues();malformedAndAmbiguousRowsRefuseWithoutPartialSuccess();originalUidTimeAndEligibilityEdgesAreExplicitInputs();std::cout<<"PASS pure original download-history: five deterministic families\n";return EXIT_SUCCESS;}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return EXIT_FAILURE;}}

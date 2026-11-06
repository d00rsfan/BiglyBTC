// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/saved_download_tag_edits.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace std::string_view_literals;
namespace c = biglybtc::core;
namespace {
void check(bool ok, const char* why) { if (!ok) { throw std::runtime_error(why); } }
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string dict(std::initializer_list<std::pair<std::string, std::string>> rows) {
  std::string out = "d"; for (const auto& [key, value] : rows) { out += bytes(key) + value; } return out + 'e';
}
std::string list(std::initializer_list<std::string> rows) { std::string out = "l"; for (const auto& value : rows) { out += value; } return out + 'e'; }
c::SavedTagEditResult edited(std::string_view raw, c::SavedTagEdit edit, c::SavedTagLimits limits = {}) {
  auto result = c::editSavedDownloadTags(raw, edit, limits); check(bool(result), "Expected pure saved-tag edit"); return std::move(*result);
}
std::string wire(const c::SavedTagEditResult& result) { return result.value.source->serialize(); }
void refused(std::string_view raw, c::SavedTagEdit edit, c::SavedTagEditError error, c::SavedTagLimits limits = {}) {
  const std::string original(raw); auto result = c::editSavedDownloadTags(raw, edit, limits);
  check(!result && result.error() == error && raw == original, "Wrong refusal or mutated input");
}
c::TorrentHash hash(std::size_t size) {
  std::string raw; for (std::size_t i = 0; i < size; ++i) { raw.push_back(static_cast<char>(i)); }
  auto value = c::TorrentHash::parse(raw); check(bool(value), "Expected genuine typed hash"); return std::move(*value);
}
constexpr auto H20 = "AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQT"sv;
constexpr auto H32 = "AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQTCQKRMFYYDENBWHA5DYPQ"sv;
const c::Node& record(const c::SavedTagEditResult& value, std::string_view id) {
  const auto* type = value.value.source->find("3"); check(type, "Missing manual type"); const auto* result = type->find(id); check(result, "Missing edited tag"); return *result;
}
std::string raw(const c::SavedTagEditResult& value, const c::Node& node) { return value.value.source->originalBytes().substr(node.begin, node.end - node.begin); }
void allocationNamesAndUid() {
  const auto first = edited("de", c::SavedTagCreate{"First", {}});
  check(first.createdId == 1 && first.createdUid == 12884901889LL && first.allocationCounter == 1 && first.changed, "Original empty counter/UID differs");
  check(wire(first) == dict({{"3", dict({{"1", dict({{"n", bytes("First")}})}})}}), "Original create should only add raw name");
  const auto sameSession = edited(wire(first), c::SavedTagCreate{"First", first.allocationCounter});
  check(sameSession.createdId == 2 && sameSession.value.tags.size() == 2, "Original session increment or duplicate names differ");
  const auto reloaded = edited(wire(first), c::SavedTagCreate{"", {}});
  check(reloaded.createdId == 3 && record(reloaded,"3").find("n")->bytes.empty(), "Original loaded id+1 then increment gap or empty name differs");
  const auto zero = dict({{"3", dict({{"0", dict({{"n",bytes("zero")}})}})}});
  check(edited(zero,c::SavedTagCreate{"next",{}}).createdId==2, "Original loaded zero-id counter differs");
  const auto renamed = edited(zero,c::SavedTagRename{0,"\xD0\xA2\xD0\xB5\xD0\xB3"});
  check(record(renamed,"0").find("n")->bytes=="\xD0\xA2\xD0\xB5\xD0\xB3", "UTF-8 MapUtils name bytes differ");
  refused(wire(first),c::SavedTagCreate{"x",0},c::SavedTagEditError::InvalidCounter);
  refused("de",c::SavedTagCreate{"x",-1},c::SavedTagEditError::InvalidCounter);
  const auto maximum=std::numeric_limits<std::int32_t>::max();
  check(edited("de",c::SavedTagCreate{"last",maximum-1}).createdId==maximum, "Last nonoverflowing int32 id refused");
  refused("de",c::SavedTagCreate{"overflow",maximum},c::SavedTagEditError::IdExhausted);
  const auto last=wire(edited("de",c::SavedTagCreate{"last",maximum-1}));
  refused(last,c::SavedTagCreate{"wrap",{}},c::SavedTagEditError::IdExhausted);
}
void localizedRenameAndVisibility() {
  const auto unknown=dict({{"later",list({"i-3e",bytes("future\0"sv)})}});
  const auto initial=dict({{"3",dict({{"7",dict({{"n",bytes("tag.movies")},{"x",unknown},{"c",dict({{"future",unknown}})}})}})}});
  const auto renamed=edited(initial,c::SavedTagRename{7,"Movies"}); const auto& row=record(renamed,"7");
  check(row.find("n")->bytes=="Movies" && row.find("c")->find("oname")->bytes=="tag.movies", "Original localized rename oname lost");
  check(raw(renamed,*row.find("x"))==unknown && raw(renamed,*row.find("c")->find("future"))==unknown, "Localized rename rewrote unknowns");
  const auto again=edited(wire(renamed),c::SavedTagRename{7,"New"});check(record(again,"7").find("c")->find("oname")->bytes=="tag.movies", "Original name witness overwritten");
  const auto hidden=edited(wire(again),c::SavedTagVisibility{7,false});check(!hidden.value.tags.front().visible && record(hidden,"7").find("c")->find("vis")->integer==0, "Original false visibility differs");
  const auto visible=edited(wire(hidden),c::SavedTagVisibility{7,true});check(visible.value.tags.front().visible && record(visible,"7").find("c")->find("vis")->integer==1, "Original true visibility differs");
  check(!edited(wire(visible),c::SavedTagVisibility{7,true}).changed, "Idempotent visibility rewrites bytes");
  const auto nonBoolean=dict({{"3",dict({{"1",dict({{"n",bytes("n")},{"c",dict({{"vis","i2e"}})}})}})}});
  check(!edited(nonBoolean,c::SavedTagVisibility{1,false}).changed, "Original false Long2 should remain raw on no-op");
  check(record(edited(nonBoolean,c::SavedTagVisibility{1,true}),"1").find("c")->find("vis")->integer==1, "Original false Long2 should become true Long1");
  const auto sparse=dict({{"3",dict({{"1",dict({{"n",bytes("n")}})}})}});
  check(record(edited(sparse,c::SavedTagVisibility{1,true}),"1").find("c")->find("vis")->integer==1, "Unset default true needs explicit Long1");
}
void rawPreservationAndRemoval() {
  // Intentionally unsorted dictionary order + binary unknown fields and IDs.
  const auto opaque=dict({{"z",bytes("\0future"sv)},{"a",list({"i-7e",dict({{"k",bytes("v")}})})}});
  const auto sibling=dict({{"n",bytes("keep")},{"unknown",opaque},{"o",list({bytes("old-unknown-id"),bytes("\0raw"sv)})}});
  const auto selected=dict({{"u",opaque},{"n",bytes("before")},{"o",list({bytes("unresolved")})}});
  const auto type=dict({{"c",opaque},{"8",sibling},{"2",selected},{"-1",opaque},{"future",opaque}});
  const auto initial=dict({{"9",opaque},{"3",type},{"future-root",opaque}});
  const auto renamed=edited(initial,c::SavedTagRename{2,"after"});
  const auto expected=dict({{"9",opaque},{"3",dict({{"c",opaque},{"8",sibling},{"2",dict({{"u",opaque},{"n",bytes("after")},{"o",list({bytes("unresolved")})}})},{"-1",opaque},{"future",opaque}})},{"future-root",opaque}});
  check(wire(renamed)==expected, "Rename failed complete byte-preserving expected delta");
  const auto removed=edited(initial,c::SavedTagRemove{2});
  check(wire(removed)==dict({{"9",opaque},{"3",dict({{"c",opaque},{"8",sibling},{"-1",opaque},{"future",opaque}})},{"future-root",opaque}}), "Remove touched unrelated types/tag records/raw IDs");
  check(!renamed.value.source->originalBytes().empty() && initial.find("before")!=initial.npos, "Edit borrowed or mutated pristine input");
  refused(initial,c::SavedTagRemove{99},c::SavedTagEditError::MissingTag);
}
void alignedMembershipAndUnknownIds() {
  const auto unknown=bytes("legacy\0unresolved"sv); const auto oldProps=dict({{"future",bytes("opaque")},{"a","i42e"}});
  const auto initial=dict({{"3",dict({{"4",dict({{"n",bytes("n")},{"o",list({unknown,bytes(H20)})},{"p",list({oldProps,"de"})},{"u",list({"i5e"})}})}})}});
  const auto added=edited(initial,c::SavedTagAddMember{4,hash(32),123});const auto& row=record(added,"4");
  check(row.find("o")->children.size()==3 && row.find("o")->children[2].bytes==H32, "Original uppercase Base32 member encoding differs");
  check(raw(added,row.find("o")->children[0])==unknown && raw(added,row.find("p")->children[0])==oldProps, "Unresolved member/property raw bytes changed");
  check(row.find("p")->children[2].find("a")->integer==123 && added.value.tags.front().uniqueHashes.size()==2, "New original added-time property or typed member projection differs");
  check(!edited(wire(added),c::SavedTagAddMember{4,hash(32),999}).changed, "Existing membership changed its time property");
  const auto removed=edited(wire(added),c::SavedTagRemoveMember{4,hash(20)});const auto& next=record(removed,"4");
  check(next.find("o")->children.size()==2 && next.find("o")->children[1].bytes==H32 && next.find("p")->children[1].find("a")->integer==123, "Removal broke positional properties");
  check(raw(removed,next.find("o")->children[0])==unknown && raw(removed,next.find("p")->children[0])==oldProps, "Removal discarded unknown raw member");
  check(!edited(wire(removed),c::SavedTagRemoveMember{4,hash(20)}).changed, "Absent member removal rewrote bytes");
  const auto sparse=dict({{"3",dict({{"1",dict({{"n",bytes("n")},{"o",list({unknown})}})}})}});
  const auto padded=edited(sparse,c::SavedTagAddMember{1,hash(20),-7});
  check(record(padded,"1").find("p")->children.size()==2 && record(padded,"1").find("p")->children[0].children.empty() && record(padded,"1").find("p")->children[1].find("a")->integer==-7, "Missing property list must align preserved raw IDs and signed times");
  const auto empty=edited(dict({{"3",dict({{"1",dict({{"n",bytes("n")}})}})}}),c::SavedTagAddMember{1,hash(20),0});
  check(record(empty,"1").find("o")->children[0].bytes==H20 && record(empty,"1").find("p")->children[0].find("a")->integer==0, "First-member o/p creation differs");
}
void malformedAndAmbiguousRefuse() {
  using E=c::SavedTagEditError;
  refused("d1:3de1:3dee",c::SavedTagCreate{"n",{}},E::Format);
  refused("le",c::SavedTagCreate{"n",{}},E::Root);refused("d1:3lee",c::SavedTagCreate{"n",{}},E::ManualType);
  const auto tag=dict({{"n",bytes("n")}});
  for(const auto& key: {std::string("01"),std::string("1x"),std::string("2147483648")}){
    refused(dict({{"3",dict({{key,tag}})}}),c::SavedTagCreate{"n",{}},E::AmbiguousId);
  }
  refused(dict({{"3",dict({{"1",tag},{"01",tag}})}}),c::SavedTagRemove{1},E::AmbiguousId);
  refused(dict({{"3",dict({{"1","i1e"}})}}),c::SavedTagRename{1,"n"},E::MalformedRecord);
  refused(dict({{"3",dict({{"1",dict({{"n","i1e"}})}})}}),c::SavedTagRename{1,"n"},E::MalformedRecord);
  refused(dict({{"3",dict({{"1",dict({{"n",bytes("n")},{"c","i1e"}})}})}}),c::SavedTagVisibility{1,false},E::MalformedConfiguration);
  refused(dict({{"3",dict({{"1",dict({{"n",bytes("tag.n")},{"c",dict({{"oname","i1e"}})}})}})}}),c::SavedTagRename{1,"n"},E::MalformedConfiguration);
  for(const auto& config: {dict({{"vis",bytes("true")}}),list({"i1e"})}){
    refused(dict({{"3",dict({{"1",dict({{"n",bytes("n")},{"c",config}})}})}}),c::SavedTagVisibility{1,true},E::MalformedConfiguration);
  }
  const auto badMember=[&](std::string objects,std::string props){return dict({{"3",dict({{"1",dict({{"n",bytes("n")},{"o",std::move(objects)},{"p",std::move(props)}})}})}});};
  for(const auto& input: {badMember(bytes("wrong"),"le"),badMember(list({bytes(H20)}),"le"),badMember(list({bytes(H20)}),list({"i1e"})),badMember(list({bytes(H20)}),list({dict({{"a",bytes("wrong")}})})),badMember(list({"i1e"}),list({"de"})),badMember(list({bytes(H20),bytes(H20)}),list({"de","de"}))}){
    refused(input,c::SavedTagAddMember{1,hash(32),0},E::MalformedMembership);
  }
  refused("de",c::SavedTagCreate{std::string("\xC0\x80",2),{}},E::InvalidName);
  refused("de",c::SavedTagCreate{std::string("\xED\xA0\x80",3),{}},E::InvalidName);
}
void quotasAndFreshOwnership() {
  const auto input=dict({{"3",dict({{"1",dict({{"n",bytes("old")}})}})}});
  auto original=input;const auto result=edited(original,c::SavedTagRename{1,"new"});original.assign(original.size(),'x');
  check(record(result,"1").find("n")->bytes=="new" && result.value.source->serialize()==wire(result), "Returned document borrowed caller storage");
  auto limits=c::SavedTagLimits{};limits.tags=1;refused(input,c::SavedTagCreate{"two",{}},c::SavedTagEditError::Limit,limits);
  limits={};limits.members=0;refused(input,c::SavedTagAddMember{1,hash(20),0},c::SavedTagEditError::Limit,limits);
  limits={};limits.fieldBytes=3;refused(input,c::SavedTagRename{1,"long"},c::SavedTagEditError::Limit,limits);
  limits={};limits.document.inputBytes=input.size();refused(input,c::SavedTagRename{1,"a much longer name"},c::SavedTagEditError::Limit,limits);
  limits={};++limits.document.depth;refused(input,c::SavedTagRename{1,"x"},c::SavedTagEditError::Limit,limits);
  limits={};limits.textBytes=2;refused(input,c::SavedTagRename{1,"x"},c::SavedTagEditError::Limit,limits);
}
void constraintPropertyPreservesUnknownsAndNeverEditsMembers() {
  const auto tail=dict({{"unknown",bytes(std::string("x\0y",3))}});
  const auto members=list({bytes(H20),bytes("future-raw-id")});
  const auto props=list({dict({{"a","i2e"}}),dict({{"future","i9e"}})});
  const auto config=dict({{"pp.constraint",list({bytes("old"),bytes("am=1;"),tail,bytes("future-string")})},{"future",tail}});
  const auto input=dict({{"other-type",tail},{"3",dict({{"1",dict({{"n",bytes("rule")},{"c",config},{"o",members},{"p",props},{"future-record",tail}})},{"c",tail}})}});
  const auto result=edited(input,c::SavedTagSetConstraint{1,"isNew() && broken(","am=2;future-option;",false});
  const auto& row=record(result,"1");const auto* changed=row.find("c");const auto* property=changed->find("pp.constraint");
  check(result.changed&&property&&property->children.size()==4&&property->children[0].bytes=="isNew() && broken("&&property->children[1].bytes=="am=2;future-option;","Expression must save as configuration without a compiler gate");
  check(raw(result,property->children[2])==tail&&property->children[3].bytes=="future-string"&&raw(result,*changed->find("future"))==tail,"Unknown property tail/configuration lost");
  check(raw(result,*row.find("o"))==members&&raw(result,*row.find("p"))==props&&raw(result,*row.find("future-record"))==tail&&raw(result,*result.value.source->find("other-type"))==tail,"Configuration save changed raw membership/other data");
  check(changed->find("pp.enabled.constraint")->integer==0,"Original enabled property encoding differs");
  const auto noop=edited(wire(result),c::SavedTagSetConstraint{1,"isNew() && broken(","am=2;future-option;",false});check(!noop.changed&&wire(noop)==wire(result),"Equivalent property save rewrote bytes");
  const auto clear=edited(wire(result),c::SavedTagSetConstraint{1,"","",true});check(record(clear,"1").find("c")->find("pp.constraint")->children.size()==4&&raw(clear,*record(clear,"1").find("o"))==members,"Empty inactive property lost unknown list tail or members");
  const auto empty=dict({{"3",dict({{"1",dict({{"n",bytes("empty")}})}})}});check(!edited(empty,c::SavedTagSetConstraint{1,"","",true}).changed,"Absent default constraint should remain a no-op");
  const auto noncanonical=dict({{"3",dict({{"1",dict({{"n",bytes("disabled")},{"c",dict({{"pp.enabled.constraint","i9e"}})}})}})}});check(!edited(noncanonical,c::SavedTagSetConstraint{1,"","",false}).changed,"Original Boolean enabled must mean exactly integer1");
  const auto malformed=[&](std::string value){return dict({{"3",dict({{"1",dict({{"n",bytes("bad")},{"c",dict({{"pp.constraint",value}})}})}})}});};
  refused(malformed("i1e"),c::SavedTagSetConstraint{1,"true","",true},c::SavedTagEditError::MalformedConfiguration);
  refused(malformed(list({"i1e"})),c::SavedTagSetConstraint{1,"true","",true},c::SavedTagEditError::MalformedConfiguration);
  refused(empty,c::SavedTagSetConstraint{1,std::string("\xff",1),"",true},c::SavedTagEditError::InvalidConstraint);
  auto limits=c::SavedTagLimits{};limits.fieldBytes=3;refused(empty,c::SavedTagSetConstraint{1,"true","",true},c::SavedTagEditError::Limit,limits);
}
void configuredGroupEffectsCannotBorrowOrdinaryMembershipEdits(){
  const auto make=[&](std::string groupSettings,bool member=true,std::string group="G"){
    return dict({{"3",dict({{"c",std::move(groupSettings)},{"1",dict({{"n",bytes("Grouped")},{"c",dict({{"gr",bytes(group)}})},{"o",member?list({bytes(H20)}):"le"}})}})}});
  };
  for(const auto& value:{dict({{"x","i1e"}}),dict({{"ar",bytes("")}}),dict({{"gc","le"}}),dict({{"future","i0e"}}),std::string("i0e")}){
    const auto current=make(dict({{"I4",value}}));
    refused(current,c::SavedTagRemoveMember{1,hash(20)},c::SavedTagEditError::UnsupportedGroupSettings);
    refused(current,c::SavedTagAddMember{1,hash(32),100},c::SavedTagEditError::UnsupportedGroupSettings);
  }
  for(const auto& settings:{std::string("de"),dict({{"I4","de"}}),dict({{"I4",dict({{"x","i0e"}})}}),dict({{"I4",dict({{"x","i2e"}})}}),dict({{"UNRELATED",dict({{"x","i1e"}})}})}){
    check(edited(make(settings),c::SavedTagRemoveMember{1,hash(20)}).changed,"Ordinary/independent group membership was blanket refused");
  }
  const auto unknown=dict({{"3",dict({{"c",dict({{"I4",dict({{"x","i1e"}})}})},{"1",dict({{"n",bytes("Ungrouped")}})}})}});
  check(edited(unknown,c::SavedTagAddMember{1,hash(20),1}).changed,"Unrelated group settings blocked an ungrouped row");
  const auto nonAscii=make(dict({{"UNRELATED","de"}}),true,"\xC3\xA9");
  refused(nonAscii,c::SavedTagRemoveMember{1,hash(20)},c::SavedTagEditError::UnsupportedGroupSettings);
  check(edited(make("de",true,"\xC3\xA9"),c::SavedTagRemoveMember{1,hash(20)}).changed,"Absent group definitions should not invent charset policy");
}
void configuredPerTagEffectsRemainDataOnlyUntilActualExecutor(){
  for(const auto& config:{dict({{"rl.up","i1024e"}}),dict({{"rl.down","i512e"}}),dict({{"pp.2","i1e"}}),dict({{"pp.tracker_templates",list({bytes("apply:template")})}}),dict({{"pp.trackers",list({bytes("tracker")})}}),dict({{"pp.untagged","i1e"}}),dict({{"future-effect","i0e"}})}){
    const auto before=dict({{"3",dict({{"1",dict({{"n",bytes("Configured")},{"c",config}})}})}});
    const auto data=edited(before,c::SavedTagAddMember{1,hash(20),1});check(data.changed,"Value-only membership projection unexpectedly became runtime permission");
    const auto* row=data.value.source->find("3")->find("1");check(!c::ordinarySavedTagMembershipEffects(data.value,*row),"Configured effect was treated as a supported executor");
    check(raw(data,*row->find("c"))==config,"Data projection changed configured effect bytes");
  }
  for(const auto& config:{std::string("de"),dict({{"vis","i1e"},{"oname",bytes("original")}}),dict({{"pp.trackers","le"},{"pp.tracker_templates","le"},{"pp.untagged","i0e"},{"pp.2","i0e"}}),dict({{"pp.constraint",list({bytes("true"),bytes("am=1;")})},{"pp.enabled.constraint","i0e"}})}){
    auto tags=c::readSavedDownloadTags(dict({{"3",dict({{"1",dict({{"n",bytes("Metadata")},{"c",config}})}})}}));check(bool(tags)&&c::ordinarySavedTagMembershipEffects(*tags,*tags->source->find("3")->find("1")),"Supported metadata-only/default property was blanket refused");
  }
}
} // namespace
int main() {
  try {allocationNamesAndUid();localizedRenameAndVisibility();rawPreservationAndRemoval();alignedMembershipAndUnknownIds();malformedAndAmbiguousRefuse();quotasAndFreshOwnership();constraintPropertyPreservesUnknownsAndNeverEditsMembers();configuredGroupEffectsCannotBorrowOrdinaryMembershipEdits();configuredPerTagEffectsRemainDataOnlyUntilActualExecutor();std::cout<<"PASS seven pure saved manual-tag edit families\n";return 0;}
  catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

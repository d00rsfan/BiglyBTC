#include "biglybtc/manual_tag_statistics.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace biglybtc;
std::size_t assertions=0,cases=0;
void check(bool value,const char* message) {++assertions;if(!value){throw std::runtime_error(message);}}
std::string bytes(std::string_view value) {return std::to_string(value.size())+':'+std::string(value);}
std::string list(std::initializer_list<std::string> values) {
  std::string out="l";for(const auto& value:values){out+=value;}return out+'e';
}
std::string dict(std::initializer_list<std::pair<std::string,std::string>> fields) {
  std::vector<std::pair<std::string,std::string>> sorted(fields);std::sort(sorted.begin(),sorted.end());
  std::string out="d";for(const auto& [key,value]:sorted){out+=bytes(key)+value;}return out+'e';
}
constexpr std::string_view A="AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQT";
constexpr std::string_view B="AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQTCQKRMFYYDENBWHA5DYPQ";
constexpr std::string_view C="77777777777777777777777777777777";
core::SavedDownloadTags saved(std::string_view raw) {
  auto value=core::readSavedDownloadTags(raw);check(bool(value),"generated tag document rejected");return std::move(*value);
}
core::TorrentHash hash(std::size_t size) {
  std::string raw(size,'\0');for(std::size_t i=0;i<size;++i){raw[i]=static_cast<char>(i);}
  auto value=core::TorrentHash::parse(raw);check(bool(value),"fixture hash rejected");return *value;
}
std::string fixture() {
  return dict({{"3",dict({
    {"1",dict({{"n",bytes("Alpha")},{"o",list({bytes(A)})}})},
    {"2",dict({{"n",bytes("Beta")},{"o",list({bytes(B)})}})},
    {"3",dict({{"n",bytes("Shared")},{"o",list({bytes(A),bytes(B)})}})},
    {"4",dict({{"n",bytes("Empty")},{"o","le"}})},
    {"5",dict({{"n",bytes("Unresolved")},{"o",list({bytes(C)})}})},
    {"6",dict({{"n",bytes("Absent membership")}})},
    {"7",dict({{"o",list({"i9e"})}})}})}, {"opaque",bytes("keep")}});
}
std::shared_ptr<const core::ManualTagMembership> joined(std::string_view raw=fixture()) {
  const auto parsed=saved(raw);const std::array hashes{hash(20),hash(32)};
  auto value=core::resolveManualTagMembership(7,11,13,parsed,hashes);check(bool(value),"generated library join failed");return *value;
}
core::ManualTagTraffic active(bool covered=true,bool current=true) {
  return {core::ManualTagTrafficState::Active,core::TargetTraffic{hash(20),101,23,80,20},covered,current};
}
std::vector<core::ManualTagRate> rates(const core::ManualTagMembership& member,core::ManualTagTraffic traffic) {
  auto value=core::projectManualTagRates(member,traffic);check(bool(value),"valid target projection failed");return *value;
}
void unknown(const core::ManualTagRate& value,core::ManualTagRateState state) {
  check(!value.downloadRate && !value.uploadRate && value.state==state,"unknown rate became numeric or lost reason");
}
}
int main() {
  using State=core::ManualTagRateState;
  try {
    const auto run=[](const char* name,auto body){body();++cases;std::cout<<"case "<<name<<'\n';};
    run("join-seals-names-uids-both-hash-sizes-and-native-membership",[]{
      const auto member=joined();check(member->session()==7 && member->libraryRevision()==11 && member->tagRevision()==13,"join binding changed");
      check(member->manualTypePresent() && member->rows().size()==7,"selected manual rows missing");
      const auto& alpha=member->rows()[0];check(alpha.uid==12884901889LL && alpha.rawName=="Alpha" && alpha.membershipKnown,"manual identity lost");
      check(alpha.resolvedCount==1 && alpha.unresolvedCount==0 && alpha.resolvedHashes[0]==hash(20),"20-byte native join incorrect");
      check(member->rows()[1].resolvedHashes[0]==hash(32) && member->rows()[2].resolvedCount==2,"32-byte/shared join incorrect");
      check(member->rows()[4].unresolvedCount==1 && member->rows()[4].resolvedCount==0,"saved declared foreign hash became resolved");
      check(member->rows()[6].unresolvedCount==1 && !member->rows()[5].membershipKnown,"malformed/absent member provenance lost");
    });
    run("raw-ownership-and-edited-projection-cannot-forge-join",[]{
      std::string raw=fixture();auto parsed=saved(raw);parsed.tags[0].rawName="Forged";parsed.tags[0].uniqueHashes.clear();parsed.tags.clear();
      const std::array hashes{hash(20),hash(32)};auto result=core::resolveManualTagMembership(7,11,13,parsed,hashes);
      check(bool(result),"original raw-backed join rejected");raw.assign("de");parsed.source.reset();
      check((*result)->rows().size()==7 && (*result)->rows()[0].rawName=="Alpha" && (*result)->rows()[0].resolvedCount==1,"mutable projection or caller bytes leaked into join");
    });
    run("absent-vs-empty-manual-root-remain-distinct",[]{
      const auto absent=joined("de"),empty=joined(dict({{"3","de"}}));
      check(!absent->manualTypePresent() && empty->manualTypePresent() && absent->rows().empty() && empty->rows().empty(),"missing root synthesized an empty present root");
    });
    run("full-owner-coverage-projects-actual-combined-rates-not-payload-or-session",[]{
      const auto member=joined();const auto value=rates(*member,active());
      check(value[0]==core::ManualTagRate{101,23,State::ObservedActive},"combined rates not copied exactly");
      check(value[2]==value[0],"overlapping manual tags should independently contain same target");
      check(value[1]==core::ManualTagRate{0,0,State::ObservedOtherTagZero},"covered other target not observed zero");
      unknown(value[4],State::UnresolvedMembership);unknown(value[5],State::UnresolvedMembership);unknown(value[6],State::UnresolvedMembership);
    });
    run("partial-owner-coverage-never-infers-other-or-multi-member-zero",[]{
      const auto value=rates(*joined(),active(false));
      check(value[0].downloadRate==101 && value[0].uploadRate==23,"single-member actual target lost");
      unknown(value[1],State::Unknown);unknown(value[2],State::Unknown);unknown(value[3],State::Unknown);
    });
    run("explicit-idle-authority-is-required-for-observed-zero",[]{
      const auto member=joined();core::ManualTagTraffic idle{core::ManualTagTrafficState::Idle,std::nullopt,false,true};
      const auto partial=rates(*member,idle);unknown(partial[0],State::Unknown);unknown(partial[3],State::Unknown);
      idle.completeOwnedCoverage=true;const auto covered=rates(*member,idle);
      check(covered[0]==core::ManualTagRate{0,0,State::ObservedIdle} && covered[3]==covered[0],"explicit idle zero missing");
      unknown(covered[4],State::UnresolvedMembership);
    });
    run("unknown-observation-never-becomes-zero",[]{
      const auto value=rates(*joined(),{});unknown(value[0],State::Unknown);unknown(value[1],State::Unknown);unknown(value[4],State::UnresolvedMembership);
    });
    run("last-observed-explicit-sample-retains-values-without-finality",[]{
      const auto value=rates(*joined(),active(true,false));
      check(value[0]==core::ManualTagRate{101,23,State::LastObserved} && value[1]==core::ManualTagRate{0,0,State::LastObserved},"last observed facts changed");
      unknown(value[4],State::UnresolvedMembership);
    });
    run("actual-zero-and-int64-maximum-copy-without-summing-overflow",[]{
      auto input=active();input.target->downloadRate=std::numeric_limits<std::int64_t>::max();input.target->uploadRate=0;
      input.target->payloadDownloadRate=0;input.target->payloadUploadRate=0;
      const auto value=rates(*joined(),input);check(value[0].downloadRate==std::numeric_limits<std::int64_t>::max() && value[0].uploadRate==0,"exact rate extreme lost");
      check(value[2]==value[0],"overlap caused summing/overflow");
    });
    run("invalid-traffic-and-foreign-target-are-rejected-atomically",[]{
      const auto member=joined();auto input=active();input.target->downloadRate=-1;
      check(!core::projectManualTagRates(*member,input),"negative target rate accepted");input=active();input.target->payloadUploadRate=24;
      check(!core::projectManualTagRates(*member,input),"payload greater than combined accepted");input=active();
      auto foreign=core::TorrentHash::parse(std::string(20,'\x7f'));check(bool(foreign),"foreign hash fixture failed");input.target->hash=*foreign;
      check(!core::projectManualTagRates(*member,input),"target not in actual library accepted");
      input={core::ManualTagTrafficState::Unavailable,std::nullopt,true,true};check(!core::validManualTagTraffic(input),"unavailable claimed coverage");
      input=active();input.state=core::ManualTagTrafficState::Idle;check(!core::validManualTagTraffic(input),"idle with target accepted");
      input={};input.state=static_cast<core::ManualTagTrafficState>(99);check(!core::validManualTagTraffic(input),"invalid traffic enum accepted");
    });
    run("ambiguous-uid-and-unresolved-members-never-publish-rates",[]{
      const auto member=joined(dict({{"3",dict({{"01",dict({{"o",list({bytes(A)})}})},{"1",dict({{"o",list({bytes(A)})}})}})}}));
      check(member->rows().size()==2 && member->rows()[0].ambiguousIdentity && member->rows()[1].ambiguousIdentity,"UID alias was silently normalized");
      for(const auto& value:rates(*member,active())){unknown(value,State::AmbiguousIdentity);}
    });
    run("name-bytes-localization-key-and-malformed-membership-are-not-coerced",[]{
      const auto member=joined(dict({{"3",dict({{"1",dict({{"n",bytes(std::string("<b>\0x",5))},{"o","i7e"}})},
          {"2",dict({{"n",bytes("tag.example")},{"o",list({bytes(A),bytes(A)})}})}})}}));
      check(member->rows()[0].rawName==std::string("<b>\0x",5) && !member->rows()[0].membershipKnown,"binary name or wrong member type coerced");
      check(member->rows()[1].nameMayRequireLocalization && member->rows()[1].resolvedCount==1,"name key/duplicate canonical member semantics changed");
    });
    run("invalid-source-library-and-fixed-cap-fail-before-join",[]{
      auto parsed=saved(fixture());const std::array hashes{hash(20),hash(20)};
      auto result=core::resolveManualTagMembership(7,11,13,parsed,hashes);check(!result && result.error()==core::ManualTagError::InvalidLibrary,"duplicate library inventory accepted");
      const std::array invalid{core::TorrentHash{}};result=core::resolveManualTagMembership(7,11,13,parsed,invalid);
      check(!result && result.error()==core::ManualTagError::InvalidLibrary,"invalid library hash accepted");
      std::vector<core::TorrentHash> tooMany(core::MaximumManualTagLibraryHashes+1);result=core::resolveManualTagMembership(7,11,13,parsed,tooMany);
      check(!result && result.error()==core::ManualTagError::Limit,"library cap ignored");
      for(const auto id:std::array<std::uint64_t,3>{0,11,13}) {
        result=core::resolveManualTagMembership(id==0?0:7,id==11?0:11,id==13?0:13,parsed,{});check(!result,"zero binding identity accepted");
      }
      parsed.source.reset();check(!core::resolveManualTagMembership(7,11,13,parsed,{}),"source-less public projection accepted");
    });
    run("hidden-manual-tags-follow-original-statistics-preference",[]{
      const auto member=joined(dict({{"3",dict({
        {"1",dict({{"n",bytes("Hidden")},{"o",list({bytes(A)})},{"c",dict({{"vis","i0e"}})}})},
        {"2",dict({{"n",bytes("Shown")},{"o",list({bytes(A)})},{"c",dict({{"vis","i1e"}})}})},
        {"3",dict({{"o","le"},{"c",dict({{"vis","i4294967297e"}})}})},
        {"4",dict({{"o","le"},{"c",dict({{"vis",bytes("bad")}})}})}})}}));
      check(member->rows().size()==2 && member->rows()[0].rawName=="Shown","hidden or high-word visibility was shown");
      check(member->rows()[0].rawVisibility==1 && !member->rows()[0].visibilityDefaulted,"explicit visibility provenance lost");
      check(member->rows()[1].visibilityMalformed && member->rows()[1].visibilityDefaulted,"malformed visibility default lost");
      const auto value=rates(*member,active());check(value.size()==2 && value[0].downloadRate==101,"hidden tags affected rate row alignment");
    });
    std::cout<<"PASS "<<cases<<" manual-tag statistics cases; "<<assertions<<" assertions; generated raw membership and explicit target facts only\n";
    return 0;
  } catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

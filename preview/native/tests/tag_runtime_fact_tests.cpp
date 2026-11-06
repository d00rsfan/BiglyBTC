// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_runtime_facts.hpp"
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace a=biglybtc::app;
namespace c=biglybtc::core::tags::constraints;
namespace ui=biglybtc::ui;
namespace e=biglybtc::engine;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
biglybtc::core::TorrentHash hash(){
  auto h=biglybtc::core::TorrentHash::parse(std::string(20,static_cast<char>(0xAA)));check(bool(h),"Test hash");return *h;
}
struct Source {
  a::ConstraintFactStamp stamp{{71,{QString(40,'A'),17},91,71},29};
  std::shared_ptr<ui::LibraryPresentation> library=std::make_shared<ui::LibraryPresentation>();
  a::ConstraintFactInputs input;
  Source(){
    library->revision=17;ui::LibraryItem row;row.hash=QString(40,'A');row.bytes=1000;
    row.providerAdmission=91;row.providerGeneration=71;library->items.push_back(row);
    input.stamp=stamp;input.library=library;input.managed.emplace();auto& m=*input.managed;
    m.hash=hash();m.admission=91;m.retained=true;m.checked=true;m.bytes=1000;m.completed=333;
    m.phase=e::ManagedPhase::Downloading;m.cumulativeSent=2050;m.cumulativeReceived=12345;
    m.downloadRate=9999;m.uploadRate=8888;m.forceStart=true;
  }
  void files(bool skipped=false){
    auto f=std::make_shared<ui::FileDetails>();f->target=stamp.binding.target;
    f->items.push_back({0,QString::fromUtf8("root/\xF0\x9F\x8C\x8D.txt"),1000,0,skipped,false});
    input.details=ui::Details{stamp.binding.target,QString::fromUtf8("Original \xF0\x9F\x8C\x8D"),1000,f};
  }
};
c::Result<c::Value> run(const a::ConstraintFactSnapshot& source,const a::ConstraintFactStamp& current,std::u16string_view rule){
  auto expression=c::compile(rule);check(bool(expression),"Unexpected test compile refusal");
  return a::evaluateCurrentConstraint(*expression,current,source);
}
void yes(const a::ConstraintFactSnapshot& source,const a::ConstraintFactStamp& stamp,std::u16string_view rule){
  auto value=run(source,stamp,rule);check(bool(value),"Expected available original fact");
  auto truth=c::truth(*value);check(truth&&*truth,"Unexpected projected fact value");
}
void missing(const a::ConstraintFactSnapshot& source,const a::ConstraintFactStamp& stamp,std::u16string_view rule){
  auto value=run(source,stamp,rule);check(!value&&value.error().kind==c::ProblemKind::Unavailable,"Missing provider became a value");
  check(c::project(value,{},true,false,true).membership==c::Membership::Keep,"Missing fact removed membership");
}
void canonicalIdentitiesAndMissingProviders(){
  const auto first=a::constraintFactHash(QString::fromStdString(hash().hex()));
  check(first&&*first==hash(),"Canonical20-byte UI identity did not decode");
  const auto longRaw=biglybtc::core::TorrentHash::parse(std::string(32,static_cast<char>(0xDD)));
  check(bool(longRaw),"Raw32-byte fixture identity");
  const auto second=a::constraintFactHash(QString::fromStdString(longRaw->hex()));
  check(second&&*second==*longRaw,"Canonical32-byte UI identity did not decode");
  for(const auto& bad:{QString(40,'a'),QString(39,'A'),QString(41,'A'),QString(63,'D'),QString(65,'D'),QString(40,'G'),QString(32,'A')}){
    check(!a::constraintFactHash(bad),"Noncanonical, malformed or raw UI identity accepted");
  }
  for(unsigned kind=0;kind<5;++kind){
    Source s;
    if(kind==0){s.input.managed.reset();}
    if(kind==1){s.input.managed->admission=92;}
    if(kind==2){s.stamp.binding.providerGeneration=72;s.input.stamp=s.stamp;s.library->items[0].providerGeneration=72;}
    if(kind==3){s.input.managed->retained=false;}
    if(kind==4){s.stamp.binding.providerAdmission=s.stamp.binding.providerGeneration=0;s.input.stamp=s.stamp;s.library->items[0].providerAdmission=s.library->items[0].providerGeneration=0;}
    const auto refused=a::captureConstraintFacts(s.stamp,s.input);
    check(!refused&&refused.error().kind==c::ProblemKind::Unavailable,"Absent/stale advertised provider became available");
    const c::Result<c::Value> unavailable=std::unexpected(refused.error());
    for(const bool member:{false,true}){check(c::project(unavailable,{},true,false,member).membership==c::Membership::Keep,"Unavailable provider changed membership");}
  }
}
void identitiesAndOwningSnapshots(){
  Source s;s.files();auto out=a::captureConstraintFacts(s.stamp,s.input);check(bool(out),"Valid source refused");
  yes(*out,s.stamp,u"(size==1000)&&isDownloading()&&isForceStart()");
  yes(*out,s.stamp,u"(uploaded==2050)&&contains(name,\"Original\")");
  s.library->items[0].state="Paused";
  auto independent=a::captureConstraintFacts(s.stamp,s.input);check(bool(independent),"Localized state affected capture");
  yes(*independent,s.stamp,u"isDownloading()&&!isPaused()");
  // Output owns strings/scalars even after producer containers mutate.
  s.library->items[0].bytes=7000;s.input.details->name="Changed";s.input.managed->cumulativeSent=1;
  yes(*out,s.stamp,u"(size==1000)&&(uploaded==2050)&&contains(name,\"Original\")");
  for(int component=0;component<6;++component){
    auto stale=s.stamp;
    if(component==0){++stale.binding.session;}if(component==1){++stale.binding.target.generation;}
    if(component==2){stale.binding.target.hash=QString(40,'B');}if(component==3){++stale.binding.providerAdmission;}
    if(component==4){++stale.binding.providerGeneration;}if(component==5){++stale.sequence;}
    missing(*out,stale,u"size>0");
  }
  Source duplicate;duplicate.library->items.push_back(duplicate.library->items[0]);
  check(!a::captureConstraintFacts(duplicate.stamp,duplicate.input),"Duplicate selected row accepted");
  Source stale;stale.input.managed->admission=92;
  check(!a::captureConstraintFacts(stale.stamp,stale.input),"Previous provider admission accepted");
  stale.input.managed->admission=91;stale.library->items[0].providerGeneration=73;
  check(!a::captureConstraintFacts(stale.stamp,stale.input),"Previous provider generation accepted");
}
void originalPercentAndUnavailableRates(){
  Source s;s.files();auto out=a::captureConstraintFacts(s.stamp,s.input);check(bool(out),"Percent source");
  yes(*out,s.stamp,u"(remaining==667)&&(percent>33)&&(percent<34)");
  // Source returns Float(333/10.0f), not double33.3; compare the exact type too.
  const auto& projected=out->facts().values;
  const auto value=std::find_if(projected.begin(),projected.end(),[](const auto& f){return f.key=="percent";});
  check(value!=projected.end()&&std::get<float>(value->value)==333/10.0f,"Original Float percent widened/rounded");
  missing(*out,s.stamp,u"down_speed>0");missing(*out,s.stamp,u"up_speed>0");
  missing(*out,s.stamp,u"downloaded>0"); // Raw payload != total GOOD received.
  s.files(true);out=a::captureConstraintFacts(s.stamp,s.input);check(bool(out),"Skipped geometry");
  missing(*out,s.stamp,u"percent>0");missing(*out,s.stamp,u"remaining>=0");
  s.input.managed->selectedComplete=true;out=a::captureConstraintFacts(s.stamp,s.input);check(bool(out),"Checked selected completion");
  yes(*out,s.stamp,u"(percent==100)&&(remaining==0)&&isComplete()");
  s.input.managed->checked=false;out=a::captureConstraintFacts(s.stamp,s.input);check(bool(out),"Checking source");
  missing(*out,s.stamp,u"isComplete()");missing(*out,s.stamp,u"percent>0");
}
void queueRatioAndSourceState(){
  Source s;s.input.queue.emplace();s.input.queue->sequence=18;
  e::ExternalQueueRow row;row.binding={hash(),91};row.facts.token=91;row.facts.shareRatio=1501;
  row.facts.secondsDownloading=123;row.facts.secondsSeeding=7;s.input.queue->rows.push_back(row);
  const auto state=biglybtc::core::Document::parse("d10:attributesd8:networksl6:Public3:I2Pee4:infod7:privatei4294967296eee");
  check(bool(state),"Test original document");const auto before=state->serialize();
  s.input.original=a::ConstraintOriginalState{s.stamp,*state};
  auto out=a::captureConstraintFacts(s.stamp,s.input);check(bool(out),"Queue/source snapshot");
  yes(*out,s.stamp,u"hasNet(\"Public\")&&hasNet(\"I2P\")&&!isPrivate()");
  yes(*out,s.stamp,u"(downloadingfor==123)&&(seedingfor==7)");
  const auto& projected=out->facts().values;
  const auto ratio=std::find_if(projected.begin(),projected.end(),[](const auto& f){return f.key=="shareratio";});
  check(ratio!=projected.end()&&std::get<float>(ratio->value)==1501/1000.0f,"Source Float ratio differs");
  check(state->serialize()==before,"Original source bytes changed");
  s.input.queue->rows[0].facts.shareRatio=-1;out=a::captureConstraintFacts(s.stamp,s.input);check(bool(out),"Infinite source ratio");
  yes(*out,s.stamp,u"shareratio==max32");
  s.input.queue->rows[0].facts.shareRatio.reset();out=a::captureConstraintFacts(s.stamp,s.input);check(bool(out),"Missing ratio source");
  missing(*out,s.stamp,u"shareratio>0");
  s.input.queue->rows[0].binding.admission=92;check(!a::captureConstraintFacts(s.stamp,s.input),"Stale queue row accepted");
}
void inactiveCachedDndAndPrivateOwner(){
  Source s;s.stamp.binding.providerAdmission=0;s.stamp.binding.providerGeneration=0;s.input.stamp=s.stamp;s.input.managed.reset();
  auto& row=s.library->items[0];row.providerAdmission=0;row.providerGeneration=0;row.sent=70;
  row.savedSelected=ui::SavedSelectedValues{700,300,571,0,0};
  auto out=a::captureConstraintFacts(s.stamp,s.input);check(bool(out),"Saved original DND snapshot");
  const auto& projected=out->facts().values;
  const auto percent=std::find_if(projected.begin(),projected.end(),[](const auto& f){return f.key=="percent";});
  check(percent!=projected.end()&&std::get<float>(percent->value)==571/10.0f,"Saved DND Float differs");
  yes(*out,s.stamp,u"(remaining==300)&&(uploaded==70)");missing(*out,s.stamp,u"isPaused()");
  Source p;p.stamp.binding.providerGeneration=72;p.input.stamp=p.stamp;p.library->items[0].providerGeneration=72;p.input.managed.reset();p.input.i2p.emplace();auto& owner=*p.input.i2p;owner.hash=hash();
  owner.value.phase=a::OwnedI2pService::Phase::Active;owner.value.current=true;
  owner.value.admission=91;owner.value.generation=72;owner.value.checked=true;owner.value.selectedComplete=true;owner.value.cumulativeSent=88;
  out=a::captureConstraintFacts(p.stamp,p.input);check(bool(out),"Real private owner DTO binding");
  yes(*out,p.stamp,u"(uploaded==88)&&(percent==100)&&isComplete()");
  missing(*out,p.stamp,u"isPaused()");missing(*out,p.stamp,u"downloaded>=0");
  owner.value.generation=73;check(!a::captureConstraintFacts(p.stamp,p.input),"Foreign private group accepted");
}
void actualManualTagWitnessesAgeAndStaleSources(){
  namespace s=biglybtc::storage;namespace fs=std::filesystem;
  std::string temporary="/tmp/biglybtc-tag-facts-XXXXXX";check(::mkdtemp(temporary.data()),"Create actual tag fact profile");
  const fs::path root=temporary;const auto cleanup=[&]{std::error_code error;fs::remove_all(root,error);};
  const auto bytes=[](std::string_view value){return std::to_string(value.size())+':'+std::string(value);};
  const auto dictionary=[&](std::initializer_list<std::pair<std::string,std::string>> values){std::string out="d";for(const auto& [key,value]:values){out+=bytes(key)+value;}return out+'e';};
  const std::string member="VKVKVKVKVKVKVKVKVKVKVKVKVKVKVKVK"; // Actual uppercase Base32 of20 AA bytes.
  const auto source=[&](bool blocked,bool unknownMember=false){
    const auto stable=dictionary({{"n",bytes("Manual")},{"c",dictionary({{"gr",bytes("Group")},{"col.rgb",bytes("+01,2,3,,")}})},{"o","l"+bytes(member)+"e"},{"p","ld1:ai100eee"}});
    const auto own=dictionary({{"n",bytes("Rule")},{"c",dictionary({{"pp.constraint","l4:truee"},{"col.rgb",bytes("0,140,66")}})},{"o","l"+bytes(unknownMember?"unknown-alias":member)+"e"},{"p","ld1:ai100eee"}});
    const auto alias=blocked?dictionary({{"n",bytes("Manual")},{"c",dictionary({{"pp.constraint","l4:truee"}})}}):dictionary({{"n",bytes("tag.localized")}});
    return dictionary({{"3",dictionary({{"1",stable},{"2",own},{"3",alias}})},{"future-root",bytes("keep")}});
  };
  const auto write=[&](const std::string& value){std::ofstream file(root/"tag.config",std::ios::binary|std::ios::trunc);file.write(value.data(),static_cast<std::streamsize>(value.size()));file.close();check(bool(file)&&::chmod((root/"tag.config").c_str(),0600)==0,"Write actual tag facts source");};
  try{
    write(source(false));{auto lease=s::ProfileLease::acquire(root);check(bool(lease),"Actual profile OFD");auto store=s::ManualTagStore::open(*lease);check(bool(store),"Actual ManualTagStore");auto snapshot=(*store)->snapshot();check(bool(snapshot),"Actual owning tag snapshot");
    Source current;auto base=a::captureConstraintFacts(current.stamp,current.input);check(bool(base),"Existing owning DTO snapshot (no provider authority)");
    auto captured=a::captureConstraintTagMembership(*base,current.stamp,**store,*lease,*snapshot,2,current.library,150100);check(bool(captured),"Actual owning tag witness capture");
    const auto runTags=[&](std::u16string_view text,const auto& facts,const auto& tags){auto expression=c::compile(text);check(bool(expression),"Compile tag witness expression");return a::evaluateCurrentConstraint(*expression,current.stamp,facts,**store,*lease,tags);};
    auto good=runTags(u"hasTag(\"Manual\")&&hasTagGroup(\"Group\")&&(tag_age==50)",*captured,*snapshot);check(good&&c::truth(*good)&&*c::truth(*good),"Actual manual positive/group/ownage facts differ");
    // Same owning source with deterministic second-boundary/backward samples.
    for(const auto& [wall,expected]:{std::pair<std::int64_t,std::int64_t>{149999,49},{150000,50},{99999,0},{100000,0},{-100000,0}}){
      auto sampled=a::captureConstraintTagMembership(*base,current.stamp,**store,*lease,*snapshot,2,current.library,wall);check(bool(sampled),"Actual source age sample refused");
      const auto test=std::u16string(u"tag_age==")+QString::number(static_cast<qlonglong>(expected)).toStdU16String();
      auto value=runTags(test,*sampled,*snapshot);check(value&&c::truth(*value)&&*c::truth(*value),"Actual own age boundary/backward wall differs");
    }
    for(const auto* text:{u"!hasTag(\"Absent\")",u"!hasTagGroup(\"Absent\")",u"hasTagAge(\"Manual\")>=0",u"countTag(\"Manual\")>=0",u"tagPosition(\"Manual\")>=0",u"contains(tag_names,\"Manual\")",u"hasTag(\"Rule\")",u"hasTag(\"tag.localized\")"}){
      auto kept=runTags(text,*captured,*snapshot);check(!kept&&kept.error().kind==c::ProblemKind::Unavailable&&c::project(kept,{},true,false,true).membership==c::Membership::Keep,"Incomplete registry/dependency became false or removal");
    }
    auto expression=c::compile(u"true");check(expression&&!a::evaluateCurrentConstraint(*expression,current.stamp,*captured),"Tag-bound facts used without current tag generation");
    const auto beforeReopen=*captured;const auto sourceBeforeReopen=*snapshot;
    check(bool((*store)->close()),"Close first exact writer");store->reset();store=s::ManualTagStore::open(*lease);check(bool(store),"Explicit equal-source writer reopen");snapshot=(*store)->snapshot();check(bool(snapshot)&&snapshot->generation()==sourceBeforeReopen.generation()&&snapshot->sameSource(sourceBeforeReopen),"Reopen must have equal bytes/metadata/counter for lineage regression");
    check(!runTags(u"hasTag(\"Manual\")",beforeReopen,*snapshot),"Reopened equal-generation writer resurrected old captured provider scope");
    captured=a::captureConstraintTagMembership(*base,current.stamp,**store,*lease,*snapshot,2,current.library,150100);check(bool(captured),"Fresh source-bound recapture after explicit reopen");
    auto fresh=(*store)->reload();check(bool(fresh)&&snapshot->sameSource(*fresh),"Same-source explicit reload fixture");check(!runTags(u"hasTag(\"Manual\")",*captured,*fresh),"Old tag generation survived a reload");
    auto wrong=current.stamp;++wrong.sequence;check(!a::captureConstraintTagMembership(*base,wrong,**store,*lease,*fresh,2,current.library,150100),"Stale fact turn accepted a new tag scope");
    // Existing ordinary source becomes unsafe only through actual selected
    // group settings; no caller registry-complete flag or fabricated packet.
    auto unsafe=biglybtc::core::Document::parse(source(false));check(bool(unsafe),"Unsafe group source parse");
    auto manual=biglybtc::core::Document::parse(biglybtc::core::encodeValue(*unsafe->find("3")).value());check(bool(manual)&&manual->replaceValue("c","d8:I5ZG65LQd1:xi1eee"),"Unsafe selected group settings");
    check(bool(unsafe->replaceValue("3",manual->serialize())),"Unsafe owning group source edit");write(unsafe->serialize());fresh=(*store)->reload();check(bool(fresh),"Reload actual unsafe group source");
    auto unsafeFacts=a::captureConstraintTagMembership(*base,current.stamp,**store,*lease,*fresh,2,current.library,150100);check(bool(unsafeFacts),"Capture current unsafe group scope");
    for(const auto* text:{u"hasTag(\"Manual\")",u"hasTagGroup(\"Group\")",u"!hasTag(\"Manual\")"}){auto value=runTags(text,*unsafeFacts,*fresh);check(!value&&value.error().kind==c::ProblemKind::Unavailable&&c::project(value,{},true,false,true).membership==c::Membership::Keep,"Unsafe group supplied a positive or negative membership effect");}
    auto unsafeOwn=a::captureConstraintTagMembership(*base,current.stamp,**store,*lease,*fresh,1,current.library,150100);check(bool(unsafeOwn)&&!runTags(u"tag_age>=0",*unsafeOwn,*fresh),"Unsafe own group retained an actionable tag-age provider");
    write(source(true));fresh=(*store)->reload();check(bool(fresh),"Actual active alias source");captured=a::captureConstraintTagMembership(*base,current.stamp,**store,*lease,*fresh,2,current.library,150100);check(bool(captured),"Capture owning blocked dependency");auto blocked=runTags(u"hasTag(\"Manual\")",*captured,*fresh);check(!blocked&&blocked.error().kind==c::ProblemKind::Unavailable,"Active alias dependency was silently settled");
    write(source(false,true));fresh=(*store)->reload();check(bool(fresh),"Actual unknown member source");captured=a::captureConstraintTagMembership(*base,current.stamp,**store,*lease,*fresh,2,current.library,150100);check(bool(captured),"Capture unknown member snapshot");check(!runTags(u"tag_age>=0",*captured,*fresh),"Unknown member alias fabricated own age0");
    const auto foreignPath=root/"foreign";check(fs::create_directory(foreignPath)&&::chmod(foreignPath.c_str(),0700)==0,"Create foreign actual profile");
    {std::ofstream file(foreignPath/"tag.config",std::ios::binary);const auto value=source(false,true);file.write(value.data(),static_cast<std::streamsize>(value.size()));file.close();check(bool(file)&&::chmod((foreignPath/"tag.config").c_str(),0600)==0,"Write foreign owning preimage");}
    auto foreignLease=s::ProfileLease::acquire(foreignPath);check(bool(foreignLease),"Foreign real OFD");auto foreignStore=s::ManualTagStore::open(*foreignLease);check(bool(foreignStore),"Foreign real writer");auto foreign=(*foreignStore)->snapshot();check(bool(foreign),"Foreign equal-byte preimage");
    check(!a::captureConstraintTagMembership(*base,current.stamp,**store,*lease,*foreign,2,current.library,150100),"Equal bytes crossed current cached snapshot lineage");
    check(!a::captureConstraintTagMembership(*base,current.stamp,**foreignStore,*lease,*foreign,2,current.library,150100),"Foreign writer crossed actual OFD lineage");
    check(bool((*store)->close()),"Release actual tag store");check(!(*store)->matchesSnapshotLineage(*fresh,*lease),"Closed writer lineage remained current");store->reset();
    auto reopened=s::ManualTagStore::open(*lease);check(bool(reopened),"Explicit actual writer reopen");fresh=(*reopened)->snapshot();check(bool(fresh),"Reopened actual source");
    check(expression&&!a::evaluateCurrentConstraint(*expression,current.stamp,*captured,**reopened,*lease,*fresh),"Identical reopened writer resurrected an old captured fact scope");
    check(bool((*reopened)->close())&&bool((*foreignStore)->close()),"Close actual read stores");}cleanup();
  }catch(...){cleanup();throw;}
}
} // namespace
int main(){
  try{canonicalIdentitiesAndMissingProviders();identitiesAndOwningSnapshots();originalPercentAndUnavailableRates();queueRatioAndSourceState();inactiveCachedDndAndPrivateOwner();actualManualTagWitnessesAgeAndStaleSources();}
  catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;
}

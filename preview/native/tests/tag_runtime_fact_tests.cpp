// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_runtime_facts.hpp"
#include <algorithm>
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
} // namespace
int main(){
  try{canonicalIdentitiesAndMissingProviders();identitiesAndOwningSnapshots();originalPercentAndUnavailableRates();queueRatioAndSourceState();inactiveCachedDndAndPrivateOwner();}
  catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;
}

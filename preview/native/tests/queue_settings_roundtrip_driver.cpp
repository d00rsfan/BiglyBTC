#include "application_settings.hpp"
#include "biglybtc/queue_policy.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace biglybtc;
using Id=core::SettingId;
void require(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
template<class T>T take(std::expected<T,storage::Error> value){
  if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);
}
core::QueuePolicy policy(const core::Document& document){
  auto value=core::readQueuePolicy(&document);
  if(!value){throw std::runtime_error("Invalid original queue policy: "+value.error().key);}return *value;
}
const core::SettingValue& stored(const storage::ApplicationSettingsSnapshot& snapshot,Id id){
  const auto& field=snapshot.fields.at(static_cast<std::size_t>(id));
  require(field.state==storage::SettingState::Stored&&field.value.has_value(),"Expected stored original queue field");return *field.value;
}
std::string opaque(const core::Document& document){
  const auto* value=document.find("native.queue.opaque");
  require(value&&value->type==core::Node::Type::List,"Original opaque list missing");
  return document.originalBytes().substr(value->begin,value->end-value->begin);
}
void unchanged(const storage::ApplicationSettingsSnapshot& snapshot){
  require(snapshot.source&&snapshot.recoverySource==storage::RecoverySource::Primary,"Expected original primary snapshot");
  const auto* ui=snapshot.source->find("ui");require(ui&&ui->type==core::Node::Type::Bytes&&ui->bytes=="az2","Original UI mode changed");
  require(opaque(*snapshot.source)=="li7e11:opaque–πe","Original opaque bytes changed");
  require(!snapshot.source->find("StartStopManager_bFirstPriority_ignore0Peer"),"Conditional original default was materialized");
  const auto& zero=snapshot.fields.at(static_cast<std::size_t>(Id::FirstPriorityIgnoreZeroPeers));
  require(zero.state==storage::SettingState::Defaulted&&zero.value&&std::get_if<bool>(&*zero.value)
      &&!std::get<bool>(*zero.value),"Conditional az2 first-priority default not false");
  require(!policy(*snapshot.source).firstPriorityIgnoreZeroPeers,"Conditional policy default not false");
}
void applied(const core::QueuePolicy& p){
  require(p.rankMode==core::QueueRankMode::Timed&&p.ignoreShareRatio==1250
      &&p.firstPriorityShareRatio==750&&p.minimumSeedSeconds==45&&p.retainForceStartOnComplete,
      "Native sparse queue changes not interpreted");
}
} // namespace
int main(int argc,char** argv){try{
  require(argc==3,"Usage: queue-settings-roundtrip-driver PROFILE apply|check");
  const std::string_view phase(argv[2]);require(phase=="apply"||phase=="check","Invalid handoff phase");
  require(core::applicationSettingsCatalog().size()==101,"Expected current101-field application settings catalog");
  const auto profile=std::filesystem::canonical(argv[1]);
  auto lease=take(storage::ProfileLease::acquire(profile));
  const auto before=take(storage::readApplicationSettings(lease));unchanged(before);
  const auto originalOpaque=opaque(*before.source);
  if(phase=="apply"){
    const auto initial=policy(*before.source);
    require(initial.rankMode==core::QueueRankMode::SeedPeerRatio&&initial.ignoreShareRatio==500
        &&initial.firstPriorityShareRatio==500&&initial.minimumSeedSeconds==300&&!initial.retainForceStartOnComplete,
        "Original Java queue inputs not interpreted");
    require(std::get<std::string>(stored(before,Id::IgnoreShareRatio))=="0.5","Original Float was not stored as bytes");
    const std::array changes{
      core::SettingChange{Id::RankMode,std::int64_t{3}},
      core::SettingChange{Id::IgnoreShareRatio,std::string("1.25")},
      core::SettingChange{Id::FirstPriorityShareRatio,std::int64_t{750}},
      core::SettingChange{Id::MinimumSeedSeconds,std::int64_t{45}},
      core::SettingChange{Id::RetainForceStartOnComplete,true}
    };
    const auto prepared=take(storage::prepareApplicationSettings(before,changes));
    require(prepared.changed(),"Expected actual sparse queue edits");applied(policy(prepared.document()));
    const auto saved=take(storage::saveApplicationSettings(lease,before,prepared));unchanged(saved);applied(policy(*saved.source));
    require(opaque(*saved.source)==originalOpaque,"Native save changed unknown original bytes");
    const auto actual=take(storage::readApplicationSettings(lease));unchanged(actual);applied(policy(*actual.source));
    require(std::get<std::string>(stored(actual,Id::IgnoreShareRatio))=="1.25","Native decimal bytes not saved");
  }else{
    const auto actual=policy(*before.source);
    require(actual.rankMode==core::QueueRankMode::SeedCount&&actual.ignoreShareRatio==2750
        &&actual.firstPriorityShareRatio==900,"Original Java resave not interpreted by native policy");
    require(actual.minimumSeedSeconds==45&&actual.retainForceStartOnComplete,"Native unchanged settings lost in original resave");
    require(std::get<std::int64_t>(stored(before,Id::RankMode))==2
        &&std::get<std::int64_t>(stored(before,Id::FirstPriorityShareRatio))==900
        &&std::get<std::string>(stored(before,Id::IgnoreShareRatio))=="2.75","Original typed queue values not reloaded");
  }
  std::cout<<"QUEUE_SETTINGS_NATIVE_"<<phase<<"_PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "owner.hpp"
#include "owner_runtime_guard.hpp"
#include "source_permit.hpp"
#include "metadata_disk_router.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#include <QDateTime>
#include <QPointer>
#include <QThread>
#include <QMetaObject>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <limits>

namespace biglybtc::services::i2p::provider {
struct OwnedTorrent::Impl {
  OwnedTorrent* q=nullptr;
  std::shared_ptr<ProfileSource> source;
  engine::detail::ManagedStorage storage;
  std::optional<ClientIdentity> identity;
  std::shared_ptr<void> providerPin;
  std::shared_ptr<engine::detail::MetadataDiskPolicy> disks=std::make_shared<engine::detail::MetadataDiskPolicy>();
  std::unique_ptr<i2cp::InitialChannel> channel;
  std::unique_ptr<Bridge> bridge;
  std::unique_ptr<SourcePermit> permit;
  std::unique_ptr<libtorrent::session> session;
  std::optional<libtorrent::session_proxy> joining;
  libtorrent::torrent_handle handle;
  Options options;Phase phase=Phase::Prepared;
  std::uint64_t admission=0,providerGeneration=0;bool notifyQueued=false,startRequested=false,bridgeRequested=false;
  Impl(std::shared_ptr<ProfileSource> s,engine::detail::ManagedStorage m,ClientIdentity key,std::shared_ptr<void> pin)
      :source(std::move(s)),storage(std::move(m)),identity(std::move(key)),providerPin(std::move(pin)){}
  bool onThread()const{return QThread::currentThread()==q->thread();}
  void notify(){
    if(notifyQueued){return;}notifyQueued=true;bool ok=false;
    try{const QPointer<OwnedTorrent> owner(q);ok=QMetaObject::invokeMethod(q,[owner]{if(!owner){return;}owner->impl_->notifyQueued=false;emit owner->changed();},Qt::QueuedConnection);}catch(...){ok=false;}
    if(!ok){notifyQueued=false;close();}
  }
  void close(){
    if(phase==Phase::Withdrawn){return;}phase=Phase::Withdrawn;source->revoke();identity.reset();
    if(bridge){bridge->close();}if(channel){channel->close();}
    if(permit){permit->stop();}
    if(session){
      if(providerGeneration){static_cast<void>(libtorrent::biglybtc_withdraw_private_i2p_owner_v1(session.get(),providerGeneration));}
      joining.emplace(session->abort());session.reset();handle={};
    }
    notify();
  }
  void join(){
    if(phase!=Phase::Withdrawn){return;}
    // Actual provider executor/disk join precedes releasing mapped payload
    // owners and SAME-OFD source authority. No successful checkpoint invented.
    joining.reset();if(bridge){bridge->joinSource();}if(permit){permit->join();}
  }
  bool install(void* opaque){
    namespace lt=libtorrent;auto& policy=*static_cast<lt::BiglybtcPrivateI2pOwnerPolicyV1*>(opaque);
    if(phase!=Phase::Starting||!session||!source->prepared()||!source->policy().matches(*source->lease(),source->hash())){return false;}
    const auto& full=storage.identity().full;
    if(full.v1){policy.hashes|=1;std::transform(full.v1->begin(),full.v1->end(),policy.torrent_v1.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}
    if(full.v2){policy.hashes|=2;std::transform(full.v2->begin(),full.v2->end(),policy.torrent_v2.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}
    if(libtorrent::biglybtc_configure_private_i2p_owner_v1(session.get(),&policy)!=0){return false;}providerGeneration=policy.generation;
    lt::settings_pack network;network.set_str(lt::settings_pack::i2p_hostname,"127.0.0.1");network.set_int(lt::settings_pack::i2p_port,policy.bridge_port);session->apply_settings(network);
    auto params=storage.parameters();
    if(auto mapped=storage.mappedFiles()){
      const auto virtualPath=(std::filesystem::path(params.save_path)/".BiglyBTC-original-storage"/(source->hash().hex()+"-owned-i2p-"+std::to_string(admission))).lexically_normal();
      auto registered=disks->registerOriginalPath(virtualPath,std::move(mapped));if(!registered){return false;}params.save_path=virtualPath.string();
    }
    lt::error_code error;handle=session->add_torrent(params,error);if(error||!handle.is_valid()){return false;}
    // No seed_mode, resume-piece assertion, synthetic piece or queue epoch.
    // Real LT checking authenticates existing data through qualified storage.
    handle.resume();phase=Phase::Active;notify();return true;
  }
  void requestStartup(){
    if((phase!=Phase::Starting&&phase!=Phase::Active)||!permit){return;}if(permit->phase()==SourcePermit::Phase::Idle){permit->request();}
  }
  void startup(){
    if((phase!=Phase::Starting&&phase!=Phase::Active)||!permit){return;}
    auto revision=permit->take();if(!revision){if(permit->phase()==SourcePermit::Phase::Failed||permit->phase()==SourcePermit::Phase::Stopped){close();}return;}
    struct Ack {SourcePermit& permit;~Ack(){permit.acknowledge();}} ack{*permit};
    try{
      if(!source->revisionCurrent(*revision)){close();return;}
      if(!channel->resumeAuthorizedEffects()){close();return;}
      if(startRequested){
        startRequested=false;const auto now=QDateTime::currentMSecsSinceEpoch();
        if(!identity||now<=0||!channel->createLeaseSession(std::move(*identity),options.session,static_cast<std::uint64_t>(now))){close();return;}identity.reset();
      }
      if(bridgeRequested){
        if(!channel->ownedTransportIdle()){return;}
        bridgeRequested=false;auto actual=channel->ownedLeaseDestination();if(!actual||actual->wire!=source->destination().wire){close();return;}
        bridge=std::make_unique<Bridge>(source,*channel,q);
        QObject::connect(bridge.get(),&Bridge::changed,q,[this]{
          if(!bridge||phase==Phase::Withdrawn){return;}const auto observed=bridge->observation();
          if(observed.phase==Bridge::Phase::Withdrawn){close();return;}
          if(observed.phase==Bridge::Phase::Ready&&phase==Phase::Starting){if(!bridge->installProvider([this](void* policy){return install(policy);})){close();}}
        });
        if(!bridge->begin(options.ports,admission)){close();return;}
      }
    }catch(...){close();}
  }
};
OwnedTorrent::OwnedTorrent(std::unique_ptr<Impl> impl):impl_(std::move(impl)){
  auto& s=*impl_;s.q=this;s.channel=std::make_unique<i2cp::InitialChannel>(this);
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::sourceEffectRequested,this,[this]{impl_->requestStartup();});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::dateReceived,this,[this]{auto& s=*impl_;if(s.phase==Phase::Starting){s.startRequested=true;s.requestStartup();}});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::leaseSetSubmitted,this,[this]{auto& s=*impl_;if(s.phase==Phase::Starting&&!s.bridge){s.bridgeRequested=true;s.requestStartup();}});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::failed,this,[this]{impl_->close();});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::closed,this,[this]{if(impl_->phase!=Phase::Prepared){impl_->close();}});
}
OwnedTorrent::~OwnedTorrent(){impl_->close();impl_->join();}
std::expected<std::unique_ptr<OwnedTorrent>,storage::Error> OwnedTorrent::prepare(const storage::ProfileLease& lease,const core::TorrentHash& hash,std::string directory){
  try{
    auto pin=privateOwnerRuntimePin();if(!pin){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Separately approved private I2P provider image required"});}
    auto source=ProfileSource::open(lease,hash,std::move(directory));if(!source){return std::unexpected(source.error());}
    auto preparedStorage=(*source)->prepareStorage();if(!preparedStorage){return std::unexpected(preparedStorage.error());}
    auto identity=(*source)->takeIdentity();if(!identity){return std::unexpected(identity.error());}
    auto impl=std::make_unique<Impl>(*source,std::move(*preparedStorage),std::move(*identity),std::move(pin));
    if(RAND_bytes(reinterpret_cast<unsigned char*>(&impl->admission),sizeof(impl->admission))!=1||!impl->admission){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Owned admission entropy unavailable"});}
    libtorrent::session_params params((*source)->policy().settings());params.disk_io_constructor=engine::detail::metadataDiskConstructor(impl->disks);
    impl->session=std::make_unique<libtorrent::session>(std::move(params));
    return std::unique_ptr<OwnedTorrent>(new OwnedTorrent(std::move(impl)));
  }catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot prepare dedicated I2P owner"});}
}
bool OwnedTorrent::begin(const Options& options){
  auto& s=*impl_;if(!s.onThread()||s.phase!=Phase::Prepared||!options.ports.peer||!options.ports.localPeer){return false;}
  s.options=options;s.phase=Phase::Starting;
  try{s.permit=std::make_unique<SourcePermit>(s.source,this);if(!s.channel->bindEffectAuthority(s.source->effectAuthority())){s.close();return false;}if(!s.channel->begin(options.router)){s.close();return false;}s.notify();return true;}catch(...){s.close();return false;}
}
void OwnedTorrent::close(){if(impl_->onThread()){impl_->close();}}
void OwnedTorrent::join(){impl_->join();}
OwnedTorrent::Observation OwnedTorrent::observation()const{
  const auto& s=*impl_;Observation value{s.phase,s.source->destination().hash,s.admission,s.providerGeneration,false,0,0,0,0};
  if(s.bridge){const auto bridge=s.bridge->observation();value.bridgePort=bridge.port;value.rejectedClients=bridge.rejectedClients;value.bridgeSockets=bridge.sockets;}
  if(s.phase==Phase::Active&&s.handle.is_valid()){
    auto actual=s.handle.status();value.seeding=actual.is_seeding;value.verifiedBytes=actual.total_done;
  }return value;
}
bool OwnedTorrent::event(QEvent* event){
  if(event->type()==SourcePermit::wakeEvent()){
    auto& s=*impl_;if(s.permit){const auto phase=s.permit->phase();if(phase==SourcePermit::Phase::Ready||phase==SourcePermit::Phase::Failed||phase==SourcePermit::Phase::Stopped){s.startup();}else if(phase==SourcePermit::Phase::Idle&&(s.startRequested||(s.bridgeRequested&&s.channel->ownedTransportIdle())||s.channel->sourceEffectsPending())){s.requestStartup();}}return true;
  }return QObject::event(event);
}
}

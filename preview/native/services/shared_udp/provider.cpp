// SPDX-License-Identifier: GPL-2.0-or-later
#include "provider.hpp"
#include <libtorrent/biglybtc_public_udp.hpp>
#include <libtorrent/session_handle.hpp>
#include <libtorrent/version.hpp>
#include <QSocketNotifier>
#include <QThread>
#include <QScopeGuard>
#include <QTimer>
#include <QRandomGenerator>
#include "biglybtc/azureus_endpoint.hpp"
#include <openssl/evp.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <array>
#include <algorithm>
namespace biglybtc::services::shared_udp {
namespace {
namespace lt=libtorrent;
using Failure=azureus_udp::Failure;
using Status=lt::BiglybtcPublicUdpStatusV1;
using RawBinding=lt::BiglybtcPublicUdpBindingV1;
using RawPacket=lt::BiglybtcPublicUdpPacketV1;
std::int64_t ticks(Clock::time_point t=Clock::now())noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}
QHostAddress host(std::uint32_t family,const std::array<unsigned char,16>& bytes){
  if(family==4){return QHostAddress((quint32(bytes[0])<<24)|(quint32(bytes[1])<<16)|(quint32(bytes[2])<<8)|bytes[3]);}
  Q_IPV6ADDR value{};std::copy(bytes.begin(),bytes.end(),value.c);return QHostAddress(value);
}
std::array<unsigned char,16> bytes(const QHostAddress& host){
  std::array<unsigned char,16> out{};
  if(host.protocol()==QAbstractSocket::IPv4Protocol){const auto v=host.toIPv4Address();for(unsigned i=0;i<4;++i){out[i]=static_cast<unsigned char>(v>>(24-8*i));}}
  else{const auto v=host.toIPv6Address();std::copy_n(v.c,16,out.begin());}return out;
}
// All function pointers and the SDK's global version symbol must resolve to
// the exact reviewed candidate. A loaded old SDK cannot mint an attachment.
struct Image {
  void* handle=nullptr;
  ~Image(){if(handle){::dlclose(handle);}}
  decltype(&lt::biglybtc_attach_public_udp_v1) attach=nullptr;
  decltype(&lt::biglybtc_detach_public_udp_v1) detach=nullptr;
  decltype(&lt::biglybtc_send_public_udp_v1) send=nullptr;
  decltype(&lt::biglybtc_public_udp_bindings_v1) query=nullptr;
};
std::shared_ptr<Image> image(){
  for(const auto* key:{"LD_PRELOAD","LD_AUDIT"}){const auto* value=std::getenv(key);if(value&&*value){return {};}}
  const int fd=::open(BIGLYBTC_SHARED_UDP_LIBRARY,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
  if(fd<0){return {};}
  const auto closed=qScopeGuard([&]{::close(fd);});
  struct stat before{},after{};if(::fstat(fd,&before)||!S_ISREG(before.st_mode)){return {};}
  auto* digest=EVP_MD_CTX_new();if(!digest){return {};}
  const auto freed=qScopeGuard([&]{EVP_MD_CTX_free(digest);});
  if(EVP_DigestInit_ex(digest,EVP_sha256(),nullptr)!=1){return {};}
  std::array<unsigned char,65536> input{};
  for(;;){const auto size=::read(fd,input.data(),input.size());if(size<0){if(errno==EINTR){continue;}return {};}if(!size){break;}if(EVP_DigestUpdate(digest,input.data(),static_cast<std::size_t>(size))!=1){return {};}}
  std::array<unsigned char,32> result{};unsigned length=0;
  if(EVP_DigestFinal_ex(digest,result.data(),&length)!=1||length!=32||::fstat(fd,&after)
      ||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino||before.st_size!=after.st_size
      ||before.st_mtim.tv_sec!=after.st_mtim.tv_sec||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec
      ||before.st_ctim.tv_sec!=after.st_ctim.tv_sec||before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec){return {};}
  if(QByteArray(reinterpret_cast<const char*>(result.data()),32).toHex()!=BIGLYBTC_SHARED_UDP_SHA256){return {};}
  auto pin=std::make_shared<Image>();pin->handle=::dlopen(BIGLYBTC_SHARED_UDP_LIBRARY,RTLD_NOW|RTLD_LOCAL);if(!pin->handle){return {};}
  Dl_info global{};if(!::dladdr(reinterpret_cast<void*>(&lt::version),&global)){return {};}
  struct stat mapped{};if(!global.dli_fname||::stat(global.dli_fname,&mapped)
      ||mapped.st_dev!=before.st_dev||mapped.st_ino!=before.st_ino){return {};}
  // Generated from the exact47-symbol evidence, including all parent roles.
  constexpr const char* names[]={
#include "symbols.inc"
  };
  for(const auto* name:names){void* address=::dlsym(pin->handle,name);Dl_info info{};
    if(!address||address!=::dlsym(RTLD_DEFAULT,name)||!::dladdr(address,&info)||info.dli_fbase!=global.dli_fbase){return {};}}
  const auto abi=reinterpret_cast<decltype(&lt::biglybtc_public_udp_abi_v1)>(::dlsym(pin->handle,"biglybtc_public_udp_abi_v1"));
  if(!abi||abi()!=1){return {};}
  pin->attach=reinterpret_cast<decltype(pin->attach)>(::dlsym(pin->handle,"biglybtc_attach_public_udp_v1"));
  pin->detach=reinterpret_cast<decltype(pin->detach)>(::dlsym(pin->handle,"biglybtc_detach_public_udp_v1"));
  pin->send=reinterpret_cast<decltype(pin->send)>(::dlsym(pin->handle,"biglybtc_send_public_udp_v1"));
  pin->query=reinterpret_cast<decltype(pin->query)>(::dlsym(pin->handle,"biglybtc_public_udp_bindings_v1"));
  return pin;
}
struct Inbox final:lt::BiglybtcPublicUdpObserverV1 {
  struct Frame {std::array<unsigned char,16> peer{},local{};std::array<char,1500> bytes{};std::uint32_t size=0,family=0;quint16 peerPort=0,localPort=0;};
  std::array<Frame,128> ring{};
  std::atomic<std::uint64_t> head{0},tail{0},lost{0};
  std::atomic<bool> active{true},sending{false};std::atomic<std::int64_t> end{0};
  int wake=::eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
  ~Inbox()override{if(wake>=0){::close(wake);}}
  bool source_current()const noexcept override{return active.load(std::memory_order_acquire)&&(!sending.load(std::memory_order_acquire)||ticks()<end.load(std::memory_order_acquire));}
  void signal()const noexcept{const std::uint64_t value=1;while(::write(wake,&value,sizeof(value))<0&&errno==EINTR){}}
  void drop()noexcept{const auto value=lost.load(std::memory_order_relaxed);auto next=value;while(next!=std::numeric_limits<std::uint64_t>::max()&&!lost.compare_exchange_weak(next,next+1,std::memory_order_relaxed)){}}
  void receive(const RawPacket& packet)noexcept override{
    if(!active.load(std::memory_order_acquire)){return;}
    const auto write=head.load(std::memory_order_relaxed),read=tail.load(std::memory_order_acquire);
    if(write-read==ring.size()||packet.size>1500||!packet.bytes||(packet.family!=4&&packet.family!=6)||packet.reserved){drop();return;}
    auto& frame=ring[write%ring.size()];frame.peer=packet.address;frame.local=packet.local_address;frame.family=packet.family;frame.peerPort=packet.peer_port;frame.localPort=packet.local_port;frame.size=packet.size;std::copy_n(packet.bytes,packet.size,frame.bytes.begin());
    head.store(write+1,std::memory_order_release);signal();
  }
};
}
// Node probing uses the already attached SDK socket. A root seed may answer
// even a wrong claimed address; its NODE contacts are hints, never address votes.
struct Provider::Probe {
  std::uint64_t serial=0;Binding local;std::int32_t network=0;bool owned=false,finished=false;
  std::unique_ptr<azureus_udp::OwnedPingTransport> actor;
  std::unique_ptr<QTimer> timer;
  std::vector<ProbePeer> candidates;std::size_t cursor=0,active=0,attempted=0;
  struct Followup {ProbePeer peer;azureus_udp::Peer claim;};std::deque<Followup> followups;
  struct Vote {azureus_udp::Peer reporter,observed;};std::vector<Vote> votes;
  Clock::time_point end;
  std::function<bool(Clock::time_point,std::move_only_function<void()>)> withPermit;
  std::function<void(AddressResult)> completed;
};
struct Provider::Impl {
  std::shared_ptr<Image> image;lt::session_handle session;std::shared_ptr<Inbox> inbox;
  std::uint64_t token=0;std::vector<RawBinding> bindings;
  std::unique_ptr<QSocketNotifier> notifier;
  std::shared_ptr<core::SharedUdpWitness> witness;
  std::function<bool(Binding,azureus_udp::Peer,std::string)> receiver;
  std::unique_ptr<Probe> probe;std::uint64_t nextProbe=0;std::vector<Listener> observed;
  explicit Impl(const lt::session_handle& value):session(value),inbox(std::make_shared<Inbox>()){}
  ~Impl(){inbox->active=false;if(image&&token){static_cast<void>(image->detach(&session,token,500));}}
};
Provider::Provider(std::unique_ptr<Impl> value):impl_(std::move(value)){}
std::expected<std::unique_ptr<Provider>,Failure> Provider::attach(const lt::session_handle& session,quint16 port){
  auto pin=image();if(!pin||!port){return std::unexpected(Failure::Authority);}
  auto state=std::make_unique<Impl>(session);state->image=std::move(pin);
  if(state->inbox->wake<0){return std::unexpected(Failure::State);}
  std::shared_ptr<lt::BiglybtcPublicUdpObserverV1> observer=state->inbox;
  std::array<RawBinding,16> bindings{};std::uint32_t count=0;
  const auto status=state->image->attach(&state->session,&observer,port,500,&state->token,bindings.data(),16,&count);
  if(status!=static_cast<std::uint32_t>(Status::Accepted)){state->inbox->active=false;return std::unexpected(Failure::Authority);}
  state->bindings.assign(bindings.begin(),bindings.begin()+count);
  auto result=std::unique_ptr<Provider>(new Provider(std::move(state)));
  auto proof=std::make_shared<core::SharedUdpWitness::State>();proof->imagePin=result->impl_->image;
  proof->active=std::shared_ptr<std::atomic<bool>>(result->impl_->inbox,&result->impl_->inbox->active);
  result->impl_->witness=std::shared_ptr<core::SharedUdpWitness>(new core::SharedUdpWitness(std::move(proof)));
  result->publishListeners();
  result->impl_->notifier=std::make_unique<QSocketNotifier>(result->impl_->inbox->wake,QSocketNotifier::Read,result.get());
  QObject::connect(result->impl_->notifier.get(),&QSocketNotifier::activated,result.get(),[raw=result.get()]{raw->drain();});
  return result;
}
Provider::~Provider(){close();}
std::shared_ptr<const core::SharedUdpWitness> Provider::witness()const{return impl_->witness;}
void Provider::publishListeners(){
  auto values=std::make_shared<std::vector<core::SharedUdpListener>>();
  for(const auto& value:listeners()){
    const auto length=static_cast<std::uint8_t>(value.local.address.protocol()==QAbstractSocket::IPv4Protocol?4:16);
    values->push_back({{bytes(value.local.address),value.local.port,length},{bytes(value.observedExternal.address),value.observedExternal.port,length}});
  }
  impl_->witness->state_->listeners.store(std::move(values),std::memory_order_release);
}
std::vector<Binding> Provider::bindings()const{
  std::vector<Binding> out;for(const auto& binding:impl_->bindings){out.push_back({host(binding.family,binding.address),binding.port});}return out;
}
std::vector<Listener> Provider::listeners()const{
  std::vector<Listener> out;for(const auto& binding:impl_->bindings){Listener value{{host(binding.family,binding.address),binding.port},{host(binding.family,binding.external_address),binding.port}};
    const auto observed=std::ranges::find_if(impl_->observed,[&](const auto& o){return o.local==value.local;});if(observed!=impl_->observed.end()){value.observedExternal=observed->observedExternal;}out.push_back(std::move(value));}return out;
}
std::expected<void,Failure> Provider::refresh(){
  if(QThread::currentThread()!=thread()){return std::unexpected(Failure::Thread);}
  if(!impl_->token){return std::unexpected(Failure::Closed);}
  std::array<RawBinding,16> bindings{};std::uint32_t count=0;
  const auto status=impl_->image->query(&impl_->session,impl_->token,500,bindings.data(),16,&count);
  if(status!=static_cast<std::uint32_t>(Status::Accepted)){impl_->inbox->active=false;impl_->witness->revoke();return std::unexpected(Failure::Authority);}
  impl_->bindings.assign(bindings.begin(),bindings.begin()+count);publishListeners();return {};
}
void Provider::receive(std::function<bool(Binding,azureus_udp::Peer,std::string)> receiver){
  if(QThread::currentThread()==thread()){impl_->receiver=std::move(receiver);}
}
std::expected<azureus_udp::SharedChannel,Failure> Provider::channel(Binding local,azureus_udp::Peer advertised,std::function<bool()> heldPermit){
  if(QThread::currentThread()!=thread()){return std::unexpected(Failure::Thread);}
  const auto choices=bindings();if(!impl_->token||!impl_->inbox->active.load()||!heldPermit||std::ranges::find(choices,local)==choices.end()
      ||!advertised.port||local.address.protocol()!=advertised.address.protocol()){return std::unexpected(Failure::Peer);}
  const auto observations=listeners();
  const auto acceptable=[&](const Listener& value){return value.local==local&&(advertised==local||(!value.observedExternal.address.isNull()&&value.observedExternal.address!=QHostAddress(QHostAddress::AnyIPv4)&&value.observedExternal.address!=QHostAddress(QHostAddress::AnyIPv6)&&value.observedExternal==advertised));};
  if(!std::ranges::any_of(observations,acceptable)){return std::unexpected(Failure::Authority);}
  QPointer<Provider> self(this);
  return azureus_udp::SharedChannel{local,advertised,[self,local,advertised]{
    if(!self||!self->impl_->inbox->active.load()){return false;}
    const auto actual=self->listeners();return std::ranges::any_of(actual,[&](const auto& value){return value.local==local&&(advertised==local||value.observedExternal==advertised);});
  },
    [self,local,heldPermit=std::move(heldPermit)](std::string_view wire,const azureus_udp::Peer& peer,Clock::time_point end)->std::expected<qint64,Failure>{
      if(!self||QThread::currentThread()!=self->thread()){return std::unexpected(Failure::Closed);}
      auto& state=*self->impl_;const auto now=Clock::now();
      if(!state.token||!state.inbox->active.load()||end<=now||!heldPermit()){return std::unexpected(Failure::Authority);}
      const auto remaining=std::chrono::ceil<std::chrono::milliseconds>(end-now);
      const auto budget=static_cast<std::uint32_t>(std::clamp<std::int64_t>(remaining.count(),1,500));
      state.inbox->end.store(ticks(std::min(end,now+std::chrono::milliseconds(budget))),std::memory_order_release);
      state.inbox->sending.store(true,std::memory_order_release);
      const auto reset=qScopeGuard([&]{state.inbox->sending.store(false,std::memory_order_release);});
      const auto target=bytes(peer.address),source=bytes(local.address);
      const auto status=state.image->send(&state.session,state.token,
        peer.address.protocol()==QAbstractSocket::IPv4Protocol?4:6,target.data(),peer.port,local.port,source.data(),wire.data(),wire.size(),budget);
      if(status!=static_cast<std::uint32_t>(Status::Accepted)){return std::unexpected(status==static_cast<std::uint32_t>(Status::Timeout)?Failure::Timeout:Failure::Authority);}
      return static_cast<qint64>(wire.size());
    },impl_->witness};
}
void Provider::drain(){
  std::uint64_t value=0;while(::read(impl_->inbox->wake,&value,sizeof(value))<0&&errno==EINTR){}
  const auto retained=impl_->inbox;auto& box=*retained;QPointer<Provider> guard(this);
  for(unsigned n=0;n<32&&box.active.load();++n){
    const auto read=box.tail.load(std::memory_order_relaxed);if(read==box.head.load(std::memory_order_acquire)){break;}
    const auto frame=box.ring[read%box.ring.size()];box.tail.store(read+1,std::memory_order_release);
    const Binding local{host(frame.family,frame.local),frame.localPort};
    const auto all=bindings();
    bool consumed=false;
    if(impl_->probe&&!impl_->probe->finished&&impl_->probe->local==local){
      consumed=impl_->probe->actor->receiveShared({host(frame.family,frame.peer),frame.peerPort},std::string(frame.bytes.data(),frame.size));
    }
    if(std::ranges::find(all,local)!=all.end()&&impl_->receiver){
      auto callback=impl_->receiver;
      try{consumed=callback(local,{host(frame.family,frame.peer),frame.peerPort},std::string(frame.bytes.data(),frame.size))||consumed;}catch(...){}
    }
    if(!consumed){box.drop();}if(!guard){return;}
  }
  if(box.active.load()&&box.tail.load()!=box.head.load()){box.signal();}
}
std::uint64_t Provider::dropped()const noexcept{return impl_->inbox->lost.load();}
void Provider::close()noexcept{
  if(!impl_){return;}impl_->inbox->active=false;if(impl_->witness){impl_->witness->revoke();}if(impl_->probe){finishProbe(std::unexpected(Failure::Closed));}
  if(impl_->notifier){impl_->notifier->setEnabled(false);}impl_->receiver={};
  if(impl_->token){static_cast<void>(impl_->image->detach(&impl_->session,impl_->token,500));impl_->token=0;}
}
bool probeAddressAllowed(const QHostAddress& address,bool owned){
  if(!address.scopeId().isEmpty()){return false;}
  if(owned){return address.isLoopback();}
  const auto raw=bytes(address);return core::azureusPublicAddress(std::span(raw).first(address.protocol()==QAbstractSocket::IPv4Protocol?4:16));
}
std::expected<void,Failure> Provider::probeAddress(Binding local,std::int32_t network,std::vector<ProbePeer> peers,bool owned,
    std::function<bool(Clock::time_point,std::move_only_function<void()>)> withPermit,
    std::function<bool()> heldPermit,std::function<void(AddressResult)> completed){
  if(QThread::currentThread()!=thread()){return std::unexpected(Failure::Thread);}
  if(!impl_->token||!impl_->inbox->active.load()||impl_->probe||!withPermit||!heldPermit||!completed
      ||peers.empty()||peers.size()>32||impl_->nextProbe==std::numeric_limits<std::uint64_t>::max()){
    return std::unexpected(Failure::State);
  }
  const auto available=bindings();if(std::ranges::find(available,local)==available.end()){return std::unexpected(Failure::Peer);}
  auto channel=this->channel(local,local,heldPermit);if(!channel){return std::unexpected(channel.error());}
  // Placeholder is deliberately unconfirmed. This actor can issue only NODE/
  // PING, never serve room data, a database, or publish membership values.
  channel->advertised.address=QHostAddress(local.address.protocol()==QAbstractSocket::IPv4Protocol?QHostAddress::AnyIPv4:QHostAddress::AnyIPv6);
  azureus_udp::Config config;config.network=network;config.bootstrapOnly=true;
  config.routingScope=owned?azureus_udp::RoutingScope::OwnedLoopback:azureus_udp::RoutingScope::PublicBound;
  config.maximumPeers=32;config.maximumPending=8;
  auto value=std::make_unique<Probe>();value->serial=++impl_->nextProbe;value->local=local;value->network=network;value->owned=owned;value->end=Clock::now()+std::chrono::seconds(15);value->withPermit=std::move(withPermit);value->completed=std::move(completed);value->candidates.reserve(64);value->votes.reserve(32);
  value->actor=std::make_unique<azureus_udp::OwnedPingTransport>(std::move(config));
  auto started=value->actor->startShared(std::move(*channel),owned?std::function<bool()>{}:heldPermit);if(!started){return std::unexpected(started.error());}
  for(auto& peer:peers){if(peer.endpoint.port&&peer.endpoint.address.protocol()==local.address.protocol()&&probeAddressAllowed(peer.endpoint.address,owned)&&peer.version>=51&&peer.version<=55
      &&std::ranges::none_of(value->candidates,[&](const auto& p){return p.endpoint==peer.endpoint;})){value->candidates.push_back(std::move(peer));}}
  if(value->candidates.empty()){return std::unexpected(Failure::Peer);}
  value->timer=std::make_unique<QTimer>();value->timer->setSingleShot(true);
  connect(value->timer.get(),&QTimer::timeout,this,[this]{finishProbe(std::unexpected(Failure::Timeout));});
  impl_->probe=std::move(value);impl_->probe->timer->start(std::chrono::seconds(15));pumpProbe();return {};
}
void Provider::pumpProbe(){
  if(!impl_->probe||impl_->probe->finished){return;}auto& value=*impl_->probe;
  while(value.active<6&&(value.cursor<value.candidates.size()||!value.followups.empty())&&value.attempted<32&&Clock::now()<value.end){
    ProbePeer peer;std::optional<azureus_udp::Peer> claim;
    if(!value.followups.empty()){auto task=std::move(value.followups.front());value.followups.pop_front();peer=std::move(task.peer);claim=std::move(task.claim);}else{peer=value.candidates[value.cursor++];}
    ++value.active;++value.attempted;const auto serial=value.serial;
    QPointer<Provider> self(this);
    const auto queued=value.withPermit(value.end,[self,serial,peer,claim]{
      if(!self||QThread::currentThread()!=self->thread()||!self->impl_->probe||self->impl_->probe->serial!=serial||self->impl_->probe->finished){return;}
      auto& p=*self->impl_->probe;
      if(Clock::now()>=p.end){self->finishProbe(std::unexpected(Failure::Timeout));return;}
      const auto provisional=claim.value_or(azureus_udp::Peer{QHostAddress(p.local.address.protocol()==QAbstractSocket::IPv4Protocol?QHostAddress::AnyIPv4:QHostAddress::AnyIPv6),p.local.port});
      auto identity=p.actor->updateBootstrapIdentity(provisional);if(!identity){self->probeReply(serial,peer,std::unexpected(identity.error()));return;}
      auto allowed=p.actor->allowPeer(peer.endpoint);if(!allowed){self->probeReply(serial,peer,std::unexpected(allowed.error()));return;}
      azureus_udp::find::nodes::NodeId target{};for(auto& byte:target){byte=static_cast<std::uint8_t>(QRandomGenerator::system()->generate());}
      const auto deadline=std::min(p.end,Clock::now()+std::chrono::seconds(5));
      auto request=p.actor->findNodeDetailed(peer.endpoint,peer.version,target,self,[self,serial,peer](auto response){if(self){self->probeReply(serial,peer,std::move(response));}},deadline-Clock::now());
      if(!request){self->probeReply(serial,peer,std::unexpected(request.error()));}
    });
    if(!queued){--value.active;}
  }
  if(!value.active&&((value.cursor>=value.candidates.size()&&value.followups.empty())||value.attempted>=32)){finishProbe(std::unexpected(Failure::Timeout));}
}
void Provider::probeReply(std::uint64_t serial,ProbePeer peer,azureus_udp::DetailedFindNodeOutcome response){
  if(!impl_->probe||impl_->probe->serial!=serial||impl_->probe->finished){return;}auto& p=*impl_->probe;if(p.active){--p.active;}
  if(response){
    if(const auto* error=std::get_if<azureus_udp::remote_error::Decoded>(&*response);error&&error->body().type==1&&error->body().originator){
      const auto& raw=*error->body().originator;azureus_udp::Peer observed{host(raw.length==4?4:6,raw.bytes),raw.port};
      if(observed.port&&observed.address.protocol()==p.local.address.protocol()&&probeAddressAllowed(observed.address,p.owned)
          &&std::ranges::none_of(p.votes,[&](const auto& vote){return vote.reporter.address==peer.endpoint.address;})){
        p.votes.push_back({peer.endpoint,observed});
        // Ordinary seeds may challenge the originator before returning any
        // contacts. Correct only a follow-up to that reporter. Other peers
        // must still see an unconfirmed claim to give independent IP votes.
        p.followups.push_back({peer,observed});
        const auto agreeing=std::ranges::count_if(p.votes,[&](const auto& vote){return vote.observed==observed;});
        if(agreeing>=2){
          const auto where=std::ranges::find_if(impl_->observed,[&](const auto& v){return v.local==p.local;});
          if(where==impl_->observed.end()){impl_->observed.push_back({p.local,observed});}else{where->observedExternal=observed;}
          publishListeners();AddressObservation result{observed,p.candidates};finishProbe(std::move(result));return;
        }
      }
    }else if(const auto* node=std::get_if<azureus_udp::find::Decoded>(&*response)){
      if(const auto* body=std::get_if<azureus_udp::find::ReplyBody>(&node->body())){
        for(const auto& contact:body->contacts){
          if(p.candidates.size()>=64){break;}ProbePeer suggestion{{host(contact.address.length==4?4:6,contact.address.bytes),contact.address.port},contact.protocol};
          if(suggestion.endpoint!=p.local&&std::ranges::none_of(p.votes,[&](const auto& vote){return vote.observed==suggestion.endpoint;})&&suggestion.endpoint.port&&suggestion.endpoint.address.protocol()==p.local.address.protocol()&&probeAddressAllowed(suggestion.endpoint.address,p.owned)
              &&std::ranges::none_of(p.candidates,[&](const auto& prior){return prior.endpoint==suggestion.endpoint;})){
            p.candidates.push_back(std::move(suggestion));
          }
        }
      }
    }
  }
  pumpProbe();
}
void Provider::finishProbe(AddressResult result){
  if(!impl_->probe||impl_->probe->finished){return;}auto& p=*impl_->probe;p.finished=true;p.timer->stop();const auto serial=p.serial;
  if(qEnvironmentVariableIsSet("BIGLYBTC_CHAT_DIAGNOSTIC")){const auto stats=p.actor->statistics();qInfo("chat-probe finish success=%d failure=%d attempted=%zu votes=%zu sent=%llu received=%llu replies=%llu foreign=%llu authority=%llu",bool(result),result?-1:int(result.error()),p.attempted,p.votes.size(),(unsigned long long)stats.sent,(unsigned long long)stats.received,(unsigned long long)stats.replies,(unsigned long long)stats.foreign,(unsigned long long)stats.authorityErrors);}
  // Leave the current actor callback before destroying the actor/its receiver.
  QMetaObject::invokeMethod(this,[this,serial,result=std::move(result)]()mutable{
    if(!impl_->probe||impl_->probe->serial!=serial){return;}auto completed=std::move(impl_->probe->completed);impl_->probe->actor->close();impl_->probe.reset();
    if(completed){completed(std::move(result));}
  },Qt::QueuedConnection);
}

}

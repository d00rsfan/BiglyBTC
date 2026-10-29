// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_transport.hpp"
#include "biglybtc/azureus_endpoint.hpp"
#include <QDateTime>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <bit>
#include <limits>
namespace biglybtc::services::azureus_udp {
namespace {
bool publicAddress(const QHostAddress& address)noexcept{
  if(!address.scopeId().isEmpty()){return false;}std::array<std::uint8_t,16> bytes{};
  if(address.protocol()==QAbstractSocket::IPv4Protocol){const auto ip=address.toIPv4Address();for(unsigned i=0;i<4;++i){bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}return core::azureusPublicAddress(std::span(bytes).first(4));}
  if(address.protocol()==QAbstractSocket::IPv6Protocol){const auto ip=address.toIPv6Address();std::copy_n(ip.c,16,bytes.begin());return core::azureusPublicAddress(bytes);}return false;
}
constexpr qint64 maximumDatagram=1400;
constexpr unsigned workPerTurn=64;
// Process-local observation identity only; no service registration or authority.
// Saturation leaves future diagnostics unavailable instead of reusing an ID.
std::uint64_t reserveStatisticsGeneration() noexcept {
  static std::atomic<std::uint64_t> next{0};auto value=next.load(std::memory_order_relaxed);
  while(value!=std::numeric_limits<std::uint64_t>::max()) {
    if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed)){return value+1;}
  }
  return 0;
}
std::int32_t readInt(std::string_view bytes,std::size_t offset) {
  std::uint32_t value=0;for (unsigned i=0;i<4;++i) { value=(value<<8)|static_cast<unsigned char>(bytes[offset+i]); }
  return std::bit_cast<std::int32_t>(value);
}
std::int64_t readLong(std::string_view bytes,std::size_t offset) {
  std::uint64_t value=0;for (unsigned i=0;i<8;++i) { value=(value<<8)|static_cast<unsigned char>(bytes[offset+i]); }
  return std::bit_cast<std::int64_t>(value);
}
packet::udp::Address originator(const Peer& endpoint) {
  packet::udp::Address address;address.port=endpoint.port;
  if (endpoint.address.protocol()==QAbstractSocket::IPv4Protocol) {
    const auto ip=endpoint.address.toIPv4Address();for (unsigned i=0;i<4;++i) { address.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i)); }
  } else { address.length=16;const auto ip=endpoint.address.toIPv6Address();for (unsigned i=0;i<16;++i) { address.bytes[i]=ip[i]; } }
  return address;
}
bool sameIp(const packet::udp::Address& first,const packet::udp::Address& second) {
  return first.length==second.length && (first.length==4 || first.length==16)
      && std::equal(first.bytes.begin(),first.bytes.begin()+first.length,second.bytes.begin());
}
std::expected<void,Failure> validateService(const GeneratedStoreService& service) {
  if (service.maximumReceipts==0 || service.maximumReceipts>256 || service.keyStates.size()>255
      || service.findNodeContacts.size()>128) { return std::unexpected(Failure::Capacity); }
  std::size_t total=0;
  const auto count=[&](std::size_t n) { if (n>128*1024-total) { return false; }total+=n;return true; };
  for (std::size_t i=0;i<service.keyStates.size();++i) {
    const auto& key=service.keyStates[i];
    if (key.bytes.size()>255) { return std::unexpected(Failure::Encode); }
    if (!count(key.bytes.size())) { return std::unexpected(Failure::Capacity); }
    for (std::size_t j=0;j<i;++j) { if (service.keyStates[j].bytes==key.bytes) { return std::unexpected(Failure::State); } }
    if (key.block) {
      for (const auto* bytes:{&key.block->request,&key.block->signature}) {
        if (*bytes && ((*bytes)->size()>8192 || !count((*bytes)->size()))) { return std::unexpected(Failure::Capacity); }
      }
    }
  }
  for (const auto& contact:service.findNodeContacts) {
    if (!find::nodes::encodeContact(contact)) { return std::unexpected(Failure::Encode); }
  }
  return {};
}
}
// Out-of-line defaults keep GCC 16 from diagnosing inactive optional payload
// members while inlining Config's move. Ordinary owning value semantics remain.
GeneratedStoreService::GeneratedStoreService()=default;
GeneratedStoreService::GeneratedStoreService(const GeneratedStoreService&)=default;
GeneratedStoreService& GeneratedStoreService::operator=(const GeneratedStoreService&)=default;
GeneratedStoreService::GeneratedStoreService(GeneratedStoreService&&) noexcept=default;
GeneratedStoreService& GeneratedStoreService::operator=(GeneratedStoreService&&) noexcept=default;
ServiceReceipt::ServiceReceipt(Peer source,ServiceRequest incoming):peer(std::move(source)),request(std::move(incoming)) {}
ServiceReceipt::ServiceReceipt(const ServiceReceipt&)=default;
ServiceReceipt& ServiceReceipt::operator=(const ServiceReceipt& other) {
  if (this!=&other) { auto copy=other;*this=std::move(copy); }return *this;
}
OwnedPingTransport::OwnedPingTransport(Config config,QObject* parent)
    :QObject(parent),config_(std::move(config)),socket_(this),deadline_(this),routingDeadline_(this),statisticsGeneration_(reserveStatisticsGeneration()) {
  socket_.setProxy(QNetworkProxy::NoProxy);deadline_.setSingleShot(true);deadline_.setTimerType(Qt::PreciseTimer);
  connect(&socket_,&QUdpSocket::readyRead,this,[this] { consume(); });
  connect(&socket_,&QUdpSocket::errorOccurred,this,[this] { ++statistics_.ioErrors; });
  deadline_.callOnTimeout(this,[this] { expire(); });
  routingDeadline_.setSingleShot(true);routingDeadline_.setTimerType(Qt::PreciseTimer);routingDeadline_.callOnTimeout(this,[this]{try{if(routing_){pumpRouting(routing_);}}catch(...){routingFailed(Failure::Capacity);}});
}
OwnedPingTransport::~OwnedPingTransport() {
  deadline_.stop();routingDeadline_.stop();socket_.close();
  for (auto& [transaction,pending]:pending_) { (void)transaction;disconnect(pending.death); }
  pending_.clear();completions_.clear(); // Destruction never invokes a callback.
}
bool OwnedPingTransport::owner() const { return QThread::currentThread()==thread(); }
#include "owned_routing.inc"
#include "owned_maintenance.inc"
std::expected<TransportStatisticsSnapshot,Failure> OwnedPingTransport::statisticsSnapshot() const noexcept {
  if(!owner()){return std::unexpected(Failure::Thread);}
  if(!statisticsGeneration_ || statisticsPublication_==std::numeric_limits<std::uint64_t>::max()) {
    return std::unexpected(Failure::Capacity);
  }
  const auto state=state_==State::Idle?TransportStatisticsState::NotStarted
      :state_==State::Bound?TransportStatisticsState::Bound:TransportStatisticsState::Closed;
  const auto publication=statisticsPublication_+1;
  TransportStatisticsSnapshot result(statisticsGeneration_,publication,config_.network,config_.version,
      statisticsFamily_,state,statistics_,pending_.size(),completions_.size(),peers_.size());
  statisticsPublication_=publication;
  return result;
}
std::expected<OwnedDhtStatisticsSnapshot,Failure> OwnedPingTransport::dhtStatisticsSnapshot() const noexcept {
  const auto transport=statisticsSnapshot();if(!transport){return std::unexpected(transport.error());}
  OwnedDhtStatisticsSnapshot value(*transport);value.operationsEnabled_=config_.observeOperations;value.vivaldiEnabled_=config_.enableVivaldi;
  value.count_=eventCount_;value.dropped_=droppedEvents_;
  for(std::size_t i=0;i<eventCount_;++i){value.events_[i]=operationEvents_[(eventBegin_+i)%operationEvents_.size()];}
  if(config_.enableVivaldi){value.local_=vivaldi_.value();value.remote_=lastRemotePosition_;value.rtt_=lastRtt_;value.coordinateObservations_=vivaldi_.observations();}
  value.ping_=lastPing_;value.pingLost_=pingLost_;
  value.peerCount_=peerPositionCount_;value.droppedPeers_=droppedPeerPositions_;value.peersLost_=peerPositionsLost_;
  std::copy_n(peerPositions_.begin(),peerPositionCount_,value.peers_.begin());
  return value;
}
void OwnedPingTransport::observePing(const Pending& pending,const Result& result)noexcept{
  if(!config_.observeOperations||pending.expectedAction!=1025||!statisticsGeneration_){return;}
  if(pingSequence_==std::numeric_limits<std::uint64_t>::max()){pingLost_=true;lastPing_.reset();return;}
  const auto now=Clock::now();
  const auto observed=std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
  const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(now-pending.started).count();
  if(observed<0||elapsed<0){pingLost_=true;lastPing_.reset();return;}
  OwnedPingObservation value;value.generation_=statisticsGeneration_;value.sequence_=++pingSequence_;value.observed_=observed;
  value.endpoint_.port=pending.peer.port;
  if(pending.peer.address.protocol()==QAbstractSocket::IPv4Protocol){
    value.endpoint_.length=4;const auto ip=pending.peer.address.toIPv4Address();
    for(unsigned i=0;i<4;++i){value.endpoint_.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}
  }else{
    value.endpoint_.length=16;const auto ip=pending.peer.address.toIPv6Address();std::copy_n(ip.c,16,value.endpoint_.bytes.begin());
  }
  if(result&&std::holds_alternative<packet::Decoded>(*result)){value.kind_=PingObservationKind::Reply;value.rtt_=elapsed;}
  else if(result){value.kind_=PingObservationKind::RemoteError;}
  else{switch(result.error()){
    case Failure::Timeout:value.kind_=PingObservationKind::Timeout;break;
    case Failure::Cancelled:value.kind_=PingObservationKind::Cancelled;break;
    case Failure::Closed:value.kind_=PingObservationKind::Closed;break;
    default:value.kind_=PingObservationKind::Failed;break;
  }}
  lastPing_=value;
}
void OwnedPingTransport::observeOperation(const Pending& pending,OperationEventKind kind) noexcept {
  if(!config_.observeOperations){return;}
  if(eventSerial_==std::numeric_limits<std::uint64_t>::max()){if(droppedEvents_!=std::numeric_limits<std::uint64_t>::max()){++droppedEvents_;}return;}
  const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-pending.started).count();
  OperationEvent event{++eventSerial_,pending.ticket.value,pending.expectedAction-1,kind,elapsed>=0?std::optional<std::int64_t>{elapsed}:std::nullopt};
  auto destination=(eventBegin_+eventCount_)%operationEvents_.size();
  if(eventCount_==operationEvents_.size()){destination=eventBegin_;eventBegin_=(eventBegin_+1)%operationEvents_.size();if(droppedEvents_!=std::numeric_limits<std::uint64_t>::max()){++droppedEvents_;}}
  else{++eventCount_;}operationEvents_[destination]=event;
}
void OwnedPingTransport::observeVivaldi(const Pending& pending,const packet::Decoded& reply) noexcept {
  if(!config_.enableVivaldi){return;}
  const auto* body=std::get_if<packet::ReplyBody>(&reply.body());if(!body){return;}
  for(const auto& position:body->positions){
    if(!position || position->type!=1 || position->bytes.size()!=16){continue;}
    std::array<float,4> parts{};
    for(std::size_t i=0;i<parts.size();++i){std::uint32_t bits=0;for(std::size_t j=0;j<4;++j){bits=(bits<<8)|static_cast<unsigned char>(position->bytes[i*4+j]);}parts[i]=std::bit_cast<float>(bits);}
    const VivaldiValue remote{{parts[0],parts[1],parts[2]},parts[3]};if(!validVivaldiValue(remote)){continue;}
    const auto now=Clock::now();
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(now-pending.started).count();if(elapsed<=0){return;}
    const auto observed=std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    const float rtt=static_cast<float>(static_cast<double>(elapsed)/1000000.0);
    const auto random=[]()noexcept{return static_cast<float>(QRandomGenerator::system()->generateDouble());};
    const bool accepted=vivaldi_.observe(rtt,remote,random);
    if(accepted){lastRemotePosition_=remote;lastRtt_=elapsed;}
    observePeerPosition(pending,remote,elapsed,observed,accepted);return;
  }
}
void OwnedPingTransport::observePeerPosition(const Pending& pending,VivaldiValue remote,std::int64_t elapsed,std::int64_t observed,bool accepted)noexcept{
  if(!statisticsGeneration_||observed<0||elapsed<=0||peerPositionSequence_==std::numeric_limits<std::uint64_t>::max()){
    peerPositionsLost_=true;return;
  }
  VivaldiPeerObservation report;report.position=remote;report.generation=statisticsGeneration_;
  report.sequence=++peerPositionSequence_;report.ticket=pending.ticket.value;
  report.observedSteadyNanoseconds=observed;report.rttNanoseconds=elapsed;report.localUpdateAccepted=accepted;
  report.endpoint.port=pending.peer.port;
  if(pending.peer.address.protocol()==QAbstractSocket::IPv4Protocol){
    report.endpoint.length=4;const auto ip=pending.peer.address.toIPv4Address();
    for(unsigned i=0;i<4;++i){report.endpoint.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}
  }else{
    report.endpoint.length=16;const auto ip=pending.peer.address.toIPv6Address();std::copy_n(ip.c,16,report.endpoint.bytes.begin());
  }
  const auto end=peerPositions_.begin()+peerPositionCount_;
  const auto existing=std::find_if(peerPositions_.begin(),end,[&](const auto& value){return value.endpoint==report.endpoint;});
  if(existing!=end){
    // Refresh recency without growing the collection or claiming a new peer.
    std::move(existing+1,end,existing);--peerPositionCount_;
  }else if(peerPositionCount_==peerPositions_.size()){
    std::move(peerPositions_.begin()+1,end,peerPositions_.begin());--peerPositionCount_;
    if(droppedPeerPositions_!=std::numeric_limits<std::uint64_t>::max()){++droppedPeerPositions_;}else{peerPositionsLost_=true;}
  }
  peerPositions_[peerPositionCount_++]=report;
}
Peer OwnedPingTransport::normalize(Peer peer) {
  if (peer.address.protocol()==QAbstractSocket::IPv6Protocol) {
    const auto ip=peer.address.toIPv6Address();bool mapped=ip[10]==255 && ip[11]==255;
    for (unsigned i=0;i<10;++i) { mapped=mapped && ip[i]==0; }
    if (mapped) { peer.address=QHostAddress((static_cast<quint32>(ip[12])<<24)|(static_cast<quint32>(ip[13])<<16)|(static_cast<quint32>(ip[14])<<8)|ip[15]); }
  }return peer;
}
bool OwnedPingTransport::allowed(const Peer& peer) const { return std::ranges::find(peers_,peer)!=peers_.end(); }
std::uint64_t OwnedPingTransport::word() { return config_.randomWord?config_.randomWord():QRandomGenerator::system()->generate64(); }
std::expected<void,Failure> OwnedPingTransport::start(const QHostAddress& bindAddress,quint16 bindPort,std::function<bool()> publicPermit) {
  if (!owner()) { return std::unexpected(Failure::Thread); }
  if (state_!=State::Idle) { return std::unexpected(Failure::State); }
  if (!bindAddress.scopeId().isEmpty()) { return std::unexpected(Failure::Peer); }
  const auto address=normalize({bindAddress,0}).address;
  const bool publiclyBound=config_.routingScope==RoutingScope::PublicBound;
  if(publiclyBound){
    if(config_.maximumPeers>32||config_.maximumPending>32){return std::unexpected(Failure::Capacity);}
    if(!publicAddress(bindAddress)||!bindPort||!((config_.network==0&&address.protocol()==QAbstractSocket::IPv4Protocol)||(config_.network==3&&address.protocol()==QAbstractSocket::IPv6Protocol))||!publicPermit){return std::unexpected(Failure::Peer);}
    bool current=false;try{current=publicPermit();}catch(...){}if(!current){return std::unexpected(Failure::Authority);}
  }else if (!address.isLoopback() || !address.scopeId().isEmpty() || publicPermit) { return std::unexpected(Failure::Peer); }
  if(config_.routingScope!=RoutingScope::ExactPeers&&!dynamicRoutingScope(config_.routingScope)){return std::unexpected(Failure::State);}
  if(dynamicRoutingScope(config_.routingScope)&&(config_.generatedStoreService||config_.findNodeReply||config_.findValueReply||config_.storeReply||config_.storeErrorReply)){return std::unexpected(Failure::State);}
  if (config_.version<51 || config_.version>55) { return std::unexpected(Failure::Version); }
  if (config_.maximumPending==0 || config_.maximumPending>256 || config_.maximumPeers==0 || config_.maximumPeers>256) { return std::unexpected(Failure::Capacity); }
  if (config_.generatedStoreService) {
    if (config_.servePings || config_.findNodeReply || config_.findValueReply || config_.storeReply || config_.storeErrorReply) { return std::unexpected(Failure::State); }
    const auto valid=validateService(*config_.generatedStoreService);if (!valid) { return std::unexpected(valid.error()); }
  }
  if (config_.storeReply && config_.storeErrorReply) { return std::unexpected(Failure::State); }
  if (config_.storeReply && config_.storeReply->size()>255) { return std::unexpected(Failure::Encode); }
  if (config_.storeErrorReply && config_.storeErrorReply->type==2
      && ((config_.storeErrorReply->request && config_.storeErrorReply->request->size()>255)
          || (config_.storeErrorReply->signature && config_.storeErrorReply->signature->size()>65535))) { return std::unexpected(Failure::Encode); }
  if (config_.findNodeReply && config_.findNodeReply->contacts.size()>128) { return std::unexpected(Failure::Capacity); }
  if (config_.findValueReply) {
    if (config_.findValueReply->size()>16) { return std::unexpected(Failure::Capacity); }
    std::size_t records=0,bytes=0;
    for (const auto& piece:*config_.findValueReply) {
      const auto count=piece.values?piece.values->size():piece.contacts.size();
      if (count>256-records) { return std::unexpected(Failure::Capacity); }records+=count;
      if (piece.values) { for (const auto& value:*piece.values) {
        if (value.bytes.size()>512) { return std::unexpected(Failure::Encode); }
        bytes+=value.bytes.size();if (bytes>128*1024) { return std::unexpected(Failure::Capacity); }
      } }
    }
  }
  auto rollback=qScopeGuard([this] { database_.reset();authority_.reset();admission_.reset();localId_.reset();nodeCache_.reset();socket_.close(); });
  if(config_.generatedStoreService&&config_.generatedStoreService->volatileDatabase){
    auto made=database::Owner::create(*config_.generatedStoreService->volatileDatabase);if(!made){return std::unexpected(Failure::Capacity);}
    database_=std::move(*made);
  }
  if (!socket_.bind(address,bindPort,QUdpSocket::DontShareAddress)) { return std::unexpected(Failure::Send); }
  socket_.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,65536);
  nodeCache_=std::make_unique<find::nodes::NodeCache>(); // Construct on the actual Qt owner, after any moveToThread.
  if (config_.generatedStoreService) {
    const auto local=nodeCache_->lookup(find::nodes::ResolvedEndpoint{originator(endpoint()),{}},config_.version);
    if (!local) { return std::unexpected(Failure::Authority); }
    auto authority=auth::Authority::create(true);if (!authority) { return std::unexpected(Failure::Authority); }
    admission_=std::make_unique<policy::Admission>();authority_=std::move(*authority);localId_=local->id;
    if(database_){find::nodes::Contact contact;contact.protocol=config_.version;contact.address=originator(endpoint());contact.id=local->id;contact.instance=config_.instance;contact.flags=config_.flags;
      if(!database_->bindLocal(contact)){return std::unexpected(Failure::State);}}
  }
  statisticsFamily_=address.protocol()==QAbstractSocket::IPv6Protocol?TransportStatisticsFamily::IPv6:TransportStatisticsFamily::IPv4;
  if(publiclyBound){bool current=false;try{current=publicPermit();}catch(...){}if(!current||socket_.localAddress()!=bindAddress||socket_.localPort()!=bindPort){return std::unexpected(Failure::Authority);}publicPermit_=std::move(publicPermit);}
  state_=State::Bound;rollback.dismiss();return {};
}
std::expected<void,Failure> OwnedPingTransport::allowPeer(Peer candidate) {
  if (!owner()) { return std::unexpected(Failure::Thread); }
  if (state_!=State::Bound) { return std::unexpected(Failure::State); }
  if (!candidate.address.scopeId().isEmpty()) { return std::unexpected(Failure::Peer); }
  candidate=normalize(std::move(candidate));
  if (candidate.port==0 || (config_.routingScope==RoutingScope::PublicBound?!publicAddress(candidate.address):!candidate.address.isLoopback()) || !candidate.address.scopeId().isEmpty()
      || candidate.address.protocol()!=socket_.localAddress().protocol()) { return std::unexpected(Failure::Peer); }
  if (allowed(candidate)) { return {}; }
  if (peers_.size()==config_.maximumPeers) { return std::unexpected(Failure::Capacity); }
  peers_.push_back(std::move(candidate));return {};
}
std::expected<Ticket,Failure> OwnedPingTransport::ping(Peer peer,std::int8_t remoteVersion,QObject* receiver,Callback callback,std::chrono::nanoseconds timeout) {
  return pingDetailed(std::move(peer),remoteVersion,receiver,ordinaryCallback<packet::Decoded>(std::move(callback)),timeout);
}
std::expected<Ticket,Failure> OwnedPingTransport::findNode(Peer peer,std::int8_t remoteVersion,find::nodes::NodeId target,QObject* receiver,FindNodeCallback callback,std::chrono::nanoseconds timeout) {
  return findNodeDetailed(std::move(peer),remoteVersion,target,receiver,ordinaryCallback<find::Decoded>(std::move(callback)),timeout);
}
std::expected<Ticket,Failure> OwnedPingTransport::findValue(Peer peer,std::int8_t remoteVersion,lookup::nodes::NodeId target,std::int32_t flags,std::int32_t maximumValues,QObject* receiver,FindValueCallback callback,std::chrono::nanoseconds timeout) {
  return findValueDetailed(std::move(peer),remoteVersion,target,flags,maximumValues,receiver,ordinaryCallback<lookup::Decoded>(std::move(callback)),timeout);
}
std::expected<Ticket,Failure> OwnedPingTransport::pingDetailed(Peer peer,std::int8_t remoteVersion,QObject* receiver,DetailedPingCallback callback,std::chrono::nanoseconds timeout) {
  return submit(std::move(peer),remoteVersion,receiver,detailedCallback<packet::Decoded>(std::move(callback)),timeout,1024,[](packet::udp::EncodeFields header) -> std::expected<std::string,Failure> {
    packet::RequestFields request;request.header=header;auto wire=packet::encodeRequest(request);
    if (!wire) { return std::unexpected(Failure::Encode); }return std::move(*wire);
  });
}
std::expected<Ticket,Failure> OwnedPingTransport::findNodeDetailed(Peer peer,std::int8_t remoteVersion,find::nodes::NodeId target,QObject* receiver,DetailedFindNodeCallback callback,std::chrono::nanoseconds timeout) {
  return submit(std::move(peer),remoteVersion,receiver,detailedCallback<find::Decoded>(std::move(callback)),timeout,1028,[target](packet::udp::EncodeFields header) -> std::expected<std::string,Failure> {
    find::RequestFields request;request.header=header;request.target=std::string_view(reinterpret_cast<const char*>(target.data()),target.size());
    auto wire=find::encodeRequest(request);if (!wire) { return std::unexpected(Failure::Encode); }return std::move(*wire);
  });
}
std::expected<Ticket,Failure> OwnedPingTransport::findValueDetailed(Peer peer,std::int8_t remoteVersion,lookup::nodes::NodeId target,std::int32_t flags,std::int32_t maximumValues,QObject* receiver,DetailedFindValueCallback callback,std::chrono::nanoseconds timeout) {
  return submit(std::move(peer),remoteVersion,receiver,detailedCallback<lookup::Decoded>(std::move(callback)),timeout,1030,[target,flags,maximumValues](packet::udp::EncodeFields header) -> std::expected<std::string,Failure> {
    lookup::RequestFields request;request.header=header;request.target=std::string_view(reinterpret_cast<const char*>(target.data()),target.size());request.flags=flags;request.maximumValues=maximumValues;
    auto wire=lookup::encodeRequest(request);if (!wire) { return std::unexpected(Failure::Encode); }return std::move(*wire);
  });
}
std::expected<Ticket,Failure> OwnedPingTransport::requestStoreToken(Peer peer,std::int8_t remoteVersion,QObject* receiver,DetailedFindNodeCallback callback,std::chrono::nanoseconds timeout) {
  return submit(std::move(peer),remoteVersion,receiver,detailedCallback<find::Decoded>(std::move(callback)),timeout,1028,[](packet::udp::EncodeFields header)->std::expected<std::string,Failure> {
    find::RequestFields request;request.header=header;request.target=std::string_view{};
    auto wire=find::encodeRequest(request);if (!wire) { return std::unexpected(Failure::Encode); }return std::move(*wire);
  });
}
std::expected<void,Failure> OwnedPingTransport::serveDatabase(std::shared_ptr<azureus_router::RouterOwner> router,
    std::shared_ptr<database::Owner> database,std::function<bool()> current,
    std::function<bool(std::move_only_function<void(bool)>)> preflight){
  if(!owner()){return std::unexpected(Failure::Thread);}
  if(state_!=State::Bound||normalRouter_||config_.generatedStoreService||config_.findNodeReply||config_.findValueReply||config_.storeReply||config_.storeErrorReply
      ||!router||!database||!current){return std::unexpected(Failure::State);}
  const auto local=nodeCache_->lookup(find::nodes::ResolvedEndpoint{originator(endpoint()),{}},config_.version);
  const auto actual=database->localContact();if(!local||!actual||!*actual||(**actual).id!=local->id||(**actual).address!=originator(endpoint())||(**actual).protocol!=config_.version){return std::unexpected(Failure::Authority);}
  const auto contact=router->find(local->id);if(!contact||!*contact||!(**contact).local){return std::unexpected(Failure::Authority);}
  try{
    if(!current()){return std::unexpected(Failure::Authority);}
    auto authority=auth::Authority::create(true);if(!authority){return std::unexpected(Failure::Authority);}
    auto admission=std::make_unique<policy::Admission>();normalContacts_.reserve(config_.maximumPeers);
    normalRouter_=std::move(router);normalDatabase_=std::move(database);normalCurrent_=std::move(current);normalPreflight_=std::move(preflight);
    localId_=local->id;authority_=std::move(*authority);admission_=std::move(admission);return {};
  }catch(...){return std::unexpected(Failure::Authority);}
}
bool OwnedPingTransport::normalCurrent()noexcept{
  if(!owner()||state_!=State::Bound||!normalRouter_||!normalDatabase_||!normalCurrent_||(knownDeadline_&&Clock::now()>=*knownDeadline_)){return false;}
  try{return normalCurrent_();}catch(...){return false;}
}
bool OwnedPingTransport::normalAlive(const find::nodes::Contact& contact)noexcept{
  if(!normalCurrent()){return false;}
  const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  // Router time is absolute steady milliseconds for the normal receiving owner.
  const auto accepted=normalRouter_->contactAlive({.id=contact.id,.instance=contact.instance,.sleeping=(static_cast<std::uint8_t>(contact.flags)&1U)!=0},now,QDateTime::currentMSecsSinceEpoch());
  if(!accepted){return false;}
  auto found=std::ranges::find(normalContacts_,contact.address,&find::nodes::Contact::address);
  if(found!=normalContacts_.end()){*found=contact;}
  else if(normalContacts_.size()<config_.maximumPeers){normalContacts_.push_back(contact);}
  else{return false;}return true;
}
std::expected<std::vector<find::nodes::Snapshot>,Failure> OwnedPingTransport::normalClosest(const find::nodes::NodeId& target){
  if(!normalCurrent()){return std::unexpected(Failure::Authority);}
  const auto state=normalRouter_->snapshot();if(!state||state->limited()){return std::unexpected(Failure::Capacity);}
  const auto closest=normalRouter_->closest(target,state->config().k,true);if(!closest){return std::unexpected(Failure::State);}
  std::vector<find::nodes::Contact> contacts;contacts.reserve(closest->count);
  for(std::size_t i=0;i<closest->count;++i){const auto& node=closest->values[i];
    if(node.local){const auto local=normalDatabase_->localContact();if(!local||!*local){return std::unexpected(Failure::State);}contacts.push_back(**local);}
    else{const auto found=std::ranges::find(normalContacts_,node.id,&find::nodes::Contact::id);if(found!=normalContacts_.end()&&found->protocolSource!=RoutingProtocolSource::ProbeHint){contacts.push_back(*found);}}
  }
  std::ranges::sort(contacts,[&](const auto& a,const auto& b){for(std::size_t i=0;i<target.size();++i){const auto x=a.id[i]^target[i],y=b.id[i]^target[i];if(x!=y){return x<y;}}return false;});
  std::vector<find::nodes::Snapshot> result;result.reserve(std::min<std::size_t>(state->config().k,contacts.size()));
  for(const auto& contact:contacts){if(result.size()==state->config().k){break;}result.push_back({true,contact.protocol,contact.address});}return result;
}
std::expected<GeneratedServiceState,Failure> OwnedPingTransport::generatedServiceState() const {
  if (!owner()) { return std::unexpected(Failure::Thread); }
  if (state_!=State::Bound || !config_.generatedStoreService || !authority_ || !admission_ || !localId_) { return std::unexpected(Failure::State); }
  const auto authority=authority_->statistics();if (!authority) { return std::unexpected(Failure::Authority); }
  const auto admission=admission_->state();if (!admission) { return std::unexpected(Failure::Authority); }
  return GeneratedServiceState{*localId_,*authority,*admission,serviceReceipts_.size()};
}
std::expected<std::optional<ServiceReceipt>,Failure> OwnedPingTransport::takeServiceReceipt() {
  if (!owner()) { return std::unexpected(Failure::Thread); }
  if (!config_.generatedStoreService) { return std::unexpected(Failure::State); }
  if (serviceReceipts_.empty()) { return std::optional<ServiceReceipt>{}; }
  std::optional<ServiceReceipt> receipt(std::in_place,std::move(serviceReceipts_.front()));serviceReceipts_.pop_front();return receipt;
}
std::expected<Ticket,Failure> OwnedPingTransport::store(Peer peer,std::int8_t remoteVersion,StoreRequest source,QObject* receiver,StoreCallback callback,std::chrono::nanoseconds timeout) {
  return submit(std::move(peer),remoteVersion,receiver,detailedCallback<storage::Decoded>(std::move(callback)),timeout,1026,[source=std::move(source)](packet::udp::EncodeFields header) -> std::expected<std::string,Failure> {
    if (source.keys.size()>255 || source.valueSets.size()>255) { return std::unexpected(Failure::Capacity); }
    std::size_t records=0;
    for (const auto& key:source.keys) { if (key.size()>255) { return std::unexpected(Failure::Encode); } }
    for (const auto& set:source.valueSets) {
      if (set.size()>256-records) { return std::unexpected(Failure::Capacity); }records+=set.size();
      for (const auto& value:set) { if (value.bytes.size()>512) { return std::unexpected(Failure::Encode); } }
    }
    std::vector<std::optional<std::string_view>> keys;keys.reserve(source.keys.size());
    for (const auto& key:source.keys) { keys.emplace_back(key); }
    std::vector<std::vector<std::optional<storage::values::Snapshot>>> snapshots;snapshots.reserve(source.valueSets.size());
    for (const auto& set:source.valueSets) {
      auto& snapshotsForSet=snapshots.emplace_back();snapshotsForSet.reserve(set.size());
      for (const auto& value:set) {
        storage::values::Snapshot snapshot;snapshot.version=value.version;snapshot.created=value.created;snapshot.bytes=value.bytes;
        snapshot.originator=storage::nodes::Snapshot{true,value.originator.protocol,value.originator.address};
        snapshot.flags=value.flags;snapshot.lifeHours=value.lifeHours;snapshot.replication=value.replication;snapshotsForSet.emplace_back(snapshot);
      }
    }
    std::vector<std::optional<storage::ValueSet>> sets;sets.reserve(snapshots.size());
    for (const auto& set:snapshots) { sets.emplace_back(storage::ValueSet{set}); }
    storage::RequestFields request;request.header=header;request.randomId=source.randomId;request.keys=storage::Keys{keys};request.valueSets=storage::ValueSets{sets};
    auto wire=storage::encodeRequest(request);if (!wire) { return std::unexpected(Failure::Encode); }return std::move(*wire);
  });
}
std::expected<qint64,Failure> OwnedPingTransport::writePacket(std::string_view wire,const Peer& peer,Clock::time_point end){
  // The Known preparation permit is finite also for OwnedLoopback. Recheck
  // after PING RNG/encoding, immediately before the one physical socket write.
  if(knownDeadline_){
    if(Clock::now()>=*knownDeadline_){++statistics_.authorityErrors;return std::unexpected(Failure::Deadline);}
    if(!normalCurrent()){++statistics_.authorityErrors;return std::unexpected(Failure::Authority);}
  }
  if(config_.routingScope==RoutingScope::PublicBound){
    bool current=false;try{current=state_==State::Bound&&allowed(peer)&&publicPermit_&&publicPermit_();}catch(...){}
    // This is after all RNG/encoding/allocation and before this physical write,
    // including each fragment/error reply. The callback checks its executing
    // worker permit's absolute deadline after taking the current stop lock.
    if(Clock::now()>=end){++statistics_.authorityErrors;return std::unexpected(Failure::Deadline);}
    if(!current){++statistics_.authorityErrors;return std::unexpected(Failure::Authority);}
  }
  return socket_.writeDatagram(wire.data(),static_cast<qint64>(wire.size()),peer.address,peer.port);
}
std::expected<Ticket,Failure> OwnedPingTransport::submit(Peer peer,std::int8_t remoteVersion,QObject* receiver,CompletionCallback callback,std::chrono::nanoseconds timeout,std::int32_t action,Encoder encode) {
  const auto entered=Clock::now();
  if (!owner()) { return std::unexpected(Failure::Thread); }
  if (state_!=State::Bound) { return std::unexpected(Failure::State); }
  if(config_.routingScope==RoutingScope::PublicBound){bool current=false;try{current=publicPermit_&&publicPermit_();}catch(...){}if(!current){return std::unexpected(Failure::Authority);}}
  if (!peer.address.scopeId().isEmpty()) { return std::unexpected(Failure::Peer); }
  peer=normalize(std::move(peer));if (!allowed(peer)) { return std::unexpected(Failure::Peer); }
  if (remoteVersion<51) { return std::unexpected(Failure::Version); }
  if (!receiver || receiver->thread()!=thread() || !callback) { return std::unexpected(Failure::Receiver); }
  if (timeout<=std::chrono::nanoseconds::zero() || timeout>std::chrono::seconds(30)) { return std::unexpected(Failure::Deadline); }
  if (pending_.size()+completions_.size()>=config_.maximumPending || nextTicket_==std::numeric_limits<std::uint64_t>::max()) { return std::unexpected(Failure::Capacity); }
  std::int64_t connection;std::int32_t transaction=0;bool available=false;
  try {
    connection=std::bit_cast<std::int64_t>(word()|(std::uint64_t{1}<<63));
    for (unsigned i=0;i<32;++i) { transaction=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(word()));if (!pending_.contains(transaction)) { available=true;break; } }
  } catch (...) { return std::unexpected(Failure::Random); }
  if (!available) { return std::unexpected(Failure::Capacity); }
  packet::udp::EncodeFields h;h.action=action;h.localVersion=config_.version;h.remoteVersion=remoteVersion;
  h.network=config_.network;h.instance=config_.instance;h.connection=connection;h.transaction=transaction;h.originator=originator(endpoint());
  h.now=QDateTime::currentMSecsSinceEpoch();h.flags=config_.flags;h.flags2=config_.flags2;
  const auto wire=encode(h);if (!wire) { return std::unexpected(wire.error()); }
  if (wire->size()>static_cast<std::size_t>(maximumDatagram)) { return std::unexpected(Failure::Encode); }
  const Ticket ticket{++nextTicket_};
  auto state=std::make_shared<CallbackState>(CallbackState{receiver,std::move(callback)});
  const auto started=Clock::now();
  const auto requestEnd=config_.routingScope==RoutingScope::PublicBound?entered+timeout:started+timeout;
  Pending pending{ticket,peer,connection,action+1,requestEnd,std::move(state),{},started};
  pending.death=connect(receiver,&QObject::destroyed,this,[this,ticket] { abandon(ticket); });
  pending_.emplace(transaction,std::move(pending));
  const auto sent=writePacket(*wire,peer,requestEnd);
  if(sent){observeSocketPayloadBytes(statistics_.sentPayloadBytes,*sent);}
  if (!sent||*sent!=static_cast<qint64>(wire->size())) {
    const auto failure=sent?Failure::Send:sent.error();
    const auto found=pending_.find(transaction);observeOperation(found->second,OperationEventKind::SendFailed);disconnect(found->second.death);pending_.erase(found);armDeadline();return std::unexpected(failure);
  }
  observeOperation(pending_.at(transaction),OperationEventKind::Sent);++statistics_.sent;armDeadline();return ticket;
}
void OwnedPingTransport::armDeadline() {
  if (state_!=State::Bound || pending_.empty()) { deadline_.stop();return; }
  auto earliest=Clock::time_point::max();for (const auto& [transaction,pending]:pending_) { (void)transaction;earliest=std::min(earliest,pending.expires); }
  deadline_.setInterval(std::max(Clock::duration::zero(),earliest-Clock::now()));deadline_.start();
}
void OwnedPingTransport::finish(std::int32_t transaction,Result outcome) {
  const auto found=pending_.find(transaction);if (found==pending_.end()) { return; }
  observePing(found->second,outcome);
  OperationEventKind kind=OperationEventKind::Reply;
  if(outcome && std::holds_alternative<remote_error::Decoded>(*outcome)){kind=OperationEventKind::RemoteError;}
  else if(!outcome){switch(outcome.error()){
    case Failure::Timeout:kind=OperationEventKind::Timeout;break;case Failure::Cancelled:kind=OperationEventKind::Cancelled;break;
    case Failure::Closed:kind=OperationEventKind::Closed;break;case Failure::Capacity:kind=OperationEventKind::Capacity;break;
    default:kind=OperationEventKind::SendFailed;break;}}
  observeOperation(found->second,kind);
  auto pending=std::move(found->second);pending_.erase(found);disconnect(pending.death);
  completions_.push_back({std::move(pending.callback),std::move(outcome)});armDeadline();scheduleDelivery();
}
void OwnedPingTransport::part(std::int32_t transaction,Result outcome) {
  const auto found=pending_.find(transaction);if (found==pending_.end()) { return; }
  if (pending_.size()+completions_.size()>=config_.maximumPending) { finish(transaction,std::unexpected(Failure::Capacity));return; }
  observeOperation(found->second,OperationEventKind::PartialReply);completions_.push_back({found->second.callback,std::move(outcome)});scheduleDelivery();
}
void OwnedPingTransport::abandon(Ticket ticket) {
  for (auto it=pending_.begin();it!=pending_.end();++it) {
    if (it->second.ticket==ticket) { observeOperation(it->second,OperationEventKind::Abandoned);disconnect(it->second.death);pending_.erase(it);++statistics_.abandoned;armDeadline();return; }
  }
}
bool OwnedPingTransport::cancel(Ticket ticket) {
  if (!owner()) { return false; }
  for (const auto& [transaction,pending]:pending_) {
    if (pending.ticket==ticket) { ++statistics_.cancelled;finish(transaction,std::unexpected(Failure::Cancelled));return true; }
  }return false;
}
void OwnedPingTransport::expire() {
  if (state_!=State::Bound) { return; }
  const auto now=Clock::now();std::vector<std::int32_t> expired;
  for (const auto& [transaction,pending]:pending_) { if (pending.expires<=now) { expired.push_back(transaction); } }
  for (auto transaction:expired) { ++statistics_.timeouts;finish(transaction,std::unexpected(Failure::Timeout)); }armDeadline();
}
void OwnedPingTransport::scheduleDelivery() {
  if (deliveryScheduled_ || delivering_ || completions_.empty()) { return; }
  deliveryScheduled_=true;QMetaObject::invokeMethod(this,[this] { deliveryScheduled_=false;deliver(); },Qt::QueuedConnection);
}
void OwnedPingTransport::deliver() {
  if (delivering_) { return; }delivering_=true;++statistics_.deliveryTurns;QPointer<OwnedPingTransport> guard(this);
  for (unsigned i=0;i<workPerTurn && !completions_.empty();++i) {
    auto completion=std::move(completions_.front());completions_.pop_front();
    if (completion.callback->receiver && completion.callback->receiver->thread()==thread()) {
      try { completion.callback->callback(std::move(completion.outcome)); }
      catch (...) { if (guard) { ++statistics_.callbackErrors; } }
      if (!guard) { return; }
    } else { ++statistics_.abandoned; }
  }
  delivering_=false;scheduleDelivery();
}
void OwnedPingTransport::close() {
  if (!owner() || state_==State::Closed) { return; }
  routingDeadline_.stop();normalSeeded_=false;routingIntents_.clear();for(auto& ping:routingPingsPending_){ping.reset();}
  publicPermit_={};routingRefreshes_.clear();importedRouting_.clear();normalCurrent_={};normalPreflight_={};normalRouter_.reset();normalDatabase_.reset();normalContacts_.clear();
  if(database_){database_->close();database_.reset();}
  state_=State::Closed;socket_.close();deadline_.stop();peers_.clear();authority_.reset();admission_.reset();localId_.reset();nodeCache_.reset();
  while (!pending_.empty()) { finish(pending_.begin()->first,std::unexpected(Failure::Closed)); }
  if(routing_&&!routing_->finished){const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();static_cast<void>(routing_->controller.close(now));for(auto& rpc:routing_->rpcs){if(rpc.node){static_cast<void>(routing_->controller.finish(rpc.node,azureus_control::LookupNodeState::Closed,now));rpc={};}}routing_->failure=Failure::Closed;routing_->finished=true;}captureRouting();
}
void OwnedPingTransport::answer(const Peer& peer,const packet::Decoded& request,std::int64_t receivedAt) {
  if (!config_.servePings) { return; }
  const auto& incoming=request.header().fields();packet::ReplyFields reply;auto& h=reply.header;
  h.network=config_.network;h.localVersion=config_.version;h.remoteVersion=incoming.originatorVersion;
  h.connection=incoming.connection;h.transaction=incoming.transaction;h.instance=config_.instance;h.flags=config_.flags;h.flags2=config_.flags2;
  h.now=QDateTime::currentMSecsSinceEpoch();h.receivedAt=receivedAt;packet::positions::Positions positions;
  if(config_.enableVivaldi){try{positions.emplace_back(packet::positions::vivaldiV1(vivaldi_.bits()));}catch(...){positions.clear();}}
  reply.positions=positions;
  const auto wire=packet::encodeReply(reply);
  if (!wire || wire->size()>static_cast<std::size_t>(maximumDatagram)) { ++statistics_.malformed;return; }
  const auto sent=writePacket(*wire,peer);if(!sent){return;}
  observeSocketPayloadBytes(statistics_.sentPayloadBytes,*sent);
  if (*sent==static_cast<qint64>(wire->size())) { ++statistics_.sent; }
}
void OwnedPingTransport::answer(const Peer& peer,const find::Decoded& request,std::int64_t receivedAt) {
  if (!config_.findNodeReply) { return; }
  const auto& incoming=request.header().fields();const auto& snapshot=*config_.findNodeReply;
  find::ReplyFields reply;auto& h=reply.header;h.network=config_.network;h.localVersion=config_.version;h.remoteVersion=incoming.originatorVersion;
  h.connection=incoming.connection;h.transaction=incoming.transaction;h.instance=config_.instance;h.flags=config_.flags;h.flags2=config_.flags2;
  h.now=QDateTime::currentMSecsSinceEpoch();h.receivedAt=receivedAt;find::positions::Positions positions;reply.positions=positions;
  reply.randomId=snapshot.randomId;reply.status=snapshot.status;reply.estimatedSize=snapshot.estimatedSize;reply.contacts=snapshot.contacts;
  const auto wire=find::encodeReply(reply);
  if (!wire || wire->size()>static_cast<std::size_t>(maximumDatagram)) { ++statistics_.malformed;return; }
  const auto sent=writePacket(*wire,peer);if(!sent){return;}
  observeSocketPayloadBytes(statistics_.sentPayloadBytes,*sent);
  if (*sent==static_cast<qint64>(wire->size())) { ++statistics_.sent; }
}
void OwnedPingTransport::answer(const Peer& peer,const lookup::Decoded& request,std::int64_t receivedAt) {
  if (!config_.findValueReply) { return; }const auto& incoming=request.header().fields();
  for (const auto& piece:*config_.findValueReply) {
    lookup::ReplyFields reply;auto& h=reply.header;h.network=config_.network;h.localVersion=config_.version;h.remoteVersion=incoming.originatorVersion;h.connection=incoming.connection;h.transaction=incoming.transaction;h.instance=config_.instance;h.flags=config_.flags;h.flags2=config_.flags2;h.now=QDateTime::currentMSecsSinceEpoch();h.receivedAt=receivedAt;
    reply.continuation=piece.continuation;reply.diversification=piece.diversification;reply.remoteClockSkew=request.header().skew()->delta;
    std::vector<std::optional<lookup::values::Snapshot>> values;
    if (piece.values) {
      values.reserve(piece.values->size());for (const auto& value:*piece.values) {
        lookup::values::Snapshot snapshot;snapshot.version=value.version;snapshot.created=value.created;snapshot.bytes=value.bytes;snapshot.originator=lookup::nodes::Snapshot{true,value.originator.protocol,value.originator.address};snapshot.flags=value.flags;snapshot.lifeHours=value.lifeHours;snapshot.replication=value.replication;values.push_back(snapshot);
      }reply.values=values;
    }
    reply.contacts=piece.contacts;lookup::positions::Positions positions;reply.positions=positions;
    const auto wire=lookup::encodeReply(reply);if (!wire || wire->size()>static_cast<std::size_t>(maximumDatagram)) { ++statistics_.malformed;return; }
    const auto sent=writePacket(*wire,peer);if(!sent){return;}
    observeSocketPayloadBytes(statistics_.sentPayloadBytes,*sent);
    if (*sent!=static_cast<qint64>(wire->size())) { ++statistics_.ioErrors;return; }++statistics_.sent;
  }
}
void OwnedPingTransport::answer(const Peer& peer,const storage::Decoded& request,std::int64_t receivedAt) {
  if (!config_.storeReply && !config_.storeErrorReply) { return; }
  const auto& incoming=request.header().fields();
  packet::udp::EncodeFields header;header.kind=packet::udp::Kind::Reply;header.network=config_.network;
  header.localVersion=config_.version;header.remoteVersion=incoming.originatorVersion;header.connection=incoming.connection;
  header.transaction=incoming.transaction;header.instance=config_.instance;header.flags=config_.flags;header.flags2=config_.flags2;
  header.now=QDateTime::currentMSecsSinceEpoch();header.receivedAt=receivedAt;
  std::expected<std::string,Failure> wire=std::unexpected(Failure::Encode);
  if (config_.storeErrorReply) {
    const auto& snapshot=*config_.storeErrorReply;remote_error::ReplyFields reply;reply.header=header;reply.header.action=1032;
    reply.type=snapshot.type;reply.originator=snapshot.originator;
    if (snapshot.request) { reply.request=*snapshot.request; }
    if (snapshot.signature) { reply.signature=*snapshot.signature; }
    auto encoded=remote_error::encodeReply(reply);if (encoded) { wire=std::move(*encoded); }
  } else {
    storage::ReplyFields reply;reply.header=header;reply.header.action=1027;reply.diversification=*config_.storeReply;
    auto encoded=storage::encodeReply(reply);if (encoded) { wire=std::move(*encoded); }
  }
  if (!wire || wire->size()>static_cast<std::size_t>(maximumDatagram)) { ++statistics_.malformed;return; }
  const auto sent=writePacket(*wire,peer);if(!sent){return;}
  observeSocketPayloadBytes(statistics_.sentPayloadBytes,*sent);
  if (*sent!=static_cast<qint64>(wire->size())) { ++statistics_.ioErrors;return; }
  ++statistics_.sent;
}
void OwnedPingTransport::sendServiceReply(ServiceReceipt& receipt,std::expected<std::string,Failure> wire) {
  if(normalRouter_&&!normalCurrent()){++statistics_.authorityErrors;return;}
  if (!wire || wire->size()>static_cast<std::size_t>(maximumDatagram)) {
    receipt.status=ServiceStatus::EncodeFailed;++statistics_.malformed;return;
  }
  const auto sent=writePacket(*wire,receipt.peer);if(!sent){receipt.status=ServiceStatus::AuthorityFailed;return;}
  observeSocketPayloadBytes(statistics_.sentPayloadBytes,*sent);
  if (*sent!=static_cast<qint64>(wire->size())) {
    receipt.status=ServiceStatus::SendFailed;++statistics_.ioErrors;return;
  }
  receipt.sent=true;receipt.status=receipt.addressMismatch?ServiceStatus::AddressRejected:ServiceStatus::Replied;++statistics_.sent;
}
void OwnedPingTransport::answerService(const Peer& peer,ServiceRequest request,std::int64_t receivedAt) {
  if(normalRouter_&&normalPreflight_&&!normalResuming_){
    auto resumed=[this,peer,request=std::move(request),receivedAt](bool accepted)mutable{
      if(state_!=State::Bound||!normalRouter_){return;}
      if(!accepted){++statistics_.authorityErrors;return;}
      if(!allowed(peer)&&!ownedRoutingPeer(peer)){return;}normalResuming_=true;
      const auto reset=qScopeGuard([&]{normalResuming_=false;});answerService(peer,std::move(request),receivedAt);
    };
    if(!normalPreflight_(std::move(resumed))){++statistics_.serviceCapacityDrops;}return;
  }
  const auto* service=config_.generatedStoreService?&*config_.generatedStoreService:nullptr;
  if(!service&&!normalCurrent()){++statistics_.authorityErrors;return;}
  if (service&&serviceReceipts_.size()>=service->maximumReceipts) { ++statistics_.serviceCapacityDrops;return; }
  ServiceReceipt receipt(peer,std::move(request));
  const auto& receivedHeader=std::visit([](const auto& incomingPacket)->const packet::udp::DecodedHeader& { return incomingPacket.header(); },receipt.request);
  const auto incoming=receivedHeader.fields();
  const auto retain=[&] { if(service){serviceReceipts_.push_back(std::move(receipt));} };
  const auto replyHeader=[&](std::int32_t action) {
    packet::udp::EncodeFields header;header.kind=packet::udp::Kind::Reply;header.action=action;
    header.network=config_.network;header.localVersion=config_.version;header.remoteVersion=incoming.originatorVersion;
    header.connection=incoming.connection;header.transaction=incoming.transaction;header.instance=config_.instance;
    header.flags=config_.flags;header.flags2=config_.flags2;header.now=QDateTime::currentMSecsSinceEpoch();header.receivedAt=receivedAt;return header;
  };
  if (!incoming.originator || !sameIp(originator(peer),*incoming.originator)) {
    receipt.addressMismatch=true;++statistics_.serviceAddressRejected;receipt.replyAction=1032;
    remote_error::ReplyFields reply;reply.header=replyHeader(1032);reply.type=1;reply.originator=remote_error::Endpoint{originator(peer)};
    auto encoded=remote_error::encodeReply(reply);std::expected<std::string,Failure> wire=std::unexpected(Failure::Encode);
    if (encoded) { wire=std::move(*encoded); }sendServiceReply(receipt,std::move(wire));retain();return;
  }
  const auto identity=nodeCache_->lookup(find::nodes::ResolvedEndpoint{*incoming.originator,{}},incoming.originatorVersion);
  if (!identity) {
    receipt.identityError=identity.error().code;receipt.status=ServiceStatus::AuthorityFailed;++statistics_.authorityErrors;retain();return;
  }
  find::nodes::Contact sender;sender.protocol=incoming.originatorVersion;sender.address=*incoming.originator;sender.id=identity->id;
  sender.instance=incoming.instance;sender.skew=receivedHeader.skew()->delta;sender.flags=incoming.flags;
  receipt.sender=sender;receipt.transportAlive=true;
  // The normal owned subset does not grant a returned routing endpoint from a
  // different claimed port. Generated original-IP projection stays unchanged.
  if(!service&&(sender.address!=originator(peer)||(dynamicRoutingScope(config_.routingScope)?!learnRoutingContact(sender,true):!normalAlive(sender)))){++statistics_.authorityErrors;return;}
  if(!service&&dynamicRoutingScope(config_.routingScope)){dispatchRoutingPings();captureRouting();}
  if (const auto* findRequest=std::get_if<find::Decoded>(&receipt.request)) {
    receipt.findNodeAlive=true;
    std::span<const find::nodes::Snapshot> contacts;std::vector<find::nodes::Snapshot> actualContacts;
    const auto& target=std::get<find::RequestBody>(findRequest->body()).target;
    if(target.size()==localId_->size()){
      if(service){contacts=service->findNodeContacts;}
      else{find::nodes::NodeId id;std::copy_n(reinterpret_cast<const std::uint8_t*>(target.data()),id.size(),id.begin());
        auto actual=normalClosest(id);if(!actual){++statistics_.policyErrors;return;}actualContacts=std::move(*actual);contacts=actualContacts;}
    }
    if(!service&&!normalCurrent()){++statistics_.authorityErrors;return;}
    const auto token=authority_->issueIp(incoming.originator);
    if (!token) {
      receipt.authorityError=token.error();receipt.status=ServiceStatus::AuthorityFailed;++statistics_.authorityErrors;retain();return;
    }
    find::ReplyFields reply;reply.header=replyHeader(1029);receipt.replyAction=1029;reply.randomId=token->value;
    reply.status=service?service->findNodeStatus:-1;reply.estimatedSize=service?service->estimatedSize:0;reply.contacts=contacts;find::positions::Positions positions;reply.positions=positions;
    auto encoded=find::encodeReply(reply);std::expected<std::string,Failure> wire=std::unexpected(Failure::Encode);
    if (encoded) { wire=std::move(*encoded); }sendServiceReply(receipt,std::move(wire));retain();return;
  }
  const auto& storeRequest=std::get<storage::RequestBody>(std::get<storage::Decoded>(receipt.request).body());
  policy::Input input;input.sender=sender.id;input.local=*localId_;
  input.now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  if(service){input.seeded=service->seeded;input.k=service->k;input.storageNetwork=service->storageNetwork;input.furthest=service->furthest;}
  else{
    const auto state=normalRouter_->snapshot();if(!state||state->limited()){++statistics_.policyErrors;return;}
    input.seeded=normalSeeded_;input.k=state->config().k;input.storageNetwork=config_.network;
    if(normalSeeded_){
      const auto factor=static_cast<std::size_t>(input.k+(config_.network==0?input.k/2:0));
      const auto group=normalRouter_->closest(*localId_,factor,true);if(!group||!group->count){++statistics_.policyErrors;return;}
      std::vector<find::nodes::NodeId> ids;ids.reserve(group->count);for(std::size_t i=0;i<group->count;++i){ids.push_back(group->values[i].id);}
      std::ranges::sort(ids,[&](const auto& a,const auto& b){for(std::size_t i=0;i<a.size();++i){const auto x=a[i]^(*localId_)[i],y=b[i]^(*localId_)[i];if(x!=y){return x<y;}}return false;});
      input.furthest=ids[std::min(factor,ids.size())-1];
    }
  }
  auto* const db=activeDatabase();
  input.keys.reserve(storeRequest.keys.size());input.valueOriginators.reserve(storeRequest.valueSets.size());
  for (const auto& key:storeRequest.keys) {
    policy::Key current;current.bytes=key;
    if(service){const auto found=std::ranges::find(service->keyStates,key,&policy::Key::bytes);if(found!=service->keyStates.end()){current=*found;}}
    else{
      if(key.size()!=localId_->size()){++statistics_.policyErrors;return;}
      find::nodes::NodeId target;std::copy_n(reinterpret_cast<const std::uint8_t*>(key.data()),target.size(),target.begin());
      const auto factor=static_cast<std::size_t>(input.k+(config_.network==0?input.k/2:0));
      const auto closest=normalRouter_->localInClosest(target,factor,true);if(!closest){++statistics_.policyErrors;return;}current.closest=*closest;
    }
    if(db){const auto exists=db->contains(key);if(!exists){receipt.databaseError=exists.error();receipt.status=ServiceStatus::DatabaseFailed;retain();return;}current.exists=*exists;}
    input.keys.push_back(std::move(current));
  }
  for (const auto& set:storeRequest.valueSets) {
    auto& origins=input.valueOriginators.emplace_back();origins.reserve(set.size());for (const auto& value:set) { origins.push_back(value.originator.id); }
  }
  receipt.admission.emplace(admission_->admit(input,[&](bool)->std::expected<bool,policy::ErrorCode> {
    const auto verified=authority_->verifyIp(incoming.originator,storeRequest.randomId);
    if (!verified) { receipt.authorityError=verified.error();return std::unexpected(policy::ErrorCode::Authority); }return *verified;
  }));
  auto& result=*receipt.admission;
  if (!result) {
    if (receipt.authorityError) { receipt.status=ServiceStatus::AuthorityFailed;++statistics_.authorityErrors; }
    else { receipt.status=ServiceStatus::PolicyFailed;++statistics_.policyErrors; }
    retain();return;
  }
  auto& decision=*result;std::expected<std::string,Failure> wire=std::unexpected(Failure::Encode);
  if(db){
    struct Wall final:database::lifecycle::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}} clock;
    for(const auto& event:decision.events){if(event.type!=policy::EventType::Store){continue;}
      if(event.index>=storeRequest.keys.size()||event.index>=storeRequest.valueSets.size()
          ||(!decision.blocked()&&(!decision.diversification||event.index>=decision.diversification->size()))){receipt.status=ServiceStatus::DatabaseFailed;retain();return;}
      if(!service&&!normalCurrent()){++statistics_.authorityErrors;return;}
      const auto stored=db->store(storeRequest.keys[event.index],sender,storeRequest.valueSets[event.index],clock);
      if(!stored){receipt.databaseError=stored.error();receipt.status=ServiceStatus::DatabaseFailed;retain();return;}
      if(decision.diversification&&event.index<decision.diversification->size()){(*decision.diversification)[event.index]=*stored;}
    }
  }
  if (decision.blocked() && sender.protocol>=14) {
    remote_error::ReplyFields reply;reply.header=replyHeader(1032);receipt.replyAction=1032;reply.type=2;
    if (decision.blockRequest) { reply.request=*decision.blockRequest; }
    if (decision.blockSignature) { reply.signature=*decision.blockSignature; }
    auto encoded=remote_error::encodeReply(reply);if (encoded) { wire=std::move(*encoded); }
  } else {
    storage::ReplyFields reply;reply.header=replyHeader(1027);receipt.replyAction=1027;std::optional<std::string> diversification;
    if (decision.blocked()) { diversification=std::string(storeRequest.keys.size(),'\0'); }
    else if (decision.diversification) {
      diversification.emplace();diversification->reserve(decision.diversification->size());
      for (auto value:*decision.diversification) { diversification->push_back(static_cast<char>(std::bit_cast<std::uint8_t>(value))); }
    }
    if (diversification) { reply.diversification=*diversification; }
    auto encoded=storage::encodeReply(reply);if (encoded) { wire=std::move(*encoded); }
  }
  if(!service&&!normalCurrent()){++statistics_.authorityErrors;return;}
  sendServiceReply(receipt,std::move(wire));retain();
}
std::expected<database::Snapshot,Failure> OwnedPingTransport::databaseState()const{
  if(!owner()){return std::unexpected(Failure::Thread);}const auto* db=activeDatabase();if(!db){return std::unexpected(Failure::State);}
  const auto value=db->snapshot();if(!value){return std::unexpected(Failure::State);}return *value;
}
void OwnedPingTransport::answerDatabase(const Peer& peer,const lookup::Decoded& request,std::int64_t receivedAt){
  if(normalRouter_&&normalPreflight_&&!normalResuming_){
    auto resumed=[this,peer,request,receivedAt](bool accepted){
      if(state_!=State::Bound||!normalRouter_||!allowed(peer)){return;}
      if(!accepted){++statistics_.authorityErrors;return;}normalResuming_=true;
      const auto reset=qScopeGuard([&]{normalResuming_=false;});answerDatabase(peer,request,receivedAt);
    };
    if(!normalPreflight_(std::move(resumed))){++statistics_.serviceCapacityDrops;}return;
  }
  auto* const db=activeDatabase();if(!db||!nodeCache_||(!config_.generatedStoreService&&!normalCurrent())){return;}
  const auto& incoming=request.header().fields();
  const auto header=[&](std::int32_t action){packet::udp::EncodeFields h;h.kind=packet::udp::Kind::Reply;h.action=action;
    h.network=config_.network;h.localVersion=config_.version;h.remoteVersion=incoming.originatorVersion;
    h.connection=incoming.connection;h.transaction=incoming.transaction;h.instance=config_.instance;h.flags=config_.flags;h.flags2=config_.flags2;
    h.now=QDateTime::currentMSecsSinceEpoch();h.receivedAt=receivedAt;return h;};
  const auto send=[&](const std::expected<std::string,lookup::Error>& encoded){
    if(normalRouter_&&!normalCurrent()){++statistics_.authorityErrors;return false;}
    if(!encoded||encoded->size()>static_cast<std::size_t>(maximumDatagram)){++statistics_.malformed;return false;}
    const auto sent=writePacket(*encoded,peer);if(!sent){return false;}
    observeSocketPayloadBytes(statistics_.sentPayloadBytes,*sent);
    if(*sent!=static_cast<qint64>(encoded->size())){++statistics_.ioErrors;return false;}++statistics_.sent;return true;
  };
  if(!incoming.originator||!sameIp(originator(peer),*incoming.originator)){
    remote_error::ReplyFields reply;reply.header=header(1032);reply.type=1;reply.originator=remote_error::Endpoint{originator(peer)};
    const auto encoded=remote_error::encodeReply(reply);if(!encoded||encoded->size()>static_cast<std::size_t>(maximumDatagram)){++statistics_.malformed;return;}
    const auto sent=writePacket(*encoded,peer);if(!sent){return;}observeSocketPayloadBytes(statistics_.sentPayloadBytes,*sent);
    if(*sent==static_cast<qint64>(encoded->size())){++statistics_.sent;}else{++statistics_.ioErrors;}return;
  }
  const auto id=nodeCache_->lookup(find::nodes::ResolvedEndpoint{*incoming.originator,{}},incoming.originatorVersion);
  if(!id){++statistics_.authorityErrors;return;}
  find::nodes::Contact reader;reader.id=id->id;reader.address=*incoming.originator;reader.protocol=incoming.originatorVersion;
  reader.instance=incoming.instance;reader.flags=incoming.flags;reader.skew=request.header().skew()->delta;
  const auto& query=std::get<lookup::RequestBody>(request.body());
  struct Wall final:database::lifecycle::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}} clock;
  if(normalRouter_&&(reader.address!=originator(peer)||!normalCurrent())){++statistics_.authorityErrors;return;}
  const auto result=db->read(query.target,reader,query.maximumValues,query.flags,clock);
  if(!result){++statistics_.policyErrors;return;}
  if(normalRouter_&&!normalAlive(reader)){++statistics_.authorityErrors;return;}
  if(!*result){
    lookup::ReplyFields reply;reply.header=header(1031);std::vector<find::nodes::Snapshot> actualContacts;
    if(config_.generatedStoreService){reply.contacts=config_.generatedStoreService->findNodeContacts;}
    else{find::nodes::NodeId target;if(query.target.size()!=target.size()){++statistics_.policyErrors;return;}
      std::copy_n(reinterpret_cast<const std::uint8_t*>(query.target.data()),target.size(),target.begin());
      auto contacts=normalClosest(target);if(!contacts){++statistics_.policyErrors;return;}actualContacts=std::move(*contacts);reply.contacts=actualContacts;}
    reply.positions=std::span<const std::optional<lookup::positions::WirePosition>>{};send(lookup::encodeReply(reply));return;
  }
  const auto& values=(**result).values;
  if(values.empty()){
    lookup::ReplyFields reply;reply.header=header(1031);reply.diversification=(**result).diversification;
    reply.values=lookup::ValueSet{};send(lookup::encodeReply(reply));return;
  }
  // One real bounded value per fragment; no partial datagram or guessed value.
  for(std::size_t i=0;i<values.size();++i){
    const auto& value=values[i];lookup::values::Snapshot snapshot{value.version,value.created,value.bytes,
        find::nodes::Snapshot{true,value.originator.protocol,value.originator.address},value.flags,value.lifeHours,value.replication};
    std::array<std::optional<lookup::values::Snapshot>,1> snapshots{snapshot};
    lookup::ReplyFields reply;reply.header=header(1031);reply.continuation=i+1<values.size();reply.diversification=(**result).diversification;
    reply.values=lookup::ValueSet{snapshots};reply.remoteClockSkew=reader.skew;
    if(!send(lookup::encodeReply(reply))){return;}
  }
}

void OwnedPingTransport::consume() {
  if (draining_ || state_!=State::Bound) { return; }draining_=true;++statistics_.readTurns;
  for (unsigned i=0;i<workPerTurn && state_==State::Bound && socket_.hasPendingDatagrams();++i) {
    const auto size=socket_.pendingDatagramSize();++statistics_.received;
    if (size>maximumDatagram) {
      socket_.receiveDatagram(0);statistics_.receivedPayloadBytes.reset();
      // Existing bounded discard intentionally obtains no actual full payload.
      // Do not substitute the peeked size,0 or an assumed packet-size constant.
      ++statistics_.oversized;continue;
    }
    const auto datagram=socket_.receiveDatagram(maximumDatagram);
    if (!datagram.isValid()) { statistics_.receivedPayloadBytes.reset();++statistics_.ioErrors;break; }
    const auto data=datagram.data();
    observeSocketPayloadBytes(statistics_.receivedPayloadBytes,data.size());
    if (datagram.senderPort()<=0 || datagram.senderPort()>65535) { ++statistics_.malformed;continue; }
    const auto peer=normalize({datagram.senderAddress(),static_cast<quint16>(datagram.senderPort())});
    const bool unknown=!allowed(peer);
    if(unknown&&(config_.routingScope==RoutingScope::PublicBound||!normalRouter_||!ownedRoutingPeer(peer))){++statistics_.unowned;continue;}
    if (data.isEmpty()) { ++statistics_.malformed;continue; }
    const auto kind=(static_cast<unsigned char>(data.front())&128)?packet::udp::Kind::Request:packet::udp::Kind::Reply;
    std::string bytes(data.constData(),static_cast<std::size_t>(data.size()));
    // Bind supported reply type/transaction/connection/endpoint before parsing
    // contact bodies, so foreign replies cannot warm the node cache.
    if (bytes.size()<16) { ++statistics_.malformed;continue; }
    const auto action=readInt(bytes,kind==packet::udp::Kind::Request?8:0);
    if(unknown&&(kind!=packet::udp::Kind::Request||(action!=1024&&action!=1028))){++statistics_.unowned;continue;}
    if ((kind==packet::udp::Kind::Request && action!=1024 && action!=1026 && action!=1028 && action!=1030)
        || (kind==packet::udp::Kind::Reply && action!=1025 && action!=1027 && action!=1029 && action!=1031 && action!=1032)) { ++statistics_.malformed;continue; }
    if (kind==packet::udp::Kind::Reply) {
      const auto transaction=readInt(bytes,4);const auto connection=readLong(bytes,8);const auto found=pending_.find(transaction);
      if (found==pending_.end() || found->second.peer!=peer || found->second.connection!=connection
          || (found->second.expectedAction!=action && action!=1032)) { ++statistics_.foreign;continue; }
      if (found->second.expires<=Clock::now()) { ++statistics_.timeouts;finish(transaction,std::unexpected(Failure::Timeout));continue; }
    }
    packet::udp::Context headerContext;headerContext.network=config_.network;headerContext.localVersion=config_.version;headerContext.now=QDateTime::currentMSecsSinceEpoch();
    if (action==1024 || action==1025) {
      packet::Context context;context.header=headerContext;context.monotonicNow=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
      auto decoded=packet::decode(kind,std::move(bytes),context);if (!decoded) { ++statistics_.malformed;continue; }
      if (kind==packet::udp::Kind::Request) { ++statistics_.requests;if(normalRouter_&&dynamicRoutingScope(config_.routingScope)){answerRoutingPing(peer,std::move(*decoded),headerContext.now);}else{answer(peer,*decoded,headerContext.now);}continue; }
      const auto transaction=decoded->header().fields().transaction;
      if (pending_.at(transaction).expires<=Clock::now()) { ++statistics_.timeouts;finish(transaction,std::unexpected(Failure::Timeout));continue; }
      observeVivaldi(pending_.at(transaction),*decoded);
      ++statistics_.replies;finish(transaction,Value{std::in_place_type<packet::Decoded>,std::move(*decoded)});
    } else if (action==1028 || action==1029) {
      find::Context context;context.header=headerContext;auto decoded=find::decode(kind,std::move(bytes),context,*nodeCache_);
      if (!decoded) { ++statistics_.malformed;continue; }
      if (kind==packet::udp::Kind::Request) {
        ++statistics_.requests;
        if (config_.generatedStoreService||normalRouter_) { answerService(peer,ServiceRequest{std::in_place_type<find::Decoded>,std::move(*decoded)},headerContext.now); }
        else { answer(peer,*decoded,headerContext.now); }continue;
      }
      const auto transaction=decoded->header().fields().transaction;
      if (pending_.at(transaction).expires<=Clock::now()) { ++statistics_.timeouts;finish(transaction,std::unexpected(Failure::Timeout));continue; }
      ++statistics_.replies;finish(transaction,Value{std::in_place_type<find::Decoded>,std::move(*decoded)});
    } else if (action==1030 || action==1031) {
      lookup::Context context;context.header=headerContext;auto decoded=lookup::decode(kind,std::move(bytes),context,*nodeCache_);
      if (!decoded) { ++statistics_.malformed;continue; }
      if (kind==packet::udp::Kind::Request) { ++statistics_.requests;if(activeDatabase()){answerDatabase(peer,*decoded,headerContext.now);}else{answer(peer,*decoded,headerContext.now);}continue; }
      const auto transaction=decoded->header().fields().transaction;
      if (pending_.at(transaction).expires<=Clock::now()) { ++statistics_.timeouts;finish(transaction,std::unexpected(Failure::Timeout));continue; }
      const auto continuation=std::get<lookup::ReplyBody>(decoded->body()).continuation;++statistics_.replies;
      if (continuation) { part(transaction,Value{std::in_place_type<lookup::Decoded>,std::move(*decoded)}); }
      else { finish(transaction,Value{std::in_place_type<lookup::Decoded>,std::move(*decoded)}); }
    } else if (action==1026 || action==1027) {
      storage::Context context;context.header=headerContext;auto decoded=storage::decode(kind,std::move(bytes),context,*nodeCache_);
      if (!decoded) { ++statistics_.malformed;continue; }
      if (kind==packet::udp::Kind::Request) {
        ++statistics_.requests;
        if (config_.generatedStoreService||normalRouter_) { answerService(peer,ServiceRequest{std::in_place_type<storage::Decoded>,std::move(*decoded)},headerContext.now); }
        else { answer(peer,*decoded,headerContext.now); }continue;
      }
      const auto transaction=decoded->header().fields().transaction;
      if (pending_.at(transaction).expires<=Clock::now()) { ++statistics_.timeouts;finish(transaction,std::unexpected(Failure::Timeout));continue; }
      ++statistics_.replies;finish(transaction,Value{std::in_place_type<storage::Decoded>,std::move(*decoded)});
    } else {
      auto decoded=remote_error::decode(std::move(bytes),headerContext);
      if (!decoded) { ++statistics_.malformed;continue; }
      const auto transaction=decoded->header().fields().transaction;
      if (pending_.at(transaction).expires<=Clock::now()) { ++statistics_.timeouts;finish(transaction,std::unexpected(Failure::Timeout));continue; }
      ++statistics_.replies;finish(transaction,Value{std::in_place_type<remote_error::Decoded>,std::move(*decoded)});
    }
  }
  draining_=false;
  if (state_==State::Bound && socket_.hasPendingDatagrams() && !drainScheduled_) {
    drainScheduled_=true;QMetaObject::invokeMethod(this,[this] { drainScheduled_=false;consume(); },Qt::QueuedConnection);
  }
}
}

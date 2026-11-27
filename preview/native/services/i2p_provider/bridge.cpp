// SPDX-License-Identifier: GPL-2.0-or-later
#include "bridge.hpp"
#include "sam_protocol.hpp"
#include "source_permit.hpp"
#include "contact_owner.hpp"
#include "generate_identity.hpp"
#include "pending_magnet_source.hpp"
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#if __has_include(<libtorrent/biglybtc_mixed_i2p_transport.hpp>)
#include <libtorrent/biglybtc_mixed_i2p_transport.hpp>
#define BIGLYBTC_BRIDGE_HAS_MIXED_HEADER 1
#else
#define BIGLYBTC_BRIDGE_HAS_MIXED_HEADER 0
#endif
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QMetaObject>
#ifdef _WIN32
#include "state_file_io.hpp"
#include <QWinEventNotifier>
#include <windows.h>
#else
#include <QSocketNotifier>
#ifdef __APPLE__
#include <fcntl.h>
#include <sys/event.h>
#include <sys/stat.h>
#else
#include <sys/inotify.h>
#endif
#include <unistd.h>
#endif
#include <cerrno>
#include <variant>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <cstring>

namespace biglybtc::services::i2p::provider {
struct Bridge::Impl {
  using Clock=std::chrono::steady_clock;
  enum class SocketPhase {Hello,Command,Control,Lookup,Connecting,Accept,Data};
  struct Socket {
    std::unique_ptr<QTcpSocket> tcp;SocketPhase phase=SocketPhase::Hello;
    Clock::time_point end{};std::uint64_t operation=0,lookup=0;
    bool authenticated=false,peer=false,eof=false,finished=false,statusSent=false;
    std::string requestedName;
    std::optional<std::uint16_t> trackerLookupPort{};
  };
  Bridge* q;std::shared_ptr<ProfileSource> source;QPointer<i2cp::InitialChannel> channel;
  QTcpServer server;QTimer timer;std::unique_ptr<SourcePermit> permit;bool sourcePaused=false;
#ifdef _WIN32
  struct DirectoryWatch {
    storage::detail::StateDescriptor directory, event;
    OVERLAPPED io{};
    alignas(DWORD) std::array<std::byte, 32768> buffer{};
    std::vector<std::wstring> criticalNames;
    std::unique_ptr<QWinEventNotifier> notifier;
    bool pending = false;

    bool arm() {
      if (!::ResetEvent(event.get())) { return false; }
      io = {};
      io.hEvent = event.get();
      constexpr DWORD events = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
          | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE
          | FILE_NOTIFY_CHANGE_SECURITY;
      pending = ::ReadDirectoryChangesW(directory.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
          FALSE, events, nullptr, &io, nullptr) != FALSE;
      return pending;
    }

    ~DirectoryWatch() {
      notifier.reset();
      if (pending) {
        ::CancelIoEx(directory.get(), &io);
        DWORD unused = 0;
        ::GetOverlappedResult(directory.get(), &io, &unused, TRUE);
      }
    }
  };
  std::vector<std::unique_ptr<DirectoryWatch>> watchers;
#else
  int watch=-1;std::unique_ptr<QSocketNotifier> watcher;std::vector<int> criticalWatches;
#endif
  const BridgeCapacity capacity;std::vector<std::optional<Socket>> sockets;
  std::vector<Destination> peers;std::vector<Destination> resolved;std::vector<std::uint64_t> retired;
  std::optional<std::size_t> control;
  Ports ports;Phase phase=Phase::Idle;std::string sessionId;Clock::time_point preparingEnd{};
  std::array<Byte,32> capability{};std::array<Byte,12> peerIdMask{};std::uint64_t generation=0,admission=0,activeRevision=0,rejectedClients=0;
  bool queued=false,notifyQueued=false,wanted=false,executing=false;
  std::function<bool(void*)> install;const char* failure=nullptr;
  using DiscoverySource = std::variant<std::shared_ptr<ProfileSource>, std::shared_ptr<SubscriptionSource>>;
  struct Discovery {
    DiscoverySource source;core::TorrentHash selected;dht::Id wire;
    std::uint64_t admission=0;bool seeding=false;Clock::time_point next{},end{};
    std::function<bool(const Hash&,std::uint64_t)> deliver;std::vector<Hash> delivered;
    std::optional<Clock::time_point> firstSubscriptionTurn;
    std::optional<dht::Id> versionKey{};
    Clock::time_point nextPresence=Clock::time_point::max();
  };
  std::shared_ptr<ContactExchange> contactExchange;
  std::unique_ptr<dht::Actor> discovery;std::vector<Discovery> discoveries;
  std::optional<std::uint64_t> discoveryAdmission;std::uint64_t discoveryOperation=0;
  std::optional<dht::Id> publishingVersion;
  struct PublicationTransport {
    std::unique_ptr<i2cp::InitialChannel> channel;
    std::unique_ptr<dht::Actor> actor;
    std::optional<ClientIdentity> identity;
  };
  std::optional<i2cp::InitialChannel::Options> publicationRouter;
  dht::Actor::Options publicationRoutes;
  PublicationTransport publisher;
  Clock::time_point publisherRetry{};
  std::shared_ptr<const engine::SubscriptionSeedPresence> subscriptionPresence(const Discovery& job) const {
    const auto* child=std::get_if<std::shared_ptr<SubscriptionSource>>(&job.source);
    return child?(*child)->presence(activeRevision):nullptr;
  }
  std::optional<dht::AzureusValue> readSubscriptionPresence(const dht::Id& key) const {
    if (!executing||!current()||source->mixedMode()) { return {}; }
    const std::string_view target(reinterpret_cast<const char*>(key.data()),key.size());
    std::shared_ptr<const engine::SubscriptionSeedPresence> match;
    for (const auto& job:discoveries) {
      const auto publication=subscriptionPresence(job);
      if (!publication||std::string_view(publication->dhtKey.data(),publication->dhtKey.size())!=target) { continue; }
      if (match) { return {}; }
      match=publication;
    }
    if (!match||!current()) { return {}; }
    dht::AzureusValue value;
    value.version=match->version;
    value.bytes.assign(match->wire.begin(),match->wire.end());
    value.flags=0x0010; // DHT.FLAG_ANON from the pure I2P single-value DDB write.
    return value;
  }
  bool beginDiscovery(const dht::Actor::Options& options,std::shared_ptr<ContactExchange> contacts,
      const i2cp::InitialChannel::Options* routerOptions){
    if(discovery||!channel||phase==Phase::Idle||phase==Phase::Withdrawn||(contacts&&(!options.inbound||!options.routingMaintenance))){return false;}
    auto actor=std::make_unique<dht::Actor>(*channel,q);
    auto selected=options;
    selected.azureusValues=!source->mixedMode();
    selected.azureusReplicas=selected.azureusValues;
    dht::AzureusLocalRead read;
    if (selected.azureusValues) {
      read=[this](const dht::Id& key){return readSubscriptionPresence(key);};
    }
    if(!actor->begin(selected,std::move(read))){return false;}
    if (selected.azureusValues&&routerOptions) {
      publicationRouter=*routerOptions;
      publicationRoutes.seeds=options.seeds;
      publicationRoutes.parallelism=options.parallelism;
      publicationRoutes.azureusValues=true;
    }
    QObject::connect(actor.get(),&dht::Actor::changed,q,[this]{schedule();});discovery=std::move(actor);contactExchange=std::move(contacts);return true;
  }
  bool discover(std::shared_ptr<ProfileSource> child,const core::TorrentHash& selected,
      const dht::Id& wire,std::uint64_t route,bool seeding,std::function<bool(const Hash&,std::uint64_t)> deliver){
    if(!onThread()||!discovery||!current()||!route||!deliver||!child||!child->prepared()
        ||!child->discoveryAllowed()||child->hash()!=selected||!child->lease()
        ||!source->lease()||!child->lease()->sharesOwnership(*source->lease())
        ||child->destination().wire!=source->destination().wire||child->mixedMode()!=source->mixedMode()){return false;}
    return addDiscovery(std::move(child), selected, wire, route, seeding, std::move(deliver));
  }
  bool discoverSubscription(std::shared_ptr<SubscriptionSource> child, std::uint64_t route,
      std::function<bool(const Hash&,std::uint64_t)> deliver) {
    if (!onThread() || !discovery || !current() || !route || !deliver || !child || child->revoked()
        || source->mixedMode() || !source->lease() || !child->lease_->sharesOwnership(*source->lease())) { return false; }
    const auto selected = child->hash();
    if (selected.bytes().size() != dht::Id{}.size()) { return false; }
    dht::Id wire{};
    std::ranges::transform(selected.bytes(), wire.begin(), [](std::byte value) { return std::to_integer<Byte>(value); });
    return addDiscovery(std::move(child), selected, wire, route, true, std::move(deliver));
  }
  bool addDiscovery(DiscoverySource child, const core::TorrentHash& selected, const dht::Id& wire,
      std::uint64_t route, bool seeding, std::function<bool(const Hash&,std::uint64_t)> deliver) {
    auto found=std::find_if(discoveries.begin(),discoveries.end(),[&](const auto& value){return value.admission==route;});
    if(found!=discoveries.end()){return found->source==child&&found->selected==selected&&found->wire==wire;}
    if(discoveries.size()>=MaximumDiscoveryJobs){return false;}
    discoveries.push_back({std::move(child),selected,wire,route,seeding,Clock::now(),{},std::move(deliver),{},{}});schedule();return true;
  }
  bool discoveryRetired(const Discovery& job) const {
    if (const auto* child = std::get_if<std::shared_ptr<ProfileSource>>(&job.source)) {
      return !(*child)->prepared() || !(*child)->discoveryAllowed();
    }
    return std::get<std::shared_ptr<SubscriptionSource>>(job.source)->revoked();
  }
  bool discoveryCurrent(const Discovery& job) const {
    if (discoveryRetired(job)) { return false; }
    if (const auto* child = std::get_if<std::shared_ptr<SubscriptionSource>>(&job.source)) {
      return (*child)->current(activeRevision);
    }
    return true;
  }
  void cancelDiscovery(std::uint64_t route){
    if(discoveryAdmission&&*discoveryAdmission==route){cancelDiscoveryOperation();}
    std::erase_if(discoveries,[&](const auto& value){return value.admission==route;});
  }
  void finishDiscovery() {
    discoveryAdmission.reset();discoveryOperation=0;publishingVersion.reset();
  }
  void cancelDiscoveryOperation() {
    auto* actor=publishingVersion?publisher.actor.get():discovery.get();
    if (actor&&discoveryOperation) { actor->cancel(discoveryOperation); }
    finishDiscovery();
  }
  void closePublisher() {
    if (publishingVersion) { cancelDiscoveryOperation(); }
    if (publisher.actor) { publisher.actor->close();publisher.actor.reset(); }
    if (publisher.channel) { publisher.channel->close();publisher.channel.reset(); }
    publisher.identity.reset();
  }
  void retryPublisher() {
    publisherRetry=Clock::now()+std::chrono::seconds(30);
    closePublisher();
  }
  bool startPublisher() {
    if (!publicationRouter||source->mixedMode()||!current()||!executing||Clock::now()<publisherRetry) { return false; }
    if (publisher.channel) { return true; }
    // A transient pure Destination keeps the version STORE separate from the
    // persistent torrent/peer identity. The existing worker still owns all I/O.
    auto identity=generateClientIdentity();
    if (!identity||!identity->destination()) { return false; }
    auto channel=std::make_unique<i2cp::InitialChannel>(q);
    if (!channel->bindEffectAuthority(source->effectAuthority())) { return false; }
    QObject::connect(channel.get(),&i2cp::InitialChannel::sourceEffectRequested,q,[this]{schedule();});
    QObject::connect(channel.get(),&i2cp::InitialChannel::stateChanged,q,[this]{schedule();});
    QObject::connect(channel.get(),&i2cp::InitialChannel::leaseSetSubmitted,q,[this]{schedule();});
    if (!channel->begin(*publicationRouter)) { return false; }
    publisher.identity=std::move(*identity);
    publisher.channel=std::move(channel);
    return true;
  }
  void pumpPublisher() {
    if (!publisher.channel) { return; }
    if (publishingVersion&&!readSubscriptionPresence(*publishingVersion)) { cancelDiscoveryOperation(); }
    auto& wire=*publisher.channel;
    if (!wire.resumeAuthorizedEffects()) { retryPublisher();return; }
    using State=i2cp::InitialChannel::State;
    if (wire.state()==State::DateReceived&&publisher.identity) {
      auto session=channel->sessionOptions();
      std::erase_if(session,[](const auto& option){return option.first=="inbound.nickname"||option.first=="outbound.nickname";});
      const auto wall=QDateTime::currentMSecsSinceEpoch();
      if (wall<=0 || !wire.createLeaseSession(std::move(*publisher.identity),session,static_cast<std::uint64_t>(wall))) {
        retryPublisher();return;
      }
      publisher.identity.reset();
    }
    if (!publisher.actor&&wire.state()==State::SessionCreated&&wire.leaseSetSubmission()) {
      const auto destination=wire.ownedLeaseDestination();
      std::array<Byte,16> random{};
      if (!destination||RAND_bytes(random.data(),static_cast<int>(random.size()))!=1) { retryPublisher();return; }
      auto options=publicationRoutes;
      options.queryPort=static_cast<std::uint16_t>(10000+((unsigned(random[0])<<8)|random[1])%55534);
      auto id=dht::makeNodeId(destination->hash,options.queryPort,View(random).subspan(2));
      if (!id) { retryPublisher();return; }
      options.localId=*id;
      auto actor=std::make_unique<dht::Actor>(wire,q);
      if (!actor->begin(options,[this](const dht::Id& key){return readSubscriptionPresence(key);})) {
        retryPublisher();return;
      }
      QObject::connect(actor.get(),&dht::Actor::changed,q,[this]{schedule();});
      publisher.actor=std::move(actor);
    }
    if (publisher.actor) { publisher.actor->resumeAuthorizedInbound(); }
  }
  void refreshPresenceJobs(Clock::time_point now) {
    for (auto& job:discoveries) {
      const auto publication=publicationRouter&&job.firstSubscriptionTurn?subscriptionPresence(job):nullptr;
      std::optional<dht::Id> key;
      if (publication) {
        key.emplace();
        std::ranges::copy(publication->dhtKey,key->begin());
      }
      if (job.versionKey!=key) {
        job.versionKey=key;
        job.nextPresence=key?now:Clock::time_point::max();
      }
    }
  }
  void withdrawRetiredDiscoveries() {
    for(auto at=discoveries.begin();at!=discoveries.end();){
      if(discoveryRetired(*at)){
        const auto id=at->admission;++at;cancelDiscovery(id);at=discoveries.begin();
      }else{++at;}
    }
  }
  void pumpDiscovery(){
    if(!discovery||!executing||!current()){return;}discovery->resumeAuthorizedInbound();if(!current()){return;}
    if(contactExchange&&contactExchange->pending()){static_cast<void>(contactExchange->capture(*discovery,*channel,source));}
    const auto now=Clock::now();
    refreshPresenceJobs(now);
    if(discoveryAdmission){
      auto job=std::find_if(discoveries.begin(),discoveries.end(),[&](const auto& v){return v.admission==*discoveryAdmission;});
      if(job==discoveries.end()||!discoveryCurrent(*job)||(publishingVersion&&publishingVersion!=job->versionKey)) {
        cancelDiscoveryOperation();
      }
      else if (publishingVersion) {
        const auto value=publisher.actor->observation();
        if(value.operation!=discoveryOperation||value.target!=*publishingVersion||now>=job->end
            ||value.phase!=dht::Phase::Running) {
          if (value.phase==dht::Phase::Running) { publisher.actor->cancel(discoveryOperation); }
          job->nextPresence=now+(value.announced?std::chrono::minutes(10):std::chrono::minutes(1));
          finishDiscovery();
        }
      }
      else{
        const auto value=discovery->observation();
        if(value.operation!=discoveryOperation||value.target!=job->wire){cancelDiscovery(job->admission);return;}
        const bool subscription=std::holds_alternative<std::shared_ptr<SubscriptionSource>>(job->source);
        const auto settled=[&](const dht::Peer& peer) {
          return peer.hash==source->destination().hash || (subscription && peer.seed)
              || std::ranges::find(job->delivered,peer.hash)!=job->delivered.end();
        };
        for(const auto& peer:value.peers){
          if(!current()||!discoveryCurrent(*job)){return;}
          if(settled(peer)){continue;}
          if(job->deliver(peer.hash,value.operation)){job->delivered.push_back(peer.hash);}
          if(phase==Phase::Withdrawn){return;}
        }
        const bool terminal=value.phase!=dht::Phase::Running;
        const bool delivered=std::ranges::all_of(value.peers,settled);
        if(now>=job->end||(terminal&&delivered)){
          if(subscription&&!job->firstSubscriptionTurn){job->firstSubscriptionTurn=now;refreshPresenceJobs(now);}
          if(!terminal){discovery->cancel(discoveryOperation);}job->next=now+std::chrono::minutes(5);job->delivered.clear();finishDiscovery();
        }
      }
    }
    if(!discoveryAdmission){
      auto job=discoveries.end();
      auto due=Clock::time_point::max();
      for (auto candidate=discoveries.begin();candidate!=discoveries.end();++candidate) {
        const auto candidateDue=std::min(candidate->next,candidate->nextPresence);
        if (discoveryCurrent(*candidate)&&candidateDue<due) { job=candidate;due=candidateDue; }
      }
      if(job!=discoveries.end()&&now>=due){
        const bool publish=job->versionKey&&job->nextPresence<job->next;
        if (publish&&!publisher.actor) {
          const bool starting=startPublisher();
          job->nextPresence=now+(starting?std::chrono::seconds(1):std::chrono::seconds(30));
          return;
        }
        auto operation=publish?publisher.actor->publishAzureusValue(*job->versionKey,{12,0},120000)
            :discovery->announceSelf(job->wire,job->seeding,120000);
        if(operation){
          if (publish) { publishingVersion=job->versionKey; }
          discoveryAdmission=job->admission;discoveryOperation=*operation;job->end=now+std::chrono::seconds(150);job->delivered.clear();
        } else {
          (publish?job->nextPresence:job->next)=now+std::chrono::seconds(30);
        }
      }
    }
  }
  explicit Impl(Bridge* owner,std::shared_ptr<ProfileSource> s,i2cp::InitialChannel& c):q(owner),source(std::move(s)),channel(&c),server(owner),timer(owner),capacity(bridgeCapacity(source->groupMode())),sockets(capacity.sockets){
    retired.reserve(capacity.streams);server.setProxy(QNetworkProxy::NoProxy);server.setMaxPendingConnections(static_cast<int>(capacity.sockets));timer.setSingleShot(true);
    QObject::connect(&server,&QTcpServer::newConnection,q,[this]{schedule();});
    QObject::connect(&timer,&QTimer::timeout,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::streamChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::destinationLookupChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::stateChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::leaseSetSubmitted,q,[this]{schedule();});
    QObject::connect(&c,&QObject::destroyed,q,[this]{channel=nullptr;close();});
  }
  ~Impl(){
    close();
#ifndef _WIN32
    watcher.reset();
    if (watch >= 0) { ::close(watch); }
#endif
    if(permit){permit->join();}OPENSSL_cleanse(capability.data(),capability.size());OPENSSL_cleanse(peerIdMask.data(),peerIdMask.size());
  }
  bool onThread()const{return QThread::currentThread()==q->thread();}
  bool leaseCurrent()const{
    if(!channel||channel->thread()!=q->thread()){return false;}
    auto identity=channel->ownedLeaseDestination();return identity&&identity->wire==source->destination().wire;
  }
  bool current()const{return phase!=Phase::Idle&&phase!=Phase::Withdrawn&&leaseCurrent()&&(!executing||source->revisionCurrent(activeRevision));}
  void notify(){
    if(notifyQueued){return;}notifyQueued=true;bool ok=false;
    try{const QPointer<Bridge> owner(q);ok=QMetaObject::invokeMethod(q,[owner]{if(!owner){return;}owner->impl_->notifyQueued=false;emit owner->changed();},Qt::QueuedConnection);}catch(...){ok=false;}
    if(!ok){notifyQueued=false;close();}
  }
  void discard(std::size_t index){
    auto& item=sockets[index];if(!item){return;}
    if(!item->authenticated&&rejectedClients!=std::numeric_limits<std::uint64_t>::max()){++rejectedClients;}
    const auto operation=item->operation;
    if(channel&&operation){
      channel->cancelStream(operation);if(phase==Phase::Withdrawn||!item||!channel){return;}
      const auto terminal=channel->managedStreamObservation(operation);
      if(executing&&current()&&terminal&&terminal->stream.phase==streaming::Phase::Closed&&terminal->stream.queuedRx){
        static_cast<void>(channel->takeStreamBytes(operation,streaming::MaximumBuffered));if(phase==Phase::Withdrawn||!item||!channel){return;}
      }
      const bool released=channel->releaseStream(operation);if(phase==Phase::Withdrawn||!item||!channel){return;}
      if(!released&&channel->managedStreamObservation(operation)&&std::find(retired.begin(),retired.end(),operation)==retired.end()){
        if(retired.size()>=capacity.streams){close("I2P retired stream bound exhausted");return;}retired.push_back(operation);
      }
    }
    if(channel&&item->lookup){channel->cancelDestinationLookup(item->lookup);if(phase==Phase::Withdrawn||!item){return;}}
    if(control&&*control==index){close("I2P control connection ended");return;}
    QObject::disconnect(item->tcp.get(),nullptr,q,nullptr);item->tcp->abort();item.reset();
  }
  void releaseRetired(){
    // A vanished SAM consumer does not imply its Closed Streaming ACK/RX has
    // settled. Only a fresh source-permit turn may drain its unread bytes.
    // ACK output uses existing receive/bytesWritten/effect wakes; empty reads
    // and failed releases must not schedule a cleanup spin.
    for(auto at=retired.begin();at!=retired.end();){
      if(!executing||!current()||!channel){return;}
      const auto observed=channel->managedStreamObservation(*at);
      if(!observed){at=retired.erase(at);continue;}
      if(observed->stream.phase==streaming::Phase::Closed&&observed->stream.queuedRx){
        static_cast<void>(channel->takeStreamBytes(*at,streaming::MaximumBuffered));
        if(!current()||!channel){return;}
      }
      const bool released=channel->releaseStream(*at);if(!current()||!channel){return;}
      if(released||!channel->managedStreamObservation(*at)){at=retired.erase(at);}else{++at;}
    }
  }
  void close(const char* reason=nullptr){
    if(reason&&!failure){failure=reason;}if(phase==Phase::Withdrawn){return;}phase=Phase::Withdrawn;wanted=false;server.close();timer.stop();
#ifdef _WIN32
    for (const auto& watcher : watchers) { watcher->notifier->setEnabled(false); }
#else
    if(watcher){watcher->setEnabled(false);}
#endif
    if (source->mixedMode()) { source->revokeTransportEffects(); }
    else { source->revoke(); }
    if(channel){channel->closeStreamListener();for(auto& item:sockets){if(item&&item->operation){channel->cancelStream(item->operation);}if(item&&item->lookup){channel->cancelDestinationLookup(item->lookup);}}}
    for(auto& item:sockets){if(item){QObject::disconnect(item->tcp.get(),nullptr,q,nullptr);item->tcp->abort();item.reset();}}
    closePublisher();if(contactExchange){contactExchange->stop();}if(discovery){discovery->close();}discoveries.clear();finishDiscovery();control.reset();peers.clear();resolved.clear();retired.clear();sessionId.clear();if(permit){permit->stop();}OPENSSL_cleanse(capability.data(),capability.size());OPENSSL_cleanse(peerIdMask.data(),peerIdMask.size());notify();
  }
  void schedule(bool work=true){
    if(phase==Phase::Idle||phase==Phase::Withdrawn){return;}wanted|=work;if(queued){return;}queued=true;bool ok=false;
    try{const QPointer<Bridge> owner(q);ok=QMetaObject::invokeMethod(q,[owner]{if(!owner){return;}auto* self=owner->impl_.get();self->queued=false;self->turn();},Qt::QueuedConnection);}catch(...){ok=false;}
    if(!ok){queued=false;close();}
  }
  void arm(){
    if(phase==Phase::Idle||phase==Phase::Withdrawn){timer.stop();return;}
    auto end=phase==Phase::Preparing?preparingEnd:Clock::time_point::max();
    for(const auto& item:sockets){if(item&&item->phase!=SocketPhase::Control&&item->phase!=SocketPhase::Data){
      if(item->phase==SocketPhase::Lookup){
        const auto now=Clock::now();if(now<item->end){end=std::min(end,item->end);}
        if(!item->lookup||now>=item->end){end=std::min(end,now+std::chrono::milliseconds(250));}
      }else{end=std::min(end,item->end);}
    }}
    if(discoveryAdmission){end=std::min(end,Clock::now()+std::chrono::seconds(1));}
    else{for(const auto& job:discoveries){
      // A due job still needs the asynchronous source permit. Its completion
      // schedules us; avoid a one-millisecond timer loop while I/O is pending.
      const auto now=Clock::now();
      const auto due=std::min(job.next,job.nextPresence);
      end=std::min(end,due<=now?now+std::chrono::milliseconds(250):due);
    }}
    if(contactExchange){const auto check=contactExchange->nextCheck();if(check){end=std::min(end,*check);}}
    if(end==Clock::time_point::max()){timer.stop();return;}
    timer.start(static_cast<int>(std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count())));
  }
  bool send(Socket& item,std::string_view bytes){
    // Sole physical write. No filesystem or caller callback under this fence.
    if(!executing||!current()||bytes.size()>64*1024||item.tcp->bytesToWrite()+static_cast<qint64>(bytes.size())>64*1024){return false;}
    return item.tcp->write(bytes.data(),static_cast<qint64>(bytes.size()))==static_cast<qint64>(bytes.size());
  }
  void acceptSockets(){
    for(std::size_t n=0;n<sockets.size()&&server.hasPendingConnections();++n){
      std::unique_ptr<QTcpSocket> tcp(server.nextPendingConnection());if(!tcp){break;}
      auto free=std::find_if(sockets.begin(),sockets.end(),[](const auto& item){return !item;});
      if(free==sockets.end()||!tcp->peerAddress().isLoopback()||tcp->localAddress()!=QHostAddress::LocalHost){tcp->abort();continue;}
      const auto index=static_cast<std::size_t>(free-sockets.begin());const auto at=generation;
      tcp->setProxy(QNetworkProxy::NoProxy);tcp->setReadBufferSize(static_cast<qint64>(MaximumCommandBytes));
      QObject::connect(tcp.get(),&QTcpSocket::readyRead,q,[this,at]{if(generation==at){schedule();}});
      QObject::connect(tcp.get(),&QTcpSocket::bytesWritten,q,[this,at](qint64){if(generation==at){schedule();}});
      QObject::connect(tcp.get(),&QTcpSocket::readChannelFinished,q,[this,at,index]{if(generation==at&&sockets[index]){sockets[index]->eof=true;schedule();}});
      QObject::connect(tcp.get(),&QTcpSocket::disconnected,q,[this,at,index]{if(generation==at&&sockets[index]){sockets[index]->eof=true;schedule();}});
      QObject::connect(tcp.get(),&QTcpSocket::errorOccurred,q,[this,at](QAbstractSocket::SocketError){if(generation==at){schedule();}});
      free->emplace(Socket{std::move(tcp),SocketPhase::Hello,Clock::now()+std::chrono::seconds(5),0,0,false,false,false,false,false,{}});
    }
  }
  bool updateListener(){
    if(peers.empty()){channel->closeStreamListener();return true;}
    return channel->listenStreams(streaming::ListenOptions{{ports.peer,ports.localPeer},peers,streaming::MaximumWindow});
  }
  bool permitPeer(const Destination& destination){
    if(std::none_of(peers.begin(),peers.end(),[&](const auto& p){return p.wire==destination.wire;})){
      if(peers.size()>=capacity.peers){return false;}peers.push_back(destination);if(!updateListener()){return false;}
    }return true;
  }
  bool known(const Destination& destination)const{return std::any_of(resolved.begin(),resolved.end(),[&](const auto& item){return item.wire==destination.wire;});}
  bool command(std::size_t index,const Command& cmd){
    auto& item=*sockets[index];const auto& f=cmd.fields;
    if(item.phase==SocketPhase::Hello){
      if(cmd.kind!=CommandKind::Hello||f.at("MIN")!="3.1"||f.at("MAX")!="3.1"){return false;}
      auto secret=hex(f.at("BIGLYBTC_OWNER"),32);auto number=decimal(f.at("GENERATION"),std::numeric_limits<std::uint64_t>::max());
      const bool valid=secret&&number&&*number==generation&&constantEqual(*secret,capability);if(secret){OPENSSL_cleanse(secret->data(),secret->size());}
      if(!valid){return false;}item.authenticated=true;item.phase=SocketPhase::Command;return send(item,"HELLO REPLY RESULT=OK VERSION=3.1\n");
    }
    if(!item.authenticated){return false;}
    if(cmd.kind==CommandKind::Create){
      if(item.phase!=SocketPhase::Command||control||f.at("STYLE")!="STREAM"||f.at("DESTINATION")!="BIGLYBTC_OWNED"||f.at("PUBLIC_HASH")!=hex(source->destination().hash)||!hex(f.at("ID"),20)){return false;}
      sessionId=f.at("ID");control=index;item.phase=SocketPhase::Control;
      return send(item,"SESSION STATUS RESULT=OK DESTINATION="+destinationText(source->destination().wire)+"\n");
    }
    if(cmd.kind==CommandKind::Naming){
      if(!control||*control!=index||item.phase!=SocketPhase::Control){return false;}
      const auto& name=f.at("NAME");auto to=decimal(f.at("TO_PORT"),65535),from=decimal(f.at("FROM_PORT"),65535);if(!to||!from){return false;}
      item.peer=f.at("PURPOSE")=="PEER";
      if(name=="ME"){if(f.at("PURPOSE")!="CONTROL"||*to||*from){return false;}return send(item,"NAMING REPLY RESULT=OK NAME=ME VALUE="+destinationText(source->destination().wire)+"\n");}
      auto hash=hashName(name);if(!hash){return false;}
      item.trackerLookupPort.reset();
      if(item.peer){if(*to!=ports.peer||*from!=ports.localPeer){return false;}}
      else {
        if (f.at("PURPOSE") != "TRACKER" || *from != 0 || *to == 0) { return false; }
        const auto port = static_cast<std::uint16_t>(*to);
        // Pause, Stop and metadata retirement can revoke a tracker grant
        // while the SDK's final announce is queued on this shared socket.
        if (!source->trackerAllowed(*hash, port)) {
          return send(item, "NAMING REPLY RESULT=KEY_NOT_FOUND NAME=" + name + "\n");
        }
        item.trackerLookupPort = port;
      }
      auto found=std::find_if(resolved.begin(),resolved.end(),[&](const auto& d){return d.hash==*hash;});
      if(found!=resolved.end()){if(item.peer&&!permitPeer(*found)){return false;}return send(item,"NAMING REPLY RESULT=OK NAME="+name+" VALUE="+destinationText(found->wire)+"\n");}
      if(resolved.size()>=capacity.destinations){return false;}
      // DHT contact resolution shares the actual Host38 slot. Waiting for it
      // neither rejects the SDK control socket nor extends this NAMING budget.
      item.lookup=0;item.requestedName=name;item.phase=SocketPhase::Lookup;item.end=Clock::now()+std::chrono::seconds(30);return true;
    }
    if(!control||sessionId.empty()||f.at("ID")!=sessionId||item.phase!=SocketPhase::Command){return false;}
    auto to=decimal(f.at("TO_PORT"),65535),from=decimal(f.at("FROM_PORT"),65535);if(!to||!from){return false;}
    item.peer=f.at("PURPOSE")=="PEER";const bool tracker=f.at("PURPOSE")=="TRACKER";
    if(!item.peer&&!tracker){return false;}
    if(cmd.kind==CommandKind::Accept){
      if(!item.peer||*to!=ports.peer||*from!=ports.localPeer||std::any_of(sockets.begin(),sockets.end(),[](const auto& s){return s&&s->phase==SocketPhase::Accept;})){return false;}
      item.phase=SocketPhase::Accept;item.end=Clock::now()+std::chrono::seconds(120);return send(item,"STREAM STATUS RESULT=OK\n");
    }
    if(cmd.kind!=CommandKind::Connect){return false;}
    auto destination=destinationText(f.at("DESTINATION"));if(!destination||!known(*destination)||destination->hash==source->destination().hash){return false;}
    if(item.peer){
      if(*to!=ports.peer||*from!=ports.localPeer){return false;}
      if(std::none_of(peers.begin(),peers.end(),[&](const auto& p){return p.wire==destination->wire;})){return false;}
    }else if(*from!=0||!source->trackerAllowed(destination->hash,static_cast<std::uint16_t>(*to))){return false;}
    auto operation=channel->connectStream(destination->wire,{static_cast<std::uint16_t>(*from),static_cast<std::uint16_t>(*to),120000,streaming::MaximumWindow});
    if(!operation){return false;}item.operation=*operation;item.phase=SocketPhase::Connecting;item.end=Clock::now()+std::chrono::seconds(120);return true;
  }
  bool pumpSocket(std::size_t index){
    auto& item=*sockets[index];
    if(item.phase!=SocketPhase::Data&&item.phase!=SocketPhase::Control&&item.phase!=SocketPhase::Lookup&&Clock::now()>=item.end){return false;}
    if(item.phase==SocketPhase::Lookup){
      const auto hash = hashName(item.requestedName);
      if (!hash) { return false; }
      const bool trackerRetired = item.trackerLookupPort
          && !source->trackerAllowed(*hash, *item.trackerLookupPort);
      if(trackerRetired || Clock::now()>=item.end){
        if(item.lookup){channel->cancelDestinationLookup(item.lookup);}item.lookup=0;item.phase=SocketPhase::Control;
        return send(item,"NAMING REPLY RESULT=KEY_NOT_FOUND NAME="+item.requestedName+"\n");
      }
      if(!item.lookup){
        const auto active=channel->destinationLookupObservation();
        if(active&&active->phase==i2cp::InitialChannel::LookupPhase::Pending){return !item.eof;}
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(item.end-Clock::now()).count();
        if(remaining<=0){return false;}
        auto lookup=channel->lookupDestination(*hash,static_cast<unsigned>(std::min<std::int64_t>(30000,remaining)));
        if(!lookup){return !item.eof;}item.lookup=*lookup;return true;
      }
      auto value=channel->destinationLookupObservation(item.lookup);if(!value||value->operation!=item.lookup){return false;}
      if(value->phase==i2cp::InitialChannel::LookupPhase::Pending){return !item.eof;}
      item.lookup=0;item.phase=SocketPhase::Control;
      if(value->phase!=i2cp::InitialChannel::LookupPhase::Resolved||!value->destination){return send(item,"NAMING REPLY RESULT=KEY_NOT_FOUND NAME="+item.requestedName+"\n");}
      if(resolved.size()>=capacity.destinations||(item.peer&&!permitPeer(*value->destination))){return false;}resolved.push_back(*value->destination);
      return send(item,"NAMING REPLY RESULT=OK NAME="+item.requestedName+" VALUE="+destinationText(value->destination->wire)+"\n");
    }
    if(item.phase==SocketPhase::Accept){
      if(item.eof){return false;}
      const auto observed=channel->streamManagerObservation();if(!observed.pendingSyns){return true;}
      auto operation=channel->acceptStream();if(!operation){return false;}item.operation=*operation;
      auto peer=channel->managedStreamPeer(*operation);if(!peer||std::none_of(peers.begin(),peers.end(),[&](const auto& p){return p.wire==peer->wire;})){return false;}
      if(!send(item,destinationText(peer->wire)+"\n")){return false;}item.phase=SocketPhase::Data;item.tcp->setReadBufferSize(65536);
    }
    if(item.phase==SocketPhase::Connecting){
      auto observed=channel->managedStreamObservation(item.operation);if(!observed){return false;}
      if(observed->stream.phase==streaming::Phase::Connecting){return !item.eof;}
      if(observed->stream.phase!=streaming::Phase::Established&&observed->stream.phase!=streaming::Phase::ReadEof){return false;}
      if(!send(item,"STREAM STATUS RESULT=OK\n")){return false;}item.phase=SocketPhase::Data;item.tcp->setReadBufferSize(65536);
    }
    if(item.phase==SocketPhase::Data){
      auto observed=channel->managedStreamObservation(item.operation);if(!observed){return false;}
      const auto phase=observed->stream.phase;
      if(phase!=streaming::Phase::Connecting&&phase!=streaming::Phase::Established&&phase!=streaming::Phase::ReadEof&&phase!=streaming::Phase::Closed){return false;}
      const auto capacity=65536-std::min<std::size_t>(65536,observed->stream.queuedTx);
      const auto count=std::min<qint64>({16384,static_cast<qint64>(capacity),item.tcp->bytesAvailable()});
      if(count>0&&!item.finished){auto bytes=item.tcp->read(count);if(bytes.size()!=count||!current()||!channel->writeStream(item.operation,View(reinterpret_cast<const Byte*>(bytes.constData()),static_cast<std::size_t>(bytes.size())))){return false;}}
      if(item.eof&&!item.finished&&item.tcp->bytesAvailable()==0){if(!current()||!channel->finishStreamWrite(item.operation)){return false;}item.finished=true;}
      if(item.tcp->bytesToWrite()<49152&&observed->stream.queuedRx){
        auto bytes=channel->takeStreamBytes(item.operation,std::min<std::size_t>(16384,observed->stream.queuedRx));
        if(!bytes||!send(item,std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()))){return false;}
      }
      const auto after=channel->managedStreamObservation(item.operation);
      if(after&&(after->stream.phase==streaming::Phase::ReadEof||after->stream.phase==streaming::Phase::Closed)&&after->stream.queuedRx==0&&item.tcp->bytesToWrite()==0){item.tcp->disconnectFromHost();return false;}
      if(item.tcp->bytesAvailable()>0&&capacity>0){wanted=true;}
      return true;
    }
    if(item.eof){return false;}
    if(!item.tcp->canReadLine()){return item.tcp->bytesAvailable()<static_cast<qint64>(MaximumCommandBytes);}
    auto line=item.tcp->readLine(static_cast<qint64>(MaximumCommandBytes+1));
    struct Wipe {QByteArray& bytes;~Wipe(){OPENSSL_cleanse(bytes.data(),static_cast<std::size_t>(bytes.size()));}} wipe{line};
    auto parsed=parseCommand(std::string_view(line.constData(),static_cast<std::size_t>(line.size())));if(!parsed||!command(index,*parsed)){return false;}
    if(item.tcp->bytesAvailable()>0){wanted=true;}return true;
  }
  void turn(){
    try{
      if(phase==Phase::Withdrawn||phase==Phase::Idle){return;}
      // Revocation is local and never waits for a filesystem permit. In
      // particular, a blocked source read cannot extend a socket's budget.
      const auto now=Clock::now();if(contactExchange){contactExchange->tick(now);}
      if(phase==Phase::Preparing&&now>=preparingEnd){close("I2P bridge preparation deadline expired");return;}
      for(std::size_t i=0;i<sockets.size()&&phase!=Phase::Withdrawn;++i){
        if(sockets[i]&&sockets[i]->phase!=SocketPhase::Control&&sockets[i]->phase!=SocketPhase::Data
          &&(now>=sockets[i]->end||sockets[i]->eof)){
          if(sockets[i]->phase==SocketPhase::Lookup&&!sockets[i]->eof){
            // The operation ends locally at its original deadline. A fresh
            // source permit sends the negative result; a missing remote peer
            // does not tear down the shared authenticated SDK control channel.
            if(sockets[i]->lookup){channel->cancelDestinationLookup(sockets[i]->lookup);sockets[i]->lookup=0;}
          }else{discard(i);}
        }
      }
      if(phase==Phase::Withdrawn){return;}if(!leaseCurrent()){close("I2P lease identity unavailable");return;}
      if(sourcePaused||!permit){arm();return;}
      auto revision=permit->take();if(!revision){if(permit->phase()==SourcePermit::Phase::Failed||permit->phase()==SourcePermit::Phase::Stopped){close("I2P bridge source validation failed");return;}if(wanted&&permit->phase()==SourcePermit::Phase::Idle){permit->request();}arm();return;}
      struct Ack {SourcePermit& permit;~Ack(){permit.acknowledge();}} ack{*permit};
      activeRevision=*revision;executing=true;wanted=false;
      // The fresh observation may have withdrawn a subscription while its
      // announce was queued. Cancel its Actor tickets before I2CP can flush.
      withdrawRetiredDiscoveries();
      pumpPublisher();
      if(!current()||!channel->resumeAuthorizedEffects()){close("I2P bridge effect authority refused");executing=false;return;}
      if(phase==Phase::Preparing){
        if(!server.listen(QHostAddress::LocalHost,0)||!current()){close();executing=false;return;}phase=Phase::Ready;notify();
      }
      if(install&&source->mixedMode()){
#if BIGLYBTC_BRIDGE_HAS_MIXED_HEADER
        libtorrent::BiglybtcMixedI2pTransportPolicyV1 policy;policy.generation=generation;policy.capability=capability;policy.public_destination_hash=source->destination().hash;
        policy.bridge_port=server.serverPort();policy.peer_port=ports.peer;policy.local_peer_port=ports.localPeer;policy.peer_id_mask=peerIdMask;
        struct Wipe {decltype(policy)& value;~Wipe(){OPENSSL_cleanse(value.capability.data(),value.capability.size());OPENSSL_cleanse(value.peer_id_mask.data(),value.peer_id_mask.size());}} wipe{policy};
        auto apply=std::move(install);if(!current()||!apply(&policy)||!current()){close("Mixed I2P provider installation refused");executing=false;return;}
#else
        close("Mixed I2P SDK is unavailable");executing=false;return;
#endif
      }else if(install){
        libtorrent::BiglybtcPrivateI2pOwnerPolicyV1 policy;policy.generation=generation;policy.admission=admission;policy.capability=capability;policy.public_destination_hash=source->destination().hash;policy.bridge_port=server.serverPort();policy.peer_port=ports.peer;policy.local_peer_port=ports.localPeer;
        struct Wipe {decltype(policy)& value;~Wipe(){OPENSSL_cleanse(value.capability.data(),value.capability.size());}} wipe{policy};
        auto apply=std::move(install);if(!current()||!apply(&policy)||!current()){close("I2P provider installation refused");executing=false;return;}
      }
      pumpDiscovery();if(phase==Phase::Withdrawn){executing=false;return;}releaseRetired();if(phase==Phase::Withdrawn){executing=false;return;}acceptSockets();
      for(std::size_t i=0;i<sockets.size()&&phase==Phase::Ready;++i){if(sockets[i]&&(!current()||!pumpSocket(i))){discard(i);}}
      if(current()){channel->resumeAuthorizedEffects();}arm();
    }catch(...){close("I2P bridge operation failed");}
    executing=false;
  }

  bool watchSource() {
#ifdef _WIN32
    namespace state = storage::detail;
    for (const auto& watched : source->watchDirectories()) {
      const auto before = state::stateStamp(watched.handle);
      const auto path = state::stateDirectoryPath(watched.handle);
      if (!before || !path || !before->directory) { return false; }
      auto watch = std::make_unique<DirectoryWatch>();
      const auto& name = path->native();
      const auto full = name.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + name.substr(2) : L"\\\\?\\" + name;
      const auto dir = ::CreateFileW(full.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL,
          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_OVERLAPPED, nullptr);
      if (dir == INVALID_HANDLE_VALUE) { return false; }
      watch->directory = state::StateDescriptor(dir);
      const auto opened = state::stateStamp(dir);
      if (!opened || !state::sameStateDirectory(*before, *opened)) { return false; }
      watch->event = state::StateDescriptor(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
      if (!watch->event.get() || !watch->arm()) { return false; }
      watch->criticalNames = watched.criticalNames;
      watch->notifier = std::make_unique<QWinEventNotifier>(watch->event.get(), q);
      QObject::connect(watch->notifier.get(), &QWinEventNotifier::activated, q,
          [this, active = watch.get()] { readSourceWatch(*active); });
      watchers.push_back(std::move(watch));
    }
    return !watchers.empty();
#else
#ifdef __APPLE__
    watch = ::kqueue();
    if (watch < 0 || ::fcntl(watch, F_SETFD, FD_CLOEXEC) < 0) { return false; }
    criticalWatches = source->criticalWatchDescriptors();
    for (const int fd : source->watchDescriptors()) {
      struct stat info{};
      if (::fstat(fd, &info) != 0) { return false; }
      auto mask = NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME | NOTE_DELETE | NOTE_REVOKE;
      if (!S_ISDIR(info.st_mode) || source->groupMode()) { mask |= NOTE_WRITE | NOTE_EXTEND; }
      struct kevent entry{};
      EV_SET(&entry, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, mask, 0, nullptr);
      if (::kevent(watch, &entry, 1, nullptr, 0, nullptr) < 0) { return false; }
    }
#else
    watch = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (watch < 0) { return false; }
    const auto critical = source->criticalWatchDescriptors();
    for (const int fd : source->watchDescriptors()) {
      const auto mask = IN_ATTRIB | IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF | IN_UNMOUNT
          | (source->groupMode() ? (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO) : 0);
      if (fd < 0) { return false; }
      const int wd = ::inotify_add_watch(watch, ("/proc/self/fd/" + std::to_string(fd)).c_str(), mask);
      if (wd < 0) { return false; }
      if (std::ranges::find(critical, fd) != critical.end()) { criticalWatches.push_back(wd); }
    }
#endif
    watcher = std::make_unique<QSocketNotifier>(watch, QSocketNotifier::Read, q);
    QObject::connect(watcher.get(), &QSocketNotifier::activated, q, [this] { readSourceWatch(); });
    return true;
#endif
  }

  void revokeWatchedSource() {
    source->revoke();
    close("Critical original I2P source watch event");
  }

#ifdef _WIN32
  void readSourceWatch(DirectoryWatch& watch) {
    if (phase == Phase::Withdrawn) { return; }
    DWORD bytes = 0;
    if (!::GetOverlappedResult(watch.directory.get(), &watch.io, &bytes, FALSE)) {
      if (::GetLastError() != ERROR_IO_INCOMPLETE) {
        watch.pending = false;
        revokeWatchedSource();
      }
      return;
    }
    watch.pending = false;
    if (!source->groupMode()) { close(); return; }
    constexpr auto header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
    if (!bytes || bytes > watch.buffer.size()) { revokeWatchedSource(); return; }
    for (std::size_t at = 0;;) {
      const auto remaining = bytes - at;
      if (remaining < header) { revokeWatchedSource(); return; }
      FILE_NOTIFY_INFORMATION entry{};
      std::memcpy(&entry, watch.buffer.data() + at, header);
      const auto length = entry.FileNameLength;
      if (!length || length % sizeof(wchar_t) || length > remaining - header) { revokeWatchedSource(); return; }
      std::wstring label(length / sizeof(wchar_t), L'\0');
      std::memcpy(label.data(), watch.buffer.data() + at + header, length);
      if (std::ranges::any_of(watch.criticalNames, [&](const auto& name) {
        return ::CompareStringOrdinal(label.data(), static_cast<int>(label.size()),
            name.data(), static_cast<int>(name.size()), TRUE) == CSTR_EQUAL;
      })) { revokeWatchedSource(); return; }
      const auto next = entry.NextEntryOffset;
      if (!next) { break; }
      if (next < header + length || next >= remaining || next % alignof(DWORD)) { revokeWatchedSource(); return; }
      at += next;
    }
    if (!watch.arm()) { revokeWatchedSource(); return; }
    source->invalidate();
    schedule();
  }
#else
  void readSourceWatch() {
#ifdef __APPLE__
    std::array<struct kevent, 32> events{};
    const timespec noWait{};
    const auto count = ::kevent(watch, nullptr, 0, events.data(), static_cast<int>(events.size()), &noWait);
    if (count < 0) {
      if (errno != EINTR) { close("I2P source watch read failed"); }
      return;
    }
    if (count == 0) { return; }
    if (!source->groupMode()) { close(); return; }
    for (int i = 0; i < count; ++i) {
      const auto& event = events[static_cast<std::size_t>(i)];
      if (event.flags & (EV_ERROR | EV_EOF) || event.fflags & NOTE_REVOKE
          || std::ranges::find(criticalWatches, static_cast<int>(event.ident)) != criticalWatches.end()) {
        revokeWatchedSource();
        return;
      }
    }
#else
    std::array<char, 4096> bytes{};
    const auto count = ::read(watch, bytes.data(), bytes.size());
    if (count < 0) {
      if (errno != EAGAIN && errno != EINTR) { close("I2P source watch read failed"); }
      return;
    }
    if (count == 0) { close("I2P source watch ended"); return; }
    if (!source->groupMode()) { close(); return; }
    const auto size = static_cast<std::size_t>(count);
    for (std::size_t offset = 0; offset < size;) {
      if (size - offset < sizeof(inotify_event)) { close(); return; }
      inotify_event event{};
      std::memcpy(&event, bytes.data() + offset, sizeof(event));
      if (event.len > size - offset - sizeof(event)) { close(); return; }
      if (event.mask & (IN_Q_OVERFLOW | IN_UNMOUNT)
          || std::ranges::find(criticalWatches, event.wd) != criticalWatches.end()) {
        revokeWatchedSource();
        return;
      }
      offset += sizeof(event) + event.len;
    }
#endif
    source->invalidate();
    schedule();
  }
#endif
};
Bridge::Bridge(std::shared_ptr<ProfileSource> source,i2cp::InitialChannel& channel,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this,std::move(source),channel)){}
Bridge::~Bridge()=default;
bool Bridge::begin(Ports ports,std::uint64_t admission){
  auto& s=*impl_;const auto end=Impl::Clock::now()+std::chrono::seconds(30);
  if(!s.onThread()||s.phase!=Phase::Idle){return false;}
  try{
  if(!s.source->prepared()||!ports.peer||!ports.localPeer||!admission||!s.leaseCurrent()){return false;}
  if(RAND_bytes(s.capability.data(),static_cast<int>(s.capability.size()))!=1||RAND_bytes(reinterpret_cast<unsigned char*>(&s.generation),sizeof(s.generation))!=1||!s.generation){s.close();return false;}
  if(s.source->mixedMode()&&RAND_bytes(s.peerIdMask.data(),static_cast<int>(s.peerIdMask.size()))!=1){s.close();return false;}
  s.ports=ports;s.admission=admission;
  static_assert(bridgeCapacity(false).streams==streaming::MaximumStreams);
  static_assert(bridgeCapacity(true).streams==streaming::MaximumConfiguredStreams);
  streaming::ManagerOptions managerOptions;managerOptions.maximumStreams=static_cast<unsigned>(s.capacity.streams);
  if(!s.channel->enableStreamManager(managerOptions)){s.close();return false;}
  s.phase=Phase::Preparing;s.preparingEnd=end;
    if (!s.watchSource()) { s.close(); return false; }
    s.permit=std::make_unique<SourcePermit>(s.source,this);
    if(Impl::Clock::now()>=end){s.close();return false;}s.arm();s.schedule();return true;
  }catch(...){s.close();return false;}
}
void Bridge::close(){impl_->close();}
const char* Bridge::failureReason()const noexcept{return impl_->failure?impl_->failure:"I2P bridge withdrew";}
bool Bridge::pauseSourceValidation(){
 auto& s=*impl_;
 if(!s.onThread()||s.sourcePaused||s.phase==Phase::Idle||s.phase==Phase::Withdrawn||!s.permit
     ||s.permit->phase()==SourcePermit::Phase::Executing){return false;}
 s.sourcePaused=true;s.permit->stop();return true;
}
bool Bridge::resumeSourceValidation(){
 auto& s=*impl_;
 if(!s.onThread()||!s.sourcePaused||s.phase==Phase::Idle||s.phase==Phase::Withdrawn
     ||!s.permit||!s.permit->joined()||!s.source->prepared()){return false;}
 try{s.permit=std::make_unique<SourcePermit>(s.source,this);s.sourcePaused=false;s.schedule();return true;}
 catch(...){s.close("Cannot restart private source validation");return false;}
}
void Bridge::joinSource(){if(impl_->permit){impl_->permit->join();}}
Bridge::Observation Bridge::observation()const noexcept{
  const auto& s=*impl_;Observation value{s.phase,s.generation,s.admission,s.source->destination().hash,s.server.serverPort(),0,0,s.peers.size(),s.rejectedClients};
  for(const auto& item:s.sockets){if(item){++value.sockets;value.streams+=item->operation!=0;}}return value;
}
bool Bridge::installProvider(std::function<bool(void*)> install){if(!impl_->onThread()||impl_->source->mixedMode()||impl_->phase!=Phase::Ready||impl_->install||!install){return false;}impl_->install=std::move(install);impl_->schedule();return true;}
bool Bridge::installMixedProvider(std::function<bool(void*)> install){
#if BIGLYBTC_BRIDGE_HAS_MIXED_HEADER
  if(!impl_->onThread()||!impl_->source->mixedMode()||impl_->phase!=Phase::Ready||impl_->install||!install){return false;}
  impl_->install=std::move(install);impl_->schedule();return true;
#else
  (void)install;return false;
#endif
}
bool Bridge::event(QEvent* event){if(event->type()==SourcePermit::wakeEvent()){const auto phase=impl_->permit?impl_->permit->phase():SourcePermit::Phase::Stopped;if(phase==SourcePermit::Phase::Ready||phase==SourcePermit::Phase::Failed||phase==SourcePermit::Phase::Stopped||impl_->wanted){impl_->schedule(false);}return true;}return QObject::event(event);}
}

namespace biglybtc::services::i2p::provider {
bool Bridge::beginDiscovery(const dht::Actor::Options& options,std::shared_ptr<ContactExchange> contacts,
    const i2cp::InitialChannel::Options* publicationRouter){return impl_->onThread()&&impl_->beginDiscovery(options,std::move(contacts),publicationRouter);}
bool Bridge::discover(std::shared_ptr<ProfileSource> source,const core::TorrentHash& hash,const dht::Id& wire,
    std::uint64_t admission,bool seeding,std::function<bool(const Hash&,std::uint64_t)> deliver){return impl_->discover(std::move(source),hash,wire,admission,seeding,std::move(deliver));}
bool Bridge::discover(std::shared_ptr<PendingMagnetSource> source,const core::TorrentHash& hash,const dht::Id& wire,
    std::uint64_t admission,bool seeding,std::function<bool(const Hash&,std::uint64_t)> deliver){return source&&impl_->discover(source->source_,hash,wire,admission,seeding,std::move(deliver));}
bool Bridge::discoverSubscription(std::shared_ptr<SubscriptionSource> source, std::uint64_t admission,
    std::function<bool(const Hash&,std::uint64_t)> deliver) {
  return impl_->discoverSubscription(std::move(source), admission, std::move(deliver));
}
bool Bridge::subscriptionDiscoveryWindowElapsed(std::uint64_t admission,
    std::chrono::steady_clock::time_point now) const {
  if (!impl_->onThread() || !impl_->current() || admission == impl_->discoveryAdmission) { return false; }
  const auto found = std::ranges::find(impl_->discoveries,admission,&Impl::Discovery::admission);
  return found!=impl_->discoveries.end() && found->firstSubscriptionTurn
      && now-*found->firstSubscriptionTurn>=std::chrono::minutes(5);
}
void Bridge::cancelDiscovery(std::uint64_t admission){if(impl_->onThread()){impl_->cancelDiscovery(admission);}}
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "bridge.hpp"
#include "sam_protocol.hpp"
#include "source_permit.hpp"
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QMetaObject>
#include <QSocketNotifier>
#include <sys/inotify.h>
#include <unistd.h>
#include <cerrno>
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
  };
  Bridge* q;std::shared_ptr<ProfileSource> source;QPointer<i2cp::InitialChannel> channel;
  QTcpServer server;QTimer timer;std::unique_ptr<SourcePermit> permit;
  int watch=-1;std::unique_ptr<QSocketNotifier> watcher;std::vector<int> criticalWatches;
  std::array<std::optional<Socket>,MaximumBridgeSockets> sockets;
  std::vector<Destination> peers;std::vector<Destination> resolved;
  std::optional<std::size_t> control;
  Ports ports;Phase phase=Phase::Idle;std::string sessionId;Clock::time_point preparingEnd{};
  std::array<Byte,32> capability{};std::uint64_t generation=0,admission=0,activeRevision=0,rejectedClients=0;
  bool queued=false,notifyQueued=false,wanted=false,executing=false;
  std::function<bool(void*)> install;const char* failure=nullptr;
  explicit Impl(Bridge* owner,std::shared_ptr<ProfileSource> s,i2cp::InitialChannel& c):q(owner),source(std::move(s)),channel(&c),server(owner),timer(owner){
    server.setProxy(QNetworkProxy::NoProxy);server.setMaxPendingConnections(static_cast<int>(MaximumBridgeSockets));timer.setSingleShot(true);
    QObject::connect(&server,&QTcpServer::newConnection,q,[this]{schedule();});
    QObject::connect(&timer,&QTimer::timeout,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::streamChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::destinationLookupChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::stateChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::leaseSetSubmitted,q,[this]{schedule();});
    QObject::connect(&c,&QObject::destroyed,q,[this]{channel=nullptr;close();});
  }
  ~Impl(){close();if(watch>=0){::close(watch);}if(permit){permit->join();}OPENSSL_cleanse(capability.data(),capability.size());}
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
    auto& item=sockets[index];if(!item){return;}if(!item->authenticated&&rejectedClients!=std::numeric_limits<std::uint64_t>::max()){++rejectedClients;}if(channel&&item->operation){channel->cancelStream(item->operation);auto terminal=channel->managedStreamObservation(item->operation);if(executing&&current()&&terminal&&terminal->stream.phase==streaming::Phase::Closed&&terminal->stream.queuedRx){static_cast<void>(channel->takeStreamBytes(item->operation,streaming::MaximumBuffered));}channel->releaseStream(item->operation);}
    if(channel&&item->lookup){channel->cancelDestinationLookup(item->lookup);}
    if(control&&*control==index){close("I2P control connection ended");return;}
    QObject::disconnect(item->tcp.get(),nullptr,q,nullptr);item->tcp->abort();item.reset();
  }
  void close(const char* reason=nullptr){
    if(reason&&!failure){failure=reason;}if(phase==Phase::Withdrawn){return;}phase=Phase::Withdrawn;wanted=false;server.close();timer.stop();if(watcher){watcher->setEnabled(false);}source->revoke();
    if(channel){channel->closeStreamListener();for(auto& item:sockets){if(item&&item->operation){channel->cancelStream(item->operation);}if(item&&item->lookup){channel->cancelDestinationLookup(item->lookup);}}}
    for(auto& item:sockets){if(item){QObject::disconnect(item->tcp.get(),nullptr,q,nullptr);item->tcp->abort();item.reset();}}
    control.reset();peers.clear();resolved.clear();sessionId.clear();if(permit){permit->stop();}OPENSSL_cleanse(capability.data(),capability.size());notify();
  }
  void schedule(bool work=true){
    if(phase==Phase::Idle||phase==Phase::Withdrawn){return;}wanted|=work;if(queued){return;}queued=true;bool ok=false;
    try{const QPointer<Bridge> owner(q);ok=QMetaObject::invokeMethod(q,[owner]{if(!owner){return;}auto* self=owner->impl_.get();self->queued=false;self->turn();},Qt::QueuedConnection);}catch(...){ok=false;}
    if(!ok){queued=false;close();}
  }
  void arm(){
    if(phase==Phase::Idle||phase==Phase::Withdrawn){timer.stop();return;}
    auto end=phase==Phase::Preparing?preparingEnd:Clock::time_point::max();
    for(const auto& item:sockets){if(item&&item->phase!=SocketPhase::Control&&item->phase!=SocketPhase::Data){end=std::min(end,item->end);}}
    if(end==Clock::time_point::max()){timer.stop();return;}
    timer.start(static_cast<int>(std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count())));
  }
  bool send(Socket& item,std::string_view bytes){
    // Sole physical write. No filesystem or caller callback under this fence.
    if(!executing||!current()||bytes.size()>64*1024||item.tcp->bytesToWrite()+static_cast<qint64>(bytes.size())>64*1024){return false;}
    return item.tcp->write(bytes.data(),static_cast<qint64>(bytes.size()))==static_cast<qint64>(bytes.size());
  }
  void acceptSockets(){
    for(std::size_t n=0;n<MaximumBridgeSockets&&server.hasPendingConnections();++n){
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
      if(peers.size()>=streaming::MaximumStreams){return false;}peers.push_back(destination);if(!updateListener()){return false;}
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
      if(item.peer){if(*to!=ports.peer||*from!=ports.localPeer){return false;}}
      else if(f.at("PURPOSE")!="TRACKER"||*from!=0||!source->trackerAllowed(*hash,static_cast<std::uint16_t>(*to))){return false;}
      auto found=std::find_if(resolved.begin(),resolved.end(),[&](const auto& d){return d.hash==*hash;});
      if(found!=resolved.end()){if(item.peer&&!permitPeer(*found)){return false;}return send(item,"NAMING REPLY RESULT=OK NAME="+name+" VALUE="+destinationText(found->wire)+"\n");}
      if(resolved.size()>=8){return false;}
      auto lookup=channel->lookupDestination(*hash,30000);if(!lookup){return false;}
      item.lookup=*lookup;item.requestedName=name;item.phase=SocketPhase::Lookup;item.end=Clock::now()+std::chrono::seconds(30);return true;
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
    if(item.phase!=SocketPhase::Data&&item.phase!=SocketPhase::Control&&Clock::now()>=item.end){return false;}
    if(item.phase==SocketPhase::Lookup){
      auto value=channel->destinationLookupObservation();if(!value||value->operation!=item.lookup){return false;}
      if(value->phase==i2cp::InitialChannel::LookupPhase::Pending){return !item.eof;}
      item.lookup=0;item.phase=SocketPhase::Control;
      if(value->phase!=i2cp::InitialChannel::LookupPhase::Resolved||!value->destination){return send(item,"NAMING REPLY RESULT=KEY_NOT_FOUND NAME="+item.requestedName+"\n");}
      if(resolved.size()>=8||(item.peer&&!permitPeer(*value->destination))){return false;}resolved.push_back(*value->destination);
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
      const auto now=Clock::now();
      if(phase==Phase::Preparing&&now>=preparingEnd){close("I2P bridge preparation deadline expired");return;}
      for(std::size_t i=0;i<sockets.size()&&phase!=Phase::Withdrawn;++i){
        if(sockets[i]&&sockets[i]->phase!=SocketPhase::Control&&sockets[i]->phase!=SocketPhase::Data
          &&(now>=sockets[i]->end||sockets[i]->eof)){discard(i);}
      }
      if(phase==Phase::Withdrawn){return;}if(!leaseCurrent()){close("I2P lease identity unavailable");return;}
      if(!permit){arm();return;}
      auto revision=permit->take();if(!revision){if(permit->phase()==SourcePermit::Phase::Failed||permit->phase()==SourcePermit::Phase::Stopped){close("I2P bridge source validation failed");return;}if(wanted&&permit->phase()==SourcePermit::Phase::Idle){permit->request();}arm();return;}
      struct Ack {SourcePermit& permit;~Ack(){permit.acknowledge();}} ack{*permit};
      activeRevision=*revision;executing=true;wanted=false;
      if(!current()||!channel->resumeAuthorizedEffects()){close("I2P bridge effect authority refused");executing=false;return;}
      if(phase==Phase::Preparing){
        if(!server.listen(QHostAddress::LocalHost,0)||!current()){close();executing=false;return;}phase=Phase::Ready;notify();
      }
      if(install){
        libtorrent::BiglybtcPrivateI2pOwnerPolicyV1 policy;policy.generation=generation;policy.admission=admission;policy.capability=capability;policy.public_destination_hash=source->destination().hash;policy.bridge_port=server.serverPort();policy.peer_port=ports.peer;policy.local_peer_port=ports.localPeer;
        struct Wipe {decltype(policy)& value;~Wipe(){OPENSSL_cleanse(value.capability.data(),value.capability.size());}} wipe{policy};
        auto apply=std::move(install);if(!current()||!apply(&policy)||!current()){close("I2P provider installation refused");executing=false;return;}
      }
      acceptSockets();
      for(std::size_t i=0;i<sockets.size()&&phase==Phase::Ready;++i){if(sockets[i]&&(!current()||!pumpSocket(i))){discard(i);}}
      if(current()){channel->resumeAuthorizedEffects();}arm();
    }catch(...){close("I2P bridge operation failed");}
    executing=false;
  }
};
Bridge::Bridge(std::shared_ptr<ProfileSource> source,i2cp::InitialChannel& channel,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this,std::move(source),channel)){}
Bridge::~Bridge()=default;
bool Bridge::begin(Ports ports,std::uint64_t admission){
  auto& s=*impl_;const auto end=Impl::Clock::now()+std::chrono::seconds(30);
  if(!s.onThread()||s.phase!=Phase::Idle){return false;}
  try{
  if(!s.source->prepared()||!ports.peer||!ports.localPeer||!admission||!s.leaseCurrent()){return false;}
  if(RAND_bytes(s.capability.data(),static_cast<int>(s.capability.size()))!=1||RAND_bytes(reinterpret_cast<unsigned char*>(&s.generation),sizeof(s.generation))!=1||!s.generation){s.close();return false;}
  s.ports=ports;s.admission=admission;
  if(!s.channel->enableStreamManager(streaming::ManagerOptions{})){s.close();return false;}
  s.phase=Phase::Preparing;s.preparingEnd=end;
    s.watch=::inotify_init1(IN_CLOEXEC|IN_NONBLOCK);if(s.watch<0){s.close();return false;}
    const auto critical=s.source->criticalWatchDescriptors();
    for(int fd:s.source->watchDescriptors()){
      const auto mask=IN_ATTRIB|IN_CLOSE_WRITE|IN_MOVE_SELF|IN_DELETE_SELF|IN_UNMOUNT
          |(s.source->groupMode()?(IN_CREATE|IN_DELETE|IN_MOVED_FROM|IN_MOVED_TO):0);
      const int wd=fd<0?-1: ::inotify_add_watch(s.watch,("/proc/self/fd/"+std::to_string(fd)).c_str(),mask);
      if(wd<0){s.close();return false;}if(std::find(critical.begin(),critical.end(),fd)!=critical.end()){s.criticalWatches.push_back(wd);}
    }
    s.watcher=std::make_unique<QSocketNotifier>(s.watch,QSocketNotifier::Read,this);
    QObject::connect(s.watcher.get(),&QSocketNotifier::activated,this,[this]{
      std::array<char,4096> bytes{};const auto n=::read(impl_->watch,bytes.data(),bytes.size());
      if(n<0){if(errno!=EAGAIN&&errno!=EINTR){impl_->close("I2P source watch read failed");}return;}if(n==0){impl_->close("I2P source watch ended");return;}
      if(!impl_->source->groupMode()){impl_->close();return;}
      for(std::size_t offset=0;offset<static_cast<std::size_t>(n);){
        if(static_cast<std::size_t>(n)-offset<sizeof(inotify_event)){impl_->close();return;}
        inotify_event event{};std::memcpy(&event,bytes.data()+offset,sizeof(event));
        if(event.len>static_cast<std::size_t>(n)-offset-sizeof(event)){impl_->close();return;}
        if((event.mask&(IN_Q_OVERFLOW|IN_UNMOUNT))||std::find(impl_->criticalWatches.begin(),impl_->criticalWatches.end(),event.wd)!=impl_->criticalWatches.end()){
          impl_->close("Critical original I2P source watch event");return;
        }
        offset+=sizeof(event)+event.len;
      }
      impl_->source->invalidate();impl_->schedule();
    });
    s.permit=std::make_unique<SourcePermit>(s.source,this);
    if(Impl::Clock::now()>=end){s.close();return false;}s.arm();s.schedule();return true;
  }catch(...){s.close();return false;}
}
void Bridge::close(){impl_->close();}
const char* Bridge::failureReason()const noexcept{return impl_->failure?impl_->failure:"I2P bridge withdrew";}
void Bridge::joinSource(){if(impl_->permit){impl_->permit->join();}}
Bridge::Observation Bridge::observation()const noexcept{
  const auto& s=*impl_;Observation value{s.phase,s.generation,s.admission,s.source->destination().hash,s.server.serverPort(),0,0,s.peers.size(),s.rejectedClients};
  for(const auto& item:s.sockets){if(item){++value.sockets;value.streams+=item->operation!=0;}}return value;
}
bool Bridge::installProvider(std::function<bool(void*)> install){if(!impl_->onThread()||impl_->phase!=Phase::Ready||impl_->install||!install){return false;}impl_->install=std::move(install);impl_->schedule();return true;}
bool Bridge::event(QEvent* event){if(event->type()==SourcePermit::wakeEvent()){const auto phase=impl_->permit?impl_->permit->phase():SourcePermit::Phase::Stopped;if(phase==SourcePermit::Phase::Ready||phase==SourcePermit::Phase::Failed||phase==SourcePermit::Phase::Stopped||impl_->wanted){impl_->schedule(false);}return true;}return QObject::event(event);}
}

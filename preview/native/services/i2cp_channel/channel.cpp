// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include <QMetaObject>
#include <QNetworkProxy>
#include <QPointer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cstring>
#include <limits>

namespace biglybtc::services::i2p::i2cp {
struct InitialChannel::Impl {
  using Clock=std::chrono::steady_clock;
  InitialChannel* q;
  QTcpSocket socket;
  QTimer timer;
  FrameDecoder decoder;
  Options options;
  QByteArray hello;
  State state=State::Idle;
  Failure failure=Failure::None;
  std::optional<SetDate> date;
  std::optional<Disconnect> disconnect;
  std::optional<std::uint16_t> session;
  SessionOptions acceptedOptions,pendingOptions;
  Bytes acceptedDestination,pendingDestination;
  std::optional<ClientIdentity> leaseSigner;
  std::optional<LeaseEncryptionKey> leaseKey;
  std::optional<LeaseSubmission> leaseSubmission;
  std::uint64_t lastRawLeasePublished=0;
  bool waitingFirstLease=false,leaseNotificationQueued=false;
  std::uint64_t revision=0;
  std::uint64_t generation=0;
  Clock::time_point expires{};
  Clock::time_point dateObserved{};
  bool notificationQueued=false,drainQueued=false,reading=false,lostPending=false;

  explicit Impl(InitialChannel* owner):q(owner),socket(owner),timer(owner){
    socket.setProxy(QNetworkProxy::NoProxy); // Local I2CP control, never app proxy.
    socket.setReadBufferSize(static_cast<qint64>(MaximumFrameBytes));
    timer.setSingleShot(true);
  }
  ~Impl(){stopIo();}
  bool active()const{return state==State::Connecting||state==State::WaitingDate||state==State::DateReceived||state==State::Creating||state==State::SessionCreated||state==State::Reconfiguring;}
  bool datedPhase()const{return state==State::Connecting||state==State::WaitingDate||state==State::Creating||state==State::Reconfiguring||waitingFirstLease;}
  bool current(std::uint64_t at)const{return generation==at&&active();}
  bool onThread()const{return QThread::currentThread()==q->thread();}
  bool ls2Router()const{
    if(!date||!date->version){return false;}std::string_view value=*date->version;std::array<unsigned,3> version{};
    for(std::size_t i=0;i<3;++i){const auto dot=value.find('.');const auto part=value.substr(0,dot);if(part.empty()){return false;}const auto parsed=std::from_chars(part.data(),part.data()+part.size(),version[i]);if(parsed.ec!=std::errc{}||parsed.ptr!=part.data()+part.size()){return false;}if(i<2){if(dot==std::string_view::npos){return false;}value.remove_prefix(dot+1);}}
    return version[0]>0||version[1]>9||(version[1]==9&&version[2]>=38);
  }
  void clearSession(){session.reset();acceptedOptions.clear();pendingOptions.clear();acceptedDestination.clear();pendingDestination.clear();revision=0;leaseSigner.reset();leaseKey.reset();leaseSubmission.reset();lastRawLeasePublished=0;waitingFirstLease=false;}
  void stopIo(){
    timer.stop();QObject::disconnect(&timer,nullptr,q,nullptr);
    QObject::disconnect(&socket,nullptr,q,nullptr);socket.abort();
  }
  void notify(){
    if(notificationQueued){return;}notificationQueued=true;bool queued=false;
    try{
    const QPointer<InitialChannel> owner(q);
    queued=QMetaObject::invokeMethod(q,[owner]{
      if(!owner){return;}auto* self=owner->impl_.get();self->notificationQueued=false;
      const auto at=self->generation;const auto version=self->revision;const auto next=self->state;const auto error=self->failure;
      emit owner->stateChanged(next);
      if(!owner||owner->impl_.get()!=self||self->generation!=at||self->state!=next||self->revision!=version){return;}
      // No access after the final signal: its listener may delete the controller.
      if(next==State::DateReceived){emit owner->dateReceived();}
      else if(next==State::SessionCreated&&version==1){emit owner->sessionCreated();}
      else if(next==State::SessionCreated&&version>1){emit owner->sessionUpdated();}
      else if(next==State::Failed){emit owner->failed(error);}
      else if(next==State::Closed){emit owner->closed();}
    },Qt::QueuedConnection);
    }catch(...){queued=false;}
    if(!queued){notificationQueued=false;date.reset();clearSession();failure=Failure::Resources;state=State::Failed;stopIo();}
  }
  void fail(Failure error){
    if(state==State::Failed||state==State::Closed){return;}
    failure=error;date.reset();clearSession();hello.clear();state=State::Failed;stopIo();notify();
  }
  void notifyLease(){
    if(leaseNotificationQueued){return;}leaseNotificationQueued=true;bool queued=false;
    try{const QPointer<InitialChannel> owner(q);queued=QMetaObject::invokeMethod(q,[owner]{
      if(!owner){return;}auto* self=owner->impl_.get();self->leaseNotificationQueued=false;
      if(self->active()&&self->leaseSubmission&&self->session&&self->socket.state()==QAbstractSocket::ConnectedState){emit owner->leaseSetSubmitted();}
      // No access after a public callback that may close/restart/delete owner.
    },Qt::QueuedConnection);}catch(...){queued=false;}
    if(!queued){leaseNotificationQueued=false;fail(Failure::Resources);}
  }
  bool expired(){
    if(datedPhase()&&Clock::now()>=expires){fail(Failure::Deadline);return true;}return false;
  }
  void scheduleDrain(){
    if(drainQueued){return;}drainQueued=true;bool queued=false;
    try{
    const QPointer<InitialChannel> owner(q);
    queued=QMetaObject::invokeMethod(q,[owner]{
      if(!owner){return;}auto* self=owner->impl_.get();self->drainQueued=false;
      // This is only a coalesced wakeup, carrying no old data/token/authority.
      if(self->active()){self->drain();}
    },Qt::QueuedConnection);
    }catch(...){queued=false;}
    if(!queued){drainQueued=false;fail(Failure::Resources);}
  }
  void message(Frame frame){
    if(frame.type==RequestVariableLeaseSetType){answerLease(std::move(frame));return;}
    auto decoded=decodeMessage(frame);
    if(!decoded){fail(decoded.error().code==ErrorCode::Unsupported?Failure::UnsupportedMessage:Failure::Framing);return;}
    if(auto* reason=std::get_if<Disconnect>(&*decoded)){
      disconnect=std::move(*reason);fail(Failure::RouterDisconnected);return;
    }
    if(auto* status=std::get_if<SessionStatus>(&*decoded)){acknowledge(*status);return;}
    auto* reply=std::get_if<SetDate>(&*decoded);
    if(!reply||state!=State::WaitingDate){fail(Failure::UnexpectedMessage);return;}
    if(reply->milliseconds==0){fail(Failure::InvalidDate);return;}
    if(expired()){return;} // Decode/allocation may have crossed the absolute budget.
    date=std::move(*reply);dateObserved=Clock::now();state=State::DateReceived;timer.stop();notify();
  }
  void acknowledge(const SessionStatus& status){
    if(state!=State::Creating&&state!=State::SessionCreated&&state!=State::Reconfiguring){fail(Failure::UnexpectedMessage);return;}
    if(state!=State::Creating&&(!session||status.sessionId!=*session)){fail(Failure::InvalidSessionId);return;}
    if(status.state==SessionState::Refused||status.state==SessionState::Invalid){fail(Failure::SessionRejected);return;}
    if(status.state==SessionState::Destroyed){fail(Failure::SessionDestroyed);return;}
    if(state==State::Creating){
      if(status.state!=SessionState::Created){fail(Failure::UnexpectedMessage);return;}
      if(status.sessionId==65535){fail(Failure::InvalidSessionId);return;}
      if(expired()){return;}
      session=status.sessionId;acceptedDestination=std::move(pendingDestination);
      acceptedOptions=std::move(pendingOptions);revision=1;
    }else if(state==State::Reconfiguring&&status.state==SessionState::Updated){
      if(expired()){return;}
      acceptedOptions=std::move(pendingOptions);pendingDestination.clear();++revision;
    }else{fail(Failure::UnexpectedMessage);return;}
    const bool initial=state==State::Creating;
    state=State::SessionCreated;timer.stop();
    if(initial&&leaseSigner){waitingFirstLease=true;expires=Clock::now()+std::chrono::milliseconds(options.deadlineMilliseconds);timer.start(options.deadlineMilliseconds);}
    notify();
  }
  ConfigResult<void> request(const ClientIdentity& identity,const SessionOptions& input,std::uint64_t creation,bool update,ClientIdentity* consume=nullptr){
    using Code=::biglybtc::services::i2p::ErrorCode;
    auto reject=[](Code code,const char* text)->ConfigResult<void>{return std::unexpected(::biglybtc::services::i2p::Error{code,text});};
    if(!onThread()){return reject(Code::InvalidFormat,"I2CP request belongs to channel thread");}
    if(state!=(update?State::SessionCreated:State::DateReceived)||!date||socket.state()!=QAbstractSocket::ConnectedState||socket.bytesToWrite()!=0||socket.bytesAvailable()!=0||decoder.bufferedBytes()!=0){
      return reject(Code::InvalidFormat,"I2CP channel phase/request/write is not available");
    }
    if(!identity.valid()||!identity.destination()){return reject(Code::KeyMismatch,"No live client identity");}
    if(consume&&!ls2Router()){return reject(Code::Unsupported,"Explicit LeaseSet2 requires a supported numeric router version");}
    if(update&&waitingFirstLease){return reject(Code::Unsupported,"Initial LeaseSet request is still outstanding");}
    if(update&&(!session||identity.destination()->wire!=acceptedDestination||revision==std::numeric_limits<std::uint64_t>::max())){
      return reject(Code::KeyMismatch,"Reconfigure must use the same accepted identity/id");
    }
    try{
      const auto at=generation;const auto previous=state;const auto requestEnd=Clock::now()+std::chrono::milliseconds(options.deadlineMilliseconds);
      if(consume||leaseSigner){auto policy=leaseSetPolicy(input);if(!policy){return std::unexpected(policy.error());}}
      auto projected=projectRouterOptions(input);if(!projected){return std::unexpected(projected.error());}
      for(const auto& [key,value]:input){
        if(key=="i2cp.SSL"&&value.size()==4&&(value[0]=='t'||value[0]=='T')&&(value[1]=='r'||value[1]=='R')&&(value[2]=='u'||value[2]=='U')&&(value[3]=='e'||value[3]=='E')){
          return reject(Code::Unsupported,"TLS-required I2CP has no plaintext registration fallback");
        }
      }
      auto cfg=signSessionConfig(identity,*projected,creation);if(!cfg){return std::unexpected(cfg.error());}
      const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-dateObserved).count();
      const auto maximum=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
      if(elapsed<0||date->milliseconds>maximum-static_cast<std::uint64_t>(elapsed)){return reject(Code::InvalidFormat,"I2CP observed date clock overflow");}
      auto verified=verifySessionConfig(*cfg,date->milliseconds+static_cast<std::uint64_t>(elapsed));
      if(!verified){return std::unexpected(verified.error());}
      auto merged=update?mergeSessionOptions(acceptedOptions,*projected):ConfigResult<SessionOptions>(*projected);
      if(!merged){return std::unexpected(merged.error());}
      if(consume||leaseSigner){auto policy=leaseSetPolicy(*merged);if(!policy){return std::unexpected(policy.error());}}
      auto encoded=update?encodeSessionMessage(ReconfigureSession{*session,*cfg}):encodeSessionMessage(CreateSession{*cfg});
      if(!encoded){return std::unexpected(encoded.error());}Bytes destination=cfg->destination.wire;
      if(!current(at)||state!=previous){return reject(Code::InvalidFormat,"I2CP channel changed during signing");}
      if(Clock::now()>=requestEnd){return reject(Code::InvalidFormat,"I2CP request preparation exceeded deadline");}
      pendingOptions=std::move(*merged);pendingDestination=std::move(destination);state=update?State::Reconfiguring:State::Creating;
      if(consume){leaseSigner.emplace(std::move(*consume));}
      const auto phase=state;expires=requestEnd;
      const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(expires-Clock::now()).count();
      timer.start(static_cast<int>(std::max<std::int64_t>(1,remaining)));notify();
      if(!current(at)||state!=phase){return reject(Code::CryptoFailure,"I2CP request notification failed");}
      if(expired()){return reject(Code::InvalidFormat,"I2CP request expired before write");}
      const auto written=socket.write(reinterpret_cast<const char*>(encoded->data()),static_cast<qint64>(encoded->size()));
      if(!current(at)||state!=phase){return reject(Code::InvalidFormat,"I2CP request lost its transport");}
      if(written!=static_cast<qint64>(encoded->size())){fail(Failure::WriteFailed);return reject(Code::InvalidFormat,"I2CP request write failed");}
      return {};
    }catch(...){if(state==State::Creating||state==State::Reconfiguring){fail(Failure::Resources);}return reject(Code::CryptoFailure,"I2CP request resources unavailable");}
  }
  void answerLease(Frame frame){
    if(!leaseSigner||!session||(state!=State::SessionCreated&&state!=State::Reconfiguring)){fail(Failure::UnsupportedMessage);return;}
    if(expired()){return;}
    const auto end=Clock::now()+std::chrono::milliseconds(options.deadlineMilliseconds);const auto at=generation;
    auto requested=decodeLeaseRequest(frame);
    if(!requested||requested->sessionId!=*session||!leaseSigner->valid()||!leaseSigner->destination()||leaseSigner->destination()->wire!=acceptedDestination){fail(Failure::InvalidLeaseSet);return;}
    auto policy=leaseSetPolicy(acceptedOptions);if(!policy){fail(Failure::InvalidLeaseSet);return;}
    if(leaseSubmission&&leaseSubmission->revision==std::numeric_limits<std::uint64_t>::max()){fail(Failure::InvalidLeaseSet);return;}
    // One bounded outstanding private-key-bearing socket write. Never queue a
    // second secret frame behind a blocked router; fail closed instead.
    if(socket.bytesToWrite()!=0){fail(Failure::WriteFailed);return;}
    if(!leaseKey){auto generated=LeaseEncryptionKey::generate();if(!generated){fail(Failure::Resources);return;}leaseKey.emplace(std::move(*generated));}
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-dateObserved).count();
    if(!date||elapsed<0||static_cast<std::uint64_t>(elapsed)>0xffffffffULL*1000||date->milliseconds>0xffffffffULL*1000-static_cast<std::uint64_t>(elapsed)){fail(Failure::InvalidDate);return;}
    const auto now=date->milliseconds+static_cast<std::uint64_t>(elapsed);
    auto signedLs=signLeaseSet2(*leaseSigner,*leaseKey,*requested,*policy,now,lastRawLeasePublished);
    if(!signedLs){fail(Failure::InvalidLeaseSet);return;}
    auto encoded=encodeCreateLeaseSet2(*session,*signedLs,*leaseKey);if(!encoded){fail(Failure::InvalidLeaseSet);return;}
    if(!current(at)||Clock::now()>=end||expired()){if(current(at)){fail(Failure::Deadline);}return;}
    // Signed local submission is the same original client-side completion
    // boundary as setLeaseSet(), not a fabricated router response/OPEN claim.
    const auto bytes=encoded->bytes();const auto written=socket.write(reinterpret_cast<const char*>(bytes.data()),static_cast<qint64>(bytes.size()));
    if(!current(at)){return;}if(written!=static_cast<qint64>(bytes.size())){fail(Failure::WriteFailed);return;}
    lastRawLeasePublished=std::max(now,lastRawLeasePublished?lastRawLeasePublished+1000:now);
    const auto next=leaseSubmission?leaseSubmission->revision+1:1;
    leaseSubmission=LeaseSubmission{next,signedLs->publishedMilliseconds,signedLs->expiresMilliseconds,signedLs->leases.size(),signedLs->unpublished};
    waitingFirstLease=false;if(state==State::SessionCreated){timer.stop();}notifyLease();
  }
  void drain(){
    if(reading||!active()||state==State::Connecting||expired()){return;}
    reading=true;
    try{
      readLoop();
    }catch(...){fail(Failure::Resources);}
    reading=false;
    if(lostPending){lostPending=false;lost();}
  }
  void readLoop(){
      std::array<char,4096> input{};std::size_t budget=16*1024;
      while(active()&&budget&&socket.bytesAvailable()>0){
        if(expired()){return;}
        const auto wanted=static_cast<qint64>(std::min(input.size(),budget));
        const auto count=socket.read(input.data(),wanted);
        if(count<=0){fail(Failure::ReadFailed);return;}
        const auto size=static_cast<std::size_t>(count);budget-=size;std::size_t offset=0;
        while(active()&&offset<size){
          auto step=decoder.consume(View(reinterpret_cast<const Byte*>(input.data()+offset),size-offset));
          if(!step||!step->consumed||step->consumed>size-offset){fail(Failure::Framing);return;}
          offset+=step->consumed;
          if(step->frame){message(std::move(*step->frame));}
        }
      }
      if(active()&&socket.bytesAvailable()>0){scheduleDrain();}
  }
  void lost(){
    if(reading){lostPending=true;return;}
    if(!active()){return;}
    try{
    const auto error=state==State::Connecting?Failure::ConnectFailed:Failure::TransportClosed;
    // Valid final Disconnect/date-sized frames are drained before EOF. Huge or
    // incomplete initial data still fails closed; no date observation survives.
    if(socket.bytesAvailable()>0){drain();}
    if(active()){fail(decoder.finish()?error:Failure::Framing);}
    }catch(...){fail(Failure::Resources);}
  }
  void connected(){
    if(state!=State::Connecting||expired()){return;}
    if(socket.peerAddress()!=options.address||socket.peerPort()!=options.port||!socket.localAddress().isLoopback()){
      fail(Failure::InvalidEndpoint);return;
    }
    const auto at=generation;socket.setSocketOption(QAbstractSocket::KeepAliveOption,1);
    if(!current(at)||state!=State::Connecting){return;}
    const auto written=socket.write(hello);
    if(!current(at)||state!=State::Connecting){return;}
    if(written!=hello.size()){fail(Failure::WriteFailed);return;}
    hello.clear();state=State::WaitingDate;notify();
  }
  bool begin(const Options& next){
    if(!onThread()||active()||generation==std::numeric_limits<std::uint64_t>::max()){return false;}
    ++generation;stopIo();decoder.reset();lostPending=false;date.reset();disconnect.reset();clearSession();failure=Failure::None;state=State::Idle;
    if(next.authenticationRequired){fail(Failure::UnsupportedAuthentication);return false;}
    if(next.tlsRequired){fail(Failure::UnsupportedTransport);return false;}
    const bool family=next.address.protocol()==QAbstractSocket::IPv4Protocol||next.address.protocol()==QAbstractSocket::IPv6Protocol;
    if(!family||!next.address.isLoopback()||!next.address.scopeId().isEmpty()||!next.port){fail(Failure::InvalidEndpoint);return false;}
    if(next.clientVersion.empty()||next.clientVersion.size()>255||std::any_of(next.clientVersion.begin(),next.clientVersion.end(),[](unsigned char byte){return byte>127;})){
      fail(Failure::InvalidVersion);return false;
    }
    if(next.deadlineMilliseconds<1||next.deadlineMilliseconds>30000){fail(Failure::InvalidDeadline);return false;}
    try{
      options=next;auto encoded=encodeMessage(GetDate{options.clientVersion});
      if(!encoded){fail(Failure::InvalidVersion);return false;}
      hello=QByteArray(static_cast<qsizetype>(1+encoded->size()),Qt::Uninitialized);
      hello[0]=static_cast<char>(ClientGreeting[0]);std::memcpy(hello.data()+1,encoded->data(),encoded->size());
    }catch(...){fail(Failure::Resources);return false;}
    const auto at=generation;
    QObject::connect(&socket,&QTcpSocket::connected,q,[this,at]{if(current(at)){try{connected();}catch(...){fail(Failure::Resources);}}});
    QObject::connect(&socket,&QTcpSocket::readyRead,q,[this,at]{if(current(at)){drain();}});
    QObject::connect(&socket,&QTcpSocket::errorOccurred,q,[this,at](QAbstractSocket::SocketError){if(current(at)){lost();}});
    QObject::connect(&socket,&QTcpSocket::disconnected,q,[this,at]{if(current(at)){lost();}});
    QObject::connect(&timer,&QTimer::timeout,q,[this,at]{
      if(!current(at)||!datedPhase()){return;}
      if(!expired()){
        const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(expires-Clock::now()).count();
        timer.start(static_cast<int>(std::max<std::int64_t>(1,left)));
      }
    });
    state=State::Connecting;expires=Clock::now()+std::chrono::milliseconds(next.deadlineMilliseconds);
    timer.start(next.deadlineMilliseconds);notify();
    if(!current(at)||state!=State::Connecting){return false;}
    if(expired()){return false;}
    try{socket.connectToHost(next.address,next.port);}catch(...){fail(Failure::Resources);return false;}
    return true;
  }
  void close(){
    if(!onThread()||state==State::Closed){return;}
    // Invalidates all generation-tagged socket/timer callbacks before abort.
    if(generation!=std::numeric_limits<std::uint64_t>::max()){++generation;}
    date.reset();clearSession();hello.clear();failure=Failure::None;state=State::Closed;stopIo();decoder.reset();notify();
  }
};
InitialChannel::InitialChannel(QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this)){}
InitialChannel::~InitialChannel()=default;
bool InitialChannel::begin(const Options& options){return impl_->begin(options);}
void InitialChannel::close(){impl_->close();}
ConfigResult<void> InitialChannel::createSession(const ClientIdentity& identity,const SessionOptions& options,std::uint64_t date){return impl_->request(identity,options,date,false);}
ConfigResult<void> InitialChannel::createLeaseSession(ClientIdentity&& identity,const SessionOptions& options,std::uint64_t date){return impl_->request(identity,options,date,false,&identity);}
ConfigResult<void> InitialChannel::reconfigureLeaseSession(const SessionOptions& options,std::uint64_t date){
  if(!impl_->onThread()){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::InvalidFormat,"I2CP request belongs to channel thread"});}
  if(!impl_->leaseSigner){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::KeyMismatch,"No retained LeaseSet signing capability"});}
  return impl_->request(*impl_->leaseSigner,options,date,true);
}
ConfigResult<void> InitialChannel::reconfigureSession(const ClientIdentity& identity,const SessionOptions& options,std::uint64_t date){return impl_->request(identity,options,date,true);}
std::optional<std::uint16_t> InitialChannel::sessionId()const noexcept{
  return impl_->socket.state()==QAbstractSocket::ConnectedState&&(impl_->state==State::SessionCreated||impl_->state==State::Reconfiguring)?impl_->session:std::nullopt;
}
SessionOptions InitialChannel::sessionOptions()const{return sessionId()?impl_->acceptedOptions:SessionOptions{};}
std::optional<InitialChannel::LeaseSubmission> InitialChannel::leaseSetSubmission()const noexcept{return sessionId()?impl_->leaseSubmission:std::nullopt;}
InitialChannel::State InitialChannel::state()const noexcept{return impl_->state;}
InitialChannel::Failure InitialChannel::failure()const noexcept{return impl_->failure;}
bool InitialChannel::socketActive()const noexcept{return impl_->socket.state()!=QAbstractSocket::UnconnectedState;}
bool InitialChannel::deadlineActive()const noexcept{return impl_->timer.isActive();}
std::optional<SetDate> InitialChannel::routerDate()const{
  return impl_->active()&&impl_->state!=State::Connecting&&impl_->state!=State::WaitingDate&&impl_->socket.state()==QAbstractSocket::ConnectedState?impl_->date:std::nullopt;
}
std::optional<Disconnect> InitialChannel::routerDisconnect()const{return impl_->disconnect;}
} // namespace biglybtc::services::i2p::i2cp

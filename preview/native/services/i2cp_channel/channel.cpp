// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include <QMetaObject>
#include <QNetworkProxy>
#include <QPointer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cstring>
#include <deque>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace biglybtc::services::i2p::i2cp {
struct InitialChannel::Impl {
  using Clock=std::chrono::steady_clock;
  InitialChannel* q;
  QTcpSocket socket;
  QTimer timer;
  QTimer statusTimer;
  QTimer receiveTimer;
  QTimer trackerTimer;
  QTimer streamTimer;
  QTimer lookupTimer;
  QTimer effectTimer;
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
  bool statusReplies=true,pendingStatusReplies=true,statusPending=false,statusNotificationQueued=false;
  std::uint64_t nextDatagramNonce=1; // Not reset by close/restart; never reused.
  Clock::time_point statusExpires{};
  std::optional<DatagramObservation> datagram;
  std::optional<std::uint32_t> datagramMessageId;
  std::uint64_t revision=0;
  std::uint64_t generation=0;
  Clock::time_point expires{};
  Clock::time_point dateObserved{};
  bool notificationQueued=false,drainQueued=false,reading=false,lostPending=false;

  explicit Impl(InitialChannel* owner):q(owner),socket(owner),timer(owner),statusTimer(owner),receiveTimer(owner),trackerTimer(owner),streamTimer(owner),lookupTimer(owner),effectTimer(owner){
    socket.setProxy(QNetworkProxy::NoProxy); // Local I2CP control, never app proxy.
    socket.setReadBufferSize(static_cast<qint64>(MaximumFrameBytes));
    timer.setSingleShot(true);
    statusTimer.setSingleShot(true);
    receiveTimer.setSingleShot(true);
    trackerTimer.setSingleShot(true);
    streamTimer.setSingleShot(true);
    lookupTimer.setSingleShot(true);
    effectTimer.setSingleShot(true);
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
  void clearSession(){clearSourceEffects();clearLookup();clearStream();clearTracker();clearReceive();session.reset();acceptedOptions.clear();pendingOptions.clear();acceptedDestination.clear();pendingDestination.clear();revision=0;leaseSigner.reset();leaseKey.reset();leaseSubmission.reset();lastRawLeasePublished=0;waitingFirstLease=false;statusPending=false;datagram.reset();datagramMessageId.reset();statusReplies=true;pendingStatusReplies=true;statusTimer.stop();}
  void stopIo(){
    timer.stop();QObject::disconnect(&timer,nullptr,q,nullptr);
    statusTimer.stop();QObject::disconnect(&statusTimer,nullptr,q,nullptr);
    receiveTimer.stop();QObject::disconnect(&receiveTimer,nullptr,q,nullptr);
    trackerTimer.stop();QObject::disconnect(&trackerTimer,nullptr,q,nullptr);
    streamTimer.stop();QObject::disconnect(&streamTimer,nullptr,q,nullptr);
    lookupTimer.stop();QObject::disconnect(&lookupTimer,nullptr,q,nullptr);
    effectTimer.stop();QObject::disconnect(&effectTimer,nullptr,q,nullptr);
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
    if(frame.type==HostReplyType){if(!observeOwnedLookup(frame)){observeHostReply(frame);}return;}
    if(frame.type==MessagePayloadType){inboundPayload(std::move(frame));return;}
    if(frame.type==MessageStatusType){observeDatagram(frame);return;}
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
      statusReplies=pendingStatusReplies; // Original immutable creation policy.
      fastReceive=pendingFastReceive; // Original immutable receive policy too.
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
    if(!effectReady()){return reject(Code::Unsupported,"Owned I2CP source permit required");}
    if(state!=(update?State::SessionCreated:State::DateReceived)||!date||socket.state()!=QAbstractSocket::ConnectedState||socket.bytesToWrite()!=0||socket.bytesAvailable()!=0||decoder.bufferedBytes()!=0){
      return reject(Code::InvalidFormat,"I2CP channel phase/request/write is not available");
    }
    if(!identity.valid()||!identity.destination()){return reject(Code::KeyMismatch,"No live client identity");}
    if(consume&&!ls2Router()){return reject(Code::Unsupported,"Explicit LeaseSet2 requires a supported numeric router version");}
    if(update&&waitingFirstLease){return reject(Code::Unsupported,"Initial LeaseSet request is still outstanding");}
    if(update&&streamsOutstanding()){return reject(Code::Unsupported,"Streaming operation is still outstanding");}
    if(update&&(tracker||ownedLookup)){return reject(Code::Unsupported,"Explicit lookup/tracker operation is still outstanding");}
    if(update&&statusPending){return reject(Code::Unsupported,"Datagram status request is still outstanding");}
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
      if(consume||leaseSigner){auto policy=leaseSetPolicy(*merged);if(!policy){return std::unexpected(policy.error());}
        if(update){auto accepted=leaseSetPolicy(acceptedOptions);if(!accepted){return std::unexpected(accepted.error());}if(policy->encryptionType!=accepted->encryptionType){return reject(Code::Unsupported,"Existing lease encryption type cannot rotate during reconfigure");}}
      }
      auto encoded=update?encodeSessionMessage(ReconfigureSession{*session,*cfg}):encodeSessionMessage(CreateSession{*cfg});
      if(!encoded){return std::unexpected(encoded.error());}Bytes destination=cfg->destination.wire;
      if(!current(at)||state!=previous){return reject(Code::InvalidFormat,"I2CP channel changed during signing");}
      if(Clock::now()>=requestEnd){return reject(Code::InvalidFormat,"I2CP request preparation exceeded deadline");}
      pendingOptions=std::move(*merged);pendingDestination=std::move(destination);state=update?State::Reconfiguring:State::Creating;
      if(!update){pendingStatusReplies=originalStatusRepliesEnabled(pendingOptions);}
      if(!update){pendingFastReceive=originalFastReceiveEnabled(pendingOptions);}
      if(consume){leaseSigner.emplace(std::move(*consume));}
      const auto phase=state;expires=requestEnd;
      const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(expires-Clock::now()).count();
      timer.start(static_cast<int>(std::max<std::int64_t>(1,remaining)));notify();
      if(!current(at)||state!=phase){return reject(Code::CryptoFailure,"I2CP request notification failed");}
      if(expired()){return reject(Code::InvalidFormat,"I2CP request expired before write");}
      if(!effectReady()){return reject(Code::Unsupported,"Owned I2CP source lost before write");}
      const auto written=socket.write(reinterpret_cast<const char*>(encoded->data()),static_cast<qint64>(encoded->size()));
      if(!current(at)||state!=phase){return reject(Code::InvalidFormat,"I2CP request lost its transport");}
      if(written!=static_cast<qint64>(encoded->size())){fail(Failure::WriteFailed);return reject(Code::InvalidFormat,"I2CP request write failed");}
      return {};
    }catch(...){if(state==State::Creating||state==State::Reconfiguring){fail(Failure::Resources);}return reject(Code::CryptoFailure,"I2CP request resources unavailable");}
  }
  void answerLease(Frame frame){
    const auto end=Clock::now()+std::chrono::milliseconds(options.deadlineMilliseconds);
    if(sourceAuthority&&!effectReady()){
      if(!active()){return;}auto request=decodeLeaseRequest(frame);if(sourceLease||!session||!request||request->sessionId!=*session){fail(Failure::InvalidLeaseSet);return;}
      sourceLease.emplace(SourceLease{std::move(frame),end,generation});return;
    }
    if(managedStreams&&socket.bytesToWrite()!=0){
      auto request=decodeLeaseRequest(frame);
      if(managedLease||!managedCurrent()||!request||request->sessionId!=*session){fail(Failure::InvalidLeaseSet);return;}
      managedLease.emplace(DeferredLease{std::move(frame),end,generation});armManagedStreams(true);return;
    }
    answerLeaseWithin(std::move(frame),end);
  }
  void answerLeaseWithin(Frame frame,Clock::time_point end){
    if(!effectReady()){return;}
    if(!leaseSigner||!session||(state!=State::SessionCreated&&state!=State::Reconfiguring)){fail(Failure::UnsupportedMessage);return;}
    if(expired()){return;}
    if(Clock::now()>=end){fail(Failure::Deadline);return;}const auto at=generation;
    // A late renewal may update the router submission, but cannot restore
    // streams/listening authority whose previous actual LS2 already expired.
    if(sourceAuthority&&leaseSubmission&&!leaseCurrent()){fail(Failure::SourceChanged);return;}
    if(managedStreams&&leaseSubmission&&!leaseCurrent()){(void)managedCurrent();}
    if(!current(at)){return;}
    auto requested=decodeLeaseRequest(frame);
    if(!requested||requested->sessionId!=*session||!leaseSigner->valid()||!leaseSigner->destination()||leaseSigner->destination()->wire!=acceptedDestination){fail(Failure::InvalidLeaseSet);return;}
    auto policy=leaseSetPolicy(acceptedOptions);if(!policy){fail(Failure::InvalidLeaseSet);return;}
    if(leaseSubmission&&leaseSubmission->revision==std::numeric_limits<std::uint64_t>::max()){fail(Failure::InvalidLeaseSet);return;}
    // One bounded outstanding private-key-bearing socket write. Never queue a
    // second secret frame behind a blocked router; fail closed instead.
    if(socket.bytesToWrite()!=0){fail(Failure::WriteFailed);return;}
    if(!leaseKey){auto generated=LeaseEncryptionKey::generate(policy->encryptionType);if(!generated){fail(Failure::Resources);return;}leaseKey.emplace(std::move(*generated));}
    if(leaseKey->type()!=policy->encryptionType){fail(Failure::InvalidLeaseSet);return;}
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-dateObserved).count();
    if(!date||elapsed<0||static_cast<std::uint64_t>(elapsed)>0xffffffffULL*1000||date->milliseconds>0xffffffffULL*1000-static_cast<std::uint64_t>(elapsed)){fail(Failure::InvalidDate);return;}
    const auto now=date->milliseconds+static_cast<std::uint64_t>(elapsed);
    auto signedLs=signLeaseSet2(*leaseSigner,*leaseKey,*requested,*policy,now,lastRawLeasePublished);
    if(!signedLs){fail(Failure::InvalidLeaseSet);return;}
    auto encoded=encodeCreateLeaseSet2(*session,*signedLs,*leaseKey);if(!encoded){fail(Failure::InvalidLeaseSet);return;}
    // Signing/encoding may cross the old lease boundary before its Qt timer.
    if(sourceAuthority&&leaseSubmission&&!leaseCurrent()){fail(Failure::SourceChanged);return;}
    if(managedStreams&&leaseSubmission&&!leaseCurrent()){(void)managedCurrent();}
    if(!current(at)||Clock::now()>=end||expired()){if(current(at)){fail(Failure::Deadline);}return;}
    // Signed local submission is the same original client-side completion
    // boundary as setLeaseSet(), not a fabricated router response/OPEN claim.
    if(!effectReady()){return;}const auto bytes=encoded->bytes();const auto written=socket.write(reinterpret_cast<const char*>(bytes.data()),static_cast<qint64>(bytes.size()));
    if(!current(at)){return;}if(written!=static_cast<qint64>(bytes.size())){fail(Failure::WriteFailed);return;}
    lastRawLeasePublished=std::max(now,lastRawLeasePublished?lastRawLeasePublished+1000:now);
    const auto next=leaseSubmission?leaseSubmission->revision+1:1;
    leaseSubmission=LeaseSubmission{next,signedLs->publishedMilliseconds,signedLs->expiresMilliseconds,signedLs->leases.size(),signedLs->unpublished};
    waitingFirstLease=false;if(state==State::SessionCreated){timer.stop();}notifyLease();
    if(statusPending){armStatusDeadline();} // Only lease fence changes; original statusExpires is immutable.
    if(managedStreams){armManagedStreams(socket.bytesToWrite()!=0);}
  }
  ConfigResult<std::uint64_t> observedNow()const{
    using Code=::biglybtc::services::i2p::ErrorCode;
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-dateObserved).count();
    if(!date||elapsed<0||static_cast<std::uint64_t>(elapsed)>0xffffffffULL*1000||date->milliseconds>0xffffffffULL*1000-static_cast<std::uint64_t>(elapsed)){return std::unexpected(::biglybtc::services::i2p::Error{Code::InvalidFormat,"Observed router clock unavailable/overflowed"});}
    return date->milliseconds+static_cast<std::uint64_t>(elapsed);
  }
  bool leaseCurrent()const{
    if(!leaseSigner||!leaseSigner->valid()||!leaseSigner->destination()||leaseSigner->destination()->wire!=acceptedDestination||!session||!leaseSubmission||socket.state()!=QAbstractSocket::ConnectedState){return false;}
    auto now=observedNow();return now&&*now<leaseSubmission->expiresMilliseconds;
  }
  void notifyDatagram(){
    if(statusNotificationQueued){return;}statusNotificationQueued=true;bool queued=false;
    try{const QPointer<InitialChannel> owner(q);queued=QMetaObject::invokeMethod(q,[owner]{if(!owner){return;}auto* self=owner->impl_.get();self->statusNotificationQueued=false;if(self->active()&&self->datagram&&self->session&&self->socket.state()==QAbstractSocket::ConnectedState){emit owner->datagramChanged();}},Qt::QueuedConnection);}catch(...){queued=false;}
    if(!queued){statusNotificationQueued=false;fail(Failure::Resources);}
  }
  void localDatagram(DatagramPhase reason){if(!statusPending||!datagram){return;}statusPending=false;statusTimer.stop();datagram->phase=reason;datagram->routerStatus.reset();notifyDatagram();}
  void armStatusDeadline(){
    if(!statusPending){return;}if(!leaseCurrent()){localDatagram(DatagramPhase::LocalLeaseExpired);return;}if(Clock::now()>=statusExpires){localDatagram(DatagramPhase::LocalTimeout);return;}
    const auto now=observedNow();if(!now){localDatagram(DatagramPhase::LocalLeaseExpired);return;}
    if(*now>=leaseSubmission->expiresMilliseconds){localDatagram(DatagramPhase::LocalLeaseExpired);return;}
    const auto statusLeft=std::chrono::duration_cast<std::chrono::milliseconds>(statusExpires-Clock::now()).count();const auto leaseLeft=leaseSubmission->expiresMilliseconds-*now;
    if(statusLeft<=0){localDatagram(DatagramPhase::LocalTimeout);return;}
    statusTimer.start(static_cast<int>(std::max<std::int64_t>(1,std::min(statusLeft,static_cast<std::int64_t>(leaseLeft)))));
  }
  void observeDatagram(const Frame& frame){
    auto status=decodeMessageStatus(frame);if(!status){fail(Failure::Framing);return;}
    if(status->code==0){availableDatagram(*status);return;}
    if(!session||status->sessionId!=*session||status->code==0){fail(Failure::UnexpectedMessage);return;}
    // Same current session, strictly retired nonce: no stale status can finish
    // a later request. No message-ID-only/nonce0 fallback is permitted here.
    if(!statusPending||!datagram||status->nonce!=datagram->nonce){if(status->nonce<nextDatagramNonce){return;}fail(Failure::UnexpectedMessage);return;}
    if(!leaseCurrent()){localDatagram(DatagramPhase::LocalLeaseExpired);return;}
    if(Clock::now()>=statusExpires){localDatagram(DatagramPhase::LocalTimeout);return;}
    if(datagramMessageId&&*datagramMessageId!=status->messageId){fail(Failure::UnexpectedMessage);return;}
    datagramMessageId=status->messageId;
    // Final fresh budget check before committing a decoded status observation.
    if(!leaseCurrent()){localDatagram(DatagramPhase::LocalLeaseExpired);return;}
    if(Clock::now()>=statusExpires){localDatagram(DatagramPhase::LocalTimeout);return;}
    datagram->routerStatus=*status;datagram->phase=status->code==1?DatagramPhase::RouterAccepted:DatagramPhase::RouterStatus;
    if(status->code!=1){statusPending=false;statusTimer.stop();}notifyDatagram();
  }
  ConfigResult<std::uint32_t> submit(View recipient,View payload,const DatagramOptions& opts,bool claimed){
    using Code=::biglybtc::services::i2p::ErrorCode;auto reject=[](Code code,const char* text)->ConfigResult<std::uint32_t>{return std::unexpected(::biglybtc::services::i2p::Error{code,text});};
    if(!onThread()){return reject(Code::InvalidFormat,"Datagram request belongs to channel thread");}
    if(state!=State::SessionCreated||waitingFirstLease||statusPending||tracker||streamsOutstanding()||!leaseCurrent()||socket.bytesToWrite()!=0||socket.bytesAvailable()!=0||decoder.bufferedBytes()!=0){return reject(Code::InvalidFormat,"Current unexpired owned LS2 and idle datagram/write state required");}
    if(!statusReplies){return reject(Code::Unsupported,"Initial reliability none cannot request tracked statuses");}
    if(nextDatagramNonce>std::numeric_limits<std::uint32_t>::max()){return reject(Code::TooLarge,"Datagram nonce lifetime exhausted; never reuse");}
    try{
      const auto at=generation;const auto end=Clock::now()+std::chrono::milliseconds(options.deadlineMilliseconds);auto dest=decodeDestination(recipient);if(!dest){return std::unexpected(dest.error());}
      const auto now=observedNow();if(!now){return std::unexpected(now.error());}
      if(opts.expirationMilliseconds&&(*opts.expirationMilliseconds<=*now||*opts.expirationMilliseconds>0xffffffffffffULL)){return reject(Code::InvalidFormat,"Datagram expiration must be future uint48 absolute time");}
      auto bytes=claimed?encode3(leaseSigner->destination()->wire,payload):leaseSigner->makeDatagram2(dest->hash,payload);if(!bytes){return std::unexpected(bytes.error());}
      auto mux=encodeMuxDatagram(MuxDatagram{claimed?Datagram3Protocol:Datagram2Protocol,opts.fromPort,opts.toPort,std::move(*bytes)},opts.gzip);if(!mux){return std::unexpected(mux.error());}
      const auto nonce=static_cast<std::uint32_t>(nextDatagramNonce);auto wire=encodeSendDatagram(SendDatagram{*session,std::move(*dest),std::move(*mux),nonce,opts.expirationMilliseconds});if(!wire){return std::unexpected(wire.error());}
      if(!current(at)||state!=State::SessionCreated||!leaseCurrent()||Clock::now()>=end){return reject(Code::InvalidFormat,"Datagram preparation lost current lease/budget");}
      if(opts.expirationMilliseconds){const auto fresh=observedNow();if(!fresh||*opts.expirationMilliseconds<=*fresh){return reject(Code::InvalidFormat,"Datagram expired during preparation");}}
      if(!effectReady()){return reject(Code::Unsupported,"Owned I2CP source permit required");}
      ++nextDatagramNonce;statusExpires=end;datagram=DatagramObservation{nonce,DatagramPhase::Pending,std::nullopt};datagramMessageId.reset();statusPending=true;armStatusDeadline();
      if(!current(at)||!statusPending){return reject(Code::InvalidFormat,"Datagram budget/notification failed before write");}
      const auto written=socket.write(reinterpret_cast<const char*>(wire->data()),static_cast<qint64>(wire->size()));
      if(!current(at)){return reject(Code::InvalidFormat,"Datagram lost its transport");}if(written!=static_cast<qint64>(wire->size())){fail(Failure::WriteFailed);return reject(Code::InvalidFormat,"Datagram enqueue failed");}
      notifyDatagram();if(!current(at)){return reject(Code::CryptoFailure,"Datagram notification failed");}return nonce;
    }catch(...){if(statusPending){fail(Failure::Resources);}return reject(Code::CryptoFailure,"Datagram resources unavailable");}
  }
#include "owned_effects.inc"
#include "streaming.inc"
#include "managed_streams.inc"
#include "tracker.inc"
#include "owned_lookup.inc"
#include "receive.inc"
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
    if(!effectReady()){return;}
    const auto at=generation;socket.setSocketOption(QAbstractSocket::KeepAliveOption,1);
    if(!current(at)||state!=State::Connecting){return;}
    if(!effectReady()){return;}
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
    QObject::connect(&statusTimer,&QTimer::timeout,q,[this,at]{if(current(at)){try{armStatusDeadline();}catch(...){fail(Failure::Resources);}}});
    QObject::connect(&receiveTimer,&QTimer::timeout,q,[this,at]{if(current(at)){try{receiveDeadline();}catch(...){fail(Failure::Resources);}}});
    QObject::connect(&trackerTimer,&QTimer::timeout,q,[this,at]{if(current(at)){try{trackerDeadline();}catch(...){fail(Failure::Resources);}}});
    QObject::connect(&effectTimer,&QTimer::timeout,q,[this,at]{if(current(at)){try{if(sourceEffectPending){if(Clock::now()>=sourceEffectEnd){fail(Failure::Deadline);}else{effectTimer.start(static_cast<int>(std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(sourceEffectEnd-Clock::now()).count())));}}}catch(...){fail(Failure::Resources);}}});
    QObject::connect(&lookupTimer,&QTimer::timeout,q,[this,at]{if(current(at)){try{lookupDeadline();}catch(...){fail(Failure::Resources);}}});
    QObject::connect(&streamTimer,&QTimer::timeout,q,[this,at]{if(current(at)){flushStream();}});
    QObject::connect(&socket,&QTcpSocket::bytesWritten,q,[this,at](qint64){if(current(at)){if(sourceAuthority&&!managedStreams&&leaseSubmission&&socket.bytesToWrite()==0){static_cast<void>(effectReady());}if(sourceAuthority&&(sourceLease||state==State::Connecting)&&effectReady()){resumeSourceEffects();}if(current(at)&&managedLease){(void)flushManagedLease();}if(current(at)){flushReceiveControls();flushTrackerWrite();flushOwnedLookup();flushStream();}}});
    state=State::Connecting;expires=Clock::now()+std::chrono::milliseconds(next.deadlineMilliseconds);
    timer.start(next.deadlineMilliseconds);notify();
    if(!current(at)||state!=State::Connecting){return false;}
    if(expired()){return false;}
    try{if(sourceAuthority&&!effectReady()){return active();}socket.connectToHost(next.address,next.port);}catch(...){fail(Failure::Resources);return false;}
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
bool InitialChannel::bindEffectAuthority(std::shared_ptr<EffectAuthority> authority){if(!impl_->onThread()||impl_->state!=State::Idle||impl_->sourceAuthority||!authority||!authority->current()){return false;}impl_->sourceAuthority=std::move(authority);return true;}
bool InitialChannel::sourceEffectsPending()const noexcept{return impl_->sourceEffectPending;}
bool InitialChannel::ownedTransportIdle()const noexcept{return impl_->onThread()&&impl_->state==State::SessionCreated&&impl_->leaseCurrent()&&impl_->socket.bytesToWrite()==0&&impl_->socket.bytesAvailable()==0&&impl_->decoder.bufferedBytes()==0&&!impl_->sourceLease&&!impl_->managedLease&&!impl_->statusPending&&!impl_->fetch;}
bool InitialChannel::resumeAuthorizedEffects(){return impl_->resumeSourceEffects();}
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
ConfigResult<std::uint32_t> InitialChannel::submitDatagram2(View recipient,View payload,const DatagramOptions& options){return impl_->submit(recipient,payload,options,false);}
ConfigResult<std::uint32_t> InitialChannel::submitClaimedDatagram3(View recipient,View payload,const DatagramOptions& options){return impl_->submit(recipient,payload,options,true);}
bool InitialChannel::cancelDatagram(std::uint32_t nonce){if(!impl_->onThread()||!impl_->statusPending||!impl_->datagram||impl_->datagram->nonce!=nonce){return false;}impl_->localDatagram(DatagramPhase::LocalCancelled);return true;}
bool InitialChannel::datagramPending()const noexcept{return impl_->statusPending;}
std::optional<InitialChannel::DatagramObservation> InitialChannel::datagramObservation()const noexcept{return sessionId()?impl_->datagram:std::nullopt;}
bool InitialChannel::enableDatagramReceiver(){return impl_->enableReceive();}
void InitialChannel::disableDatagramReceiver(){impl_->disableReceive();}
std::optional<InitialChannel::ReceivedDatagram> InitialChannel::takeReceivedDatagram(){return impl_->takeReceive();}
InitialChannel::ReceiverObservation InitialChannel::receiverObservation()const noexcept{return impl_->receiveSnapshot();}
ConfigResult<std::uint64_t> InitialChannel::scrapeTracker(const TrackerRequest& input){return impl_->startTracker(input);}
bool InitialChannel::cancelTracker(std::uint64_t operation){if(!impl_->onThread()||!impl_->tracker||impl_->tracker->operation!=operation){return false;}impl_->finishTracker(TrackerPhase::LocalCancelled);return true;}
std::optional<TrackerObservation> InitialChannel::trackerObservation()const{return sessionId()?impl_->trackerResult:std::nullopt;}
ConfigResult<std::uint64_t> InitialChannel::connectStream(View destination,const StreamOptions& options){
  if(impl_->sourceAuthority&&!impl_->managedStreams){return std::unexpected(::biglybtc::services::i2p::Error{::biglybtc::services::i2p::ErrorCode::Unsupported,"Owned source effects require the managed stream path"});}
  return impl_->managedStreams?impl_->startManagedStream(destination,options):impl_->startStream(destination,options);
}
ConfigResult<void> InitialChannel::writeStream(std::uint64_t operation,View bytes){return impl_->managedStreams?impl_->writeManagedStream(operation,bytes):impl_->streamWrite(operation,bytes);}
ConfigResult<Bytes> InitialChannel::takeStreamBytes(std::uint64_t operation,std::size_t maximum){return impl_->managedStreams?impl_->readManagedStream(operation,maximum):impl_->streamRead(operation,maximum);}
bool InitialChannel::finishStreamWrite(std::uint64_t operation){
  if(impl_->managedStreams){if(!impl_->onThread()||!impl_->managedCurrent()){return false;}try{const bool ok=impl_->managedStreams->finishWrite(operation,impl_->streamNow());impl_->flushManagedStreams();impl_->armManagedStreams();impl_->notifyStream();return ok&&impl_->active();}catch(...){impl_->fail(Failure::Resources);return false;}}
  if(!impl_->onThread()||!impl_->stream||impl_->stream->observation().operation!=operation||!impl_->streamCurrent()||!impl_->stream->active()){return false;}
  impl_->stream->finishWrite();impl_->flushStream();return impl_->stream.has_value()&&(impl_->stream->active()||impl_->stream->observation().phase==streaming::Phase::Closed)&&impl_->current(impl_->streamGeneration);
}
bool InitialChannel::cancelStream(std::uint64_t operation){
  if(impl_->managedStreams){if(!impl_->onThread()){return false;}const bool ok=impl_->managedStreams->cancel(operation);impl_->flushManagedStreams();impl_->armManagedStreams();impl_->notifyStream();return ok;}
  if(!impl_->onThread()||!impl_->stream||impl_->stream->observation().operation!=operation||!impl_->stream->active()){return false;}
  impl_->stream->terminate(streaming::Phase::Cancelled);impl_->streamTimer.stop();if(!impl_->receiverEnabled&&!impl_->trackerReceives()&&impl_->fetch){impl_->cancelFetch(ReceiveDrop::Cancelled);}impl_->notifyStream();return true;
}
std::optional<streaming::Observation> InitialChannel::streamObservation()const noexcept{if(impl_->managedStreams){const auto value=managedStreamObservation(impl_->managedLastOperation);return value?std::optional(value->stream):std::nullopt;}return sessionId()&&impl_->stream?std::optional(impl_->stream->observation()):std::nullopt;}
bool InitialChannel::enableStreamManager(const streaming::ManagerOptions& options){return impl_->enableManagedStreams(options);}
bool InitialChannel::listenStreams(const streaming::ListenOptions& options){return impl_->listenManagedStreams(options);}
void InitialChannel::closeStreamListener(){if(!impl_->onThread()||!impl_->managedStreams){return;}try{impl_->managedStreams->stopListening();if(!impl_->receiverEnabled&&!impl_->trackerReceives()&&!impl_->streamReceives()&&impl_->fetch){impl_->cancelFetch(ReceiveDrop::Cancelled);}impl_->armManagedStreams();impl_->notifyStream();}catch(...){impl_->fail(Failure::Resources);}}
ConfigResult<std::uint64_t> InitialChannel::acceptStream(){return impl_->startManagedStream({},StreamOptions{},true);}
std::optional<streaming::ManagedObservation> InitialChannel::managedStreamObservation(std::uint64_t operation)const noexcept{return sessionId()&&impl_->managedStreams?impl_->managedStreams->observation(operation):std::nullopt;}
streaming::ManagerObservation InitialChannel::streamManagerObservation()const noexcept{return impl_->managedStreams?impl_->managedStreams->observation():streaming::ManagerObservation{};}
bool InitialChannel::waitStream(std::uint64_t operation,streaming::WaitFor kind,std::size_t minimum,unsigned period){if(!impl_->onThread()||!impl_->managedCurrent()){return false;}try{const bool ok=impl_->managedStreams->wait(operation,kind,minimum,period,impl_->streamNow());impl_->armManagedStreams();impl_->notifyStream();return ok&&impl_->active();}catch(...){impl_->fail(Failure::Resources);return false;}}
bool InitialChannel::releaseStream(std::uint64_t operation){if(!impl_->onThread()||!impl_->managedStreams){return false;}const bool ok=impl_->managedStreams->release(operation);if(ok){impl_->armManagedStreams();impl_->notifyStream();}return ok;}

ConfigResult<std::uint64_t> InitialChannel::lookupDestination(const Hash& hash,unsigned timeout){return impl_->startOwnedLookup(hash,timeout);}
bool InitialChannel::cancelDestinationLookup(std::uint64_t id){if(!impl_->onThread()||!impl_->ownedLookup||impl_->ownedLookup->operation!=id){return false;}impl_->finishOwnedLookup(LookupPhase::Cancelled);return true;}
std::optional<InitialChannel::LookupObservation> InitialChannel::destinationLookupObservation()const{return impl_->ownedLookupResult;}
std::optional<Destination> InitialChannel::ownedLeaseDestination()const{if(!impl_->onThread()||!impl_->session||!impl_->leaseSigner||!impl_->leaseCurrent()||impl_->state!=State::SessionCreated){return {};}return *impl_->leaseSigner->destination();}
std::optional<Destination> InitialChannel::managedStreamPeer(std::uint64_t id)const{if(!impl_->onThread()||!impl_->session||!impl_->leaseCurrent()||!impl_->managedStreams){return {};}const auto* peer=impl_->managedStreams->peer(id);return peer?std::optional(*peer):std::nullopt;}

InitialChannel::State InitialChannel::state()const noexcept{return impl_->state;}
InitialChannel::Failure InitialChannel::failure()const noexcept{return impl_->failure;}
bool InitialChannel::socketActive()const noexcept{return impl_->socket.state()!=QAbstractSocket::UnconnectedState;}
bool InitialChannel::deadlineActive()const noexcept{return impl_->timer.isActive();}
std::optional<SetDate> InitialChannel::routerDate()const{
  return impl_->active()&&impl_->state!=State::Connecting&&impl_->state!=State::WaitingDate&&impl_->socket.state()==QAbstractSocket::ConnectedState?impl_->date:std::nullopt;
}
std::optional<Disconnect> InitialChannel::routerDisconnect()const{return impl_->disconnect;}
} // namespace biglybtc::services::i2p::i2cp

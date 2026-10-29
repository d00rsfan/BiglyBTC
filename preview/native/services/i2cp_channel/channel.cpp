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
  std::uint64_t generation=0;
  Clock::time_point expires{};
  bool notificationQueued=false,drainQueued=false,reading=false,lostPending=false;

  explicit Impl(InitialChannel* owner):q(owner),socket(owner),timer(owner){
    socket.setProxy(QNetworkProxy::NoProxy); // Local I2CP control, never app proxy.
    socket.setReadBufferSize(static_cast<qint64>(MaximumFrameBytes));
    timer.setSingleShot(true);
  }
  ~Impl(){stopIo();}
  bool active()const{return state==State::Connecting||state==State::WaitingDate||state==State::DateReceived;}
  bool datedPhase()const{return state==State::Connecting||state==State::WaitingDate;}
  bool current(std::uint64_t at)const{return generation==at&&active();}
  bool onThread()const{return QThread::currentThread()==q->thread();}
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
      const auto at=self->generation;const auto next=self->state;const auto error=self->failure;
      emit owner->stateChanged(next);
      if(!owner||owner->impl_.get()!=self||self->generation!=at||self->state!=next){return;}
      // No access after the final signal: its listener may delete the controller.
      if(next==State::DateReceived){emit owner->dateReceived();}
      else if(next==State::Failed){emit owner->failed(error);}
      else if(next==State::Closed){emit owner->closed();}
    },Qt::QueuedConnection);
    }catch(...){queued=false;}
    if(!queued){notificationQueued=false;date.reset();failure=Failure::Resources;state=State::Failed;stopIo();}
  }
  void fail(Failure error){
    if(state==State::Failed||state==State::Closed){return;}
    failure=error;date.reset();hello.clear();state=State::Failed;stopIo();notify();
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
    auto decoded=decodeMessage(frame);
    if(!decoded){fail(decoded.error().code==ErrorCode::Unsupported?Failure::UnsupportedMessage:Failure::Framing);return;}
    if(auto* reason=std::get_if<Disconnect>(&*decoded)){
      disconnect=std::move(*reason);fail(Failure::RouterDisconnected);return;
    }
    auto* reply=std::get_if<SetDate>(&*decoded);
    if(!reply||state!=State::WaitingDate){fail(Failure::UnexpectedMessage);return;}
    if(reply->milliseconds==0){fail(Failure::InvalidDate);return;}
    if(expired()){return;} // Decode/allocation may have crossed the absolute budget.
    date=std::move(*reply);state=State::DateReceived;timer.stop();notify();
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
    ++generation;stopIo();decoder.reset();lostPending=false;date.reset();disconnect.reset();failure=Failure::None;state=State::Idle;
    if(next.authenticationRequired){fail(Failure::UnsupportedAuthentication);return false;}
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
    try{socket.connectToHost(next.address,next.port);}catch(...){fail(Failure::Resources);return false;}
    return true;
  }
  void close(){
    if(!onThread()||state==State::Closed){return;}
    // Invalidates all generation-tagged socket/timer callbacks before abort.
    if(generation!=std::numeric_limits<std::uint64_t>::max()){++generation;}
    date.reset();hello.clear();failure=Failure::None;state=State::Closed;stopIo();decoder.reset();notify();
  }
};
InitialChannel::InitialChannel(QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this)){}
InitialChannel::~InitialChannel()=default;
bool InitialChannel::begin(const Options& options){return impl_->begin(options);}
void InitialChannel::close(){impl_->close();}
InitialChannel::State InitialChannel::state()const noexcept{return impl_->state;}
InitialChannel::Failure InitialChannel::failure()const noexcept{return impl_->failure;}
bool InitialChannel::socketActive()const noexcept{return impl_->socket.state()!=QAbstractSocket::UnconnectedState;}
bool InitialChannel::deadlineActive()const noexcept{return impl_->timer.isActive();}
std::optional<SetDate> InitialChannel::routerDate()const{
  return impl_->state==State::DateReceived&&impl_->socket.state()==QAbstractSocket::ConnectedState?impl_->date:std::nullopt;
}
std::optional<Disconnect> InitialChannel::routerDisconnect()const{return impl_->disconnect;}
} // namespace biglybtc::services::i2p::i2cp

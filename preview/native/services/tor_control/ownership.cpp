// SPDX-License-Identifier: GPL-2.0-or-later
// Original setup sequence: TorPlugin.ControlConnection.connect/getInfo,
// ae8efb8d40fb38246ac1790ab5cfb11584689c4a, Copyright2013 Azureus Software, Inc.
#include "ownership.hpp"
#include <QPointer>
#include <QThread>
#include <utility>
namespace biglybtc::services::tor {
namespace {
QByteArray trimOriginal(QByteArray bytes) {
  qsizetype first=0,last=bytes.size();
  while(first<last && static_cast<unsigned char>(bytes[first])<=32){++first;}
  while(last>first && static_cast<unsigned char>(bytes[last-1])<=32){--last;}
  return bytes.sliced(first,last-first);
}
}
OwnershipHandshake::OwnershipHandshake(QObject* parent)
    :QObject(parent),channel_(this),deadline_(this) {
  deadline_.setSingleShot(true);deadline_.setTimerType(Qt::PreciseTimer);
  deadline_.callOnTimeout(this,[this]{if(!terminal() && state_!=State::Ready){finish(State::Failed,QStringLiteral("Control setup timed out"));}});
  connect(&channel_,&ControlChannel::stateChanged,this,[this](ControlChannel::State state){
    if(state_==State::Authenticating && (state==ControlChannel::State::Connecting || state==ControlChannel::State::Challenge
        || state==ControlChannel::State::Authentication || state==ControlChannel::State::Ready)){expired();}
  });
  connect(&channel_,&ControlChannel::authenticated,this,[this]{
    if(state_==State::Authenticating && !expired()){issue(State::TakingOwnership,"TAKEOWNERSHIP");}
  });
  connect(&channel_,&ControlChannel::replyLine,this,&OwnershipHandshake::consume);
  connect(&channel_,&ControlChannel::failed,this,[this]{
    if(!terminal()){finish(State::Failed,QStringLiteral("Control channel failed"));}
  });
  connect(&channel_,&ControlChannel::closed,this,[this]{
    if(!terminal()){finish(State::Failed,QStringLiteral("Control channel ended"));}
  });
}
OwnershipHandshake::~OwnershipHandshake() {
  // Stop the socket while all wrapper members still exist. QObject connections
  // and QPointers remain live until base destruction, so child teardown must
  // not reenter the wrapper or emit public failure/closed notifications.
  deadline_.stop();state_=State::Closed;
  disconnect(&channel_,nullptr,this,nullptr);
  channel_.blockSignals(true);
  channel_.stop();
}
bool OwnershipHandshake::terminal() const { return state_==State::Closed || state_==State::Failed; }
bool OwnershipHandshake::transition(State state) {
  state_=state;QPointer<OwnershipHandshake> guard(this);emit stateChanged(state);
  return guard && state_==state;
}
bool OwnershipHandshake::expired() {
  if(Clock::now()<expires_){return false;}
  finish(State::Failed,QStringLiteral("Control setup timed out"));return true;
}
bool OwnershipHandshake::beginOwnedPeer(quint16 port,const QByteArray& cookie,const QByteArray& clientNonce,int deadlineMilliseconds) {
  if(QThread::currentThread()!=thread() || state_!=State::Idle){return false;}
  if(port==0 || cookie.size()!=32 || clientNonce.size()!=32 || deadlineMilliseconds<1 || deadlineMilliseconds>30000){
    finish(State::Failed,QStringLiteral("Invalid control setup parameters"));return false;
  }
  expires_=Clock::now()+std::chrono::milliseconds(deadlineMilliseconds);
  deadline_.setInterval(std::chrono::milliseconds(deadlineMilliseconds));deadline_.start();
  if(!transition(State::Authenticating)){return true;}
  if(expired()){return true;}
  return channel_.begin(port,cookie,clientNonce,deadlineMilliseconds);
}
void OwnershipHandshake::issue(State state,const QByteArray& command) {
  if(!transition(state)){return;}
  if(expired()){return;}
  QPointer<OwnershipHandshake> guard(this);
  const auto sent=channel_.sendCommand(command);
  if(guard && !sent && !terminal()){finish(State::Failed,QStringLiteral("Control setup command failed"));}
}
void OwnershipHandshake::consume(QByteArray line) {
  if(terminal()){return;}
  // There is no event subscription or transaction multiplexer in this bounded
  // setup wrapper. Source getInfo likewise rejects unexpected non250 replies.
  if(state_==State::Ready){finish(State::Failed,QStringLiteral("Unexpected control setup reply"));return;}
  if(expired()){return;}
  line=trimOriginal(std::move(line));
  if(state_==State::TakingOwnership || state_==State::ResettingOwner){
    if(!line.startsWith("250 OK")){finish(State::Failed,QStringLiteral("Control ownership acknowledgment failed"));return;}
    if(state_==State::TakingOwnership){issue(State::ResettingOwner,"RESETCONF __OwningControllerProcess");}
    else{issue(State::GettingInfo,"GETINFO version");}
    return;
  }
  if(state_!=State::GettingInfo || ++infoLines_>32 || line.size()>4096-infoBytes_){
    finish(State::Failed,QStringLiteral("Control setup reply limit exceeded"));return;
  }
  infoBytes_+=line.size();
  if(!line.startsWith("250")){finish(State::Failed,QStringLiteral("Control version reply failed"));return;}
  if(line=="250 OK"){
    if(expired()){return;}deadline_.stop();versionInfo_=std::move(pendingInfo_);
    if(!transition(State::Ready)){return;}
    emit ready();return;
  }
  if(line.size()<4){finish(State::Failed,QStringLiteral("Control version reply failed"));return;}
  pendingInfo_=line.sliced(4); // Preserve source's last-line projection verbatim.
}
void OwnershipHandshake::finish(State state,const QString& reason) {
  if(terminal()){return;}
  state_=state;deadline_.stop();pendingInfo_.clear();QPointer<OwnershipHandshake> guard(this);channel_.stop();
  if(!guard){return;}emit stateChanged(state);
  if(!guard){return;}
  if(state==State::Failed){emit failed(reason);}else{emit closed();}
}
void OwnershipHandshake::stop() {
  if(QThread::currentThread()==thread()){finish(State::Closed);}
}
}

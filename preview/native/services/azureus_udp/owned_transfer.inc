// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside the existing Qt UDP actor. DATA never uses STORE or reply headers.
struct OwnedPingTransport::TransferState {
  struct Call {
    Ticket ticket;Peer peer;std::int8_t version=55;std::int64_t connection=0,replyConnection=0;
    std::string key,request;Clock::time_point end,writeAt,readAt;std::optional<std::uint64_t> revision;
    azureus_transfer::WriteProgress write;azureus_transfer::Reassembly reply;
    QPointer<QObject> receiver;TransferCallback callback;unsigned noAckRetries=0,emptyReads=0;
    bool firstReply=false;
  };
  struct Incoming {
    TransferReceipt receipt;Peer peer;std::int8_t version=55;std::int64_t connection=0,replyConnection=0;
    std::string key,answer;Clock::time_point end,readAt,writeAt;std::uint64_t revision=0;
    azureus_transfer::Reassembly request;azureus_transfer::WriteProgress write;
    bool published=false,answered=false,done=false,firstPacket=false,responseStarted=false;
    unsigned emptyReads=0;
  };
  struct Completed {Peer peer;std::string key;std::int64_t connection=0;std::int32_t total=0;Clock::time_point end;std::uint64_t revision=0;std::int8_t version=55;bool ackNeeded=true;};
  TransferState()=default;
  explicit TransferState(const std::shared_ptr<TransferState>& source)
      :handler(source->handler),disableAcks(source->disableAcks),mode(source->mode),receiver(source->receiver),incoming(source->incoming),revision(source->revision){}
  std::string handler;bool disableAcks=false,working=false;TransferMode mode=TransferMode::Call;
  QPointer<QObject> receiver;TransferRequestCallback incoming;
  std::function<std::optional<std::uint64_t>()> revision;
  std::optional<Call> call;std::optional<Incoming> request;std::optional<Completed> completed;
};
std::expected<void,Failure> OwnedPingTransport::registerTransferHandler(std::string handler,bool disableAcks,QObject* receiver,TransferRequestCallback incoming,std::function<std::optional<std::uint64_t>()> revision,TransferMode mode){
  if(!owner()){return std::unexpected(Failure::Thread);}if(state_!=State::Bound||!normalRouter_||!normalDatabase_||!normalPreflight_||handlers_.size()>=32){return std::unexpected(Failure::State);}
  if(handler.size()!=20||!receiver||receiver->thread()!=thread()||!incoming||!revision||!statisticsGeneration_){return std::unexpected(Failure::Receiver);}
  if(std::ranges::any_of(handlers_,[&](const auto& item){return item->handler==handler;})){return std::unexpected(Failure::State);}
  auto value=std::make_shared<TransferState>();value->handler=std::move(handler);value->mode=mode;value->disableAcks=disableAcks;value->receiver=receiver;value->incoming=std::move(incoming);value->revision=std::move(revision);handlers_.push_back(std::move(value));return {};
}
bool OwnedPingTransport::transferOwned(const std::shared_ptr<TransferState>& state)const noexcept{
  return std::ranges::find(transfers_,state)!=transfers_.end();
}
void OwnedPingTransport::reapTransfers(){
  const auto now=Clock::now();std::erase_if(transfers_,[&](const auto& state){
    if(state->working){return false;}
    if(state->completed&&now>=state->completed->end){state->completed.reset();}
    if(state->request&&now>=state->request->end){state->request.reset();}
    return !state->call&&!state->request&&!state->completed;
  });
}
bool OwnedPingTransport::unregisterTransferHandler(std::string_view handler){
  if(!owner()){return false;}const auto removed=std::erase_if(handlers_,[&](const auto& state){return state->handler==handler;});
  if(!removed){return false;}const auto states=transfers_;for(const auto& state:states){if(state->handler==handler&&state->call){finishTransfer(state,std::unexpected(Failure::Cancelled));}}
  std::erase_if(transfers_,[&](const auto& state){return state->handler==handler;});armDeadline();return true;
}
bool OwnedPingTransport::transferCurrent(const std::shared_ptr<TransferState>& state,std::uint64_t revision)noexcept{
  try{return transferOwned(state)&&normalCurrent()&&state->receiver&&state->revision&&state->revision()==std::optional{revision};}catch(...){return false;}
}
std::expected<void,Failure> OwnedPingTransport::sendTransfer(const std::shared_ptr<TransferState>& state,const Peer& peer,std::int8_t version,std::int64_t connection,const azureus_transfer::Data& data,Clock::time_point end){
  if(!transferOwned(state)||!allowed(peer)||!normalCurrent()){return std::unexpected(Failure::Authority);}const auto revision=state->revision();if(!revision){return std::unexpected(Failure::Authority);}
  packet::udp::EncodeFields h;h.action=1035;h.network=config_.network;h.localVersion=config_.version;h.remoteVersion=version;h.instance=config_.instance;h.connection=connection;try{h.transaction=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(word()));}catch(...){return std::unexpected(Failure::Random);}h.originator=originator(endpoint());h.now=QDateTime::currentMSecsSinceEpoch();h.flags=config_.flags;h.flags2=config_.flags2;
  auto wire=azureus_transfer::encode(h,data);if(!wire){return std::unexpected(Failure::Encode);}
  // Final physical effect: no RNG/encode allocation may stretch the actual
  // operation budget or outlive a source/stop withdrawal on either scope.
  if(Clock::now()>=end){return std::unexpected(Failure::Timeout);}if(!transferCurrent(state,*revision)){return std::unexpected(Failure::Authority);}
  auto sent=writePacket(*wire,peer,end);if(sent){observeSocketPayloadBytes(statistics_.sentPayloadBytes,*sent);}if(!sent){return std::unexpected(sent.error());}if(*sent!=static_cast<qint64>(wire->size())){return std::unexpected(Failure::Send);}++statistics_.sent;return {};
}
std::expected<Ticket,Failure> OwnedPingTransport::callTransfer(Peer peer,std::int8_t version,std::string handler,std::string request,QObject* receiver,TransferCallback callback,Clock::time_point end){return startTransfer(std::move(peer),version,std::move(handler),std::move(request),receiver,std::move(callback),end,TransferMode::Call);}
std::expected<Ticket,Failure> OwnedPingTransport::readTransfer(Peer peer,std::int8_t version,std::string handler,std::string request,QObject* receiver,TransferCallback callback,Clock::time_point end){return startTransfer(std::move(peer),version,std::move(handler),std::move(request),receiver,std::move(callback),end,TransferMode::Read);}
std::expected<Ticket,Failure> OwnedPingTransport::readTransfer(Peer peer,std::int8_t version,std::string handler,std::string request,QObject* receiver,TransferCallback callback,Clock::time_point end,std::function<std::optional<std::uint64_t>()> revision){if(!revision){return std::unexpected(Failure::Authority);}return startTransfer(std::move(peer),version,std::move(handler),std::move(request),receiver,std::move(callback),end,TransferMode::Read,std::move(revision));}
std::expected<Ticket,Failure> OwnedPingTransport::startTransfer(Peer peer,std::int8_t version,std::string handler,std::string request,QObject* receiver,TransferCallback callback,Clock::time_point end,TransferMode mode,std::function<std::optional<std::uint64_t>()> revision){
  const auto now=Clock::now();if(!owner()){return std::unexpected(Failure::Thread);}if(state_!=State::Bound){return std::unexpected(Failure::State);}
  peer=normalize(std::move(peer));if(!allowed(peer)||!peer.address.scopeId().isEmpty()){return std::unexpected(Failure::Peer);}if(version<51||version>55){return std::unexpected(Failure::Version);}
  const auto registration=std::ranges::find_if(handlers_,[&](const auto& value){return value->handler==handler;});
  if((!revision&&(registration==handlers_.end()||(*registration)->mode!=mode))||handler.size()!=20||request.size()>azureus_transfer::MaximumValue||(mode==TransferMode::Read&&(request.empty()||request.size()>255))){return std::unexpected(Failure::Encode);}if(!receiver||receiver->thread()!=thread()||!callback){return std::unexpected(Failure::Receiver);}
  if(end<=now||end-now>std::chrono::seconds(30)){return std::unexpected(Failure::Deadline);}reapTransfers();
  const auto active=std::ranges::count_if(transfers_,[](const auto& value){return value->call.has_value();});
  const auto inRoom=std::ranges::count_if(transfers_,[&](const auto& value){return value->handler==handler&&value->call.has_value();});
  if(transfers_.size()>=256||active>=64||inRoom>=6||nextTicket_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Failure::Capacity);}
  std::shared_ptr<TransferState> state;if(revision){state=std::make_shared<TransferState>();state->handler=std::move(handler);state->mode=mode;state->receiver=receiver;state->revision=std::move(revision);}else{state=std::make_shared<TransferState>(*registration);}
  TransferState::Call call;call.ticket={++nextTicket_};call.peer=std::move(peer);call.version=version;call.key.resize(20);try{call.connection=std::bit_cast<std::int64_t>(word()|(std::uint64_t{1}<<63));
  for(std::size_t i=0;i<20;i+=4){const auto random=word();for(unsigned j=0;j<4;++j){call.key[i+j]=static_cast<char>(random>>(8*j));}}}catch(...){return std::unexpected(Failure::Random);}
  if(mode==TransferMode::Read){call.key=request;call.replyConnection=call.connection;call.write.complete=true;}call.request=std::move(request);call.receiver=receiver;call.callback=std::move(callback);call.end=end;call.writeAt=now;call.readAt=mode==TransferMode::Read?now:end;const auto ticket=call.ticket;state->call.emplace(std::move(call));transfers_.push_back(state);pumpTransfer();return ticket;
}
void OwnedPingTransport::finishTransfer(const std::shared_ptr<TransferState>& state,TransferOutcome outcome){
  if(!state->call){return;}auto call=std::move(*state->call);state->call.reset();
  if(state->mode==TransferMode::Call&&outcome&&call.replyConnection&&call.revision){state->completed=TransferState::Completed{call.peer,call.key,call.replyConnection,static_cast<std::int32_t>(outcome->size()),call.end,*call.revision,call.version,!(state->disableAcks&&outcome->size()<=azureus_transfer::MaximumData)};}
  auto callback=std::make_shared<TransferCallback>(std::move(call.callback));const auto receiver=call.receiver;
  // This queued frame is after the source-worker commit/ACK. QObject context
  // removes it on actor destruction; the receiver is never borrowed cross-thread.
  const QPointer<OwnedPingTransport> actor(this);QMetaObject::invokeMethod(this,[actor,receiver,callback,outcome=std::move(outcome)]()mutable{if(receiver&&*callback){try{(*callback)(std::move(outcome));}catch(...){if(actor){++actor->statistics_.callbackErrors;}}}},Qt::QueuedConnection);armDeadline();
}
bool OwnedPingTransport::cancelTransfer(Ticket ticket){
  if(!owner()){return false;}for(const auto& state:transfers_){if(state->call&&state->call->ticket==ticket){finishTransfer(state,std::unexpected(Failure::Cancelled));return true;}}return false;
}
std::expected<void,Failure> OwnedPingTransport::answerTransfer(const TransferReceipt& receipt,std::string reply){
  if(!owner()){return std::unexpected(Failure::Thread);}if(state_!=State::Bound){return std::unexpected(Failure::State);}
  const auto match=std::ranges::find_if(transfers_,[&](const auto& value){return value->request&&value->request->receipt==receipt;});
  if(match==transfers_.end()){return std::unexpected(Failure::State);}const auto state=*match;auto& request=*state->request;
  if(receipt!=request.receipt||!request.published||request.answered||request.done){return std::unexpected(Failure::State);}if(!transferCurrent(state,request.revision)){return std::unexpected(Failure::Authority);}if(reply.size()>azureus_transfer::MaximumValue){return std::unexpected(Failure::Capacity);}if(Clock::now()>=request.end){return std::unexpected(Failure::Timeout);}
  if(state->mode==TransferMode::Read){
    auto pieces=azureus_transfer::fragments(azureus_transfer::Type::ReadReply,state->handler,request.key,reply);if(!pieces){return std::unexpected(Failure::Encode);}
    request.answer=std::move(reply);request.replyConnection=request.connection;request.answered=true;
    for(const auto& part:*pieces){auto sent=sendTransfer(state,request.peer,request.version,request.connection,part,request.end);if(!sent){state->request.reset();return sent;}}request.done=true;armDeadline();return {};
  }
  try{request.replyConnection=std::bit_cast<std::int64_t>(word()|(std::uint64_t{1}<<63));}catch(...){return std::unexpected(Failure::Random);}if(request.replyConnection==request.connection){return std::unexpected(Failure::Random);}
  request.answer=std::move(reply);request.answered=true;request.writeAt=Clock::now();pumpTransfer();return {};
}
OwnedPingTransport::Clock::time_point OwnedPingTransport::transferWakeup()const noexcept{
  auto end=Clock::time_point::max();for(const auto& item:transfers_){const auto& state=*item;
  if(state.call){const auto& call=*state.call;end=std::min(end,call.end);if(!state.working){if(!call.write.complete){end=std::min(end,call.writeAt);}if(state.mode==TransferMode::Read||call.reply.started()){end=std::min(end,call.readAt);}}}
  if(state.request&&!state.request->done){const auto& request=*state.request;end=std::min(end,request.end);if(!state.working){if(!request.published){end=std::min(end,request.readAt);}if(request.answered&&!request.write.complete){end=std::min(end,request.writeAt);}}}}return end;
}
void OwnedPingTransport::pumpTransfer(){
  if(state_!=State::Bound){return;}const auto states=transfers_;const auto now=Clock::now();
  for(const auto& state:states){
    if(state->call&&now>=state->call->end){finishTransfer(state,std::unexpected(Failure::Timeout));}
    if(state->request&&now>=state->request->end){state->request.reset();}
    if(state->working||(!state->call&&(!state->request||state->request->done))){continue;}state->working=true;
    const auto resume=[this,state](bool accepted){state->working=false;if(!transferOwned(state)||state_!=State::Bound){return;}if(!accepted||!normalCurrent()){if(state->call){finishTransfer(state,std::unexpected(Failure::Authority));}state->request.reset();armDeadline();return;}try{transferTurn(state);}catch(...){if(state->call){finishTransfer(state,std::unexpected(Failure::Capacity));}state->request.reset();}armDeadline();};
    if(!normalPreflight_||!normalPreflight_(resume)){state->working=false;if(state->call){finishTransfer(state,std::unexpected(Failure::Capacity));}state->request.reset();}
  }reapTransfers();armDeadline();
}

void OwnedPingTransport::transferTurn(const std::shared_ptr<TransferState>& state){
  namespace data=azureus_transfer;const auto now=Clock::now();const auto revision=state->revision();if(!revision){if(state->call){finishTransfer(state,std::unexpected(Failure::Authority));}state->request.reset();return;}
  const auto sendPieces=[&](const Peer& peer,std::int8_t version,std::int64_t connection,std::string_view key,std::string_view bytes,bool first,Clock::time_point end){auto pieces=data::fragments(data::Type::WriteRequest,state->handler,key,bytes,0,0,first);if(!pieces){return std::expected<void,Failure>{std::unexpected(Failure::Encode)};}for(const auto& part:*pieces){auto sent=sendTransfer(state,peer,version,connection,part,end);if(!sent){return sent;}}return std::expected<void,Failure>{};};
  const auto read=[&](const Peer& peer,std::int8_t version,std::int64_t connection,std::string_view key,const data::Reassembly& assembly,Clock::time_point end){for(const auto& gap:assembly.missing()){data::Data packet{data::Type::ReadRequest,state->handler,std::string(key),gap.start,gap.length,0,{}};auto sent=sendTransfer(state,peer,version,connection,packet,end);if(!sent){return sent;}}return std::expected<void,Failure>{};};
  if(state->call){auto& call=*state->call;if(!call.revision){call.revision=*revision;}if(!transferCurrent(state,*call.revision)){finishTransfer(state,std::unexpected(Failure::Authority));}
    else if(now>=call.end){finishTransfer(state,std::unexpected(Failure::Timeout));}
    else{const bool noAcks=state->disableAcks&&call.request.size()<=data::MaximumData;
      if(!call.write.complete&&now>=call.writeAt){if(noAcks&&call.noAckRetries>=2){call.writeAt=call.end;}else{auto sent=sendPieces(call.peer,call.version,call.connection,call.key,call.request,call.write.receivedAck,call.end);if(!sent){finishTransfer(state,std::unexpected(sent.error()));}else{++call.noAckRetries;call.writeAt=now+std::chrono::milliseconds(data::WriteResendMilliseconds);}}}
      if(state->call&&(state->mode==TransferMode::Read||call.reply.started())&&now>=call.readAt){auto sent=read(call.peer,call.version,call.replyConnection,call.key,call.reply,call.end);if(!sent){finishTransfer(state,std::unexpected(sent.error()));}else{call.readAt=now+std::chrono::milliseconds(data::ReadRetryMilliseconds);}}
    }
  }
  if(state->request){auto& request=*state->request;if(!transferCurrent(state,request.revision)||now>=request.end){state->request.reset();return;}if(request.done){return;}
    if(!request.published&&now>=request.readAt){auto sent=read(request.peer,request.version,request.connection,request.key,request.request,request.end);if(!sent){state->request.reset();return;}request.readAt=now+std::chrono::milliseconds(data::ReadRetryMilliseconds);}
    if(request.answered&&!request.write.complete&&now>=request.writeAt){
      if(!request.responseStarted&&state->disableAcks&&request.request.value()->size()<=data::MaximumData&&request.answer.size()>data::MaximumData){auto ack=sendTransfer(state,request.peer,request.version,request.connection,data::Data{data::Type::WriteReply,state->handler,request.key,0,static_cast<std::int32_t>(request.request.value()->size()),0,{}},request.end);if(!ack){state->request.reset();return;}}
      request.responseStarted=true;auto sent=sendPieces(request.peer,request.version,request.replyConnection,request.key,request.answer,request.write.receivedAck,request.end);if(!sent){state->request.reset();return;}request.writeAt=now+std::chrono::milliseconds(data::WriteResendMilliseconds);if(state->disableAcks&&request.request.value()->size()<=data::MaximumData&&request.answer.size()<=data::MaximumData){request.done=true;}}
  }
}
void OwnedPingTransport::transferPacket(const Peer& peer,azureus_transfer::Packet packet){
  namespace data=azureus_transfer;if(packet.data.key.empty()||packet.data.key.size()>255){++statistics_.foreign;return;}
  const auto arrival=Clock::now();const auto header=packet.header.fields();auto body=std::move(packet.data);reapTransfers();
  std::shared_ptr<TransferState> state;
  for(const auto& candidate:transfers_){if(candidate->handler!=body.handler){continue;}
    if((candidate->call&&candidate->call->peer==peer&&candidate->call->key==body.key&&(candidate->mode==TransferMode::Call||candidate->call->connection==header.connection))
        ||(candidate->request&&candidate->request->peer==peer&&candidate->request->key==body.key&&(candidate->mode==TransferMode::Call||candidate->request->connection==header.connection))
        ||(candidate->completed&&candidate->completed->peer==peer&&candidate->completed->key==body.key&&candidate->completed->connection==header.connection)){state=candidate;break;}}
  if(!state){
    const auto registration=std::ranges::find_if(handlers_,[&](const auto& value){return value->handler==body.handler&&value->receiver;});
    if(registration==handlers_.end()||((*registration)->mode==TransferMode::Read?(body.type!=data::Type::ReadRequest||body.start!=0||body.length!=0):(body.type!=data::Type::WriteRequest||body.key.size()!=20))||transfers_.size()>=256
        ||std::ranges::count_if(transfers_,[](const auto& v){return v->request&&!v->request->done;})>=64
        ||std::ranges::count_if(transfers_,[&](const auto& v){return v->handler==body.handler&&v->request&&!v->request->done;})>=8){++statistics_.foreign;return;}
    state=std::make_shared<TransferState>(*registration);
    TransferState::Incoming pending;pending.peer=peer;pending.version=header.originatorVersion;pending.connection=header.connection;pending.key=body.key;pending.end=arrival+std::chrono::seconds(30);pending.readAt=arrival+std::chrono::milliseconds(data::ReadRetryMilliseconds);state->request.emplace(std::move(pending));state->working=true;transfers_.push_back(state);
  }
  if(!state->receiver){++statistics_.foreign;return;}
  auto resume=[this,state,peer,header,body=std::move(body),arrival](bool accepted)mutable{
    if(!transferOwned(state)||state_!=State::Bound){return;}state->working=false;try{
      const auto revision=accepted&&normalCurrent()?state->revision():std::nullopt;
      if(!revision){if(state->call){finishTransfer(state,std::unexpected(Failure::Authority));}state->request.reset();return;}
      if(Clock::now()>=arrival+std::chrono::seconds(30)){return;}
      const auto send=[&](data::Data value,std::int64_t connection,Clock::time_point end){return sendTransfer(state,peer,header.originatorVersion,connection,value,end);};
      const auto acknowledge=[&](std::int32_t start,std::int32_t length,Clock::time_point end){return send(data::Data{data::Type::WriteReply,state->handler,body.key,start,length,0,{}},header.connection,end);};
      if(state->completed&&state->completed->peer==peer&&state->completed->key==body.key&&state->completed->connection==header.connection){const auto& old=*state->completed;if(old.ackNeeded&&old.revision==*revision&&Clock::now()<old.end&&body.type==data::Type::WriteRequest&&body.total==old.total){static_cast<void>(acknowledge(0,old.total,old.end));}return;}
      TransferState::Call* call=state->call&&state->call->peer==peer&&state->call->key==body.key?&*state->call:nullptr;
      TransferState::Incoming* request=state->request&&state->request->peer==peer&&state->request->key==body.key?&*state->request:nullptr;
      if(call&&(!call->revision||*call->revision!=*revision||Clock::now()>=call->end)){finishTransfer(state,std::unexpected(Failure::Authority));return;}
      if(request&&request->revision==0){
        if(nextTransferSerial_==std::numeric_limits<std::uint64_t>::max()){state->request.reset();return;}
        request->revision=*revision;request->receipt.generation_=statisticsGeneration_;request->receipt.serial_=++nextTransferSerial_;request->receipt.nonce_=word();
      }
      if(request&&(request->revision!=*revision||Clock::now()>=request->end)){state->request.reset();return;}
      if(state->mode==TransferMode::Read&&body.type==data::Type::ReadReply){
        if(!call||call->connection!=header.connection){return;}auto complete=call->reply.add(body);if(!complete){finishTransfer(state,std::unexpected(Failure::Encode));return;}call->readAt=Clock::now()+std::chrono::milliseconds(data::ReadRetryMilliseconds);if(*complete){finishTransfer(state,std::move(*call->reply.value()));}armDeadline();return;
      }
      if(state->mode==TransferMode::Read&&body.type==data::Type::ReadRequest&&request&&!request->published){
        if(request->connection!=header.connection||body.start!=0||body.length!=0){return;}request->published=true;
        auto value=std::make_shared<TransferRequest>();value->receipt=request->receipt;value->peer=peer;value->network=config_.network;value->handler=state->handler;value->request=body.key;value->sourceRevision=*revision;value->deadline=request->end;
        const QPointer<OwnedPingTransport> actor(this);const auto receiver=state->receiver;const auto callback=state->incoming;QMetaObject::invokeMethod(this,[actor,state,receiver,callback,value]{if(actor&&actor->transferOwned(state)&&state->request&&state->request->receipt==value->receipt&&Clock::now()<value->deadline&&receiver&&callback){try{callback(value);}catch(...){if(actor){++actor->statistics_.callbackErrors;}}}},Qt::QueuedConnection);armDeadline();return;
      }
      if(state->mode==TransferMode::Read&&body.type!=data::Type::ReadRequest){return;}
      if(body.type==data::Type::ReadRequest){std::string_view bytes;Clock::time_point end;if(call&&call->connection==header.connection){bytes=call->request;end=call->end;}else if(request&&request->answered&&request->replyConnection==header.connection){bytes=request->answer;end=request->end;}else{return;}
        auto pieces=data::fragments(data::Type::ReadReply,state->handler,body.key,bytes,body.start,body.length);if(!pieces){return;}for(auto& part:*pieces){if(!send(std::move(part),header.connection,end)){return;}}return;
      }
      if(body.type==data::Type::WriteReply){if(call&&call->connection==header.connection){static_cast<void>(call->write.acknowledge(body,call->request.size()));if(call->write.complete){auto value=call->reply.value();if(value){finishTransfer(state,std::move(*value));}}}else if(request&&request->answered&&request->replyConnection==header.connection){static_cast<void>(request->write.acknowledge(body,request->answer.size()));request->done=request->write.complete;}armDeadline();return;}
      if(call&&header.connection!=call->connection&&(body.type==data::Type::WriteRequest||body.type==data::Type::ReadReply)){
        if(call->replyConnection&&call->replyConnection!=header.connection){return;}if(!call->replyConnection){if(body.type!=data::Type::WriteRequest){return;}call->replyConnection=header.connection;}
        auto complete=call->reply.add(body);if(!complete){finishTransfer(state,std::unexpected(Failure::Encode));return;}const bool noAcks=state->disableAcks&&body.total==body.length;
        if(!noAcks&&(!call->firstReply||*complete)){const auto sent=acknowledge(*complete?0:body.start,*complete?body.total:body.length,call->end);if(!sent){finishTransfer(state,std::unexpected(sent.error()));return;}}call->firstReply=true;call->readAt=Clock::now()+std::chrono::milliseconds(data::ReadRetryMilliseconds);
        if(*complete&&(call->write.complete||(state->disableAcks&&call->request.size()<=data::MaximumData))){auto value=call->reply.value();finishTransfer(state,std::move(*value));}armDeadline();return;
      }
      if(call&&header.connection==call->connection){return;}
      if(body.type!=data::Type::WriteRequest&&body.type!=data::Type::ReadReply){return;}
      if(!request){if(body.type!=data::Type::WriteRequest||(state->request&&!state->request->done)||nextTransferSerial_==std::numeric_limits<std::uint64_t>::max()){return;}TransferState::Incoming next;next.peer=peer;next.version=header.originatorVersion;next.connection=header.connection;next.key=body.key;next.end=arrival+std::chrono::seconds(30);next.revision=*revision;next.readAt=arrival+std::chrono::milliseconds(data::ReadRetryMilliseconds);next.receipt.generation_=statisticsGeneration_;next.receipt.serial_=++nextTransferSerial_;next.receipt.nonce_=word();state->request.emplace(std::move(next));request=&*state->request;}
      if(request->connection!=header.connection){return;}if(request->published){
        const auto original=request->request.value();
        if(!original||body.total!=static_cast<std::int32_t>(original->size())||body.start<0||body.length<0
            ||static_cast<std::size_t>(body.start)>original->size()||static_cast<std::size_t>(body.length)>original->size()-static_cast<std::size_t>(body.start)
            ||body.payload!=original->substr(static_cast<std::size_t>(body.start),static_cast<std::size_t>(body.length))){return;}
        if(request->answered&&request->done&&state->disableAcks&&body.total==body.length&&request->answer.size()<=data::MaximumData){
          auto retry=data::fragments(data::Type::WriteRequest,state->handler,body.key,request->answer);
          if(retry){for(const auto& part:*retry){if(!send(part,request->replyConnection,request->end)){return;}}}
        }else if(!(state->disableAcks&&body.total==body.length)){static_cast<void>(acknowledge(0,static_cast<std::int32_t>(original->size()),request->end));}return;
      }
      auto complete=request->request.add(body);if(!complete){state->request.reset();return;}const bool noAcks=state->disableAcks&&body.total==body.length;
      if(!noAcks&&(!request->firstPacket||*complete)){if(!acknowledge(*complete?0:body.start,*complete?body.total:body.length,request->end)){state->request.reset();return;}}request->firstPacket=true;request->readAt=Clock::now()+std::chrono::milliseconds(data::ReadRetryMilliseconds);
      if(*complete){request->published=true;auto value=std::make_shared<TransferRequest>();value->receipt=request->receipt;value->peer=peer;value->network=config_.network;value->handler=state->handler;value->request=std::move(*request->request.value());value->sourceRevision=*revision;value->deadline=request->end;const auto receiver=state->receiver;const auto callback=state->incoming;
        const QPointer<OwnedPingTransport> actor(this);QMetaObject::invokeMethod(this,[actor,state,receiver,callback,value=std::move(value)]{if(actor&&actor->transferOwned(state)&&state->request&&state->request->receipt==value->receipt&&Clock::now()<value->deadline&&receiver&&callback){try{callback(value);}catch(...){if(actor){++actor->statistics_.callbackErrors;}}}},Qt::QueuedConnection);}
      armDeadline();
    }catch(...){if(state->call){finishTransfer(state,std::unexpected(Failure::Capacity));}state->request.reset();}
  };
  if(!normalPreflight_||!normalPreflight_(std::move(resume))){state->working=false;if(state->request&&!state->request->revision){state->request.reset();}++statistics_.serviceCapacityDrops;}
}
void OwnedPingTransport::closeTransfer(){
  const auto states=transfers_;for(const auto& state:states){if(state->call){finishTransfer(state,std::unexpected(Failure::Closed));}state->request.reset();}transfers_.clear();handlers_.clear();
}

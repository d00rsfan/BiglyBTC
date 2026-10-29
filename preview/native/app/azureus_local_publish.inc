// Included inside OwnedAzureusService::Impl::Owner. One bounded explicit local
// publication; every continuation remains on the actual Qt actor thread.
    struct Publishing {
      Command command;
      AzureusPublishSnapshot snapshot;
      std::chrono::steady_clock::time_point deadline;
      azureus::Ticket ticket;
      std::size_t readbackSlices=0;
      std::deque<std::pair<azureus::FindValueOutcome,std::shared_ptr<void>>> pendingReadback;
      bool readbackIo=false;
      bool finished=false,cancelled=false;
    };
    std::shared_ptr<Publishing> publishing_;
    std::shared_ptr<services::azureus_db_runtime::Owner> localDatabase_;
    struct PublicationClock final:services::azureus_db_value::WallClock {
      std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}
    };
    bool initializeLocalDatabase(){
      auto made=services::azureus_db_runtime::Owner::create();if(!made){return false;}
      const auto peer=actor_.endpoint();const auto id=routerIdentity(peer,options_.version);if(!id){return false;}
      nodes::Contact contact;contact.protocol=options_.version;contact.id=*id;contact.address.port=peer.port;
      if(peer.address.protocol()==QAbstractSocket::IPv4Protocol){contact.address.length=4;const auto ip=peer.address.toIPv4Address();for(unsigned i=0;i<4;++i){contact.address.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}
      else{contact.address.length=16;const auto ip=peer.address.toIPv6Address();std::copy_n(ip.c,16,contact.address.bytes.begin());}
      if(!(*made)->bindLocal(contact)){return false;}localDatabase_=std::move(*made);return true;
    }
    bool livePublish(const std::shared_ptr<Publishing>& value)const noexcept{return publishing_==value&&!value->finished;}
    void publicationMemoryFailure()noexcept{
      // An observer/callback allocation error cannot turn a partial round into
      // success or leave a live operation without a bounded retirement path.
      std::lock_guard lock(state_->mutex);state_->stop=true;state_->value.current=false;state_->value.state=AzureusServiceState::Closing;state_->value.error=AzureusServiceError::Memory;state_->advance();loop_.quit();
    }
    void capturePublish(const std::shared_ptr<Publishing>& value){
      auto snapshot=std::make_shared<const AzureusPublishSnapshot>(value->snapshot);
      std::lock_guard lock(state_->mutex);state_->value.publishSnapshot=std::move(snapshot);state_->advance();
    }
    void finishPublish(const std::shared_ptr<Publishing>& value,AzureusPublishState state,
        std::optional<AzureusServiceError> error={},std::optional<azureus::Failure> transport={}){
      if(!livePublish(value)){return;}value->finished=true;if(state==AzureusPublishState::Failed||state==AzureusPublishState::Cancelled){static_cast<void>(actor_.cancel(value->ticket));}
      value->snapshot.state=state;value->snapshot.error=error;value->snapshot.transportFailure=transport;
      // Allocate the owning result BEFORE the final deadline fence. No terminal
      // success is ever published through the generic reply() first.
      auto snapshot=std::make_shared<AzureusPublishSnapshot>(value->snapshot);
      publish();
      std::lock_guard lock(state_->mutex);
      // Profile validation and readback copying can cross the absolute budget.
      // Check at the actual locked commit; mutating this not-yet-published copy
      // requires no allocation and never extends the deadline.
      if(state==AzureusPublishState::Complete&&state_->stop){
        snapshot->state=AzureusPublishState::Closed;snapshot->error=AzureusServiceError::Closed;snapshot->transportFailure=azureus::Failure::Closed;
        value->snapshot.state=snapshot->state;value->snapshot.error=snapshot->error;value->snapshot.transportFailure=snapshot->transportFailure;
      }else if(state==AzureusPublishState::Complete&&std::chrono::steady_clock::now()>=value->deadline){
        snapshot->state=AzureusPublishState::Failed;snapshot->error=AzureusServiceError::Transport;snapshot->transportFailure=azureus::Failure::Timeout;
        value->snapshot.state=snapshot->state;value->snapshot.error=snapshot->error;value->snapshot.transportFailure=snapshot->transportFailure;
      }
      releaseLocked();state_->value.lastResult=AzureusServiceResult{value->command.id,value->command.target,value->command.kind,true,snapshot->error,snapshot->transportFailure};state_->value.publishSnapshot=std::move(snapshot);state_->advance();
    }
    bool validatePublish(const std::shared_ptr<Publishing>& value){
      if(!livePublish(value)){return false;}
      bool closing=false;{std::lock_guard lock(state_->mutex);closing=state_->stop;}
      if(closing){finishPublish(value,AzureusPublishState::Closed,AzureusServiceError::Closed,azureus::Failure::Closed);return false;}
      if(value->cancelled){finishPublish(value,AzureusPublishState::Cancelled,AzureusServiceError::Transport,azureus::Failure::Cancelled);return false;}
      if(std::chrono::steady_clock::now()>=value->deadline){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Transport,azureus::Failure::Timeout);return false;}
      if(!isOwned(value->command.peer)){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Invalid,azureus::Failure::Peer);return false;}
      return true;
    }
    std::chrono::nanoseconds publishRemaining(const Publishing& value)const{return std::chrono::duration_cast<std::chrono::nanoseconds>(value.deadline-std::chrono::steady_clock::now());}
    void withPublish(const std::shared_ptr<Publishing>& value,bool reserve,
        std::move_only_function<void(const ProfileIo::Data&)> step){
      if(!validatePublish(value)){return;}
      auto current=[this,value]{bool stop=false;{std::lock_guard lock(state_->mutex);stop=state_->stop;}return !stop&&livePublish(value)&&!value->cancelled;};
      auto committed=[this,value,step=std::move(step)](const ProfileIo::Data& data)mutable{if(validatePublish(value)){step(data);}};
      auto failed=[this,value](storage::Error error){if(!validatePublish(value)){return;}value->snapshot.profileFailure=std::move(error);finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Profile);};
      if(!deferProfile(value->command,reserve,value->deadline,std::move(current),std::move(committed),std::move(failed),false,
          [this,value]{finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Transport,azureus::Failure::Timeout);})){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Capacity);}
    }
    void queuePublishReadback(const std::shared_ptr<Publishing>& value,azureus::FindValueOutcome outcome){
      if(!livePublish(value)){return;}
      if(value->pendingReadback.size()+value->readbackSlices>=64){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Capacity);return;}
      auto held=holdProfileFrame();if(!held){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Capacity);return;}
      value->pendingReadback.emplace_back(std::move(outcome),std::move(held));pumpPublishReadback(value);
    }
    void pumpPublishReadback(const std::shared_ptr<Publishing>& value){
      if(!livePublish(value)||value->readbackIo||value->pendingReadback.empty()){return;}
      value->readbackIo=true;auto batch=std::move(value->pendingReadback);value->pendingReadback.clear();
      withPublish(value,false,[this,value,batch=std::move(batch)](const auto&)mutable{
        for(auto& outcome:batch){if(!livePublish(value)){break;}processPublishReadback(value,std::move(outcome.first));}
        value->readbackIo=false;pumpPublishReadback(value);
      });
    }
    void processPublishReadback(const std::shared_ptr<Publishing>& value,azureus::FindValueOutcome outcome){
        if(!validatePublish(value)){return;}
        if(!outcome){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Transport,outcome.error());return;}
        const auto* reply=std::get_if<azureus::lookup::ReplyBody>(&outcome->body());
        const auto* found=reply?std::get_if<azureus::lookup::FoundValues>(&reply->result):nullptr;
        if(!reply||!found){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Readback);return;}
        if(++value->readbackSlices>64||found->values.size()>64-value->snapshot.readback.size()){
          finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Capacity);return;
        }
        value->snapshot.readback.insert(value->snapshot.readback.end(),found->values.begin(),found->values.end());
        // A matching partial value is not proof of a complete readback stream.
        // Retain only bounded owned values; timeout/cancel/profile checks still
        // apply to every part and to the final locked publication commit.
        if(reply->continuation){
          if(value->readbackSlices==64||value->snapshot.readback.size()==64){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Capacity);}
          return;
        }
        const auto& local=*value->snapshot.localValue;const auto* origin=local.originator().get();const auto bytes=local.payload().view();
        const bool matching=origin&&bytes&&std::ranges::any_of(value->snapshot.readback,[&](const auto& item){return item.version==local.version()&&item.bytes==*bytes&&item.originator.id==origin->id&&item.originator.address==origin->address&&item.flags==local.flags()&&item.lifeHours==local.lifeTimeHours()&&item.replication==local.replicationControl();});
        if(!matching){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Readback);return;}
        finishPublish(value,AzureusPublishState::Complete);

    }
    void publishReadback(const std::shared_ptr<Publishing>& value){
      if(!validatePublish(value)){return;}value->snapshot.state=AzureusPublishState::Readback;capturePublish(value);
      auto sent=actor_.findValue(value->command.peer,options_.version,value->snapshot.key,0,64,this,
          [this,value](azureus::FindValueOutcome outcome){try{queuePublishReadback(value,std::move(outcome));}catch(...){publicationMemoryFailure();}},publishRemaining(*value));
      if(!sent){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Transport,sent.error());return;}value->ticket=*sent;
    }
    void publishStore(const std::shared_ptr<Publishing>& value,std::int32_t token){
      if(!validatePublish(value)){return;}value->snapshot.state=AzureusPublishState::Store;capturePublish(value);
      const auto& local=*value->snapshot.localValue;const auto* origin=local.originator().get();const auto payload=local.payload().view();
      if(!origin||!payload){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Invalid);return;}
      azureus::StoreRequest request;request.randomId=token;request.keys={std::string(reinterpret_cast<const char*>(value->snapshot.key.data()),value->snapshot.key.size())};
      request.valueSets={{{local.version(),local.creationTime(),std::string(*payload),*origin,local.flags(),local.lifeTimeHours(),local.replicationControl()}}};
      auto sent=actor_.store(value->command.peer,options_.version,std::move(request),this,[this,value](azureus::StoreOutcome outcome){try{withPublish(value,false,[this,value,outcome=std::move(outcome)](const auto&)mutable{
        if(!validatePublish(value)){return;}
        if(!outcome){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Transport,outcome.error());return;}
        const auto* decoded=std::get_if<azureus::storage::Decoded>(&*outcome);const auto* reply=decoded?std::get_if<azureus::storage::ReplyBody>(&decoded->body()):nullptr;
        if(!reply||!reply->diversification||*reply->diversification!=std::string(1,char(1))){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Transport,azureus::Failure::Remote);return;}
        publishReadback(value);

      });      }catch(...){publicationMemoryFailure();}},publishRemaining(*value));
      if(!sent){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Transport,sent.error());return;}value->ticket=*sent;
    }
    void beginPublish(const Command& command){
      if((publishing_&&!publishing_->finished)||!localDatabase_||!options_.valueVersions){failCommand(command,AzureusServiceError::Capacity);return;}
      auto value=std::make_shared<Publishing>();value->command=command;value->deadline=command.deadline;value->snapshot.request=command.id;
      const auto digest=QCryptographicHash::hash(QByteArray(command.applicationKey.data(),static_cast<qsizetype>(command.applicationKey.size())),QCryptographicHash::Sha1);
      std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.constData()),value->snapshot.key.size(),value->snapshot.key.begin());publishing_=value;
      withPublish(value,true,[this,value,command](const ProfileIo::Data& data){
      if(!data.version){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Profile);return;}PublicationClock clock;const auto version=*data.version;
      const auto key=std::string_view(reinterpret_cast<const char*>(value->snapshot.key.data()),value->snapshot.key.size());
      auto local=localDatabase_->putLocal(key,command.payload,version,clock,command.publicationFlags);if(!local){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Invalid);return;}value->snapshot.localValue=std::move(*local);
      if(!validatePublish(value)){return;}value->snapshot.state=AzureusPublishState::Token;capturePublish(value);
      auto sent=actor_.requestStoreToken(command.peer,options_.version,this,[this,value](azureus::DetailedFindNodeOutcome outcome){try{withPublish(value,false,[this,value,outcome=std::move(outcome)](const auto&)mutable{
        if(!validatePublish(value)){return;}
        if(!outcome){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Transport,outcome.error());return;}
        const auto* decoded=std::get_if<azureus::find::Decoded>(&*outcome);const auto* reply=decoded?std::get_if<azureus::find::ReplyBody>(&decoded->body()):nullptr;
        if(!reply){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Transport,azureus::Failure::Remote);return;}publishStore(value,reply->randomId);

      });      }catch(...){publicationMemoryFailure();}},publishRemaining(*value));
      if(!sent){finishPublish(value,AzureusPublishState::Failed,AzureusServiceError::Transport,sent.error());return;}value->ticket=*sent;publish();
      });
    }

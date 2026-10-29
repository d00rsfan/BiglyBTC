// Included inside OwnedAzureusService::Impl::Owner. Explicit single-peer rating
// consumer only; no publication queue, automatic lookup timer or Host activation.
    std::shared_ptr<const storage::ProfileLease> ratingLease_;
    struct RatingRead {
      Command command;
      AzureusRatingReadSnapshot snapshot;
      std::chrono::steady_clock::time_point deadline;
      azureus::Ticket ticket;
      std::size_t slices=0;
      std::deque<std::pair<azureus::FindValueOutcome,std::shared_ptr<void>>> pending;
      bool profileIo=false;
      bool finished=false,cancelled=false;
    };
    std::shared_ptr<RatingRead> ratingRead_;
    std::expected<void,storage::Error> validateRating(const Command& command){
      if(!ratingLease_||!command.ratingHash||!command.ratingSubject||options_.network!=0){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"No bound rating Public DDB context"});}
      if(!profilePermit_||profileCommand_!=command.id){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"No current rating source permit"});}
      {std::lock_guard lock(state_->mutex);if(state_->stop){return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Rating owner is closing"});}}
      return {};
    }
    void withdrawRatings(){
      std::lock_guard lock(state_->mutex);if(!state_->value.ratingReadSnapshot||!state_->value.ratingReadSnapshot->current){return;}
      std::shared_ptr<AzureusRatingReadSnapshot> withdrawn;
      try{withdrawn=std::make_shared<AzureusRatingReadSnapshot>(*state_->value.ratingReadSnapshot);}catch(...){state_->value.ratingReadSnapshot.reset();state_->advance();throw;}
      withdrawn->current=false;withdrawn->ratings=plugins::rating::RatingResults{};withdrawn->localFallback=false;
      withdrawn->state=state_->stop?AzureusRatingReadState::Closed:AzureusRatingReadState::Failed;withdrawn->error=state_->stop?AzureusServiceError::Closed:AzureusServiceError::Profile;
      state_->value.ratingReadSnapshot=std::move(withdrawn);state_->advance();
    }
    void refreshRatingObservation(){
      if(profilePermit_||drainingIo_){return;}if(passiveSkip_){passiveSkip_=false;return;}
      bool current=false;{std::lock_guard lock(state_->mutex);current=state_->value.ratingReadSnapshot&&state_->value.ratingReadSnapshot->current;}
      if(!current||!ratingRead_||!ratingRead_->finished||passiveIo_){return;}passiveIo_=true;auto value=ratingRead_;
      const auto fresh=[this,value]{return ratingRead_==value&&value->finished;};
      if(!deferProfile(value->command,false,std::chrono::steady_clock::time_point::max(),fresh,
          [this](const auto& data){passiveIo_=false;std::lock_guard lock(state_->mutex);state_->ratingRevision=data.revision;},
          [this](storage::Error){passiveIo_=false;withdrawRatings();},true)){passiveIo_=false;withdrawRatings();}
    }
    void beginRating(Command command){
      if(!options_.valueVersions||options_.network!=0||!command.ratingHash||!isOwned(command.peer)){
        failCommand(command,AzureusServiceError::Invalid);return;
      }
      ratingLease_=state_->profileIo->lease();
      auto settled=std::make_shared<bool>(false);
      auto current=[this,command,settled]{std::lock_guard lock(state_->mutex);return !*settled&&!state_->stop&&isOwned(command.peer);};
      if(!deferProfile(command,false,command.deadline,std::move(current),
          [this,command,settled](const ProfileIo::Data& data)mutable{
            if(std::exchange(*settled,true)){return;}
            if(!data.rating){failCommand(command,AzureusServiceError::Profile);return;}
            command.ratingSubject=data.rating;beginRatingPrepared(std::move(command));
          },[this,command,settled](storage::Error){if(std::exchange(*settled,true)){return;}withdrawRatings();failCommand(command,AzureusServiceError::Profile);},false,
          [this,command,settled]{if(!std::exchange(*settled,true)){reply(command,azureus::Failure::Timeout,true);}},
          [this,command,settled]{if(!std::exchange(*settled,true)){reply(command,azureus::Failure::Cancelled,true);}})){failCommand(command,AzureusServiceError::Capacity);}
    }
    void beginRatingPrepared(Command command){
      const auto& source=*command.ratingSubject;
      command.applicationKey.assign(reinterpret_cast<const char*>(source.key.data()),source.key.size());
      const auto digest=QCryptographicHash::hash(QByteArray(command.applicationKey.data(),static_cast<qsizetype>(command.applicationKey.size())),QCryptographicHash::Sha1);
      std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.constData()),command.node.size(),command.node.begin());
      if(command.kind==AzureusServiceCommand::PublishRating){
        // Source writeRating skips score0. This explicit first branch supports
        // ordinary1..5, retaining negative legacy values as unsupported.
        if(source.local.local.value.score<1||source.local.local.value.score>5){failCommand(command,AzureusServiceError::Invalid);return;}
        auto encoded=source.local.local.value.encode();if(!encoded||encoded->empty()||encoded->size()>512){failCommand(command,AzureusServiceError::Invalid);return;}
        command.payload=std::move(*encoded);command.publicationFlags=AzureusPublicationFlags::Anonymous;beginPublish(command);return;
      }
      if(ratingRead_&&!ratingRead_->finished){failCommand(command,AzureusServiceError::Capacity);return;}
      auto value=std::make_shared<RatingRead>();value->command=command;value->deadline=command.deadline;
      value->snapshot.request=command.id;value->snapshot.hash=command.ratingHash;value->snapshot.sourceRevision=command.ratingRevision;value->snapshot.key=command.node;ratingRead_=value;
      if(!validateRatingRead(value)){return;}
      auto snapshot=std::make_shared<const AzureusRatingReadSnapshot>(value->snapshot);
      {std::lock_guard lock(state_->mutex);state_->value.ratingReadSnapshot=std::move(snapshot);state_->advance();}
      const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(value->deadline-std::chrono::steady_clock::now());
      auto submitted=actor_.findValue(command.peer,options_.version,command.node,0,64,this,[this,value](azureus::FindValueOutcome outcome){
        try{queueRatingReply(value,std::move(outcome));}catch(...){publicationMemoryFailure();}
      },remaining);
      if(!submitted){finishRatingRead(value,AzureusRatingReadState::Failed,AzureusServiceError::Transport,submitted.error());return;}value->ticket=*submitted;publish();
    }
    bool validateRatingRead(const std::shared_ptr<RatingRead>& value){
      if(ratingRead_!=value||value->finished){return false;}
      {std::lock_guard lock(state_->mutex);if(state_->stop){value->cancelled=true;}}
      if(value->cancelled){
        bool closing=false;{std::lock_guard lock(state_->mutex);closing=state_->stop;}
        finishRatingRead(value,closing?AzureusRatingReadState::Closed:AzureusRatingReadState::Cancelled,
            closing?AzureusServiceError::Closed:AzureusServiceError::Transport,closing?azureus::Failure::Closed:azureus::Failure::Cancelled);return false;
      }
      if(std::chrono::steady_clock::now()>=value->deadline){finishRatingRead(value,AzureusRatingReadState::TimedOut,AzureusServiceError::Transport,azureus::Failure::Timeout);return false;}
      if(!isOwned(value->command.peer)){finishRatingRead(value,AzureusRatingReadState::Failed,AzureusServiceError::Invalid,azureus::Failure::Peer);return false;}
      auto valid=validateRating(value->command);if(!valid){value->snapshot.profileFailure=valid.error();finishRatingRead(value,AzureusRatingReadState::Failed,AzureusServiceError::Profile);return false;}
      if(std::chrono::steady_clock::now()>=value->deadline){finishRatingRead(value,AzureusRatingReadState::TimedOut,AzureusServiceError::Transport,azureus::Failure::Timeout);return false;}return true;
    }
    void finishRatingRead(const std::shared_ptr<RatingRead>& value,AzureusRatingReadState result,
        std::optional<AzureusServiceError> error={},std::optional<azureus::Failure> transport={}){
      if(ratingRead_!=value||value->finished){return;}
      if(result==AzureusRatingReadState::Complete||result==AzureusRatingReadState::TimedOut){
        auto current=validateRating(value->command);if(!current){value->snapshot.profileFailure=current.error();result=AzureusRatingReadState::Failed;error=AzureusServiceError::Profile;transport.reset();}
      }
      value->finished=true;
      if(result!=AzureusRatingReadState::Complete){static_cast<void>(actor_.cancel(value->ticket));}
      // Original download read uses local fallback ONLY if no VALUE_READ events
      // arrived. A nonempty malformed stream cannot turn into a local rating.
      if((result==AzureusRatingReadState::Complete||result==AzureusRatingReadState::TimedOut)&&!value->snapshot.receivedValues){
        const auto& local=value->command.ratingSubject->local.local.value;
        if(local.needsPublishing()){auto added=value->snapshot.ratings.add(local,false);if(!added){++value->snapshot.decodeErrors;}value->snapshot.localFallback=true;}
      }
      value->snapshot.state=result;value->snapshot.error=error;value->snapshot.transportFailure=transport;
      value->snapshot.current=result==AzureusRatingReadState::Complete||result==AzureusRatingReadState::TimedOut;
      if(error==AzureusServiceError::Profile||result==AzureusRatingReadState::Closed||result==AzureusRatingReadState::Cancelled){
        value->snapshot.current=false;value->snapshot.ratings=plugins::rating::RatingResults{};value->snapshot.localFallback=false;
      }
      auto snapshot=std::make_shared<AzureusRatingReadSnapshot>(value->snapshot);publish();
      std::lock_guard lock(state_->mutex);
      if(state_->stop){snapshot->state=AzureusRatingReadState::Closed;snapshot->error=AzureusServiceError::Closed;snapshot->transportFailure=azureus::Failure::Closed;snapshot->current=false;snapshot->ratings=plugins::rating::RatingResults{};snapshot->localFallback=false;}
      else if(result==AzureusRatingReadState::Complete&&std::chrono::steady_clock::now()>=value->deadline){snapshot->state=AzureusRatingReadState::TimedOut;snapshot->error=AzureusServiceError::Transport;snapshot->transportFailure=azureus::Failure::Timeout;}
      if(snapshot->current){state_->ratingRevision=profileRevision_;}
      releaseLocked();state_->value.lastResult=AzureusServiceResult{value->command.id,0,AzureusServiceCommand::ReadRatings,true,snapshot->error,snapshot->transportFailure};state_->value.ratingReadSnapshot=std::move(snapshot);state_->advance();
    }
    void queueRatingReply(const std::shared_ptr<RatingRead>& value,azureus::FindValueOutcome outcome){
      if(ratingRead_!=value||value->finished){return;}
      auto held=holdProfileFrame();if(!held||value->pending.size()+value->slices>=64){finishRatingRead(value,AzureusRatingReadState::Failed,AzureusServiceError::Capacity);return;}
      value->pending.emplace_back(std::move(outcome),std::move(held));pumpRatingReplies(value);
    }
    void pumpRatingReplies(const std::shared_ptr<RatingRead>& value){
      if(ratingRead_!=value||value->finished||value->profileIo||value->pending.empty()){return;}
      value->profileIo=true;auto batch=std::move(value->pending);value->pending.clear();
      auto current=[this,value]{bool stop=false;{std::lock_guard lock(state_->mutex);stop=state_->stop;}return !stop&&ratingRead_==value&&!value->finished&&!value->cancelled;};
      if(!deferProfile(value->command,false,value->deadline,std::move(current),
          [this,value,batch=std::move(batch)](const auto&)mutable{
            for(auto& item:batch){if(value->finished){break;}ratingReply(value,std::move(item.first));}
            value->profileIo=false;pumpRatingReplies(value);
          },[this,value](storage::Error error){
            value->profileIo=false;if(value->finished){return;}value->snapshot.profileFailure=std::move(error);
            finishRatingRead(value,AzureusRatingReadState::Failed,AzureusServiceError::Profile);
          },true)){value->profileIo=false;finishRatingRead(value,AzureusRatingReadState::Failed,AzureusServiceError::Capacity);}
    }
    void ratingReply(const std::shared_ptr<RatingRead>& value,azureus::FindValueOutcome outcome){
      if(!validateRatingRead(value)){return;}
      if(!outcome){finishRatingRead(value,outcome.error()==azureus::Failure::Timeout?AzureusRatingReadState::TimedOut:AzureusRatingReadState::Failed,AzureusServiceError::Transport,outcome.error());return;}
      routerReply(value->command.peer,outcome);
      const auto* body=std::get_if<azureus::lookup::ReplyBody>(&outcome->body());
      if(!body||++value->slices>64){finishRatingRead(value,AzureusRatingReadState::Failed,AzureusServiceError::Capacity);return;}
      if(const auto* found=std::get_if<azureus::lookup::FoundValues>(&body->result)){
        if(found->values.size()>64-value->snapshot.receivedValues){finishRatingRead(value,AzureusRatingReadState::Failed,AzureusServiceError::Capacity);return;}
        for(const auto& item:found->values){
          ++value->snapshot.receivedValues;auto decoded=plugins::rating::decode(item.bytes);
          if(!decoded){++value->snapshot.decodeErrors;++value->snapshot.ignoredMalformedValues;continue;}
          // Preserve the qualified original partial-object/add behavior; failed
          // add can retain its already-applied average/count mutation.
          auto added=value->snapshot.ratings.add(decoded->value,false);if(!decoded->complete()||!added){++value->snapshot.decodeErrors;}
        }
      }else if(body->continuation){finishRatingRead(value,AzureusRatingReadState::Failed,AzureusServiceError::Readback);return;}
      if(!body->continuation){finishRatingRead(value,AzureusRatingReadState::Complete);return;}
      if(value->snapshot.receivedValues==64){finishRatingRead(value,AzureusRatingReadState::Failed,AzureusServiceError::Capacity);return;}
      auto snapshot=std::make_shared<const AzureusRatingReadSnapshot>(value->snapshot);
      std::lock_guard lock(state_->mutex);state_->value.ratingReadSnapshot=std::move(snapshot);state_->advance();
    }

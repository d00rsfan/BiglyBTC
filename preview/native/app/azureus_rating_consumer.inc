// Included inside OwnedAzureusService::Impl::Owner. Explicit single-peer rating
// consumer only; no publication queue, automatic lookup timer or Host activation.
    std::shared_ptr<const storage::ProfileLease> ratingLease_;
    struct RatingRead {
      Command command;
      AzureusRatingReadSnapshot snapshot;
      std::chrono::steady_clock::time_point deadline;
      azureus::Ticket ticket;
      std::size_t slices=0;
      bool finished=false,cancelled=false;
    };
    std::shared_ptr<RatingRead> ratingRead_;
    std::expected<void,storage::Error> validateRating(const Command& command){
      if(!ratingLease_||!command.ratingHash||!command.ratingSubject||options_.network!=0){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"No bound rating Public DDB context"});}
      auto valid=options_.valueVersions->validate();if(!valid){return valid;}
      auto current=storage::readDistributedRating(*ratingLease_,*command.ratingHash);
      if(!current){return std::unexpected(current.error());}
      if(current->sourceRevision!=command.ratingRevision||current->sourceRevision!=command.ratingSubject->sourceRevision
          ||current->local.attributesRevision!=command.ratingSubject->local.attributesRevision||current->key!=command.ratingSubject->key){
        return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Saved rating identity, permissions or attributes changed"});
      }
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
      bool current=false;{std::lock_guard lock(state_->mutex);current=state_->value.ratingReadSnapshot&&state_->value.ratingReadSnapshot->current;}
      if(current&&ratingRead_&&ratingRead_->finished&&!validateRating(ratingRead_->command)){withdrawRatings();}
    }
    void beginRating(Command command){
      if(!options_.valueVersions||options_.network!=0||!command.ratingHash||!isOwned(command.peer)){
        failCommand(command,AzureusServiceError::Invalid);return;
      }
      if(!ratingLease_){auto lease=options_.valueVersions->retainProfile();if(!lease){failCommand(command,AzureusServiceError::Profile);return;}ratingLease_=std::move(*lease);}
      auto current=storage::readDistributedRating(*ratingLease_,*command.ratingHash);
      if(!current||current->sourceRevision!=command.ratingRevision){withdrawRatings();failCommand(command,AzureusServiceError::Profile);return;}
      command.ratingSubject=std::make_shared<const storage::DistributedRatingSnapshot>(std::move(*current));
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
      auto value=std::make_shared<RatingRead>();value->command=command;value->deadline=std::chrono::steady_clock::now()+command.timeout;
      value->snapshot.request=command.id;value->snapshot.hash=command.ratingHash;value->snapshot.sourceRevision=command.ratingRevision;value->snapshot.key=command.node;ratingRead_=value;
      if(!validateRatingRead(value)){return;}
      auto snapshot=std::make_shared<const AzureusRatingReadSnapshot>(value->snapshot);
      {std::lock_guard lock(state_->mutex);state_->value.ratingReadSnapshot=std::move(snapshot);state_->advance();}
      const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(value->deadline-std::chrono::steady_clock::now());
      auto submitted=actor_.findValue(command.peer,options_.version,command.node,0,64,this,[this,value](azureus::FindValueOutcome outcome){
        try{ratingReply(value,std::move(outcome));}catch(...){publicationMemoryFailure();}
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
      releaseLocked();state_->value.lastResult=AzureusServiceResult{value->command.id,0,AzureusServiceCommand::ReadRatings,true,snapshot->error,snapshot->transportFailure};state_->value.ratingReadSnapshot=std::move(snapshot);state_->advance();
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

// Included in the private owner. Actor-only NODE completion is observed here;
// application code cannot install a seeded/closest/routing snapshot.
    std::optional<Command> routingCommand_;
    std::uint64_t routingOperation_=0;
    bool routingResultPublished_=true;
    void captureOwnedRouting(){
      const auto snapshot=actor_.routingSnapshot();
      {std::lock_guard lock(state_->mutex);if(state_->value.routingSnapshot!=snapshot){state_->value.routingSnapshot=snapshot;state_->advance();}}
      if(!routingCommand_||routingResultPublished_||!routingOperation_||!snapshot||snapshot->operation!=routingOperation_||!snapshot->finished){return;}
      bool closing=false;{std::lock_guard lock(state_->mutex);closing=state_->stop;}
      routingResultPublished_=true;reply(*routingCommand_,closing?std::optional{azureus::Failure::Closed}:snapshot->failure,true);
    }
    void beginRouting(const Command& command){
      if(!routingResultPublished_){failCommand(command,AzureusServiceError::Capacity);return;}
      routingCommand_=command;routingOperation_=0;routingResultPublished_=false;
      auto current=[this,id=command.id]{std::lock_guard lock(state_->mutex);return !state_->stop&&!routingResultPublished_&&routingCommand_&&routingCommand_->id==id;};
      if(!deferProfile(command,false,command.deadline,std::move(current),
          [this,command](const ProfileIo::Data&){
            if(routingResultPublished_){return;}const auto remaining=command.deadline-std::chrono::steady_clock::now();
            auto started=actor_.integrateRouting(command.peer,remaining);if(!started){routingResultPublished_=true;reply(command,started.error(),true);return;}routingOperation_=*started;captureOwnedRouting();publish();
          },[this,command](storage::Error){if(!routingResultPublished_){routingResultPublished_=true;failCommand(command,AzureusServiceError::Profile);}},false,
          [this,command]{if(!routingResultPublished_){routingResultPublished_=true;reply(command,azureus::Failure::Timeout,true);}})){
        routingResultPublished_=true;failCommand(command,AzureusServiceError::Capacity);
      }
    }

// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the existing private owner. PublicBound adds no thread or timer.
// Its cached policy bit can revoke only: a true bit never replaces an IO permit.
    bool publiclyBound()const noexcept{return options_.routingScope==azureus::RoutingScope::PublicBound;}
    bool publicPacketPermit()const noexcept{
      if(!publiclyBound()||!profilePermit_||!options_.publicPolicy||!options_.publicPolicy->current()){return false;}
      std::lock_guard lock(state_->mutex);return !state_->stop&&Clock::now()<profileDeadline_;
    }
    void withdrawPublic()noexcept{
      if(!publiclyBound()||options_.publicPolicy->current()){return;}
      {std::lock_guard lock(state_->mutex);state_->stop=true;state_->value.current=false;state_->value.error=AzureusServiceError::Profile;state_->value.state=AzureusServiceState::Closing;state_->advance();}
      // The ordinary Thread shutdown closes the UDP socket before external IO
      // join. No profile lock or future is acquired on this network thread.
      loop_.quit();
    }
    bool rpcCurrent(std::uint64_t id)const noexcept{return std::ranges::any_of(active_,[&](const auto& value){return value.id==id;});}
    void beginPublicCommand(Command command){
      auto pending=std::make_shared<bool>(true);
      const auto current=[this,pending]{return *pending&&serviceCurrent()&&options_.publicPolicy->current();};
      const auto finish=[this,command,pending](std::optional<azureus::Failure> failure){if(!*pending){return;}*pending=false;reply(command,failure,true);};
      if(!deferProfile(command,false,command.deadline,current,
        [this,command,pending](const ProfileIo::Data&)mutable{
          if(!*pending){return;}*pending=false;
          const auto remaining=command.deadline-Clock::now();if(remaining<=Clock::duration::zero()||!publicPacketPermit()){reply(command,azureus::Failure::Timeout,true);return;}
          command.timeout=std::chrono::duration_cast<std::chrono::nanoseconds>(remaining);this->command(std::move(command));
        },[finish](storage::Error){finish(azureus::Failure::Authority);},false,
        [finish]{finish(azureus::Failure::Timeout);},[finish]{finish(azureus::Failure::Cancelled);})){finish(azureus::Failure::Capacity);}
    }
    template<class Result>void completeRpc(const Command& command,Result result){
      if(!rpcCurrent(command.id)){return;}
      // A failed/cancelled/closed RPC grants no peer/DB authority. Settle it
      // immediately without waiting for disk; PublicBound does not mutate the
      // router from this unpermitted failure continuation.
      if(publiclyBound()&&!result){reply(command,result.error(),true);return;}
      auto held=publiclyBound()?holdProfileFrame():std::shared_ptr<void>{};
      if(publiclyBound()&&!held){reply(command,azureus::Failure::Capacity,true);return;}
      auto complete=[this,command,result=std::move(result),held=std::move(held)](const ProfileIo::Data&)mutable{
        if(!rpcCurrent(command.id)){return;}
        if(publiclyBound()&&(Clock::now()>=command.deadline||!publicPacketPermit())){reply(command,azureus::Failure::Timeout,true);return;}
        bool terminal=true;
        if constexpr(std::is_same_v<Result,azureus::FindValueOutcome>){const auto* body=result?std::get_if<azureus::lookup::ReplyBody>(&result->body()):nullptr;terminal=!body||!body->continuation;}
        routerReply(command.peer,result);reply(command,result?std::optional<azureus::Failure>{}:result.error(),terminal);
      };
      if(!publiclyBound()){complete(ProfileIo::Data{});return;}
      const auto finish=[this,command](azureus::Failure failure){if(rpcCurrent(command.id)){reply(command,failure,true);}};
      if(!deferProfile(command,false,command.deadline,[this,id=command.id]{return rpcCurrent(id)&&serviceCurrent()&&options_.publicPolicy->current();},
        std::move(complete),[finish](storage::Error){finish(azureus::Failure::Authority);},false,
        [finish]{finish(azureus::Failure::Timeout);},[finish]{finish(azureus::Failure::Cancelled);})){finish(azureus::Failure::Capacity);}
    }

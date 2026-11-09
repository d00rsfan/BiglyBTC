// SPDX-License-Identifier: GPL-2.0-or-later
// One finite DATA call on the existing worker/actor. No room crypto or delivery.
    std::optional<Command> transferCommand_;
    azureus::Ticket transferTicket_;
    void finishTransferCommand(const Command& command,azureus::TransferOutcome outcome){
      if(!transferCommand_||transferCommand_->id!=command.id){return;}
      if(!outcome){publishTransferCommand(command,std::move(outcome));return;}
      auto held=holdProfileFrame();if(!held){publishTransferCommand(command,std::unexpected(azureus::Failure::Capacity));return;}
      const auto fail=[this,command](azureus::Failure failure){publishTransferCommand(command,std::unexpected(failure));};
      if(!deferProfile(command,false,command.deadline,[this,id=command.id]{return transferCommand_&&transferCommand_->id==id&&serviceCurrent();},
        [this,command,outcome=std::move(outcome),held=std::move(held)](const ProfileIo::Data& ready)mutable{
          if(ready.revision!=command.transferRevision){publishTransferCommand(command,std::unexpected(azureus::Failure::Authority));return;}
          publishTransferCommand(command,std::move(outcome));
        },[fail](storage::Error){fail(azureus::Failure::Authority);},false,[fail]{fail(azureus::Failure::Timeout);},[fail]{fail(azureus::Failure::Cancelled);})){fail(azureus::Failure::Capacity);}
    }
    void publishTransferCommand(Command command,azureus::TransferOutcome outcome){
      if(!transferCommand_||transferCommand_->id!=command.id){return;}
      auto value=std::make_shared<AzureusTransferSnapshot>();value->request=command.id;value->terminal=true;
      if(outcome){value->reply=std::move(*outcome);}else{value->failure=outcome.error();}
      transferCommand_.reset();transferTicket_={};
      // Result and command release are one atomic passive publication. The
      // actual bytes are not a verified message or application ACK.
      std::lock_guard lock(state_->mutex);
      if(outcome&&(state_->stop||std::chrono::steady_clock::now()>=command.deadline)){outcome=std::unexpected(state_->stop?azureus::Failure::Closed:azureus::Failure::Timeout);value->reply.clear();value->failure=outcome.error();}
      state_->value.transferSnapshot=std::move(value);state_->value.lastResult=AzureusServiceResult{command.id,0,command.kind,true,outcome?std::nullopt:std::optional{AzureusServiceError::Transport},outcome?std::nullopt:std::optional{outcome.error()}};releaseLocked();state_->advance();
    }
    void beginTransfer(Command command){
      if(!options_.transferHandler||!options_.serveValues||!options_.valueVersions){complete(command,std::unexpected(azureus::Failure::State));return;}
      if(command.kind==AzureusServiceCommand::TransferAnswer){
        // Receipt matching/source revision/deadline are still actor-owned.
        auto held=holdProfileFrame();if(!held){complete(command,std::unexpected(azureus::Failure::Capacity));return;}
        const auto fail=[this,command](azureus::Failure why){complete(command,std::unexpected(why));};
        if(!deferProfile(command,false,command.deadline,[this]{return serviceCurrent();},
          [this,command,held=std::move(held)](const ProfileIo::Data& ready)mutable{
            if(ready.revision!=command.transferRevision){complete(command,std::unexpected(azureus::Failure::Authority));return;}
            auto result=actor_.answerTransfer(command.transferReceipt,std::move(command.payload));complete(command,result);
            if(result){std::lock_guard lock(state_->mutex);if(state_->value.transferRequest&&state_->value.transferRequest->receipt==command.transferReceipt){state_->value.transferRequest.reset();state_->advance();}}
          },[fail](storage::Error){fail(azureus::Failure::Authority);},false,[fail]{fail(azureus::Failure::Timeout);},[fail]{fail(azureus::Failure::Cancelled);})){fail(azureus::Failure::Capacity);}return;
      }
      if(!profilePermit_){beginPermittedCommand(std::move(command));return;}
      command.transferRevision=profileRevision_;
      if(transferCommand_){complete(command,std::unexpected(azureus::Failure::Capacity));return;}
      transferCommand_=command;auto snapshot=std::make_shared<AzureusTransferSnapshot>();snapshot->request=command.id;
      {std::lock_guard lock(state_->mutex);state_->value.transferSnapshot=std::move(snapshot);state_->advance();}
      auto result=actor_.callTransfer(command.peer,options_.version,std::move(command.applicationKey),std::move(command.payload),this,[this,command](azureus::TransferOutcome value){finishTransferCommand(command,std::move(value));},command.deadline);
      if(!result){finishTransferCommand(command,std::unexpected(result.error()));return;}transferTicket_=*result;
    }

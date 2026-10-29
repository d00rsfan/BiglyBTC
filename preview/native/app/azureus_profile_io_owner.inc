// Included in the private Qt owner. Only this thread owns these continuations;
// disk jobs own storage values, not Qt/node-cache handles or actor callbacks.
    using ProfileIo=detail::AzureusProfileIo;
    struct IoSlot {
      ProfileIo::Handle job;
      std::uint64_t command=0;
      std::chrono::steady_clock::time_point deadline;
      std::move_only_function<bool()> current;
      std::move_only_function<void(const ProfileIo::Data&)> commit;
      std::move_only_function<void(storage::Error)> failed;
      std::optional<storage::Error> error;
      std::move_only_function<void()> expired,cancelled;
      bool consumed=false,expiryNotified=false;
    };
    std::array<IoSlot,8> ioSlots_{};
    QChronoTimer ioDeadline_;
    void armProfileDeadline()noexcept{
      try{
        auto earliest=std::chrono::steady_clock::time_point::max();for(const auto& slot:ioSlots_){if(slot.job&&!slot.expiryNotified){earliest=std::min(earliest,slot.deadline);}}
        if(earliest==std::chrono::steady_clock::time_point::max()){ioDeadline_.stop();return;}
        ioDeadline_.setInterval(std::max(std::chrono::nanoseconds{1},std::chrono::duration_cast<std::chrono::nanoseconds>(earliest-std::chrono::steady_clock::now())));ioDeadline_.start();
      }catch(...){try{ioDeadline_.stop();}catch(...){}publicationMemoryFailure();}
    }
    bool profilePermit_=false,passiveIo_=false,drainingIo_=false;
    std::uint64_t profileCommand_=0,profileRevision_=0;
    bool passiveSkip_=false;
    std::shared_ptr<std::atomic<std::size_t>> pendingFrames_=std::make_shared<std::atomic<std::size_t>>(0);
    struct ProfileFrame final {
      explicit ProfileFrame(std::shared_ptr<std::atomic<std::size_t>> value):count(std::move(value)){}
      ~ProfileFrame(){count->fetch_sub(1);}
      std::shared_ptr<std::atomic<std::size_t>> count;
    };
    std::shared_ptr<void> holdProfileFrame(){
      if(pendingFrames_->fetch_add(1)>=64){pendingFrames_->fetch_sub(1);return {};}
      try{return std::make_shared<ProfileFrame>(pendingFrames_);}
      catch(...){pendingFrames_->fetch_sub(1);throw;}
    }
    void drainProfileIo(){
      if(drainingIo_||!state_->profileIo){return;}drainingIo_=true;
      const auto leave=qScopeGuard([&]()noexcept{drainingIo_=false;armProfileDeadline();});
      bool stop=false;{std::lock_guard lock(state_->mutex);stop=state_->stop;}
      try{for(auto& slot:ioSlots_){if(!slot.job){continue;}
        if(!slot.expiryNotified&&std::chrono::steady_clock::now()>=slot.deadline){
          slot.expiryNotified=true;ProfileIo::revoke(slot.job);if(slot.expired){slot.expired();}
        }
        if(stop||std::chrono::steady_clock::now()>=slot.deadline||!slot.current()){
          ProfileIo::revoke(slot.job);slot.error=storage::Error{storage::ErrorCode::Cancelled,"Profile operation stopped, stale or expired"};
        }
        if(!slot.consumed){if(auto ready=ProfileIo::take(slot.job)){
          slot.consumed=true;passiveSkip_=true;
          if(!*ready){slot.error=ready->error();}
          else if(!slot.error&&state_->profileIo->lease()->sourceRevision()==ready->value().revision){
            profilePermit_=true;profileCommand_=slot.command;profileRevision_=ready->value().revision;
            try{slot.commit(ready->value());}catch(...){publicationMemoryFailure();}
            profilePermit_=false;profileCommand_=0;profileRevision_=0;
          }else if(!slot.error){slot.error=storage::Error{storage::ErrorCode::Conflict,"Profile source permit changed"};}
          ProfileIo::acknowledge(slot.job);
        }}
        if(ProfileIo::released(slot.job)){
          if(!slot.consumed&&!slot.error){slot.error=ProfileIo::failure(slot.job);}
          passiveSkip_=true;auto done=std::move(slot.failed);auto error=std::move(slot.error);state_->profileIo->release(slot.job);slot={};
          // The worker has released the source gate. Failure notifications and
          // any reentrant continuation destruction run only after that point.
          if(error){done(std::move(*error));}
        }
      }}catch(...){
        profilePermit_=false;profileCommand_=0;profileRevision_=0;
        for(auto& slot:ioSlots_){if(slot.job){ProfileIo::acknowledge(slot.job);ProfileIo::revoke(slot.job);}}
        publicationMemoryFailure();
      }
    }
    bool deferProfile(Command command,bool reserve,
        std::chrono::steady_clock::time_point deadline,std::move_only_function<bool()> current,
        std::move_only_function<void(const ProfileIo::Data&)> commit,
        std::move_only_function<void(storage::Error)> failed,bool allowExpired=false,
        std::move_only_function<void()> expired={},std::move_only_function<void()> cancelled={}){
      if(!state_->profileIo){return false;}
      IoSlot* slot=nullptr;for(auto& item:ioSlots_){if(!item.job){slot=&item;break;}}if(!slot){return false;}
      auto versions=options_.valueVersions;auto lease=state_->profileIo->lease();
      auto submitted=state_->profileIo->submit([versions,lease,command,reserve,deadline,allowExpired](std::stop_token stop)->ProfileIo::Result{
        const auto cancelled=[&]{return stop.stop_requested()||(!allowExpired&&std::chrono::steady_clock::now()>=deadline);};
        if(cancelled()){return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Profile IO expired"});}
        auto valid=versions->validate();if(!valid){return std::unexpected(valid.error());}
        ProfileIo::Data result;
        const auto rating=[&]()->std::expected<void,storage::Error>{
          if(!command.ratingHash){return {};}
          auto fresh=storage::readDistributedRating(*lease,*command.ratingHash);if(!fresh){return std::unexpected(fresh.error());}
          if(fresh->sourceRevision!=command.ratingRevision||(command.ratingSubject&&(
              fresh->local.attributesRevision!=command.ratingSubject->local.attributesRevision||fresh->key!=command.ratingSubject->key))){
            return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Saved rating source changed"});}
          result.rating=std::make_shared<const storage::DistributedRatingSnapshot>(std::move(*fresh));return {};
        };
        valid=rating();if(!valid){return std::unexpected(valid.error());}
        if(reserve){auto next=versions->next(QDateTime::currentMSecsSinceEpoch());if(!next){return std::unexpected(next.error());}result.version=*next;}
        if(cancelled()){return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Profile IO expired"});}
        return result;
      });
      if(!submitted){return false;}
      slot->job=std::move(*submitted);slot->command=command.id;slot->deadline=allowExpired?std::chrono::steady_clock::time_point::max():deadline;slot->current=std::move(current);slot->commit=std::move(commit);slot->failed=std::move(failed);slot->expired=std::move(expired);slot->cancelled=std::move(cancelled);armProfileDeadline();return true;
    }
    bool normalProfileRequest(std::move_only_function<void()> resume){try{
      auto held=holdProfileFrame();if(!held){return false;}
      Command command;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
      return deferProfile(command,false,deadline,[this]{std::lock_guard lock(state_->mutex);return !state_->stop;},
        [resume=std::move(resume),held=std::move(held)](const auto&)mutable{resume();},[](storage::Error){});
    }catch(...){publicationMemoryFailure();return false;}}

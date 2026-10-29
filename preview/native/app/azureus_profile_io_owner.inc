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
    struct NormalPending {
      std::shared_ptr<std::move_only_function<void(bool)>> complete;
      std::shared_ptr<void> frame;
      std::chrono::steady_clock::time_point deadline;
      std::uint64_t order=0;
    };
    // These are the SAME held normal frames, not64 extra worker permissions.
    // Already received replies wait here until one of the eight slots releases.
    std::array<NormalPending,64> normalPending_{};
    std::uint64_t normalOrder_=0;
    bool pumpingNormal_=false;
    QChronoTimer ioDeadline_;
    void armProfileDeadline()noexcept{
      try{
        auto earliest=std::chrono::steady_clock::time_point::max();for(const auto& slot:ioSlots_){if(slot.job&&!slot.consumed&&!slot.expiryNotified){earliest=std::min(earliest,slot.deadline);}}
        for(const auto& pending:normalPending_){if(pending.complete){earliest=std::min(earliest,pending.deadline);}}
        if(earliest==std::chrono::steady_clock::time_point::max()){ioDeadline_.stop();return;}
        ioDeadline_.setInterval(std::max(std::chrono::nanoseconds{1},std::chrono::duration_cast<std::chrono::nanoseconds>(earliest-std::chrono::steady_clock::now())));ioDeadline_.start();
      }catch(...){try{ioDeadline_.stop();}catch(...){}publicationMemoryFailure();}
    }
    bool profilePermit_=false,passiveIo_=false,drainingIo_=false;
    std::chrono::steady_clock::time_point profileDeadline_{};
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
      const auto leave=qScopeGuard([&]()noexcept{drainingIo_=false;pumpNormalProfiles();armProfileDeadline();});
      bool stop=false;{std::lock_guard lock(state_->mutex);stop=state_->stop;}
      try{for(auto& slot:ioSlots_){if(!slot.job){continue;}
        if(!slot.consumed&&!slot.expiryNotified&&std::chrono::steady_clock::now()>=slot.deadline){
          slot.expiryNotified=true;ProfileIo::revoke(slot.job);if(slot.expired){slot.expired();}
        }
        if(!slot.consumed&&(stop||std::chrono::steady_clock::now()>=slot.deadline||!slot.current())){
          ProfileIo::revoke(slot.job);slot.error=storage::Error{storage::ErrorCode::Cancelled,"Profile operation stopped, stale or expired"};
        }
        if(!slot.consumed){if(auto ready=ProfileIo::take(slot.job)){
          slot.consumed=true;passiveSkip_=true;
          if(!*ready){slot.error=ready->error();}
          else if(!slot.error&&state_->profileIo->lease()->sourceRevision()==ready->value().revision){
            profileDeadline_=slot.deadline;profilePermit_=true;profileCommand_=slot.command;profileRevision_=ready->value().revision;
            try{slot.commit(ready->value());}catch(...){publicationMemoryFailure();}
            profilePermit_=false;profileDeadline_={};profileCommand_=0;profileRevision_=0;
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
        profilePermit_=false;profileDeadline_={};profileCommand_=0;profileRevision_=0;
        for(auto& slot:ioSlots_){if(slot.job){ProfileIo::acknowledge(slot.job);ProfileIo::revoke(slot.job);}}
        publicationMemoryFailure();
      }
    }
    bool deferProfile(Command command,bool reserve,
        std::chrono::steady_clock::time_point deadline,std::move_only_function<bool()> current,
        std::move_only_function<void(const ProfileIo::Data&)> commit,
        std::move_only_function<void(storage::Error)> failed,bool allowExpired=false,
        std::move_only_function<void()> expired={},std::move_only_function<void()> cancelled={},
        std::move_only_function<std::expected<std::shared_ptr<const storage::AzureusContactsSnapshot>,storage::Error>(std::stop_token)> contactWork={}){
      if(!state_->profileIo){return false;}
      IoSlot* slot=nullptr;for(auto& item:ioSlots_){if(!item.job){slot=&item;break;}}if(!slot){return false;}
      auto versions=options_.valueVersions;auto contacts=options_.contacts;auto publicPolicy=options_.publicPolicy;auto lease=state_->profileIo->lease();const bool routingPolicy=options_.routingScope==azureus::RoutingScope::OwnedLoopback;
      auto submitted=state_->profileIo->submit([versions,contacts,publicPolicy,lease,command,reserve,deadline,allowExpired,routingPolicy,contactWork=std::move(contactWork)](std::stop_token stop)mutable->ProfileIo::Result{
        const auto cancelled=[&]{return stop.stop_requested()||(!allowExpired&&std::chrono::steady_clock::now()>=deadline);};
        if(cancelled()){return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Profile IO expired"});}
        auto valid=publicPolicy?publicPolicy->validate():versions->validate();if(!valid){return std::unexpected(valid.error());}if(contacts){valid=contacts->validate();if(!valid){if(publicPolicy){publicPolicy->revoke();}return std::unexpected(valid.error());}}
        if(routingPolicy){
          auto config=storage::ConfigFile::load(*lease,"biglybt.config");if(!config&&config.error().code!=storage::ErrorCode::Missing){return std::unexpected(config.error());}
          if(config){if(config->recoverySource()!=storage::RecoverySource::Primary){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Resolve routing configuration recovery first"});}
            if(const auto* enabled=config->document().find("Plugin.DHT.dht.enabled")){if(enabled->type!=core::Node::Type::Integer){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Invalid original DHT availability"});}if(static_cast<std::uint32_t>(enabled->integer)==0){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Original Azureus DHT is disabled"});}}}
        }
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
        if(contactWork){auto value=contactWork(stop);if(!value){return std::unexpected(value.error());}result.contacts=std::move(*value);}
        if(cancelled()){return std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Profile IO expired"});}
        return result;
      });
      if(!submitted){return false;}
      slot->job=std::move(*submitted);slot->command=command.id;slot->deadline=allowExpired?std::chrono::steady_clock::time_point::max():deadline;slot->current=std::move(current);slot->commit=std::move(commit);slot->failed=std::move(failed);slot->expired=std::move(expired);slot->cancelled=std::move(cancelled);armProfileDeadline();return true;
    }
    void clearNormalProfiles()noexcept{for(auto& pending:normalPending_){pending={};}}
    void pumpNormalProfiles()noexcept{
      if(pumpingNormal_||profilePermit_||!state_->profileIo){return;}pumpingNormal_=true;
      const auto reset=qScopeGuard([&]()noexcept{pumpingNormal_=false;});
      try{for(;;){
        NormalPending* pending=nullptr;for(auto& item:normalPending_){if(item.complete&&(!pending||item.order<pending->order)){pending=&item;}}
        if(!pending){return;}
        bool stop=false;{std::lock_guard lock(state_->mutex);stop=state_->stop;}
        if(stop||std::chrono::steady_clock::now()>=pending->deadline){
          auto complete=std::move(pending->complete);auto held=std::move(pending->frame);*pending={};(*complete)(false);continue;
        }
        if(std::ranges::none_of(ioSlots_,[](const auto& slot){return !slot.job;})){return;}
        const auto complete=pending->complete;const auto held=pending->frame;const auto deadline=pending->deadline;Command command;
        // If the physical executor's tail has not released yet, keep the same
        // frame. Its next actual Ready/Released wake retries; no timer spin.
        if(!deferProfile(command,false,deadline,[this]{std::lock_guard lock(state_->mutex);return !state_->stop;},
          [complete,held](const auto&){(*complete)(true);},
          [complete](storage::Error){(*complete)(false);})){return;}
        *pending={};
      }}catch(...){publicationMemoryFailure();}
    }
    bool normalProfileRequest(std::move_only_function<void(bool)> complete){try{
      if(!state_->profileIo||normalOrder_==UINT64_MAX){return false;}auto held=holdProfileFrame();if(!held){return false;}
      NormalPending* pending=nullptr;for(auto& item:normalPending_){if(!item.complete){pending=&item;break;}}if(!pending){return false;}
      pending->complete=std::make_shared<std::move_only_function<void(bool)>>(std::move(complete));pending->frame=std::move(held);
      pending->deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);pending->order=++normalOrder_;
      pumpNormalProfiles();armProfileDeadline();return true;
    }catch(...){publicationMemoryFailure();return false;}}

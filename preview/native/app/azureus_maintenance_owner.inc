// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the existing owner; all filesystem work stays on profile IO.
    AzureusMaintenanceObservation maintenanceStatus_;
    bool maintenanceIoPending_=false,contactsBusy_=false,exportDue_=false,contactsLoaded_=false;
    std::uint64_t maintenanceTurns_=0,contactsSerial_=0;
    void captureMaintenance(){
      if(!options_.maintenance){return;}maintenanceStatus_.routing=actor_.maintenanceFacts();maintenanceStatus_.ioPending=maintenanceIoPending_||contactsBusy_;
      std::lock_guard lock(state_->mutex);state_->value.maintenance=maintenanceStatus_;
    }
    bool serviceCurrent()const{std::lock_guard lock(state_->mutex);return !state_->stop;}
    static std::vector<azureus::SavedRoutingContact> savedRouting(const storage::AzureusContactsSnapshot& snapshot){
      std::vector<azureus::SavedRoutingContact> out;out.reserve(snapshot.contacts.size());for(const auto& saved:snapshot.contacts){nodes::Snapshot value;nodes::udp::Address address;address.length=saved.length;address.bytes=saved.address;address.port=saved.port;value.address=address;value.protocol=saved.protocol;out.push_back({value,saved.timeAlive});}return out;
    }
    std::expected<void,azureus::Failure> restoreContactData(const ProfileIo::Data& data){
      if(!data.contacts){return std::unexpected(azureus::Failure::State);}if(!contactsLoaded_){auto restored=actor_.restoreRoutingContacts(savedRouting(*data.contacts));if(!restored){return restored;}contactsLoaded_=true;}
      maintenanceStatus_.savedRecords=data.contacts->contacts.size();maintenanceStatus_.cacheWritable=data.contacts->writable;return {};
    }
    void beginSavedRouting(const Command& command){
      if(!routingResultPublished_){failCommand(command,AzureusServiceError::Capacity);return;}
      routingCommand_=command;routingOperation_=0;routingResultPublished_=false;
      auto current=[this,id=command.id]{return serviceCurrent()&&!routingResultPublished_&&routingCommand_&&routingCommand_->id==id;};
      auto contacts=options_.contacts;
      if(!deferProfile(command,false,command.deadline,std::move(current),[this,command](const ProfileIo::Data& data){
        if(!data.contacts){routingResultPublished_=true;failCommand(command,AzureusServiceError::Profile);return;}
        const auto restored=restoreContactData(data);if(!restored){routingResultPublished_=true;reply(command,restored.error(),true);return;}
        maintenanceStatus_.savedRecords=data.contacts->contacts.size();maintenanceStatus_.cacheWritable=data.contacts->writable;maintenanceStatus_.error.reset();
        const auto started=actor_.integrateKnownRouting(command.deadline-std::chrono::steady_clock::now());if(!started){routingResultPublished_=true;reply(command,started.error(),true);return;}routingOperation_=*started;captureOwnedRouting();publish();
      },[this,command](storage::Error error){if(!routingResultPublished_){maintenanceStatus_.error=error;routingResultPublished_=true;failCommand(command,AzureusServiceError::Profile);}},false,
      [this,command]{if(!routingResultPublished_){routingResultPublished_=true;reply(command,azureus::Failure::Timeout,true);}}, {},
      [contacts](std::stop_token)->std::expected<std::shared_ptr<const storage::AzureusContactsSnapshot>,storage::Error>{return contacts->snapshot();})){
        routingResultPublished_=true;failCommand(command,AzureusServiceError::Capacity);
      }
    }
    void finishContactFlush(const Command& command,std::uint64_t serial,std::optional<storage::Error> error={},std::optional<azureus::Failure> failure={}){
      if(!contactsBusy_||serial!=contactsSerial_){return;}contactsBusy_=false;maintenanceStatus_.error=std::move(error);
      if(command.id){if(maintenanceStatus_.error){failCommand(command,AzureusServiceError::Profile);}else{reply(command,failure,true);}}else{publish();}
    }
    void writeContactSnapshot(const Command& command,std::uint64_t serial,std::vector<storage::AzureusSavedContact> records){
      auto contacts=options_.contacts;
      if(!deferProfile(command,false,command.deadline,[this,serial]{return serviceCurrent()&&contactsBusy_&&serial==contactsSerial_;},
        [this,command,serial](const ProfileIo::Data& data){
          if(!data.contacts){finishContactFlush(command,serial,storage::Error{storage::ErrorCode::Io,"No committed contact export snapshot"});return;}
          maintenanceStatus_.savedRecords=data.contacts->contacts.size();maintenanceStatus_.cacheWritable=data.contacts->writable;
          if(maintenanceStatus_.exports==UINT64_MAX){finishContactFlush(command,serial,storage::Error{storage::ErrorCode::Unsupported,"Contact export counter exhausted"});return;}++maintenanceStatus_.exports;finishContactFlush(command,serial);
        },[this,command,serial](storage::Error error){finishContactFlush(command,serial,std::move(error));},false,
        [this,command,serial]{finishContactFlush(command,serial,{},azureus::Failure::Timeout);},
        [this,command,serial]{finishContactFlush(command,serial,{},azureus::Failure::Cancelled);},
        [contacts,deadline=command.deadline,records=std::move(records)](std::stop_token stop)->std::expected<std::shared_ptr<const storage::AzureusContactsSnapshot>,storage::Error>{auto saved=contacts->save(records,stop,deadline);if(!saved){return std::unexpected(saved.error());}return contacts->snapshot();})){
        finishContactFlush(command,serial,storage::Error{storage::ErrorCode::Unsupported,"Contact IO capacity exhausted"});
      }
    }
    void beginContactFlush(Command command){
      if(contactsBusy_||contactsSerial_==UINT64_MAX){if(command.id){failCommand(command,AzureusServiceError::Capacity);}return;}
      if(command.deadline==std::chrono::steady_clock::time_point{}){command.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);}contactsBusy_=true;const auto serial=++contactsSerial_;
      if(!deferProfile(command,false,command.deadline,[this,serial]{return serviceCurrent()&&contactsBusy_&&serial==contactsSerial_;},
        [this,command,serial](const ProfileIo::Data& data){
          auto imported=restoreContactData(data);if(!imported){finishContactFlush(command,serial,{},imported.error());return;}auto captured=actor_.exportRoutingContacts();if(!captured){finishContactFlush(command,serial,{},captured.error());return;}
          std::vector<storage::AzureusSavedContact> records;records.reserve(captured->size());for(const auto& item:*captured){if(!item.contact.address){finishContactFlush(command,serial,{},azureus::Failure::Authority);return;}const auto& address=*item.contact.address;storage::AzureusSavedContact saved;saved.timeAlive=item.timeAlive;saved.protocol=item.contact.protocol;saved.length=address.length;saved.address=address.bytes;saved.port=address.port;records.push_back(saved);}writeContactSnapshot(command,serial,std::move(records));
        },[this,command,serial](storage::Error error){finishContactFlush(command,serial,std::move(error));},false,
        [this,command,serial]{finishContactFlush(command,serial,{},azureus::Failure::Timeout);},
        [this,command,serial]{finishContactFlush(command,serial,{},azureus::Failure::Cancelled);},
        [contacts=options_.contacts](std::stop_token)->std::expected<std::shared_ptr<const storage::AzureusContactsSnapshot>,storage::Error>{return contacts->snapshot();})){
        finishContactFlush(command,serial,storage::Error{storage::ErrorCode::Unsupported,"Contact IO capacity exhausted"});
      }
    }
    void maintenanceWake(){
      if(!options_.maintenance||!serviceCurrent()){return;}if(maintenanceTurns_==UINT64_MAX){maintenanceTimer_.stop();maintenanceStatus_.error=storage::Error{storage::ErrorCode::Unsupported,"Maintenance generation exhausted"};publish();return;}
      ++maintenanceTurns_;if(maintenanceTurns_==12||maintenanceTurns_%60==0){exportDue_=true;}
      if(maintenanceIoPending_){return;}maintenanceIoPending_=true;Command command;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
      if(!deferProfile(command,false,deadline,[this]{return serviceCurrent()&&maintenanceIoPending_;},
        [this](const ProfileIo::Data& data){maintenanceIoPending_=false;auto imported=restoreContactData(data);if(!imported){maintenanceStatus_.error=storage::Error{storage::ErrorCode::Unsupported,"Saved contact import refused"};publish();return;}captureOwnedRouting();const auto tick=actor_.maintainRouting(QDateTime::currentMSecsSinceEpoch());if(!tick){maintenanceStatus_.error=storage::Error{storage::ErrorCode::Unsupported,"Bounded routing maintenance refused"};}if(exportDue_&&!contactsBusy_){exportDue_=false;beginContactFlush(Command{});}publish();},
        [this](storage::Error error){maintenanceIoPending_=false;maintenanceStatus_.error=std::move(error);publish();},false,
        [this]{maintenanceIoPending_=false;maintenanceStatus_.error=storage::Error{storage::ErrorCode::Cancelled,"Maintenance preflight expired"};publish();}, {},
        [contacts=options_.contacts](std::stop_token)->std::expected<std::shared_ptr<const storage::AzureusContactsSnapshot>,storage::Error>{return contacts->snapshot();})){
        maintenanceIoPending_=false;maintenanceStatus_.error=storage::Error{storage::ErrorCode::Unsupported,"Maintenance preflight capacity exhausted"};publish();
      }
    }

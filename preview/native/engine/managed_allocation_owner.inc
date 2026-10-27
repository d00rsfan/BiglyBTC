// Included inside Impl; existing sole worker/poll owns every allocation step.
void advanceAllocation(Managed& value) {
  if(!value.status.retained||value.allocationAttempted){return;}
  const auto finish=[&](bool success){
    value.allocationAttempted=true;
    auto settled=managedLease?value.storage.settleAllocation(*managedLease,success)
        :std::expected<void,storage::Error>{std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Allocation lost its held lease"})};
    if(!settled&&!value.status.failure){value.status.failure=settled.error();}
    value.status.allocationComplete=success&&settled.has_value();
    value.status.retained=false;value.flushed=settled.has_value();value.removed=settled.has_value();
    value.status.phase=value.status.failure?ManagedPhase::Failed:ManagedPhase::Stopped;
    value.status.manuallyPaused=success&&settled&&value.status.allocationMode==core::TorrentAllocationMode::Paused;
    value.status.downloadRate=0;value.status.uploadRate=0;
  };
  if(value.stop){finish(false);return;}
  const auto advanced=value.storage.advanceAllocation();
  if(!advanced){value.status.failure=advanced.error();finish(false);return;}
  value.status.allocationBytes=advanced->completedBytes;value.status.allocationTotalBytes=advanced->totalBytes;
  if(advanced->complete){finish(true);}
}

// Included inside the sole actual PersistentSession::Impl worker owner.
void advanceManagedRecheck(Managed& value){
  if(!value.status.recheck||!value.handle.is_valid()||value.removing){return;}
  if(closingAdmission||value.recheckStop.stop_requested()){value.recheckCancelled=true;}
  if(value.stop){return;}
  if(value.recheckCancelled){value.status.recheck->phase=ManagedRecheckPhase::Retiring;requestStop(value);return;}
  if(value.status.received||value.status.sent){value.status.failure=storage::Error{storage::ErrorCode::Conflict,"Read-only recheck observed unexpected payload traffic"};}
  if(value.status.failure){value.status.recheck->phase=ManagedRecheckPhase::Retiring;requestStop(value);return;}
  if(value.checkedAlert&&value.status.checked){
    if(!value.seedPiecesExact||value.verified.size()!=static_cast<std::size_t>(value.status.totalPieces)){
      value.status.failure=storage::Error{storage::ErrorCode::Conflict,"Actual full checked piece map is unavailable"};requestStop(value);return;
    }
    const auto outcomes=value.storage.recheckHashOutcomes();
    if(outcomes.size()!=value.verified.size()||!std::ranges::all_of(outcomes,[](auto outcome){return outcome==1||outcome==2;})){
      value.status.failure=storage::Error{storage::ErrorCode::Io,"Full recheck contains an unobserved/failed hash job; no certified map saved"};requestStop(value);return;
    }
    value.recheckChecked=true;value.status.recheck->scannedBytes=value.storage.recheckScannedBytes();
    value.status.recheck->phase=ManagedRecheckPhase::Retiring;requestStop(value);
  }
}
void sampleManagedRecheckCounts(Managed& value){
  auto& progress=*value.status.recheck;progress.goodPieces=progress.badPieces=progress.missingPieces=progress.unknownPieces=0;
  const auto outcomes=value.storage.recheckHashOutcomes();
  for(int i=0;i<value.status.totalPieces;++i){const auto index=static_cast<std::size_t>(i);
    if(index<value.verified.size()&&value.verified[index]){++progress.goodPieces;}
    else if(index<outcomes.size()&&outcomes[index]==2){++progress.missingPieces;}
    else if(index<outcomes.size()&&outcomes[index]==1){++progress.badPieces;}
    else{++progress.unknownPieces;}
  }
}
void finishManagedRecheck(Managed& value){
  sampleManagedRecheckCounts(value);auto& progress=*value.status.recheck;
  progress.phase=progress.pieceMapSaved?ManagedRecheckPhase::Finished:value.status.failure?ManagedRecheckPhase::Failed
      :value.recheckCancelled?ManagedRecheckPhase::Cancelled:ManagedRecheckPhase::Failed;
}

bool SessionBridge::createHybridCompanion(ui::Target source,bool selectV2) {
  if(profileClosed_||!source.generation||source.hash.isEmpty()){return false;}
  return dispatch([this,source=std::move(source),selectV2](std::stop_token stop){
    if(!host_||!host_->engine){throw Failure{{storage::ErrorCode::Unsupported,"No actual torrent owner is available"}};}
    host_->accepts(source);checkpoint(stop);
    if(host_->fileRecoveryRequired||storage::hasPendingOriginalCompaction(host_->lease)){
      throw Failure{{storage::ErrorCode::Conflict,"Recover pending storage changes before creating a hybrid companion"}};
    }
    // No caller-provided empty retained list: the real owner gathers its live
    // and uncertified states and validates the actual source removal fence.
    const auto key=take(host_->engine->createHybridCompanion(host_->lease,identity(source),
        selectV2?storage::OriginalSelectedHash::V2:storage::OriginalSelectedHash::V1,stop));
    ready(host_->query({}),tr("Stopped hybrid companion added: %1. Source data is preserved. Start one identity at a time.").arg(text(key.hex())));
  });
}

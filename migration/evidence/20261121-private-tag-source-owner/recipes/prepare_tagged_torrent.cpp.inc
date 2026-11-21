std::expected<std::unique_ptr<OwnedProfile::Prepared>,Error> OwnedProfile::prepareTorrentWithTagRates(
    const core::TorrentHash& hash,const Retained& retained,engine::ProfileTagRates& rates,
    std::optional<std::string_view> expectedConfiguration){
  if(impl_->ioWorker!=std::this_thread::get_id()||!impl_->session){return std::unexpected(Error{Code::Conflict,"Private tag preparation requires its original I/O worker/session"});}
  std::shared_ptr<ProfileSource> child;lt::info_hash_t expected;
  {
    auto access=bootstrapSourceAccess(*impl_->source->lease(),expectedConfiguration);if(!access){return std::unexpected(access.error());}
    auto captured=impl_->source->prepareChild(hash,retained.claims,retained.identities);if(!captured){return std::unexpected(captured.error());}child=*captured;
    auto full=child->previewTagTarget();if(!full){return std::unexpected(full.error());}expected=*full;
  }
  // Both SDK class preparation and complete-cohort capture run without FS gates.
  auto target=rates.prepareAttachment(hash,expected,*impl_->session);if(!target){return std::unexpected(target.error());}
  auto witness=rates.preparePrivateSource(hash,*impl_->session,**target);if(!witness){return std::unexpected(witness.error());}
  auto access=bootstrapSourceAccess(*impl_->source->lease(),expectedConfiguration);if(!access){return std::unexpected(access.error());}
  auto storage=child->prepareStorageWithTagSource(*witness,expected,*impl_->session);if(!storage){return std::unexpected(storage.error());}
  if(!storage->parameters().ti||storage->parameters().ti->info_hashes()!=expected){return std::unexpected(Error{Code::Conflict,"Prepared private metadata changed its full SDK identity"});}
  auto value=std::unique_ptr<Prepared>(new Prepared(std::make_unique<Prepared::Impl>(hash,child,std::move(*storage))));
  value->impl_->tagTarget=std::shared_ptr<engine::ProfilePreparedTagAttachment>(std::move(*target));
  auto inventory=storage::ConfigFile::load(*impl_->source->lease(),"downloads.config");if(!inventory){return std::unexpected(inventory.error());}auto library=core::readLibrary(inventory->document());if(!library){return std::unexpected(Error{Code::InvalidFormat,"Invalid I2P queue inventory"});}
  for(std::size_t n=0;n<library->downloads.size();++n){if(library->downloads[n].hash==hash){value->impl_->inventoryIndex=n;break;}}
  if(!entropy(value->impl_->admission)){return std::unexpected(Error{Code::Io,"I2P member admission entropy unavailable"});}return value;
}

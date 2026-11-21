std::expected<libtorrent::info_hash_t,storage::Error> ProfileSource::previewTagTarget()const{
  try{auto access=take(impl_->lease->sourceAccess());
    require(!impl_->session&&!impl_->mixed&&!impl_->pending&&!impl_->stamp&&!impl_->revoked&&impl_->namedCurrent(),ErrorCode::Conflict,"Current dedicated private child required before storage");
    require(impl_->anchor&&impl_->accepted&&impl_->memberSnapshot(true)==*impl_->accepted,ErrorCode::Conflict,"Private child source changed before tag preparation");
    auto state=take(core::Document::parse(impl_->accepted->state,SourceLimits).transform_error([](const auto&){return Error{ErrorCode::InvalidFormat,"Invalid private tag target source"};}));
    const auto info=take(storage::originalTorrentInfoSubject(state));const auto* record=state.find("info");const auto* version=record?record->find("meta version"):nullptr;
    const bool v2=version&&version->type==core::Node::Type::Integer&&version->integer==2;const bool v1=!v2||(record&&record->find("pieces"));
    libtorrent::info_hash_t expected;if(v1){expected.v1=libtorrent::hasher(info.data(),static_cast<int>(info.size())).final();}
    if(v2){expected.v2=libtorrent::hasher256(info.data(),static_cast<int>(info.size())).final();}
    require(impl_->hash.bytes().size()==20&&expected.has_v1()&&std::equal(impl_->hash.bytes().begin(),impl_->hash.bytes().end(),reinterpret_cast<const std::byte*>(expected.v1.data())),ErrorCode::Unsupported,"Private tag target requires its original canonical v1 identity");
    return expected;
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot capture private tag target before storage"});}
}
std::expected<engine::detail::ManagedStorage,storage::Error> ProfileSource::prepareStorageWithTagSource(
    const engine::PrivateTagRateSource& witness,const libtorrent::info_hash_t& expected,
    libtorrent::session_handle session){
  try{auto access=take(impl_->lease->sourceAccess());auto actual=take(previewTagTarget());
    require(actual==expected&&witness.current()&&session.native_handle(),ErrorCode::Conflict,"Actual private SDK/tag target required before storage");
    auto prepared=std::make_shared<PrivateRatePreparation>();prepared->source=witness;prepared->expected=expected;prepared->session=session;
    impl_->privateRates.store(std::move(prepared));return prepareStorage();
  }catch(const Rejected& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot bind private tag source before storage"});}
}

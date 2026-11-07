std::int64_t ManagedStorage::recheckScannedBytes() const {return impl_&&impl_->mapped?impl_->mapped->recheckScannedBytes():0;}
bool ManagedStorage::recheckSaveAttempted() const noexcept {return impl_&&impl_->purpose==ManagedStoragePurpose::RecheckReadOnly&&impl_->attempted;}
// Included only by managed_storage.cpp; private actual stopped-check publisher.
std::vector<std::uint8_t> ManagedStorage::recheckHashOutcomes() const {
  return impl_&&impl_->purpose==ManagedStoragePurpose::RecheckReadOnly&&impl_->mapped
      ?impl_->mapped->recheckHashOutcomes():std::vector<std::uint8_t>{};
}
std::expected<void,Error> ManagedStorage::checkpointRecheck(const storage::ProfileLease& lease,
    std::span<const std::uint8_t> verified,bool complete){
  try{
    require(impl_&&impl_->purpose==ManagedStoragePurpose::RecheckReadOnly&&!impl_->attempted,
        "No unconsumed read-only recheck checkpoint",ErrorCode::Conflict);auto& owner=*impl_;
    require(owner.categoryLease&&lease.sharesOwnership(*owner.categoryLease)&&lease.retain().has_value(),
        "Recheck checkpoint actual profile/OFD changed",ErrorCode::Conflict);
    done(validateExistingSnapshot());if(owner.mapped){done(owner.mapped->flush());}
    require(verified.size()==static_cast<std::size_t>(owner.params.ti->num_pieces())
        &&std::ranges::all_of(verified,[](auto bit){return bit<=1;})
        &&(!complete||std::ranges::all_of(verified,[](auto bit){return bit==1;})),
        "Invalid actual checked recheck piece map",ErrorCode::InvalidFormat);
    auto inventory=take(storage::ConfigFile::load(lease,"downloads.config"));
    require(inventory.recoverySource()==storage::RecoverySource::Primary,"Resolve recheck inventory fallback",ErrorCode::Conflict);
    const auto current=selected(inventory.document(),owner.hash);
    require(encoded(entryFor(inventory.document(),current,!inventory.document().find("downloads")))==owner.recheckInventoryRow,
        "Saved target intent/counters changed during recheck",ErrorCode::Conflict);
    const auto actual=take(owner.store.load(lease,owner.hash));
    require(!actual.recoveredBackup()&&actual.document().serialize()==owner.state.document().serialize(),
        "Saved state changed during recheck",ErrorCode::Conflict);
    std::vector<std::int64_t> counts(owner.layout.files.size());std::int64_t total=0;
    for(std::size_t i=0;i<owner.layout.files.size();++i){const auto& file=owner.layout.files[i];
      if(file.bytes){const auto first=file.offset/owner.layout.pieceLength,last=(file.offset+file.bytes-1)/owner.layout.pieceLength;
        for(auto piece=first;piece<=last;++piece){if(!verified[static_cast<std::size_t>(piece)]){continue;}
          const auto begin=piece*owner.layout.pieceLength,end=std::min(owner.layout.bytes,begin+owner.layout.pieceLength);
          counts[i]+=std::min(file.offset+file.bytes,end)-std::max(file.offset,begin);}}
      require(counts[i]>=0&&counts[i]<=file.bytes&&counts[i]<=std::numeric_limits<std::int64_t>::max()-total,
          "Checked recheck file bytes exceed geometry",ErrorCode::InvalidFormat);total+=counts[i];
    }
    auto resume=dictionary(owner.state.document().find("resume"));auto data=dictionary(resume.find("data"));
    std::string bits;bits.reserve(verified.size());for(const auto bit:verified){bits+=bit?'\1':'\0';}
    put(data,"resume data",stringValue(bits));put(data,"valid","i1e");put(data,"blocks","de");
    put(resume,"data",data.serialize());done(owner.state.replaceSection("resume",resume.serialize()));
    auto attributes=dictionary(owner.state.document().find("attributes"));put(attributes,"resumecomplete",complete?"i2e":"i1e");
    auto downloaded=dictionary(attributes.find("filedownloaded"));std::string list="l";for(const auto value:counts){list+=intValue(value);}put(downloaded,"downloaded",list+'e');
    put(attributes,"filedownloaded",downloaded.serialize());done(owner.state.replaceSection("attributes",attributes.serialize()));
    auto row=dictionary(&entryFor(inventory.document(),current,!inventory.document().find("downloads")));
    put(row,"completedbytes",intValue(total));
    const auto outcomes=recheckHashOutcomes();const bool allocated=std::ranges::all_of(owner.slots,[](const auto& slot){return slot.padding||slot.data.get()>=0;})
        &&std::ranges::all_of(outcomes,[](auto value){return value==1;});put(row,"allocated",allocated?"i1e":"i0e");
    if(current.legacyKey){put(inventory.document(),*current.legacyKey,row.serialize());}
    else{const auto* rows=inventory.document().find("downloads");std::string encodedRows="l";
      for(std::size_t i=0;i<rows->children.size();++i){encodedRows+=i==current.sourceIndex?row.serialize():encoded(rows->children[i]);}put(inventory.document(),"downloads",encodedRows+'e');}
    // Publication follows true handle flush/removal/weak-expiration in the
    // owner. No payload writes, completion names, counters, intent or history.
    done(validateExistingSnapshot());owner.attempted=true;
    done(owner.store.save(lease,owner.state));done(inventory.save(lease));return {};
  }catch(const Failure& error){return std::unexpected(error.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Recheck checkpoint may have saved; inspect actual state/inventory before retry"});}
}

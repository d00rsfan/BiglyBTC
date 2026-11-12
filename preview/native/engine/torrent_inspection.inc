// SPDX-License-Identifier: GPL-2.0-or-later
void PersistentSession::clearTorrentInspection() noexcept {if(impl_->onWorker()){impl_->inspectionWatch.clear();impl_->inspectionEvents.clear();impl_->inspectionAdmission=0;}}
std::expected<TorrentInspection,storage::Error> PersistentSession::inspectTorrent(
    const core::TorrentHash& hash,InspectionSection section,int firstPiece) const {
 if(!impl_->onWorker()||impl_->closed||impl_->poisoned||impl_->call||firstPiece<0){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Torrent inspection is unavailable"});}
 TorrentInspection out;out.hash=hash.hex();
 if(section!=InspectionSection::Console||impl_->inspectionWatch!=out.hash){impl_->inspectionWatch.clear();impl_->inspectionEvents.clear();impl_->inspectionAdmission=0;}
 const auto found=impl_->managed.find(out.hash);if(found==impl_->managed.end()){return out;}
 const auto& value=*found->second;const auto& status=value.status;
 if(section==InspectionSection::Console){
  if(impl_->inspectionWatch!=out.hash||impl_->inspectionAdmission!=status.admission){impl_->inspectionEvents.clear();impl_->inspectionWatch=out.hash;impl_->inspectionAdmission=status.admission;}
  out.events.assign(impl_->inspectionEvents.begin(),impl_->inspectionEvents.end());
 }else{impl_->inspectionWatch.clear();impl_->inspectionEvents.clear();impl_->inspectionAdmission=0;}
 out.admission=status.admission;out.retained=status.retained;out.checked=status.checked;out.forceStart=status.forceStart;out.paused=value.manualPaused;
 out.bytes=status.bytes;out.completed=status.completed;out.received=status.received;out.sent=status.sent;out.downloadRate=status.downloadRate;out.uploadRate=status.uploadRate;
 out.cumulativeReceived=status.cumulativeReceived;out.cumulativeSent=status.cumulativeSent;
 out.totalPieces=status.totalPieces;out.resumeDecision=status.originalResumeDecision;out.discarded=status.discardedBytes.value_or(0);out.hashFailed=status.hashFailedBytes.value_or(0);
 if(status.failure){out.error=status.failure->message;}
 if(!status.retained||value.stop||value.removing||!value.handle.is_valid()){return out;}
 try{
  const auto metadata=value.handle.torrent_file();
  if(metadata){out.name=metadata->name().substr(0,65536);out.pieceLength=metadata->piece_length();out.privateTorrent=metadata->priv();out.comment=metadata->comment().substr(0,65536);out.creator=metadata->creator().substr(0,4096);out.created=metadata->creation_date();
   const auto& hashes=value.storage.identity().full;
   constexpr char digits[]="0123456789ABCDEF";auto hex=[&](const auto& bytes){std::string result;for(auto byte:bytes){const auto b=std::to_integer<unsigned char>(byte);result+=digits[b>>4];result+=digits[b&15];}return result;};
   if(hashes.v1){out.v1=hex(*hashes.v1);}if(hashes.v2){out.v2=hex(*hashes.v2);}
  }
  const bool needPieces=section==InspectionSection::Files||section==InspectionSection::Map||section==InspectionSection::Pieces;
  const bool boundedPieces=out.totalPieces<=2000000;
  const auto sample=value.handle.status(needPieces&&boundedPieces?lt::torrent_handle::query_pieces:lt::torrent_handle::status_flags_t{});
  std::vector<std::uint8_t> observedPieces;
  const bool classified=status.checked&&sample.state!=lt::torrent_status::checking_files&&sample.state!=lt::torrent_status::checking_resume_data;
  if(needPieces&&boundedPieces&&classified&&sample.pieces.size()==out.totalPieces){observedPieces.resize(std::size_t(out.totalPieces));for(int n=0;n<out.totalPieces;++n){observedPieces[std::size_t(n)]=sample.pieces[lt::piece_index_t{n}]?1:0;}}
  if(needPieces&&!boundedPieces){out.truncated=true;}

  out.connections=sample.num_peers;out.seeds=sample.num_seeds;out.availability=sample.distributed_copies;out.activeSeconds=sample.active_duration.count();out.seedingSeconds=sample.seeding_duration.count();
  out.dht=(value.storage.mainlinePolicy().discover||value.storage.mainlinePolicy().announce)&&!out.privateTorrent;out.lsd=!bool(sample.flags&lt::torrent_flags::disable_lsd)&&!out.privateTorrent;out.trackersAllowed=metadata&&!metadata->trackers().empty();out.pex=!bool(sample.flags&lt::torrent_flags::disable_pex)&&!out.privateTorrent;
  if(section==InspectionSection::Options){out.connectionLimit=value.handle.max_connections();out.uploadSlots=value.handle.max_uploads();out.downloadLimit=value.handle.download_limit();out.uploadLimit=value.handle.upload_limit();}
  if(section==InspectionSection::Files&&classified&&!observedPieces.empty()){out.fileCompleted=value.storage.inspectFileProgress(observedPieces);}
  if(section==InspectionSection::Map||section==InspectionSection::Pieces){
   out.piecesKnown=classified&&!observedPieces.empty()&&observedPieces.size()==std::size_t(out.totalPieces);out.piecesFirst=std::min(firstPiece,out.totalPieces);
   const auto end=std::min<std::int64_t>(out.totalPieces,std::int64_t(out.piecesFirst)+65536);
   if(out.piecesKnown){out.pieces.assign(observedPieces.begin()+out.piecesFirst,observedPieces.begin()+end);}
   // Availability RPC allocates for all pieces; bound this before calling it.
   if(out.totalPieces<=1000000){std::vector<int> available;value.handle.piece_availability(available);if(std::size_t(end)<=available.size()){out.pieceAvailability.assign(available.begin()+out.piecesFirst,available.begin()+end);}}
   else{out.truncated=true;}
  }
  if((section==InspectionSection::Pieces||section==InspectionSection::Blocks)&&out.totalPieces<=1000000){
   std::vector<lt::partial_piece_info> queue;value.handle.get_download_queue(queue);
   for(const auto& piece:queue){if(out.activePieces.size()>=4096){out.truncated=true;break;}
    out.activePieces.push_back({static_cast<int>(piece.piece_index),piece.blocks_in_piece,piece.finished,piece.writing,piece.requested});
    // SDK blocks are borrowed: copy now, before another SDK call.
    if(section==InspectionSection::Blocks){for(int n=0;n<piece.blocks_in_piece;++n){if(out.blocks.size()>=16384){out.truncated=true;break;}const auto& b=piece.blocks[n];const auto peer=b.peer();out.blocks.push_back({static_cast<int>(piece.piece_index),n,int(b.state),int(b.bytes_progress),int(b.block_size),int(b.num_peers),peer.address().to_string()+":"+std::to_string(peer.port())});}}
   }
  }
  if(section==InspectionSection::Trackers){
   const auto trackers=value.handle.trackers();
   for(const auto& tracker:trackers){
    if(out.trackers.size()>=256){out.truncated=true;break;}bool route=false;
    for(const auto& endpoint:tracker.endpoints){for(const auto protocol:lt::all_versions){
     if(!metadata||!metadata->info_hashes().has(protocol)){continue;}
     if(out.trackers.size()>=256){out.truncated=true;break;}route=true;
     const auto& result=endpoint.info_hashes[protocol];const auto health=value.trackerHealth.routeSnapshot(tracker.url,endpoint.local_endpoint,protocol==lt::protocol_version::V1?1u:2u);
     TrackerInspection row;row.url=tracker.url;row.tier=tracker.tier;row.local=endpoint.local_endpoint.address().to_string()+":"+std::to_string(endpoint.local_endpoint.port());row.protocol=protocol==lt::protocol_version::V1?"v1":"v2";
     row.state=health.state;row.peers=health.peers;row.message=!health.detail.empty()?health.detail:!result.message.empty()?result.message:result.last_error?result.last_error.message():std::string{};
     row.seeds=result.scrape_complete;row.leechers=result.scrape_incomplete;row.completed=result.scrape_downloaded;row.failures=result.fails;
     if(result.next_announce!=lt::time_point32::min()){row.nextSeconds=int(std::max<std::int64_t>(0,lt::total_seconds(result.next_announce-lt::time_point_cast<lt::seconds32>(lt::clock_type::now()))));}
     out.trackers.push_back(std::move(row));
    }}
    if(!route){TrackerInspection row;row.url=tracker.url;row.tier=tracker.tier;out.trackers.push_back(std::move(row));}
   }
  }
  return out;
 }catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot inspect the retained torrent"});}
}

std::expected<void,storage::Error> PersistentSession::requestTorrentTracker(const core::TorrentHash& hash,std::uint64_t admission,bool scrape){
 if(!impl_->onWorker()||impl_->closed||impl_->poisoned||impl_->call||impl_->closingAdmission||!admission){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Tracker request is unavailable"});}
 const auto found=impl_->managed.find(hash.hex());if(found==impl_->managed.end()){return std::unexpected(storage::Error{storage::ErrorCode::Missing,"No retained torrent"});}
 const auto& v=*found->second;
 if(v.status.admission!=admission||!v.status.retained||!v.status.checked||v.status.failure||v.stop||v.removing||v.manualPaused||v.policyHeld||v.status.verificationHeld||!v.handle.is_valid()||(v.status.phase!=ManagedPhase::Downloading&&v.status.phase!=ManagedPhase::Seeding)){
  return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Torrent must be active and checked before a tracker request"});
 }
 try{if(v.handle.trackers().empty()){return std::unexpected(storage::Error{storage::ErrorCode::Missing,"Torrent has no configured trackers"});}if(scrape){v.handle.scrape_tracker();}else{v.handle.force_reannounce();}return {};}
 catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot request the torrent tracker"});}
}

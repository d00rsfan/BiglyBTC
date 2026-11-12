// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the retained session actor. Only the independent read/hash worker
// touches payloads; this actor alone validates policy and submits SDK writes.
core::FileMergeSettings mergeSettings;
detail::FileMergeIo mergeIo;
struct MergeDirection {
 core::MergeFile source,target;std::int64_t cursor=0,sourceProgress=-1,targetProgress=-1,sourceReceived=-1,targetReceived=-1,submitted=0;
 unsigned failures=0,verified=0;std::string state="Waiting for matching data";
};
struct MergePending {std::string direction;detail::MergeIoOwner source,target;int piece=0;std::int64_t bytes=0;int pieceBytes=0;Clock::time_point expiry{};std::vector<std::pair<int,int>> supplied;};
std::map<std::string,MergeDirection> mergeDirections;
std::map<std::uint64_t,MergePending> mergeReads,mergeWrites;
std::map<std::string,std::pair<std::uint64_t,std::vector<core::MergeFile>>> mergeIndex;
Clock::time_point mergeNextIndex{};std::uint64_t mergeJob=0;
std::int64_t mergeSubmitted=0;std::uint64_t mergeVerified=0,mergeRejected=0;bool mergeTruncated=false;
std::string mergeState="Waiting for eligible torrents";std::size_t mergeRotate=0;
static detail::MergeIoOwner mergeOwner(const Managed& value){return {value.status.hash.hex(),value.status.admission};}
Managed* mergeCurrent(const detail::MergeIoOwner& id){const auto at=managed.find(id.hash);return at!=managed.end()&&at->second->status.admission==id.admission?at->second.get():nullptr;}
bool mergeAllowed(const Managed& value,bool target)const{
 return value.handle.is_valid()&&value.status.retained&&value.status.checked&&!value.stop&&!value.removing&&!value.manualPaused
  &&!value.status.failure&&!value.status.recheck&&!value.status.verificationHeld&&!value.checkingHeld&&!value.policyHeld
  &&!value.allocationIntent&&value.storage.purpose()==detail::ManagedStoragePurpose::Download
  &&(value.status.phase==ManagedPhase::Downloading||(!target&&value.status.phase==ManagedPhase::Seeding));
}
void cancelMerge(Managed& value){const auto owner=mergeOwner(value);mergeIo.cancelOwner(owner);std::erase_if(mergeReads,[&](const auto& job){return job.second.source==owner||job.second.target==owner;});mergeNextIndex={};}
void rebuildMergeIndex(){
 if(Clock::now()<mergeNextIndex){return;}mergeNextIndex=Clock::now()+10s;
 std::vector<core::MergeFile> files;std::set<std::string> retained;mergeTruncated=false;std::size_t names=0;
 for(const auto& [key,ptr]:managed){if(files.size()>=200000||names>=16*1024*1024){mergeTruncated=true;break;}const auto& value=*ptr;if(!mergeAllowed(value,false)){continue;}retained.insert(key);
  auto at=mergeIndex.find(key);if(at==mergeIndex.end()||at->second.first!=value.status.admission){at=mergeIndex.insert_or_assign(key,std::make_pair(value.status.admission,value.storage.mergeFiles())).first;}
  for(const auto& source:at->second.second){if(files.size()>=200000||source.name.size()>16*1024*1024-names){mergeTruncated=true;break;}auto f=source;f.admission=value.status.admission;f.ownerComplete=value.status.selectedComplete;names+=f.name.size();files.push_back(std::move(f));}
 }
 std::erase_if(mergeIndex,[&](const auto& entry){return !retained.contains(entry.first);});
 auto plan=core::planFileMerges(files,mergeSettings);if(!plan){mergeState="File merge index unavailable";mergeDirections.clear();return;}mergeTruncated=mergeTruncated||plan->truncated;
 std::map<std::string,MergeDirection> next;
 for(const auto& pair:plan->pairs){for(const bool reverse:{false,true}){
  const auto& source=files[reverse?pair.second:pair.first];const auto& target=files[reverse?pair.first:pair.second];if(target.ownerComplete){continue;}
  const auto key=source.owner+":"+std::to_string(source.admission)+":"+std::to_string(source.index)+">"+target.owner+":"+std::to_string(target.admission)+":"+std::to_string(target.index);
  auto old=mergeDirections.find(key);if(old!=mergeDirections.end()){next.emplace(key,std::move(old->second));}else{next.emplace(key,MergeDirection{source,target});}
 }}mergeDirections=std::move(next);mergeState=mergeDirections.empty()?"No eligible matching files":"Matching files available";
}
bool mergePieceBusy(const detail::MergeIoOwner& target,int piece)const{
 const auto same=[&](const auto& p){return p.second.target==target&&p.second.piece==piece;};return std::ranges::any_of(mergeReads,same)||std::ranges::any_of(mergeWrites,same);
}
void advanceFileMerge(){
 // No new network queries or source publication; routing permissions do not
 // change as a consequence of local similarity.
 const bool allowed=mergeSettings.enabled&&!closingAdmission&&!closed&&!poisoned&&!counterFailure&&!storageError&&!transferError&&!removalLost&&session&&managedLease&&network;
 for(auto& completed:mergeIo.take()){
  const auto at=mergeReads.find(completed.id);if(at==mergeReads.end()){continue;}auto job=std::move(at->second);mergeReads.erase(at);auto direction=mergeDirections.find(job.direction);
  auto* source=mergeCurrent(job.source);auto* target=mergeCurrent(job.target);
  if(!completed.data){if(direction!=mergeDirections.end()&&completed.data.error().code!=detail::MergeIoFailure::Cancelled){if(completed.data.error().code==detail::MergeIoFailure::Hash){++direction->second.failures;}direction->second.state=completed.data.error().code==detail::MergeIoFailure::Hash?"Candidate failed target hash":"Source bytes unavailable";if(completed.data.error().code==detail::MergeIoFailure::Hash){++mergeRejected;}}continue;}
  if(!allowed||!source||!target||!mergeAllowed(*source,false)||!mergeAllowed(*target,true)||!bindingHealthy()){continue;}
  const auto sourceStatus=source->handle.status();if((sourceStatus.flags & lt::torrent_flags::paused)||(sourceStatus.state!=lt::torrent_status::downloading&&sourceStatus.state!=lt::torrent_status::finished&&sourceStatus.state!=lt::torrent_status::seeding)){continue;}
  const auto status=target->handle.status();if(status.flags & lt::torrent_flags::paused||status.state!=lt::torrent_status::downloading||target->handle.have_piece(lt::piece_index_t{job.piece})){continue;}
  if(!target->storage.mergedBytes()||job.bytes>std::numeric_limits<std::int64_t>::max()-*target->storage.mergedBytes()){continue;}
  // Exclude blocks already supplied by a peer before this local submission.
  // This is a scheduling observation, as in Java's writeBlock counter; the SDK
  // still decides whether later racing blocks need a write.
  std::vector<lt::partial_piece_info> queue;target->handle.get_download_queue(queue);std::vector<bool> occupied(std::size_t((job.pieceBytes+16383)/16384),false);
  for(const auto& part:queue){if(static_cast<int>(part.piece_index)!=job.piece){continue;}if(part.blocks_in_piece<0||std::size_t(part.blocks_in_piece)>occupied.size()){occupied.assign(occupied.size(),true);break;}for(int n=0;n<part.blocks_in_piece;++n){occupied[std::size_t(n)]=part.blocks[n].state==lt::block_info::writing||part.blocks[n].state==lt::block_info::finished;}break;}
  std::int64_t credit=0;for(const auto& [offset,length]:job.supplied){int at=offset;const int end=offset+length;while(at<end){const int take=std::min(end-at,16384-at%16384);if(!occupied[std::size_t(at/16384)]){credit+=take;}at+=take;}}
  if(!credit){continue;}job.bytes=credit;
  target->handle.add_piece(lt::piece_index_t{job.piece},completed.data->data());
  const auto credited=target->storage.recordMergedBytes(job.bytes);if(!credited){target->status.failure=credited.error();requestStop(*target);continue;}
  target->status.mergedBytes=target->storage.mergedBytes();mergeSubmitted+=job.bytes;
  if(direction!=mergeDirections.end()){direction->second.submitted+=job.bytes;direction->second.state="Submitted hash-verified local data";}
  job.expiry=Clock::now()+30s;mergeWrites.emplace(completed.id,std::move(job));
 }
 for(auto it=mergeWrites.begin();it!=mergeWrites.end();){auto& job=it->second;auto* target=mergeCurrent(job.target);bool erase=!target||!mergeAllowed(*target,true);
  if(target&&target->handle.is_valid()&&target->handle.have_piece(lt::piece_index_t{job.piece})){
   ++mergeVerified;if(auto at=mergeDirections.find(job.direction);at!=mergeDirections.end()){++at->second.verified;at->second.failures=0;at->second.state="Local piece verified by engine";}erase=true;
  }else if(Clock::now()>=job.expiry){if(auto at=mergeDirections.find(job.direction);at!=mergeDirections.end()){++at->second.failures;at->second.state="Engine piece confirmation timed out";}erase=true;}
  if(erase){it=mergeWrites.erase(it);}else{++it;}
 }
 if(!allowed){mergeState=mergeSettings.enabled?"File merge inactive":"File merge disabled";return;}
 rebuildMergeIndex();if(mergeDirections.empty()||mergeReads.size()+mergeWrites.size()>=32){return;}
 std::map<std::string,lt::torrent_status> samples;
 std::map<std::string,std::map<int,std::vector<unsigned char>>> finishedBlocks;std::size_t considered=0,submitted=0;const auto count=mergeDirections.size();auto it=mergeDirections.begin();std::advance(it,std::min(mergeRotate,count-1));
 const auto sample=[&](Managed& v)->const lt::torrent_status&{auto at=samples.find(v.status.hash.hex());if(at==samples.end()){at=samples.emplace(v.status.hash.hex(),v.handle.status(lt::torrent_handle::query_pieces)).first;}return at->second;};
 const auto availability=[&](Managed& source,std::int64_t offset,int pieceLength)->unsigned char{
  const auto& state=sample(source);const int piece=int(offset/pieceLength),block=int((offset%pieceLength)/16384);
  if(piece>=0&&piece<state.pieces.size()&&state.pieces[lt::piece_index_t{piece}]){return 4;}
  const auto key=source.status.hash.hex();auto at=finishedBlocks.find(key);if(at==finishedBlocks.end()){
   std::map<int,std::vector<unsigned char>> blocks;std::vector<lt::partial_piece_info> queue;source.handle.get_download_queue(queue);std::size_t total=0;
   for(const auto& p:queue){if(blocks.size()>=4096||p.blocks_in_piece<0||p.blocks_in_piece>16384||std::size_t(p.blocks_in_piece)>16384-total){break;}std::vector<unsigned char> states(std::size_t(p.blocks_in_piece));for(int n=0;n<p.blocks_in_piece;++n){states[std::size_t(n)]=static_cast<unsigned char>(p.blocks[n].state);}total+=states.size();blocks.emplace(static_cast<int>(p.piece_index),std::move(states));}at=finishedBlocks.emplace(key,std::move(blocks)).first;
  }const auto found=at->second.find(piece);return found!=at->second.end()&&block>=0&&std::size_t(block)<found->second.size()?found->second[std::size_t(block)]:0;
 };
 while(considered++<std::min<std::size_t>(count,16)&&submitted<8&&mergeReads.size()+mergeWrites.size()<32){
  if(it==mergeDirections.end()){it=mergeDirections.begin();}auto& [key,d]=*it++;++mergeRotate;if(mergeRotate>=count){mergeRotate=0;}
  auto* source=mergeCurrent({d.source.owner,d.source.admission});auto* target=mergeCurrent({d.target.owner,d.target.admission});if(!source||!target||!mergeAllowed(*source,false)||!mergeAllowed(*target,true)){continue;}
  if(d.sourceProgress!=source->status.completed||d.targetProgress!=target->status.completed||d.sourceReceived!=source->status.received||d.targetReceived!=target->status.received){d.failures=0;d.cursor=0;d.sourceProgress=source->status.completed;d.targetProgress=target->status.completed;d.sourceReceived=source->status.received;d.targetReceived=target->status.received;}
  if(d.failures>=3){continue;}if(source->status.totalPieces>2000000||target->status.totalPieces>2000000){d.state="Torrent exceeds merge piece-map limit";continue;}const auto& observed=sample(*target);if(observed.flags & lt::torrent_flags::paused||observed.state!=lt::torrent_status::downloading||observed.pieces.size()>2000000){continue;}
  const auto params=target->storage.parameters();if(!params.ti){continue;}const auto& ti=*params.ti;const auto& fs=ti.files();const auto begin=d.target.offset/ti.piece_length(),end=(d.target.offset+d.target.bytes-1)/ti.piece_length();
  if(d.cursor<begin){d.cursor=begin;}if(d.cursor>end){continue;}
  int piece=-1;for(int n=0;n<4096&&d.cursor<=end;++n){const auto candidate=int(d.cursor++);if(candidate>=observed.pieces.size()||observed.pieces[lt::piece_index_t{candidate}]||mergePieceBusy(mergeOwner(*target),candidate)){continue;}piece=candidate;break;}if(piece<0){continue;}
  const auto size=ti.piece_size(lt::piece_index_t{piece});if(size<=0||size>16*1024*1024){d.state="Piece exceeds local reuse limit";++d.failures;continue;}
  std::size_t writes=0;for(const auto& [id,w]:mergeWrites){(void)id;writes+=std::size_t(w.pieceBytes)*3;}if(std::size_t(size)*3+mergeIo.reservedBytes()+writes>64*1024*1024){--d.cursor;continue;}
  auto verifier=target->storage.prepareMergeVerifier(piece);if(!verifier){d.state=verifier.error().message;++d.failures;continue;}
  struct Part {detail::ManagedStorage::MergeRead read;std::size_t output=0;};std::vector<Part> parts;
  struct Span {Managed* owner;std::int64_t offset,length;std::size_t output;};std::vector<Span> spans;std::vector<std::pair<int,int>> supplied;std::int64_t reused=0;bool ready=true;
  const auto start=std::int64_t(piece)*ti.piece_length();std::int64_t offset=start;std::size_t done=0;
  while(done<std::size_t(size)&&spans.size()<128){const auto fi=fs.file_index_at_offset(offset);const auto within=offset-fs.file_offset(fi);auto length=std::min<std::int64_t>(size-std::int64_t(done),fs.file_size(fi)-within);auto* owner=target;auto readOffset=offset;
   if(static_cast<int>(fi)==d.target.index&&within<d.source.bytes){const auto sourceOffset=d.source.offset+within;length=std::min({length,d.source.bytes-within,std::int64_t(16384)-(sourceOffset%d.source.pieceLength)%16384,std::int64_t(16384)-(offset%ti.piece_length())%16384});
    const auto targetState=availability(*target,offset,ti.piece_length());if(targetState==lt::block_info::writing){--d.cursor;ready=false;break;}
    if(targetState<lt::block_info::writing&&availability(*source,sourceOffset,int(d.source.pieceLength))>=lt::block_info::finished){owner=source;readOffset=sourceOffset;reused+=length;
     if(!supplied.empty()&&supplied.back().first+supplied.back().second==int(done)){supplied.back().second+=int(length);}else{supplied.emplace_back(int(done),int(length));}}}
   if(length<=0){ready=false;break;}if(!spans.empty()&&spans.back().owner==owner&&spans.back().offset+spans.back().length==readOffset){spans.back().length+=length;}else{spans.push_back({owner,readOffset,length,done});}offset+=length;done+=std::size_t(length);
  }
  if(!ready||done!=std::size_t(size)||!reused){continue;}
  for(const auto& span:spans){auto read=span.owner->storage.prepareMergeRead(*managedLease,std::uint64_t(span.offset),std::uint32_t(span.length));if(!read){d.state=read.error().message;ready=false;break;}parts.push_back({std::move(*read),span.output});}if(!ready){continue;}
  const auto id=++mergeJob;MergePending pending{key,mergeOwner(*source),mergeOwner(*target),piece,reused,size,{},std::move(supplied)};
  detail::MergeIoJob job{id,{pending.source,pending.target},std::size_t(size)*3,[parts=std::move(parts),verify=std::move(*verifier),size](std::stop_token stop)->detail::MergeIoData{
   std::vector<char> data(std::size_t(size),0);for(const auto& part:parts){if(stop.stop_requested()){return std::unexpected(detail::MergeIoFailure{detail::MergeIoFailure::Cancelled,"Local reuse cancelled"});}auto read=part.read(stop);if(!read){return std::unexpected(detail::MergeIoFailure{detail::MergeIoFailure::Read,read.error().message});}std::copy(read->begin(),read->end(),data.begin()+std::ptrdiff_t(part.output));}
   if(stop.stop_requested()){return std::unexpected(detail::MergeIoFailure{detail::MergeIoFailure::Cancelled,"Local reuse cancelled"});}
   if(!verify(data)){return std::unexpected(detail::MergeIoFailure{detail::MergeIoFailure::Hash,"Candidate differs from target hash"});}return data;
  }};
  if(mergeIo.submit(std::move(job))){mergeReads.emplace(id,std::move(pending));d.state="Reading matching local data";++submitted;}else{--d.cursor;}
 }
}
FileMergeStatus fileMergeSnapshot()const{
 FileMergeStatus out;out.settings=mergeSettings;out.candidates=mergeDirections.size();out.pending=mergeReads.size()+mergeWrites.size();out.reservedBytes=mergeIo.reservedBytes();out.truncated=mergeTruncated;out.submittedBytes=mergeSubmitted;out.verifiedPieces=mergeVerified;out.rejectedPieces=mergeRejected;out.state=mergeState;
 for(const auto& [key,d]:mergeDirections){(void)key;if(out.rows.size()==256){out.truncated=true;break;}out.rows.push_back({d.source.owner,d.target.owner,d.source.index,d.target.index,d.submitted,d.verified,d.failures,d.state});}return out;
}

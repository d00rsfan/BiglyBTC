// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
constexpr std::uint32_t MergeMaximumPieceBytes=16*1024*1024;
struct MergeReadPart {
 std::shared_ptr<Descriptor> file,parent;
 fs::path path;struct stat identity{};storage::OriginalFileGeometry geometry{};
 storage::OriginalStorageKind kind=storage::OriginalStorageKind::Linear;
 std::shared_ptr<OriginalControl> control;std::optional<std::string> controlBytes;
 std::uint64_t within=0;std::size_t output=0,length=0;bool padding=false;
};
bool mergeNamedIdentity(const MergeReadPart& part){
 struct stat held{},named{},a{},b{};auto parent=parentDirectory(part.path.parent_path(),false);
 return parent.get()>=0&&::fstat(parent.get(),&a)==0&&::fstat(part.parent->get(),&b)==0&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino
  &&::fstat(part.file->get(),&held)==0&&::fstatat(part.parent->get(),part.path.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)==0
  &&sameFile(part.identity,held)&&sameFile(held,named)&&held.st_uid==part.identity.st_uid&&held.st_nlink==part.identity.st_nlink;
}
lt::sha256_hash mergeMerkle(std::vector<lt::sha256_hash> leaves,lt::sha256_hash pad={}){
 if(leaves.empty()){return {};}std::size_t width=1;while(width<leaves.size()){width*=2;}leaves.resize(width,pad);
 for(auto n=width;n>1;n/=2){for(std::size_t i=0;i<n;i+=2){lt::hasher256 hash;hash.update(leaves[i].data(),32);hash.update(leaves[i+1].data(),32);leaves[i/2]=hash.final();}}return leaves.front();
}
}
std::optional<std::int64_t> ManagedStorage::mergedBytes()const noexcept{
 if(!impl_){return {};}const auto* attrs=impl_->state.document().find("attributes");const auto* value=attrs?attrs->find("mergedata"):nullptr;
 if(value&&(value->type!=Node::Type::Integer||value->integer<0)){return {};}const auto prefix=value?value->integer:0;
 if(impl_->mergeDelta>std::numeric_limits<std::int64_t>::max()-prefix){return {};}return prefix+impl_->mergeDelta;
}
std::expected<void,Error> ManagedStorage::recordMergedBytes(std::int64_t amount){
 const auto current=mergedBytes();if(!current||amount<0||amount>std::numeric_limits<std::int64_t>::max()-*current){return std::unexpected(Error{ErrorCode::InvalidFormat,"Original merged-data counter cannot accept this local submission"});}impl_->mergeDelta+=amount;return {};
}
std::vector<core::MergeFile> ManagedStorage::mergeFiles()const{
 std::vector<core::MergeFile> result;if(!impl_||!impl_->params.ti||impl_->purpose==ManagedStoragePurpose::AllocateOnly){return result;}
 const auto flags=core::originalDownloadFlags(impl_->state.document().find("attributes"));if(!flags||(*flags&(0x10|0x200))){return result;}
 const auto& files=impl_->params.ti->files();result.reserve(std::min<std::size_t>(100000,static_cast<std::size_t>(files.num_files())));
 for(const auto index:files.file_range()){
  if(result.size()>=200000){break;}
  core::MergeFile f;f.owner=impl_->hash.hex();f.index=static_cast<int>(index);f.offset=files.file_offset(index);f.bytes=files.file_size(index);f.pieceLength=files.piece_length();f.padding=files.pad_file_at(index);
  const auto slot=std::ranges::find(impl_->slots,std::size_t(f.index),&Slot::index);if(slot==impl_->slots.end()){continue;}f.name=slot->actual.filename().string();f.skipped=slot->skipped;
  if(impl_->params.ti->info_hashes().has_v2()&&!f.padding&&f.bytes){const auto root=files.root(index);std::array<std::byte,32> hash{};std::copy_n(reinterpret_cast<const std::byte*>(root.data()),32,hash.begin());f.root=hash;}
  result.push_back(std::move(f));
 }return result;
}
std::expected<ManagedStorage::MergeRead,Error> ManagedStorage::prepareMergeRead(const storage::ProfileLease& lease,std::uint64_t offset,std::uint32_t bytes)const{
 try{
  require(impl_&&impl_->params.ti&&bytes&&bytes<=MergeMaximumPieceBytes,"Invalid local merge read",ErrorCode::InvalidFormat);
  struct stat profile{};require(lease.valid()&&::fstat(lease.directoryHandle(),&profile)==0&&profile.st_dev==impl_->profileDevice&&profile.st_ino==impl_->profileInode,"Local merge read belongs to a different profile",ErrorCode::UnsafePath);
  const auto& files=impl_->params.ti->files();const auto total=static_cast<std::uint64_t>(files.total_size());require(offset<=total&&bytes<=total-offset,"Local merge read exceeds torrent geometry",ErrorCode::InvalidFormat);
  auto access=take(lease.sourceAccess());auto retained=take(lease.retain());auto parts=std::make_shared<std::vector<MergeReadPart>>();std::size_t doneBytes=0;
  while(doneBytes<bytes){
   const auto index=files.file_index_at_offset(static_cast<std::int64_t>(offset));const auto within=offset-static_cast<std::uint64_t>(files.file_offset(index));
   const auto length=std::min<std::uint64_t>(bytes-doneBytes,static_cast<std::uint64_t>(files.file_size(index))-within);require(length&&parts->size()<128,"Local merge fragment limit",ErrorCode::Unsupported);
   MergeReadPart part;part.output=doneBytes;part.length=std::size_t(length);part.within=within;part.padding=files.pad_file_at(index);
   if(!part.padding){
    const auto slot=std::ranges::find(impl_->slots,std::size_t(static_cast<int>(index)),&Slot::index);require(slot!=impl_->slots.end()&&slot->data.get()>=0&&slot->parent.get()>=0,"Local merge source fragment is absent",ErrorCode::Missing);
    require(slot->geometry.torrentOffset==static_cast<std::uint64_t>(files.file_offset(index))&&slot->geometry.fileBytes==static_cast<std::uint64_t>(files.file_size(index)),"Local merge original/SDK fragment geometry differs",ErrorCode::Unsupported);
    part.file=std::make_shared<Descriptor>(::fcntl(slot->data.get(),F_DUPFD_CLOEXEC,3));part.parent=std::make_shared<Descriptor>(::fcntl(slot->parent.get(),F_DUPFD_CLOEXEC,3));require(part.file->get()>=0&&part.parent->get()>=0,"Cannot retain local merge read descriptors",ErrorCode::Io);
    part.path=(impl_->save/slot->actual).lexically_normal();part.identity=slot->identity;part.geometry=slot->geometry;part.kind=slot->kind;
    if(part.kind!=storage::OriginalStorageKind::Linear){part.control=take(OriginalControl::open(lease,impl_->hash.hex(),slot->index,part.kind,false));if(part.control){part.controlBytes=take(part.control->loadValidated(part.geometry));}}
   }parts->push_back(std::move(part));offset+=length;doneBytes+=std::size_t(length);
  }
  return MergeRead{[parts,retained,bytes](std::stop_token stop)->std::expected<std::vector<char>,Error>{try{
   require(retained->valid(),"Local merge profile ownership ended",ErrorCode::Conflict);std::vector<char> result(bytes);
   for(const auto& part:*parts){require(!stop.stop_requested(),"Local merge read cancelled",ErrorCode::Cancelled);if(part.padding){std::ranges::fill(std::span<char>(result).subspan(part.output,part.length),'\0');continue;}
    require(mergeNamedIdentity(part),"Local merge source pathname/identity changed",ErrorCode::Conflict);
    if(part.control){require(take(part.control->loadValidated(part.geometry))==part.controlBytes,"Local merge control changed before read",ErrorCode::Conflict);}
    auto io=take(storage::OriginalFileIo::open(part.file->get(),part.geometry,part.kind,part.controlBytes,{},false,false,static_cast<unsigned>(part.identity.st_nlink)));
    std::size_t done=0;while(done<part.length){require(!stop.stop_requested(),"Local merge read cancelled",ErrorCode::Cancelled);const auto count=std::min<std::size_t>(128*1024,part.length-done);auto data=std::as_writable_bytes(std::span<char>(result).subspan(part.output+done,count));const auto read=take(io.read(part.within+done,data));require(read.complete(),"Local merge source has missing or transient bytes",ErrorCode::Missing);done+=count;}
    require(mergeNamedIdentity(part),"Local merge source pathname changed during read",ErrorCode::Conflict);if(part.control){require(take(part.control->loadValidated(part.geometry))==part.controlBytes,"Local merge control changed during read",ErrorCode::Conflict);}
   }return result;
  }catch(const Failure& f){return std::unexpected(f.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Local merge read failed"});}}};
 }catch(const Failure& f){return std::unexpected(f.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare local merge read"});}
}
std::expected<ManagedStorage::MergeVerifier,Error> ManagedStorage::prepareMergeVerifier(int piece){
 try{
  require(impl_&&impl_->params.ti&&piece>=0&&piece<impl_->params.ti->num_pieces(),"Invalid merge target piece",ErrorCode::InvalidFormat);const auto& ti=*impl_->params.ti;const auto size=ti.piece_size(lt::piece_index_t{piece});require(size>0&&size<=int(MergeMaximumPieceBytes),"Local merge piece exceeds buffer limit",ErrorCode::Unsupported);
  if(ti.info_hashes().has_v1()&&impl_->selectedIdentity.selected==storage::OriginalSelectedHash::V1){const auto expected=ti.hash_for_piece(lt::piece_index_t{piece});return MergeVerifier{[expected,size](std::span<const char> data){return data.size()==std::size_t(size)&&lt::hasher(data.data(),int(data.size())).final()==expected;}};}
  require(ti.info_hashes().has_v2(),"Merge target has no supported piece hashes",ErrorCode::Unsupported);const auto& files=ti.files();const auto start=std::int64_t(piece)*ti.piece_length();const auto index=files.file_index_at_offset(start);require(!files.pad_file_at(index),"Merge target is virtual padding",ErrorCode::Unsupported);
  const auto fileStart=files.file_offset(index),fileBytes=files.file_size(index);require(start>=fileStart&&(start-fileStart)%ti.piece_length()==0,"Unaligned v2 merge piece",ErrorCode::Unsupported);const auto filePiece=(start-fileStart)/ti.piece_length();const auto realBytes=std::min<std::int64_t>(size,fileBytes-(start-fileStart));require(realBytes>0,"Empty v2 merge piece",ErrorCode::InvalidFormat);
  const auto root=files.root(index);lt::sha256_hash expected=root;
  if(fileBytes>ti.piece_length()){
   const auto* layers=impl_->state.document().find("piece layers");const auto* layer=layers?layers->find(root.to_string()):nullptr;const auto count=(fileBytes+ti.piece_length()-1)/ti.piece_length();require(layer&&layer->type==Node::Type::Bytes&&count<=1000000&&layer->bytes.size()==std::size_t(count)*32,"Authenticated v2 piece layer is unavailable",ErrorCode::Unsupported);
   const int key=static_cast<int>(index);if(!impl_->mergeLayerProofs.contains(key)){
    std::vector<lt::sha256_hash> hashes;hashes.reserve(std::size_t(count));for(std::int64_t n=0;n<count;++n){hashes.emplace_back(layer->bytes.data()+n*32);}
    lt::sha256_hash padding;for(int leaves=ti.piece_length()/16384;leaves>1;leaves/=2){lt::hasher256 pair;pair.update(padding.data(),32);pair.update(padding.data(),32);padding=pair.final();}
    impl_->mergeLayerProofs[key]=mergeMerkle(std::move(hashes),padding)==root;
   }require(impl_->mergeLayerProofs[key],"V2 merge piece layer root differs",ErrorCode::Conflict);expected=lt::sha256_hash(layer->bytes.data()+filePiece*32);
  }
  const auto pieceLength=ti.piece_length();return MergeVerifier{[expected,size,realBytes,fileBytes,pieceLength](std::span<const char> data){
   if(data.size()!=std::size_t(size)||!std::ranges::all_of(data.subspan(std::size_t(realBytes)),[](char c){return c==0;})){return false;}
   std::vector<lt::sha256_hash> leaves;for(std::int64_t at=0;at<realBytes;at+=16384){leaves.push_back(lt::hasher256(data.data()+at,int(std::min<std::int64_t>(16384,realBytes-at))).final());}
   std::size_t width=std::size_t(pieceLength/16384);if(fileBytes<pieceLength){width=1;while(width<leaves.size()){width*=2;}}leaves.resize(width);return mergeMerkle(std::move(leaves))==expected;
  }};
 }catch(const Failure& f){return std::unexpected(f.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare local merge target verifier"});}
}

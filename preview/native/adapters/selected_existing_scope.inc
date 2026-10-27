// Included by torrent_intake.cpp after verified_existing_scope.inc. No payload
// content is invented and no control recovery/publication occurs in this code.
std::expected<SelectedExistingScope,Error> SelectedExistingScope::decode(const Node& node){
  const auto* version=node.find("version"),*digest=node.find("digest");
  if(node.type!=Node::Type::Dictionary||node.children.size()!=4||!version||version->type!=Node::Type::Integer||version->integer!=1
      ||!digest||digest->type!=Node::Type::Bytes||digest->bytes.size()!=64||!std::ranges::all_of(digest->bytes,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');})){
    return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid versioned selected-existing scope digest"});}
  return SelectedExistingScope(std::string(digest->bytes));
}
std::expected<SelectedExistingScope,Error> SelectedExistingScope::capture(const ProfileLease& lease,
    const core::Document& source,const compat::OriginalLayout& layout,const OriginalTorrentIdentity& identity,
    std::span<const compat::FilePolicy> policies,std::span<const OriginalStorageKind> kinds,
    std::span<const fs::path> paths){
  try{
    require(lease.valid()&&layout.pieceLength>0&&layout.bytes>0&&layout.files.size()<=4096&&!layout.files.empty()&&policies.size()==layout.files.size()
        &&kinds.size()==layout.files.size()&&paths.size()==layout.files.size(),ErrorCode::InvalidFormat,"Invalid bounded selected-existing domain");
    const auto goal=core::existingPieceGoal(layout,policies,core::ExistingVerificationGoal::SelectedFiles,
        static_cast<std::size_t>(layout.bytes/layout.pieceLength+(layout.bytes%layout.pieceLength!=0)));
    require(goal.has_value(),ErrorCode::InvalidFormat,"Invalid selected-existing piece goal");
    const fs::path profile=profilePath(lease);std::set<fs::path> unique;std::vector<std::uint64_t> maxima;std::string types="l",choices="l";
    for(std::size_t n=0;n<paths.size();++n){const auto& file=layout.files[n];const auto kind=kinds[n];
      const auto maximum=take(originalMaximumPhysicalSize({static_cast<std::uint64_t>(file.offset),static_cast<std::uint64_t>(file.bytes),static_cast<std::uint32_t>(layout.pieceLength)},kind));maxima.push_back(maximum);
      const auto code=kind==OriginalStorageKind::Linear?'L':kind==OriginalStorageKind::Compact?'C':kind==OriginalStorageKind::Reordered?'R':'X';types+=b(std::string(1,code));
      choices+="l"+i(policies[n].skipped?1:0)+i(policies[n].priority)+'e';if(file.padding){require(paths[n].empty(),ErrorCode::InvalidFormat,"Virtual padding cannot own selected-existing data");continue;}
      pathShape(paths[n]);require(!existing_scope::inside(paths[n],profile)&&!existing_scope::inside(profile,paths[n])&&unique.insert(paths[n]).second,ErrorCode::UnsafePath,"Selected-existing payload overlaps profile or aliases a real index");
      for(const auto& part:paths[n].relative_path()){require(part!=".git"&&!part.native().starts_with(".BiglyBTC"),ErrorCode::UnsafePath,"Selected-existing path overlaps reserved data");}
    }
    for(const auto& path:unique){for(auto ancestor=path.parent_path();ancestor!=ancestor.root_path()&&!ancestor.empty();ancestor=ancestor.parent_path()){require(!unique.contains(ancestor),ErrorCode::UnsafePath,"Selected-existing file is another file's ancestor");}}
    types+='e';choices+='e';auto attrs=parse("de");put(attrs,"storetypes",types);
    const auto storage=take(resolveOriginalStorageTypes(lease,identity.originalKey,layout,attrs.root()));
    require(!storage.unsupportedConversion&&std::ranges::equal(storage.actual,kinds),ErrorCode::Conflict,"Selected-existing physical kinds differ from original controls");
    std::string controls="l";existing_scope::Dirs controlDirs;std::size_t controlBytes=0;
    const auto controlPath=profile/"active"/identity.originalKey.hex();
    // An absent control directory grants nothing. Creating the ordinary active
    // state directory during intake cannot change the meaning of absent maps.
    auto controlParent=fileop::directory(controlPath,false,true);
    std::map<std::string,std::string,std::less<>> candidates;
    if(controlParent.get()>=0){
      auto observed=existing_scope::parent(controlPath,[&](const fs::path& path,const struct stat& value){
        controlDirs.emplace(path.string(),std::pair{existing_scope::number(value.st_dev),existing_scope::number(value.st_ino)});
        require(controlDirs.size()<=8192,ErrorCode::Unsupported,"Selected control ancestor bound");});
      struct stat a{},bdir{};require(::fstat(observed.get(),&a)==0&&::fstat(controlParent.get(),&bdir)==0&&fileop::identity(a,bdir),ErrorCode::Conflict,"Selected control directory changed");
      for(const auto& marker:storage.markers){
        const auto value=read(controlParent.get(),marker.leaf,16*1024*1024);require(value.has_value(),ErrorCode::Conflict,"Selected original control disappeared");
        require(value->info.st_dev==marker.device&&value->info.st_ino==marker.inode&&static_cast<std::uint64_t>(value->info.st_size)==marker.bytes
            &&value->info.st_mtim.tv_sec==marker.modifiedSeconds&&value->info.st_mtim.tv_nsec==marker.modifiedNanoseconds
            &&value->info.st_ctim.tv_sec==marker.changedSeconds&&value->info.st_ctim.tv_nsec==marker.changedNanoseconds,ErrorCode::Conflict,"Selected control changed during capture");
        require(value->bytes.size()<=MaximumInventory-controlBytes,ErrorCode::Unsupported,"Selected control byte bound");controlBytes+=value->bytes.size();
        controls+="d5:index"+i(static_cast<std::int64_t>(marker.index))+"4:leaf"+b(marker.leaf)+"5:stamp"+existing_scope::stamp(value->info)+"6:digest"+b(fileop::digest(value->bytes))+'e';candidates.emplace(marker.leaf,value->bytes);
      }
    }
    require(storage.markers.empty()||controlParent.get()>=0,ErrorCode::Conflict,"Selected controls lost their directory");
    for(std::size_t n=0;n<kinds.size();++n){if(kinds[n]==OriginalStorageKind::Linear||layout.files[n].padding){continue;}
      const auto leaf=originalControlLeaf(n,kinds[n]);const auto primary=candidates.find(leaf),backup=candidates.find(leaf+".bak");
      const OriginalFileGeometry geometry{static_cast<std::uint64_t>(layout.files[n].offset),static_cast<std::uint64_t>(layout.files[n].bytes),static_cast<std::uint32_t>(layout.pieceLength)};
      const auto valid=[&](auto candidate){return candidate!=candidates.end()&&OriginalFileIo::validateControl(geometry,kinds[n],candidate->second).has_value();};
      const auto selected=valid(primary)?primary:valid(backup)?backup:candidates.end();
      require(selected!=candidates.end()||(primary==candidates.end()&&backup==candidates.end()),ErrorCode::InvalidFormat,"No valid original selected control candidate");
      if(selected!=candidates.end()){const auto document=parse(selected->second);if(const auto* redo=document.find("BiglyBTC.reorderedCompletion")){
        require(redo->type==Node::Type::Dictionary&&redo->children.empty(),ErrorCode::Conflict,"Selected read-only verification cannot recover native completion redo");}}
    }
    controls+='e';std::string dirs="l";for(const auto& [path,id]:controlDirs){dirs+="l"+b(path)+i(id.first)+i(id.second)+'e';}dirs+='e';
    const auto rawInfo=take(originalTorrentInfoSubject(source));std::string layers;if(const auto* value=source.find("piece layers")){layers=existing_scope::encoded(*value);}
    const MetadataSubject subject{source,layout,identity.originalKey,0,identity};
    const auto observed=intakeDestinationSnapshot(subject,paths,maxima);
    auto encoded="d4:dirs"+dirs+"5:files"+b(observed)+"8:controls"+controls+"7:choices"+choices+"5:kinds"+types
        +"4:info"+b(fileop::digest(rawInfo))+"6:layers"+b(fileop::digest(layers))+"8:selected"+b(identity.originalKey.hex())
        +"5:swarm"+i(identity.selected==OriginalSelectedHash::V1?1:2)+"2:v1"+b(existing_scope::bytes(identity.full.v1))+"2:v2"+b(existing_scope::bytes(identity.full.v2))+"7:versioni1ee";
    return SelectedExistingScope(fileop::digest(encoded));
  }catch(const Failure& error){return std::unexpected(error.error);}
  catch(const fileop::Failure& error){return std::unexpected(error.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot capture selected-existing scope"});}
}
std::expected<void,Error> SelectedExistingScope::validate(const ProfileLease& lease,const core::Document& source,
    const compat::OriginalLayout& layout,const OriginalTorrentIdentity& identity,
    std::span<const compat::FilePolicy> policies,std::span<const OriginalStorageKind> kinds,std::span<const fs::path> paths)const{
  const auto current=capture(lease,source,layout,identity,policies,kinds,paths);if(!current){return std::unexpected(current.error());}
  if(current->digest()!=digest()){return std::unexpected(Error{ErrorCode::Conflict,"Approved selected-existing source/map/policy/control observations changed"});}return {};
}

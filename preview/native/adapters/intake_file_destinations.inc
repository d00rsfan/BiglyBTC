// Included by torrent_intake.cpp after MetadataSubject. Pure value projection;
// every filesystem/claim check remains in the held-lease transaction below.
core::TorrentIntakeFileDestinations initialFileDestinations(const MetadataSubject& source) {
  core::TorrentIntakeFileDestinations result;const auto* properties=source.torrent.find("azureus_private_properties");
  if(!properties){return result;}
  require(properties->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid original initial linkage properties");
  const auto* map=properties->find("initial_linkage");std::optional<EncodedDocument> compressed;
  if(const auto* zipped=properties->find("initial_linkage2")){
    require(zipped->type==Node::Type::Bytes&&zipped->bytes.size()<=MaximumInventory,ErrorCode::InvalidFormat,"Invalid bounded original compressed linkage");
    auto value=EncodedDocument::parse(std::string(zipped->bytes),compatibleConfigLimits());
    require(value&&value->compressed(),ErrorCode::InvalidFormat,"Original compressed linkage needs valid bounded gzip");compressed.emplace(std::move(*value));map=&compressed->document().root();
  }
  if(!map){return result;}
  require(map->type==Node::Type::Dictionary&&map->children.size()%2==0&&map->children.size()<=8192,ErrorCode::InvalidFormat,"Invalid bounded original initial linkage map");
  for(std::size_t n=0;n<map->children.size();n+=2){const auto& key=map->children[n];const auto& value=map->children[n+1];std::uint32_t index=0;
    require(key.type==Node::Type::Bytes&&!key.bytes.empty()&&key.bytes.size()<=5,ErrorCode::InvalidFormat,"Invalid bounded original initial linkage index");
    const auto parsed=std::from_chars(key.bytes.data(),key.bytes.data()+key.bytes.size(),index);
    require(key.type==Node::Type::Bytes&&parsed.ec==std::errc{}&&parsed.ptr==key.bytes.data()+key.bytes.size()&&std::to_string(index)==key.bytes
        &&value.type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid original initial linkage index/path");
    result.push_back({index,std::string(value.bytes)});
  }
  std::ranges::sort(result,{},&core::TorrentIntakeFileDestination::index);
  require(core::validTorrentIntakeFileDestinations(result),ErrorCode::UnsafePath,"Original initial linkage needs unique safe absolute targets");
  for(const auto& value:result){require(value.index<source.layout.files.size()&&!source.layout.files[value.index].padding,ErrorCode::InvalidFormat,"Original initial linkage does not name a real file index");}
  return result;
}
fs::path projectedIntakeTarget(const core::TorrentIntakeFileDestination& value,std::optional<std::string_view> conversions) {
  require(core::validTorrentIntakeFilePath(value.path),ErrorCode::UnsafePath,"File destination must be a safe absolute path");
  const fs::path path(value.path);if(!conversions){return path;}
  const auto leaf=compat::originalFilenameComponent(path.filename().string(),*conversions);
  require(leaf.has_value(),ErrorCode::UnsafePath,"File destination conversion is unsupported or unsafe");return path.parent_path()/ *leaf;
}
std::vector<fs::path> physicalIntakePaths(const compat::OriginalLayout& layout,const fs::path& directory,std::string_view leaf,
    const core::TorrentIntakeFileDestinations& values,std::optional<std::string_view> conversions) {
  require(core::validTorrentIntakeFileDestinations(values),ErrorCode::InvalidFormat,"Invalid indexed file destinations");std::vector<fs::path> paths(layout.files.size());
  for(std::size_t n=0;n<layout.files.size();++n){const auto& file=layout.files[n];if(!file.padding){paths[n]=directory/(layout.multifile?fs::path(leaf)/file.path:fs::path(leaf));}}
  for(const auto& value:values){require(value.index<layout.files.size()&&!layout.files[value.index].padding,ErrorCode::InvalidFormat,"File destination does not name a real original index");paths[value.index]=projectedIntakeTarget(value,conversions);}
  std::set<fs::path> actual;
  for(const auto& path:paths){if(path.empty()){continue;}require(actual.insert(path).second,ErrorCode::Conflict,"Two original indices have the same file destination");}
  for(const auto& path:actual){for(auto ancestor=path.parent_path();!ancestor.empty()&&ancestor!=ancestor.root_path();ancestor=ancestor.parent_path()){require(!actual.contains(ancestor),ErrorCode::Conflict,"One file destination is another file's parent");}}
  return paths;
}
std::string encodeFileDestinations(const core::TorrentIntakeFileDestinations& values) {
  std::string result="l";for(const auto& value:values){result+="d5:index"+i(value.index)+"4:path"+b(value.path)+'e';}return result+'e';
}
core::TorrentIntakeFileDestinations decodeFileDestinations(const Node& list) {
  require(list.type==Node::Type::List&&list.children.size()<=4096,ErrorCode::InvalidFormat,"Invalid destination journal list");core::TorrentIntakeFileDestinations result;
  for(const auto& item:list.children){const auto* index=item.find("index");const auto* path=item.find("path");
    require(item.type==Node::Type::Dictionary&&item.children.size()==4&&index&&index->type==Node::Type::Integer&&index->integer>=0&&index->integer<100000
        &&path&&path->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid destination journal entry");result.push_back({static_cast<std::uint32_t>(index->integer),std::string(path->bytes)});
  }
  require(core::validTorrentIntakeFileDestinations(result),ErrorCode::InvalidFormat,"Invalid/duplicate/unordered destination journal entries");return result;
}
// Transaction-only observation. This never becomes an engine/RO grant. Missing
// components are recorded at their first authenticated boundary, not created.
template<class T> std::int64_t destinationStatNumber(T value){require(std::in_range<std::int64_t>(value),ErrorCode::Unsupported,"Destination stat exceeds signed journal bound");return static_cast<std::int64_t>(value);}
std::string intakeDestinationSnapshot(const MetadataSubject& subject,std::span<const fs::path> paths,std::span<const std::uint64_t> maxima={}){
  try{
    require(paths.size()==subject.layout.files.size(),ErrorCode::InvalidFormat,"Destination snapshot differs from original indices");
    require(maxima.empty()||maxima.size()==paths.size(),ErrorCode::InvalidFormat,"Destination physical bound count differs");
    std::map<std::string,std::pair<std::int64_t,std::int64_t>,std::less<>> directories;std::string records="l";
    for(std::size_t index=0;index<paths.size();++index){const auto& path=paths[index];if(subject.layout.files[index].padding){require(path.empty(),ErrorCode::InvalidFormat,"Virtual padding cannot have a destination snapshot");continue;}pathShape(path);
      Descriptor current(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));struct stat root{};require(current.get()>=0&&::fstat(current.get(),&root)==0,ErrorCode::Io,"Cannot inspect destination root");
      fs::path prefix="/",missing;struct stat last=root;
      const auto observeDirectory=[&](const fs::path& name,const struct stat& value){const auto identity=std::pair{destinationStatNumber(value.st_dev),destinationStatNumber(value.st_ino)};const auto [found,inserted]=directories.emplace(name.string(),identity);require(inserted||found->second==identity,ErrorCode::Conflict,"Destination ancestor changed during observation");require(directories.size()<=8192,ErrorCode::Unsupported,"Destination ancestor bound");};
      observeDirectory(prefix,root);
      for(const auto& component:path.parent_path().relative_path()){
        struct stat named{};if(::fstatat(current.get(),component.c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0){require(errno==ENOENT,ErrorCode::UnsafePath,"Destination ancestor is inaccessible");missing=prefix/component;break;}
        require(S_ISDIR(named.st_mode),ErrorCode::UnsafePath,"Destination ancestor is not a plain directory");Descriptor next(::openat(current.get(),component.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat held{},again{};
        require(next.get()>=0&&::fstat(next.get(),&held)==0&&::fstatat(current.get(),component.c_str(),&again,AT_SYMLINK_NOFOLLOW)==0&&fileop::identity(held,named)&&fileop::identity(again,named),ErrorCode::Conflict,"Destination ancestor changed while opening");prefix/=component;observeDirectory(prefix,held);last=held;current=std::move(next);
      }
      std::string observation;
      if(!missing.empty()){require(last.st_uid==::geteuid(),ErrorCode::UnsafePath,"Missing destination requires an owned ancestor");observation="d7:missing"+b(missing.string())+'e';}
      else{
        struct stat named{};
        if(::fstatat(current.get(),path.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0){require(errno==ENOENT&&last.st_uid==::geteuid(),ErrorCode::UnsafePath,"Missing destination is not authorized by its parent");observation="d7:missing"+b(path.string())+'e';}
        else{
          require(S_ISREG(named.st_mode)&&named.st_uid==::geteuid()&&named.st_nlink==1&&named.st_size>=0&&static_cast<std::uint64_t>(named.st_size)<=(maxima.empty()?static_cast<std::uint64_t>(subject.layout.files[index].bytes):maxima[index]),ErrorCode::UnsafePath,"Existing indexed target is not safe ordinary data");
          Descriptor file(::openat(current.get(),path.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat held{},again{};
          require(file.get()>=0&&::fstat(file.get(),&held)==0&&::fstatat(current.get(),path.filename().c_str(),&again,AT_SYMLINK_NOFOLLOW)==0&&fileop::unchanged(held,named)&&fileop::unchanged(again,named),ErrorCode::Conflict,"Destination leaf changed while observing");
          observation="d5:stampl"+i(destinationStatNumber(held.st_dev))+i(destinationStatNumber(held.st_ino))+i(destinationStatNumber(held.st_size))+i(destinationStatNumber(held.st_mode))+i(destinationStatNumber(held.st_uid))+i(destinationStatNumber(held.st_nlink))+i(destinationStatNumber(held.st_mtim.tv_sec))+i(destinationStatNumber(held.st_mtim.tv_nsec))+i(destinationStatNumber(held.st_ctim.tv_sec))+i(destinationStatNumber(held.st_ctim.tv_nsec))+"ee";
        }
      }
      records+="d5:index"+i(static_cast<std::int64_t>(index))+"4:path"+b(path.string())+"8:snapshot"+observation+'e';require(records.size()<=4*1024*1024,ErrorCode::Unsupported,"Destination snapshot byte bound");
    }
    records+='e';std::string dirs="l";for(const auto& [name,id]:directories){dirs+="l"+b(name)+i(id.first)+i(id.second)+'e';}dirs+='e';
    const auto rawInfo=take(originalTorrentInfoSubject(subject.torrent));return fileop::digest("d4:dirs"+dirs+"5:files"+records+"4:info"+b(fileop::digest(rawInfo))+"8:selected"+b(subject.hash.hex())+'e');
  }catch(const fileop::Failure& error){throw Failure{error.error};}
}

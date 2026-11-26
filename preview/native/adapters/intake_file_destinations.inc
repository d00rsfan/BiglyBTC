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
  const auto path = pathFromUtf8(value.path);if(!conversions){return path;}
  const auto leaf=compat::originalFilenameComponent(pathToUtf8(path.filename()),*conversions);
  require(leaf.has_value(),ErrorCode::UnsafePath,"File destination conversion is unsupported or unsafe");return path.parent_path()/pathFromUtf8(*leaf);
}
std::vector<fs::path> physicalIntakePaths(const compat::OriginalLayout& layout,const fs::path& directory,std::string_view leaf,
    const core::TorrentIntakeFileDestinations& values,std::optional<std::string_view> conversions) {
  require(core::validTorrentIntakeFileDestinations(values),ErrorCode::InvalidFormat,"Invalid indexed file destinations");std::vector<fs::path> paths(layout.files.size());
  for(std::size_t n=0;n<layout.files.size();++n){const auto& file=layout.files[n];if(!file.padding){paths[n]=directory/(layout.multifile?pathFromUtf8(leaf)/pathFromUtf8(file.path):pathFromUtf8(leaf));}}
  for(const auto& value:values){require(value.index<layout.files.size()&&!layout.files[value.index].padding,ErrorCode::InvalidFormat,"File destination does not name a real original index");paths[value.index]=projectedIntakeTarget(value,conversions);}
  std::set<fs::path, detail::FilesystemPathLess> actual;
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
std::string intakeDestinationSnapshot(const MetadataSubject& subject,std::span<const fs::path> paths) {
  return take(captureIntakeDestinationSnapshot(subject.torrent,subject.layout,subject.hash,paths));
}

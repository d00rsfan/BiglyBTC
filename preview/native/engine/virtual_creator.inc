// Original BYO descriptor support. Included after ordinary source scan/open helpers.
// No placeholder files or physical aliases are created; logical names and opened
// immutable physical observations remain separate throughout creation.
Descriptor openEntry(int root,const Entry& file,const fs::path& path){
  if(file.physical.empty()) return openRelative(root,file.relative,path);
  auto parent=openParent(file.physical);
  if(!file.physicalParent||!sameIdentity(*file.physicalParent,parent.identity)) fail(CreateErrorCode::Changed,path,"Mapped source parent changed");
  return openChild(parent.directory.get(),parent.leaf,path);
}
bool logicalLess(const Entry& a,const Entry& b){
  std::string_view x=a.relative,y=b.relative;
  for(;;){const auto xs=x.find('/'),ys=y.find('/');const auto xc=x.substr(0,xs),yc=y.substr(0,ys);
    if(xc!=yc) return std::lexicographical_compare(xc.begin(),xc.end(),yc.begin(),yc.end(),[](char p,char q){return static_cast<unsigned char>(p)<static_cast<unsigned char>(q);});
    if(xs==x.npos||ys==y.npos) return xs==x.npos&&ys!=y.npos;
    x.remove_prefix(xs+1);y.remove_prefix(ys+1);
  }
}
std::vector<std::uint16_t> virtualUtf16(std::string_view value){
  std::vector<std::uint16_t> result;for(std::size_t i=0;i<value.size();){const auto head=static_cast<unsigned char>(value[i++]);std::uint32_t code=head;unsigned rest=0;
    if(head>=0xf0){code=head&7;rest=3;}else if(head>=0xe0){code=head&15;rest=2;}else if(head>=0xc0){code=head&31;rest=1;}
    while(rest--) code=(code<<6)|(static_cast<unsigned char>(value[i++])&63);
    if(code<0x10000) result.push_back(static_cast<std::uint16_t>(code));else{code-=0x10000;result.push_back(static_cast<std::uint16_t>(0xd800+(code>>10)));result.push_back(static_cast<std::uint16_t>(0xdc00+(code&1023)));}}
  return result;
}
bool logicalUtf16Less(const Entry& a,const Entry& b){
  std::string_view x=a.relative,y=b.relative;for(;;){const auto xs=x.find('/'),ys=y.find('/');const auto xc=x.substr(0,xs),yc=y.substr(0,ys);
    if(xc!=yc){const auto u=virtualUtf16(xc),v=virtualUtf16(yc);return std::lexicographical_compare(u.begin(),u.end(),v.begin(),v.end());}
    if(xs==x.npos||ys==y.npos) return xs==x.npos&&ys!=y.npos;x.remove_prefix(xs+1);y.remove_prefix(ys+1);}
}
std::size_t descriptorKeys(const core::Node& item,std::string_view name){
  if(item.type!=core::Node::Type::Dictionary) return 0;std::size_t count=0;
  for(std::size_t i=0;i+1<item.children.size();i+=2) if(item.children[i].bytes==name) ++count;
  return count;
}
Inventory scanDescriptor(int descriptor,const Operation& operation){
  constexpr std::size_t MaximumDescriptorBytes=4*1024*1024;
  const auto observed=describe(descriptor,operation.options.source);
  if(!S_ISREG(observed.mode)||observed.size<0||static_cast<std::uint64_t>(observed.size)>MaximumDescriptorBytes) fail(CreateErrorCode::Options,operation.options.source,"Descriptor must be a bounded regular file");
  std::string raw(static_cast<std::size_t>(observed.size),'\0');std::size_t offset=0;
  while(offset<raw.size()){
    operation.cancelled();const auto count=::pread(descriptor,raw.data()+offset,raw.size()-offset,static_cast<off_t>(offset));
    if(count<0&&errno==EINTR) continue;
    if(count<0) fail(CreateErrorCode::Read,operation.options.source,"Cannot read layout descriptor",errno);
    if(count==0) fail(CreateErrorCode::Changed,operation.options.source,"Descriptor became short");
    offset+=static_cast<std::size_t>(count);
  }
  if(!same(observed,describe(descriptor,operation.options.source))) fail(CreateErrorCode::Changed,operation.options.source,"Descriptor changed during read");
  auto limits=storage::compatibleConfigLimits();limits.inputBytes=limits.stringBytes=MaximumDescriptorBytes;
  auto parsed=core::Document::parse(std::move(raw),limits);
  const auto* entries=parsed?parsed->find("file_map"):nullptr;
  if(!entries||entries->type!=core::Node::Type::List||entries->children.empty()||entries->children.size()>CreateMaximumFiles) fail(CreateErrorCode::Options,operation.options.source,"Invalid bounded original file_map descriptor");
  // Validate the entire known-key schema before opening ANY physical target.
  if(descriptorKeys(parsed->root(),"file_map")!=1) fail(CreateErrorCode::Options,operation.options.source,"Duplicate descriptor file_map key");
  for(const auto& item:entries->children){
    if(descriptorKeys(item,"logical_path")!=1||descriptorKeys(item,"target")!=1) fail(CreateErrorCode::Options,operation.options.source,"Duplicate or missing descriptor mapping key");
    const auto* logical=item.find("logical_path");const auto* target=item.find("target");
    if(!logical||logical->type!=core::Node::Type::List||logical->children.empty()||logical->children.size()>CreateMaximumDepth||!target||target->type!=core::Node::Type::Bytes) fail(CreateErrorCode::Options,operation.options.source,"Invalid descriptor mapping type");
    for(const auto& part:logical->children) if(part.type!=core::Node::Type::Bytes) fail(CreateErrorCode::Options,operation.options.source,"Invalid descriptor component type");
  }
  if(operation.options.filenameConversions.size()>16384) fail(CreateErrorCode::Options,operation.options.source,"Filename conversion policy exceeds bound");
  Inventory result{observed,false,{},{},0};unsigned topEntries=0;std::set<std::string> logicalFiles,logicalDirectories;
  for(const auto& item:entries->children){
    operation.cancelled();const auto* logical=item.find("logical_path");const auto* targetNode=item.find("target");
    if(item.type!=core::Node::Type::Dictionary||!logical||logical->type!=core::Node::Type::List||logical->children.empty()||logical->children.size()>CreateMaximumDepth||!targetNode||targetNode->type!=core::Node::Type::Bytes) fail(CreateErrorCode::Options,operation.options.source,"Descriptor entry needs logical_path and target");
    std::vector<std::string> components;std::string relative;
    for(const auto& component:logical->children){
      if(component.type!=core::Node::Type::Bytes) fail(CreateErrorCode::Options,operation.options.source,"Logical descriptor components must be strings");
      const auto converted=compat::originalFilenameComponent(component.bytes,operation.options.filenameConversions);
      if(!converted||!nameValid(*converted)) fail(CreateErrorCode::Options,operation.options.source,"Unsafe converted logical filename");
      components.push_back(*converted);
    }
    if(result.logicalName.empty()) result.logicalName=components.front();
    if(result.logicalName!=components.front()) fail(CreateErrorCode::Options,operation.options.source,"Descriptor requires exactly one logical top level");
    if(components.size()==1&&++topEntries>1) fail(CreateErrorCode::Options,operation.options.source,"Descriptor repeats top-level target");
    for(std::size_t i=1;i<components.size();++i) relative=relativeChild(relative,components[i],operation.options.source);
    const fs::path target(std::string(targetNode->bytes));absoluteParts(target);
    if(target.lexically_normal()!=target) fail(CreateErrorCode::Path,target,"Descriptor targets must be normalized absolute paths");
    auto parent=openParent(target);auto opened=openChild(parent.directory.get(),parent.leaf,target);
    auto options=operation.options;options.source=target;const Operation child{options,operation.callback,operation.stop};auto physical=scan(opened.get(),child);
    const bool directory=physical.directory;result.directory=result.directory||directory||components.size()>1||entries->children.size()>1;
    result.origins.push_back({relative,physical.root,target,parent.identity});
    if(directory&&!relative.empty()) logicalDirectories.insert(relative);
    for(auto& file:physical.files){
      const auto name=directory?relativeChild(relative,file.relative,target):relative;
      const fs::path logicalFile(name);
      if(static_cast<std::size_t>(std::distance(logicalFile.begin(),logicalFile.end()))>CreateMaximumDepth) fail(CreateErrorCode::Limit,target,"Logical descriptor depth exceeded");
      if(!logicalFiles.insert(name).second) fail(CreateErrorCode::Options,target,"Duplicate logical file in descriptor");
      const auto physicalPath=directory?target/file.relative:target;auto fileParent=openParent(physicalPath);
      if(result.files.size()>=CreateMaximumFiles||static_cast<std::uint64_t>(file.observed.size)>CreateMaximumPayloadBytes-result.total) fail(CreateErrorCode::Limit,target,"Descriptor payload/file bound exceeded");
      result.total+=static_cast<std::uint64_t>(file.observed.size);result.files.push_back({name,file.observed,physicalPath,fileParent.identity});
    }
    for(auto& folder:physical.directories){
      if(result.directories.size()>=CreateMaximumDirectories) fail(CreateErrorCode::Limit,target,"Descriptor directory bound exceeded");
      const auto name=relativeChild(relative,folder.relative,target);logicalDirectories.insert(name);
      result.directories.push_back({name,folder.observed,target/folder.relative,{}});
    }
  }
  for(const auto& file:logicalFiles){
    if((result.directory&&file.empty())||logicalDirectories.contains(file)) fail(CreateErrorCode::Options,operation.options.source,"Logical file collides with a directory");
    auto parent=fs::path(file).parent_path();while(!parent.empty()){
      if(logicalFiles.contains(parent.generic_string())) fail(CreateErrorCode::Options,operation.options.source,"Logical file is an ancestor of another file");
      parent=parent.parent_path();
    }
  }
  for(const auto& folder:logicalDirectories){auto parent=fs::path(folder);while(!parent.empty()){if(logicalFiles.contains(parent.generic_string())) fail(CreateErrorCode::Options,operation.options.source,"Logical directory descends from a file");parent=parent.parent_path();}}
  std::sort(result.files.begin(),result.files.end(),logicalLess);std::sort(result.directories.begin(),result.directories.end(),logicalLess);
  // The current generator inventory uses UTF8 order. Java File/String compares
  // UTF16; refuse their divergent non-BMP/BMP cross-order instead of mislabelling
  // a different original piece stream as compatible.
  auto originalOrder=result.files;std::sort(originalOrder.begin(),originalOrder.end(),logicalUtf16Less);
  for(std::size_t i=0;i<result.files.size();++i) if(result.files[i].relative!=originalOrder[i].relative) fail(CreateErrorCode::Options,operation.options.source,"Virtual UTF16/UTF8 ordering divergence needs a separately qualified generator");
  return result;
}
std::string gzipInitialLinkage(std::string_view input){
  struct Stream {z_stream value{};bool initialized=false;~Stream(){if(initialized) ::deflateEnd(&value);}} stream;
  if(input.size()>MaximumMetadataBytes||::deflateInit2(&stream.value,Z_DEFAULT_COMPRESSION,Z_DEFLATED,15+16,8,Z_DEFAULT_STRATEGY)!=Z_OK) fail(CreateErrorCode::Generator,{},"Cannot encode original initial_linkage2");
  stream.initialized=true;std::string result(::deflateBound(&stream.value,static_cast<uLong>(input.size())),'\0');
  stream.value.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));stream.value.avail_in=static_cast<uInt>(input.size());
  stream.value.next_out=reinterpret_cast<Bytef*>(result.data());stream.value.avail_out=static_cast<uInt>(result.size());
  if(::deflate(&stream.value,Z_FINISH)!=Z_STREAM_END) fail(CreateErrorCode::Generator,{},"Incomplete original initial_linkage2 encoding");
  result.resize(stream.value.total_out);return result;
}
void applyInitialLinkage(lt::entry& entry,const Inventory& inventory,const std::string& name){
  std::vector<char> bytes;lt::bencode(std::back_inserter(bytes),entry);auto limits=storage::compatibleConfigLimits();limits.inputBytes=limits.stringBytes=MaximumMetadataBytes;
  auto document=core::Document::parse(std::string(bytes.data(),bytes.size()),limits);if(!document) fail(CreateErrorCode::Generator,{},"Cannot decode generated original linkage subject");
  const auto layout=compat::originalLayout(*document,2*CreateMaximumFiles);
  if(!layout||layout->name!=name||layout->multifile!=inventory.directory) fail(CreateErrorCode::Generator,{},"Original linkage geometry differs from created metadata");
  std::map<std::string,const Entry*> physical;
  for(const auto& file:inventory.files) physical.emplace(inventory.directory?name+'/'+file.relative:name,&file);
  lt::entry links(lt::entry::dictionary_t);
  for(std::size_t index=0;index<layout->files.size();++index){const auto& file=layout->files[index];if(file.padding) continue;
    const auto path=layout->multifile?layout->name+'/'+file.path:layout->name;const auto found=physical.find(path);
    if(found==physical.end()||found->second->physical.empty()||found->second->observed.size!=file.bytes) fail(CreateErrorCode::Generator,{},"Original initial linkage lost a physical file");
    links[std::to_string(index)]=found->second->physical.string();physical.erase(found);
  }
  if(!physical.empty()||links.dict().size()!=inventory.files.size()) fail(CreateErrorCode::Generator,{},"Initial linkage has unresolved physical files");
  if(links.dict().size()<100) entry["azureus_private_properties"]["initial_linkage"]=std::move(links);
  else{std::vector<char> encoded;lt::bencode(std::back_inserter(encoded),links);entry["azureus_private_properties"]["initial_linkage2"]=gzipInitialLinkage(std::string_view(encoded.data(),encoded.size()));}
}

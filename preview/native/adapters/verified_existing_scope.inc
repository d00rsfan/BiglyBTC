// Included by torrent_intake.cpp; shares the bounded document/error helpers.
namespace {
namespace existing_scope {
constexpr std::size_t MaximumFiles=4096,MaximumDirectories=8192,MaximumBytes=4*1024*1024;
void checked(std::expected<void,Error> result) {if(!result){throw Failure{result.error()};}}
using Dirs=std::map<std::string,std::pair<std::int64_t,std::int64_t>,std::less<>>;
template<class T> std::int64_t number(T value) {
  require(std::in_range<std::int64_t>(value),ErrorCode::Unsupported,"Existing scope stat exceeds signed journal range");
  return static_cast<std::int64_t>(value);
}
std::string stamp(const struct stat& value) {
  require(S_ISREG(value.st_mode)&&value.st_uid==::geteuid()&&value.st_nlink==1&&value.st_size>=0,
      ErrorCode::UnsafePath,"Existing scope needs complete owned regular single-link leaves");
  return "l"+i(number(value.st_dev))+i(number(value.st_ino))+i(number(value.st_size))+i(number(value.st_mode))
      +i(number(value.st_uid))+i(number(value.st_nlink))+i(number(value.st_mtim.tv_sec))+i(number(value.st_mtim.tv_nsec))
      +i(number(value.st_ctim.tv_sec))+i(number(value.st_ctim.tv_nsec))+'e';
}
template<class Visit> Descriptor parent(const fs::path& path,Visit visit) {
  fileop::pathShape(path);Descriptor current(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));struct stat root{};
  require(current.get()>=0&&::fstat(current.get(),&root)==0,ErrorCode::Io,"Cannot inspect existing scope root");
  fs::path prefix="/";visit(prefix,root);
  for(const auto& part:path.relative_path()) {
    Descriptor next(::openat(current.get(),part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
    struct stat held{},named{};
    require(next.get()>=0&&::fstat(next.get(),&held)==0&&::fstatat(current.get(),part.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0
        &&S_ISDIR(named.st_mode)&&fileop::identity(held,named),ErrorCode::Conflict,"Existing scope directory changed or is symbolic");
    prefix/=part;visit(prefix,held);current=std::move(next);
  }
  return current;
}
const Node& field(const Node& node,std::string_view key,Node::Type type) {
  const auto* value=node.find(key);require(value&&value->type==type,ErrorCode::InvalidFormat,"Invalid existing scope field");return *value;
}
std::int64_t integerField(const Node& node,std::string_view key) {return field(node,key,Node::Type::Integer).integer;}
std::string encoded(const Node& node) {
  const auto value=core::encodeValue(node,compatibleConfigLimits());require(value.has_value(),ErrorCode::InvalidFormat,"Invalid bounded existing scope");return *value;
}
template<std::size_t N> std::string bytes(const std::optional<std::array<std::byte,N>>& value) {
  return value?std::string(reinterpret_cast<const char*>(value->data()),N):std::string{};
}
bool inside(const fs::path& path,const fs::path& root) {
  const auto relative=path.lexically_relative(root);return !relative.empty()&&*relative.begin()!="..";
}
}
}
struct VerifiedExistingScope::Data {
  struct Leaf {fs::path path;std::string stamp;};
  std::string encoded;
  existing_scope::Dirs directories;
  std::map<std::size_t,Leaf> files;
};
const std::string& VerifiedExistingScope::encoded() const noexcept {return data_->encoded;}
std::expected<VerifiedExistingScope,Error> VerifiedExistingScope::decode(const Node& node) {
  try {
    using namespace existing_scope;
    require(node.type==Node::Type::Dictionary&&node.children.size()==20,ErrorCode::InvalidFormat,"Unknown existing scope fields");uniqueKeys(node);
    require(integerField(node,"version")==1,ErrorCode::Unsupported,"Unknown existing scope version");
    const auto swarm=integerField(node,"swarm");require(swarm==1||swarm==2,ErrorCode::InvalidFormat,"Invalid existing scope selection");
    require(field(node,"selected",Node::Type::Bytes).bytes.size()==20,ErrorCode::InvalidFormat,"Invalid existing selected identity");
    for(const auto* key:{"info","layers"}) {const auto value=field(node,key,Node::Type::Bytes).bytes;
      require(value.size()==64&&std::ranges::all_of(value,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}),ErrorCode::InvalidFormat,"Invalid existing subject digest");}
    const auto v1=field(node,"v1",Node::Type::Bytes).bytes.size(),v2=field(node,"v2",Node::Type::Bytes).bytes.size();
    require((v1==0||v1==20)&&(v2==0||v2==32)&&(swarm==1?v1==20:v2==32),ErrorCode::InvalidFormat,"Invalid existing full identities");
    const fs::path root(field(node,"root",Node::Type::Bytes).bytes);fileop::pathShape(root);
    auto data=std::make_shared<Data>();data->encoded=existing_scope::encoded(node);
    require(data->encoded.size()<=MaximumBytes,ErrorCode::Unsupported,"Existing scope exceeds journal bound");
    const auto& directories=field(node,"dirs",Node::Type::List);
    require(!directories.children.empty()&&directories.children.size()<=MaximumDirectories,ErrorCode::Unsupported,"Existing scope directory bound");
    std::string previous;
    for(const auto& entry:directories.children) {
      require(entry.type==Node::Type::Dictionary&&entry.children.size()==6,ErrorCode::InvalidFormat,"Invalid existing directory record");
      const auto path=std::string(field(entry,"path",Node::Type::Bytes).bytes);fileop::pathShape(path);
      const auto dev=integerField(entry,"dev"),ino=integerField(entry,"ino");
      require(dev>=0&&ino>0&&(previous.empty()||previous<path),ErrorCode::InvalidFormat,"Duplicate/unsorted existing directory identity");
      data->directories.emplace(path,std::pair{dev,ino});previous=path;
    }
    const auto& files=field(node,"files",Node::Type::List);
    require(!files.children.empty()&&files.children.size()<=MaximumFiles,ErrorCode::Unsupported,"Existing scope file bound");
    std::int64_t previousIndex=-1;std::set<fs::path> paths;
    for(const auto& entry:files.children) {
      require(entry.type==Node::Type::Dictionary&&entry.children.size()==6,ErrorCode::InvalidFormat,"Invalid existing leaf record");
      const auto index=integerField(entry,"index");
      require(index>=0&&index<100000&&index>previousIndex&&std::in_range<std::size_t>(index),ErrorCode::InvalidFormat,"Duplicate/invalid existing file index");
      const fs::path path(field(entry,"path",Node::Type::Bytes).bytes);fileop::pathShape(path);
      require(inside(path,root)&&paths.insert(path).second,ErrorCode::InvalidFormat,"Existing scope path is outside its root or duplicated");
      const auto& values=field(entry,"stamp",Node::Type::List);
      require(values.children.size()==10&&std::ranges::all_of(values.children,[](const auto& v){return v.type==Node::Type::Integer;}),ErrorCode::InvalidFormat,"Invalid existing leaf stamp");
      for(const auto n:{0U,1U,2U,3U,4U,5U}) {require(values.children[n].integer>=0,ErrorCode::InvalidFormat,"Negative existing stat identity");}
      require(values.children[1].integer>0&&values.children[5].integer==1&&values.children[7].integer>=0&&values.children[7].integer<1000000000
          &&values.children[9].integer>=0&&values.children[9].integer<1000000000,ErrorCode::InvalidFormat,"Invalid existing stat identity/time");
      data->files.emplace(static_cast<std::size_t>(index),Data::Leaf{path,existing_scope::encoded(values)});previousIndex=index;
    }
    return VerifiedExistingScope{std::move(data)};
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(const fileop::Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot decode existing payload scope"});}
}
std::expected<VerifiedExistingScope,Error> VerifiedExistingScope::capture(const ProfileLease& lease,
    const core::Document& source,const compat::OriginalLayout& layout,const OriginalTorrentIdentity& identity,
    const fs::path& directory,std::string_view leaf) {
  try {
    using namespace existing_scope;
    require(core::validTorrentIntakeSaveName(leaf),ErrorCode::UnsafePath,"Existing scope requires a safe destination leaf");
    const auto root=directory/fs::path(leaf);fileop::pathShape(root);const fs::path profile=fileop::profilePath(lease);
    require(!inside(root,profile)&&!inside(profile,root),ErrorCode::UnsafePath,"Existing payload overlaps the held profile");
    for(const auto& part:root.relative_path()){require(part!=".git"&&!part.native().starts_with(".BiglyBTC"),ErrorCode::UnsafePath,"Existing payload overlaps reserved paths");}
    Dirs dirs;std::string files="l";std::size_t count=0;
    require(!layout.files.empty()&&layout.files.size()<=100000,ErrorCode::Unsupported,"Existing original file bound");
    for(std::size_t index=0;index<layout.files.size();++index) {
      const auto& file=layout.files[index];if(file.padding){continue;}
      require(++count<=MaximumFiles&&file.bytes>=0,ErrorCode::Unsupported,"Existing real file bound");
      const auto path=layout.multifile?root/fs::path(file.path):root;fileop::pathShape(path);
      require(inside(path,root),ErrorCode::UnsafePath,"Existing leaf escapes its chosen root");
      auto parentFd=parent(path.parent_path(),[&](const fs::path& prefix,const struct stat& value){
        const auto identity=std::pair{number(value.st_dev),number(value.st_ino)};
        const auto [found,inserted]=dirs.emplace(prefix.string(),identity);
        require(inserted||found->second==identity,ErrorCode::Conflict,"Existing directory changed during capture");
        require(dirs.size()<=MaximumDirectories,ErrorCode::Unsupported,"Existing directory bound");
      });
      Descriptor fd(::openat(parentFd.get(),path.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
      struct stat held{},named{};
      require(fd.get()>=0&&::fstat(fd.get(),&held)==0&&::fstatat(parentFd.get(),path.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)==0
          &&fileop::unchanged(held,named)&&held.st_size==file.bytes,ErrorCode::Conflict,"Existing payload is missing, short or changed");
      files+="d"+b("index")+i(static_cast<std::int64_t>(index))+b("path")+b(path.string())+b("stamp")+stamp(held)+'e';
      require(files.size()<=MaximumBytes,ErrorCode::Unsupported,"Existing scope file paths exceed bound");
    }
    files+='e';std::string directories="l";
    for(const auto& [path,id]:dirs){directories+="d"+b("dev")+i(id.first)+b("ino")+i(id.second)+b("path")+b(path)+'e';}directories+='e';
    const auto rawInfo=take(originalTorrentInfoSubject(source));std::string layers;
    if(const auto* value=source.find("piece layers")){layers=existing_scope::encoded(*value);}
    const auto key=identity.originalKey.bytes();
    auto encoded="d"+b("dirs")+directories+b("files")+files+b("info")+b(fileop::digest(rawInfo))
        +b("layers")+b(fileop::digest(layers))+b("root")+b(root.string())
        +b("selected")+b(std::string_view(reinterpret_cast<const char*>(key.data()),key.size()))
        +b("swarm")+i(identity.selected==OriginalSelectedHash::V1?1:2)+b("v1")+b(bytes(identity.full.v1))+b("v2")+b(bytes(identity.full.v2))+b("version")+"i1ee";
    require(encoded.size()<=MaximumBytes,ErrorCode::Unsupported,"Existing scope exceeds journal bound");
    auto document=parse(std::move(encoded));auto result=take(decode(document.root()));
    for(const auto& [index,file]:result.data_->files){
      auto parentFd=fileop::directory(file.path.parent_path());Descriptor fd(::openat(parentFd.get(),file.path.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
      require(fd.get()>=0,ErrorCode::Conflict,"Existing payload disappeared during capture");auto proof=take(result.bind(index,file.path,fd.get()));existing_scope::checked(proof());
    }
    return result;
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(const fileop::Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot capture existing payload scope"});}
}
std::expected<void,Error> VerifiedExistingScope::validate(const ProfileLease& lease,const core::Document& source,
    const compat::OriginalLayout& layout,const OriginalTorrentIdentity& identity,const fs::path& directory,std::string_view leaf) const {
  const auto now=capture(lease,source,layout,identity,directory,leaf);if(!now){return std::unexpected(now.error());}
  if(now->encoded()!=encoded()){return std::unexpected(Error{ErrorCode::Conflict,"Approved existing payload subject/path/identity changed"});}return {};
}
std::expected<VerifiedExistingScope::Proof,Error> VerifiedExistingScope::bind(std::size_t index,const fs::path& path,int descriptor) const {
  try {
    const auto found=data_->files.find(index);
    require(found!=data_->files.end()&&found->second.path==path,ErrorCode::Conflict,"Descriptor is outside approved existing file indices");
    Proof proof=[data=data_,index,path,descriptor]() -> std::expected<void,Error> {
      try {
        auto parentFd=existing_scope::parent(path.parent_path(),[&](const fs::path& prefix,const struct stat& value){
          const auto found=data->directories.find(prefix.string());
          require(found!=data->directories.end()&&found->second==std::pair{existing_scope::number(value.st_dev),existing_scope::number(value.st_ino)},
              ErrorCode::Conflict,"Approved existing directory identity changed");
        });
        struct stat held{},named{};const int flags=::fcntl(descriptor,F_GETFL);
        require(flags>=0&&(flags&O_ACCMODE)==O_RDONLY&&::fstat(descriptor,&held)==0
            &&::fstatat(parentFd.get(),path.filename().c_str(),&named,AT_SYMLINK_NOFOLLOW)==0
            &&fileop::unchanged(held,named)&&existing_scope::stamp(held)==data->files.at(index).stamp,
            ErrorCode::Conflict,"Approved read-only descriptor/name/content metadata changed");return {};
      }catch(const Failure& failure){return std::unexpected(failure.error);}
      catch(const fileop::Failure& failure){return std::unexpected(failure.error);}
      catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate approved existing descriptor"});}
    };
    existing_scope::checked(proof());return proof;
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot bind approved existing descriptor"});}
}

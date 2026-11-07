// Private source-reader implementation only; never a grant or effect callback.
using E=SavedRssSourceError;
using storage::detail::Descriptor;
constexpr std::size_t MetaBytes=4*1024*1024,SettingsBytes=1024*1024,TrustBytes=4*1024*1024;
constexpr core::Limits ConfigLimits{MetaBytes,MetaBytes,100000,64};
struct File {
  std::string name,bytes;
  Descriptor descriptor{-1};
  struct stat identity{};
  bool present=false;
};
bool same(const struct stat& a,const struct stat& b){return engine::detail::http_trust::sameFile(a,b);}
std::expected<File,E> named(int directory,std::string name,std::size_t maximum){
  File out;out.name=std::move(name);
  out.descriptor=Descriptor(::openat(directory,out.name.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));
  struct stat label{};
  if(out.descriptor.get()<0){
    if(errno!=ENOENT){return std::unexpected(E::Malformed);}
    if(::fstatat(directory,out.name.c_str(),&label,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT){return std::unexpected(E::SourceChanged);}
    return out;
  }
  if(::fstat(out.descriptor.get(),&out.identity)!=0||::fstatat(directory,out.name.c_str(),&label,AT_SYMLINK_NOFOLLOW)!=0
      ||!same(out.identity,label)){return std::unexpected(E::Malformed);}
  if(out.identity.st_size<0||static_cast<std::uint64_t>(out.identity.st_size)>maximum){return std::unexpected(E::Limit);}
  out.bytes.resize(static_cast<std::size_t>(out.identity.st_size));
  std::size_t offset=0;
  while(offset<out.bytes.size()){
    const auto got=::read(out.descriptor.get(),out.bytes.data()+offset,out.bytes.size()-offset);
    if(got<0&&errno==EINTR){continue;}if(got<=0){return std::unexpected(E::SourceChanged);}offset+=static_cast<std::size_t>(got);
  }
  struct stat after{};
  if(::fstat(out.descriptor.get(),&after)!=0||::fstatat(directory,out.name.c_str(),&label,AT_SYMLINK_NOFOLLOW)!=0
      ||!same(out.identity,after)||!same(after,label)){return std::unexpected(E::SourceChanged);}
  out.present=true;return out;
}
std::size_t cap(const std::string& name){
  if(name.starts_with("metasearch.config")){return MetaBytes;}
  if(name.starts_with("biglybt.config")){return SettingsBytes;}
  if(name==".azlock"){return 4096;}
  return TrustBytes;
}
std::expected<void,E> unchanged(int directory,const std::vector<File>& files){
  for(const auto& before:files){
    auto after=named(directory,before.name,cap(before.name));if(!after){return std::unexpected(after.error());}
    if(before.present!=after->present){return std::unexpected(E::SourceChanged);}
    if(before.present){
      struct stat held{};
      if(::fstat(before.descriptor.get(),&held)!=0||!same(before.identity,held)||!same(before.identity,after->identity)
          ||before.bytes!=after->bytes){return std::unexpected(E::SourceChanged);}
    }
  }return {};
}
bool unique(const core::Node& node){
  if(node.type==core::Node::Type::Dictionary){
    if(node.children.size()%2){return false;}std::set<std::string_view> keys;
    for(std::size_t i=0;i<node.children.size();i+=2){if(node.children[i].type!=core::Node::Type::Bytes||!keys.insert(node.children[i].bytes).second){return false;}}
  }
  return std::ranges::all_of(node.children,[](const auto& child){return unique(child);});
}
bool emptyBytes(const core::Node& row,std::string_view key){const auto* p=row.find(key);return !p||(p->type==core::Node::Type::Bytes&&p->bytes.empty());}
bool zero(const core::Node& row,std::string_view key){const auto* p=row.find(key);return !p||(p->type==core::Node::Type::Integer&&p->integer==0);}
bool publicV4(quint32 ip){
  const auto range=[&](quint32 network,quint32 mask){return (ip&mask)==network;};
  return !range(0,0xff000000u)&&!range(0x0a000000u,0xff000000u)&&!range(0x64400000u,0xffc00000u)
      &&!range(0x7f000000u,0xff000000u)&&!range(0xa9fe0000u,0xffff0000u)&&!range(0xac100000u,0xfff00000u)
      &&!range(0xc0000000u,0xffffff00u)&&!range(0xc0000200u,0xffffff00u)&&!range(0xc0586300u,0xffffff00u)
      &&!range(0xc0a80000u,0xffff0000u)&&!range(0xc6120000u,0xfffe0000u)&&!range(0xc6336400u,0xffffff00u)
      &&!range(0xcb007100u,0xffffff00u)&&(ip>>24)<224;
}
enum class ContextSpelling { Canonical, JavaFormQuery };
bool equivalentQuerySpelling(const QByteArray& raw,const QByteArray& canonical){
  const auto view=[](const QByteArray& bytes){return std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size()));};
  const auto tail=[](std::string_view bytes){const auto end=bytes.find_first_of("/?#",bytes.find("://")+3);return end==bytes.npos?bytes.size():end;};
  const auto actual=view(raw),parsed=view(canonical);const auto actualTail=tail(actual),parsedTail=tail(parsed);
  // Only the source-owned path/query spelling may differ. Scheme, host,
  // credentials and port must still have their exact canonical spelling.
  if(actual.substr(0,actualTail)!=parsed.substr(0,parsedTail)){return false;}
  const auto normalized=[](std::string_view bytes)->std::optional<std::string>{
    const auto digit=[](unsigned char c)->int{if(c>='0'&&c<='9'){return c-'0';}if(c>='a'&&c<='f'){return c-'a'+10;}if(c>='A'&&c<='F'){return c-'A'+10;}return -1;};
    const auto unreserved=[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='.'||c=='_'||c=='~';};
    constexpr char Hex[]="0123456789ABCDEF";std::string out;out.reserve(bytes.size());
    for(std::size_t n=0;n<bytes.size();++n){
      const auto c=static_cast<unsigned char>(bytes[n]);if(c!='%'){out+=static_cast<char>(c);continue;}
      if(n+2>=bytes.size()){return std::nullopt;}const auto high=digit(static_cast<unsigned char>(bytes[n+1])),low=digit(static_cast<unsigned char>(bytes[n+2]));
      if(high<0||low<0){return std::nullopt;}const auto decoded=static_cast<unsigned char>((high<<4)|low);n+=2;
      if(unreserved(decoded)){out+=static_cast<char>(decoded);}else{out+='%';out+=Hex[decoded>>4];out+=Hex[decoded&15];}
    }return out;
  };
  const auto left=normalized(actual.substr(actualTail)),right=normalized(parsed.substr(parsedTail));return left&&right&&*left==*right;
}
std::expected<RssContext,E> context(std::string_view text,bool searchContext=true,ContextSpelling spelling=ContextSpelling::Canonical){
  if(text.empty()||text.size()>4096||text.find('\\')!=text.npos
      ||!std::ranges::all_of(text,[](unsigned char c){return c>=33&&c<=126;})){return std::unexpected(E::Unsupported);}
  const auto raw=QByteArray(text.data(),static_cast<qsizetype>(text.size()));const auto url=QUrl::fromEncoded(raw,QUrl::StrictMode);
  if(!url.isValid()||(url.scheme()!="http"&&url.scheme()!="https")||!(spelling==ContextSpelling::Canonical?url.toEncoded(QUrl::FullyEncoded)==raw:equivalentQuerySpelling(raw,url.toEncoded(QUrl::FullyEncoded)))
      ||url.hasFragment()||!url.userInfo().isEmpty()||url.authority().contains('@')
      ||url.port(url.scheme()=="https"?443:80)<=0||url.port(80)>65535){return std::unexpected(E::Unsupported);}
  const auto literal=QString::fromLatin1(raw);
  if(literal.contains("azmethod=",Qt::CaseInsensitive)||literal.contains(":COOKIE:",Qt::CaseInsensitive)){return std::unexpected(E::Unsupported);}
  const auto host=url.host();
  if(searchContext){for(const auto piece:{"biglybt","vuze","azureus","aelitis","plusone.google.com"}){if(host.contains(QLatin1String(piece))){return std::unexpected(E::Unsupported);}}
  if(QRegularExpression(QStringLiteral("^clients[0-9]\\.google\\.com$ ").trimmed()).match(host).hasMatch()){return std::unexpected(E::Unsupported);}
  }
  QHostAddress numeric(host);
  if(!numeric.isNull()){
    if(numeric.protocol()!=QAbstractSocket::IPv4Protocol||host!=numeric.toString()||!publicV4(numeric.toIPv4Address())){return std::unexpected(E::Unsupported);}
  }else{
    if(host.size()>253||!host.contains('.')||host!=host.toLower()||host.endsWith('.')){return std::unexpected(E::Unsupported);}
    for(const auto suffix:{".onion",".i2p",".localhost",".local",".lan",".home",".internal",".invalid",".test",".example"}){
      if(host.endsWith(QLatin1String(suffix))){return std::unexpected(E::Unsupported);}
    }
    for(const auto& label:host.split('.')){
      if(label.isEmpty()||label.size()>63||label.startsWith('-')||label.endsWith('-')){return std::unexpected(E::Unsupported);}
      for(const auto c:label){if(!((c>=u'a'&&c<=u'z')||(c>=u'0'&&c<=u'9')||c==u'-')){return std::unexpected(E::Unsupported);}}
    }
    if(!std::ranges::any_of(host.section('.',-1),[](QChar c){return c>=u'a'&&c<=u'z';})){return std::unexpected(E::Unsupported);}
    // Reject legacy numeric forms which a later resolver could reinterpret.
    if(std::ranges::all_of(host,[](QChar c){return (c>=u'0'&&c<=u'9')||c==u'.';})){return std::unexpected(E::Unsupported);}
  }
  // Literal copies of WebEngine.init's configured-URL patterns. The greedy
  // base includes a query slash when Original does; final response URL is absent.
  const auto root=QRegularExpression(QStringLiteral("((?:tor:)?https?://[^/]+)")).match(literal);
  const auto base=QRegularExpression(QStringLiteral("((?:tor:)?https?://.*/)")).match(literal);
  return RssContext{root.hasMatch()?root.captured(1):QString{},base.hasMatch()?base.captured(1):QString{}};
}
std::expected<void,E> settings(const core::Node& root,std::int64_t id){
  if(root.type!=core::Node::Type::Dictionary||!unique(root)){return std::unexpected(E::Malformed);}
  for(const auto key:{"metasearch.config.proxy.type","metasearch.config.proxy.enable","Enable.Proxy","Enable.SOCKS","Proxy.Data.Enable"}){
    if(!zero(root,key)){return std::unexpected(E::Unsupported);}
  }
  if(const auto* local=root.find("metasearch.engine."+std::to_string(id)+".local")){
    if(local->type!=core::Node::Type::Dictionary){return std::unexpected(E::Malformed);}
    for(const auto key:{"cookies"}){if(!emptyBytes(*local,key)){return std::unexpected(E::Unsupported);}}
  }
  if(!emptyBytes(root,"metasearch.web.last.headers")){return std::unexpected(E::Unsupported);}
  return {};
}
std::expected<struct stat,E> directory(const storage::ProfileLease& lease,const std::filesystem::path& path){
  std::error_code ec;
  if(!lease.valid()||!path.is_absolute()||std::filesystem::canonical(path,ec)!=path||ec){return std::unexpected(E::Lease);}
  Descriptor namedDirectory(::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  struct stat held{},named{};
  if(namedDirectory.get()<0||::fstat(lease.directoryHandle(),&held)!=0||::fstat(namedDirectory.get(),&named)!=0
      ||!S_ISDIR(held.st_mode)||held.st_uid!=::geteuid()||held.st_dev!=named.st_dev||held.st_ino!=named.st_ino){return std::unexpected(E::Lease);}
  return held;
}

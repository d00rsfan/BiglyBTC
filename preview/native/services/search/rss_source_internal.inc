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
bool emptyList(const core::Node& row,std::string_view key,bool required=false){const auto* p=row.find(key);return p?(p->type==core::Node::Type::List&&p->children.empty()):!required;}
bool zero(const core::Node& row,std::string_view key){const auto* p=row.find(key);return !p||(p->type==core::Node::Type::Integer&&p->integer==0);}
bool integer(const core::Node& row,std::string_view key,std::int64_t low,std::int64_t high,std::int64_t fallback){
  const auto* p=row.find(key);return p?(p->type==core::Node::Type::Integer&&p->integer>=low&&p->integer<=high):(fallback>=low&&fallback<=high);
}
bool bytes(const core::Node* p,std::size_t maximum,bool nonempty=false){return p&&p->type==core::Node::Type::Bytes&&p->bytes.size()<=maximum&&(!nonempty||!p->bytes.empty());}
bool utf8(const core::Node* p,std::size_t maximum){
  return bytes(p,maximum)&&QString::fromUtf8(p->bytes.data(),static_cast<qsizetype>(p->bytes.size())).toUtf8().toStdString()==p->bytes;
}
bool publicV4(quint32 ip){
  const auto range=[&](quint32 network,quint32 mask){return (ip&mask)==network;};
  return !range(0,0xff000000u)&&!range(0x0a000000u,0xff000000u)&&!range(0x64400000u,0xffc00000u)
      &&!range(0x7f000000u,0xff000000u)&&!range(0xa9fe0000u,0xffff0000u)&&!range(0xac100000u,0xfff00000u)
      &&!range(0xc0000000u,0xffffff00u)&&!range(0xc0000200u,0xffffff00u)&&!range(0xc0586300u,0xffffff00u)
      &&!range(0xc0a80000u,0xffff0000u)&&!range(0xc6120000u,0xfffe0000u)&&!range(0xc6336400u,0xffffff00u)
      &&!range(0xcb007100u,0xffffff00u)&&(ip>>24)<224;
}
std::expected<RssContext,E> context(std::string_view text){
  if(text.empty()||text.size()>2048||text.find('%')!=text.npos||text.find('\\')!=text.npos
      ||!std::ranges::all_of(text,[](unsigned char c){return c>=33&&c<=126;})){return std::unexpected(E::Unsupported);}
  const auto raw=QByteArray(text.data(),static_cast<qsizetype>(text.size()));const auto url=QUrl::fromEncoded(raw,QUrl::StrictMode);
  if(!url.isValid()||(url.scheme()!="http"&&url.scheme()!="https")||url.toEncoded(QUrl::FullyEncoded)!=raw
      ||url.hasFragment()||!url.userInfo().isEmpty()||url.authority().contains('@')||!url.path().startsWith('/')
      ||url.port(url.scheme()=="https"?443:80)<=0||url.port(80)>65535){return std::unexpected(E::Unsupported);}
  const auto literal=QString::fromLatin1(raw);
  if(literal.contains("azmethod=",Qt::CaseInsensitive)||literal.contains(":COOKIE:",Qt::CaseInsensitive)){return std::unexpected(E::Unsupported);}
  const auto host=url.host();QHostAddress numeric(host);
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
  if(!root.hasMatch()||!base.hasMatch()){return std::unexpected(E::Unsupported);}
  return RssContext{root.captured(1),base.captured(1)};
}
std::expected<void,E> settings(const core::Node& root,std::int64_t id){
  if(root.type!=core::Node::Type::Dictionary||!unique(root)){return std::unexpected(E::Malformed);}
  for(const auto key:{"metasearch.config.proxy.type","metasearch.config.proxy.enable","Enable.Proxy","Enable.SOCKS","Proxy.Data.Enable"}){
    if(!zero(root,key)){return std::unexpected(E::Unsupported);}
  }
  if(const auto* local=root.find("metasearch.engine."+std::to_string(id)+".local")){
    if(local->type!=core::Node::Type::Dictionary){return std::unexpected(E::Malformed);}
    for(const auto key:{"cookies","etag","last_mod"}){if(!emptyBytes(*local,key)){return std::unexpected(E::Unsupported);}}
  }return {};
}
std::expected<RssGetDescription,E> describeRow(const core::Node& row){
  if(row.type!=core::Node::Type::Dictionary||!unique(row)||!row.find("type")||row.find("type")->type!=core::Node::Type::Integer){return std::unexpected(E::Malformed);}
  if(row.find("type")->integer!=4||!integer(row,"az_version",1,5,5)||!integer(row,"version",1,std::numeric_limits<std::int32_t>::max(),1)
      ||!integer(row,"selected",0,3,0)||!integer(row,"source",0,2,0)||!utf8(row.find("name"),4096)
      ||!emptyList(row,"web.maps",true)||!emptyList(row,"l1_map")||!emptyList(row,"l2_map")){return std::unexpected(E::Unsupported);}
  if(!zero(row,"web.needs_auth")||!emptyBytes(row,"web.login_page")||!emptyBytes(row,"web.full_cookies")
      ||!emptyList(row,"web.required_cookies")||!emptyBytes(row,"web.dl_link_css")||!emptyBytes(row,"update_url")){return std::unexpected(E::Unsupported);}
  if(const auto* auth=row.find("web.auth_method");auth&&(auth->type!=core::Node::Type::Bytes||auth->bytes!="transparent")){return std::unexpected(E::Unsupported);}
  constexpr std::array<std::string_view,12> webFields{"web.search_url_format","web.maps","web.needs_auth","web.auth_method","web.login_page","web.required_cookies","web.full_cookies","web.auto_date","web.time_zone","web.date_format","web.dl_link_css","web.icon_url"};
  for(std::size_t i=0;i<row.children.size();i+=2){const auto key=row.children[i].bytes;
    if((key.starts_with("web.")&&std::ranges::find(webFields,key)==webFields.end())||key=="headers"||key=="plugin_id"||key=="plugin"||key=="provider"){return std::unexpected(E::Unsupported);}
  }
  if(!integer(row,"web.auto_date",0,1,1)){return std::unexpected(E::Malformed);}
  for(const auto key:{"web.time_zone","web.date_format","web.icon_url"}){
    if(const auto* p=row.find(key);p&&!utf8(p,4096)){return std::unexpected(E::Malformed);}
  }
  const auto* url=row.find("web.search_url_format");if(!bytes(url,2048,true)){return std::unexpected(E::Malformed);}
  auto prefixes=context(url->bytes);if(!prefixes){return std::unexpected(prefixes.error());}
  RssGetDescription out;out.literalGetUrl=std::string(url->bytes);out.context=std::move(*prefixes);return out;
}
std::expected<SavedRssDescription,E> describe(const core::Document& catalog,const SavedRssSelector& selector){
  const auto& root=catalog.root();if(root.type!=core::Node::Type::Dictionary||!unique(root)){return std::unexpected(E::Malformed);}
  const auto* list=root.find("engines");if(!list||list->type!=core::Node::Type::List){return std::unexpected(E::Malformed);}
  if(list->children.size()>4096||selector.uid.empty()||selector.uid.size()>256){return std::unexpected(E::Limit);}
  if(selector.sourceIndex>=list->children.size()){return std::unexpected(E::Ambiguous);}
  std::set<std::int64_t> ids;std::set<std::string_view> uids;
  for(const auto& row:list->children){
    if(row.type!=core::Node::Type::Dictionary||!row.find("id")||row.find("id")->type!=core::Node::Type::Integer
        ||!bytes(row.find("uid"),256,true)||!row.find("type")||row.find("type")->type!=core::Node::Type::Integer){return std::unexpected(E::Malformed);}
    if(!ids.insert(row.find("id")->integer).second||!uids.insert(row.find("uid")->bytes).second){return std::unexpected(E::Ambiguous);}
  }
  const auto& row=list->children[selector.sourceIndex];
  if(row.find("id")->integer!=selector.id||row.find("uid")->bytes!=selector.uid){return std::unexpected(E::Ambiguous);}
  auto value=describeRow(row);if(!value){return std::unexpected(value.error());}
  SavedRssDescription out;static_cast<RssGetDescription&>(out)=std::move(*value);out.selected=selector;return out;
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

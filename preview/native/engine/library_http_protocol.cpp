#include "library_http_tracker.hpp"
#include "biglybtc/bencode.hpp"
#include <bit>
#include <array>
#include <algorithm>
#include <charconv>
#include <set>
#include <zlib.h>

namespace biglybtc::engine::detail {
namespace {
auto unsupported(){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Standalone HTTP scrape requires one direct public route without credentials or proxy"});}
bool unique(const core::Node& value){
  if(value.type!=core::Node::Type::Dictionary||value.children.size()%2){return false;}
  std::set<std::string_view> keys;for(std::size_t i=0;i<value.children.size();i+=2){if(value.children[i].type!=core::Node::Type::Bytes||!keys.insert(value.children[i].bytes).second){return false;}}return true;
}
std::optional<std::int32_t> number(const core::Node& root,std::string_view key,std::int32_t fallback){
  const auto* p=root.find(key);if(!p){return fallback;}if(p->type!=core::Node::Type::Integer){return {};}
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(p->integer));
}
bool privateAddress(const QHostAddress& a){
  if(a.protocol()==QAbstractSocket::IPv4Protocol){const auto x=a.toIPv4Address();return (x>>24)==0||(x>>24)>=224||(x&0xff000000u)==0x0a000000u||(x&0xfff00000u)==0xac100000u||(x&0xffff0000u)==0xc0a80000u||(x&0xffc00000u)==0x64400000u||(x&0xffff0000u)==0xa9fe0000u;}
  const auto x=a.toIPv6Address();return a.isLinkLocal()||a.isSiteLocal()||(x[0]&0xfe)==0xfc;
}
}
std::expected<LibraryHttpRoute,storage::Error> libraryHttpRoute(const NetworkPolicy& p,std::string_view raw){
  if(p.mode!=NetworkMode::Normal||!p.dht||!p.httpTrackers||p.proxy.kind!=NetworkProxyKind::None||raw.empty()||raw.size()>2048){return unsupported();}
  std::string text;for(const unsigned char c:raw){if(c==' '){continue;}if(c<33||c>126){return unsupported();}text+=static_cast<char>(c);}
  // Literal original TrackerStatus constructor: replace announce prefix after
  // final slash, or append scrape only when no dot follows the final slash.
  const auto slash=text.rfind('/');if(slash==text.npos){return unsupported();}
  if(text.compare(slash+1,8,"announce")==0){text.replace(slash+1,8,"scrape");}
  else if(text.rfind('.')==text.npos||text.rfind('.')<slash){text+=(text.ends_with('/')?"":"/");text+="scrape";}
  else{return unsupported();}
  const auto url=QUrl::fromEncoded(QByteArray::fromStdString(text),QUrl::StrictMode);
  if(!url.isValid()||(url.scheme()!="http"&&url.scheme()!="https")||url.host().isEmpty()
      ||!url.userInfo().isEmpty()||url.authority(QUrl::FullyEncoded).contains('@')||url.hasFragment()||url.port(80)<=0||url.port(80)>65535){return unsupported();}
  // Normal hostname routes use the resolver, but Tor/I2P and scoped literals
  // cannot accidentally become local-DNS/direct fallbacks.
  auto host=url.host().toLower();while(host.endsWith('.')){host.chop(1);}if(host.endsWith(".onion")||host.endsWith(".i2p")||host.contains('%')){return unsupported();}
  LibraryHttpRoute route{url,{},p.ipv6};
  for(const auto& rawLocal:p.bindAddresses){QHostAddress local(QString::fromStdString(rawLocal));
    if(local.isNull()||(!p.ipv6&&local.protocol()==QAbstractSocket::IPv6Protocol)){if(local.protocol()==QAbstractSocket::IPv6Protocol&&!p.ipv6){continue;}return unsupported();}
    const auto same=std::ranges::find_if(route.locals,[&](const auto& old){return old.protocol()==local.protocol();});
    if(same!=route.locals.end()&&*same!=local){return unsupported();}if(same==route.locals.end()){route.locals.push_back(local);}
  }if(route.locals.empty()){return unsupported();}return route;
}
std::expected<LibraryHttpRoute,storage::Error> libraryHttpRedirectRoute(
    const LibraryHttpRoute& source,const QByteArray& location,std::span<const std::byte,20> hash){
  if(location.isEmpty()||location.size()>2048||!std::ranges::all_of(location,[](unsigned char c){return c>=33&&c<=126;})){return unsupported();}
  auto target=source.scrape.resolved(QUrl::fromEncoded(location,QUrl::StrictMode));
  if(!target.isValid()||target.scheme()!=source.scrape.scheme()||(target.scheme()!="http"&&target.scheme()!="https")
      ||target.host().isEmpty()||!target.userInfo().isEmpty()||target.authority(QUrl::FullyEncoded).contains('@')
      ||target.hasFragment()||target.port(80)<=0||target.port(80)>65535||target.toEncoded().size()>2048){return unsupported();}
  auto host=target.host().toLower();while(host.endsWith('.')){host.chop(1);}if(host.endsWith(".onion")||host.endsWith(".i2p")||host.contains('%')){return unsupported();}
  // Never issue a redirected request for a different hash, or duplicate our
  // appended selected hash. Preserve all other raw query/passkey components.
  const auto expected=QByteArray(reinterpret_cast<const char*>(hash.data()),20);
  const auto fields=target.query(QUrl::FullyEncoded).toUtf8().split('&');QList<QByteArray> kept;unsigned hashes=0;
  for(const auto& field:fields){const auto equal=field.indexOf('=');const auto key=QByteArray::fromPercentEncoding(field.left(equal<0?field.size():equal));
    if(key=="info_hash"){
      if(++hashes>1||equal<0){return unsupported();}auto value=field.mid(equal+1);value.replace('+',' ');
      if(QByteArray::fromPercentEncoding(value)!=expected){return unsupported();}
    }else{kept.push_back(field);}
  }
  if(hashes){const auto query=kept.join('&');target.setQuery(query.isEmpty()?QString():QString::fromLatin1(query));}
  return LibraryHttpRoute{target,source.locals,source.ipv6};
}
std::string libraryHttpRouteScope(const LibraryHttpRoute& r,std::string_view config){
  auto out=std::string(config)+":"+r.scrape.toEncoded(QUrl::FullyEncoded).toStdString()+":"+std::to_string(r.ipv6);
  for(const auto& a:r.locals){out+=':'+a.toString().toStdString();}return out;
}
std::optional<std::pair<QHostAddress,QHostAddress>> libraryHttpEndpoint(const LibraryHttpRoute& r,const QList<QHostAddress>& candidates){
  if(candidates.size()>32){return {};}
  for(const auto& remote:candidates){
    if(remote.isNull()||(remote==QHostAddress::Any||remote==QHostAddress::AnyIPv4||remote==QHostAddress::AnyIPv6)||remote.isMulticast()||!remote.scopeId().isEmpty()
        ||(remote.protocol()!=QAbstractSocket::IPv4Protocol&&remote.protocol()!=QAbstractSocket::IPv6Protocol)
        ||(!r.ipv6&&remote.protocol()==QAbstractSocket::IPv6Protocol)){continue;}
    if(remote.protocol()==QAbstractSocket::IPv6Protocol){const auto b=remote.toIPv6Address();bool mapped=true;for(int i=0;i<10;++i){mapped&=b[i]==0;}if(mapped&&b[10]==255&&b[11]==255){continue;}}
    const auto local=std::ranges::find_if(r.locals,[&](const auto& a){return a.protocol()==remote.protocol();});if(local==r.locals.end()){continue;}
    if((remote.isLoopback()||privateAddress(remote))&&!(remote.isLoopback()&&local->isLoopback())){continue;}
    return std::pair{remote,*local};
  }return {};
}
namespace http_tracker_wire {
std::int64_t interval(std::int32_t minimum,std::int32_t seeds,unsigned fuzz) noexcept {
  // Java int overflow occurs before comparisons and before conversion to long.
  auto seconds=std::bit_cast<std::int32_t>(std::uint32_t{900}+static_cast<std::uint32_t>(seeds)*10u);
  if(minimum>seconds){seconds=minimum;}seconds=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(seconds)+fuzz);
  if(seconds>10800){seconds=10800;}return static_cast<std::int64_t>(seconds)*1000;
}
namespace {
std::optional<QByteArray> gunzip(const QByteArray& encoded){
  // zlib validates every gzip header/trailer, CRC32 and ISIZE. Accept a bounded
  // concatenation, never a partial member or trailing uninterpreted bytes.
  z_stream stream{};if(inflateInit2(&stream,MAX_WBITS+16)!=Z_OK){return {};}
  struct End{z_stream* s;~End(){inflateEnd(s);}} end{&stream};
  stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(encoded.constData()));stream.avail_in=static_cast<uInt>(encoded.size());
  QByteArray decoded;unsigned members=1;std::array<char,8192> block{};
  for(;;){stream.next_out=reinterpret_cast<Bytef*>(block.data());stream.avail_out=block.size();const auto before=stream.avail_in;
    const auto result=inflate(&stream,Z_NO_FLUSH);const auto count=block.size()-stream.avail_out;
    if(count>static_cast<std::size_t>(128*1024-decoded.size())){return {};}decoded.append(block.data(),static_cast<qsizetype>(count));
    if(result==Z_STREAM_END){if(!stream.avail_in){return decoded;}if(++members>8){return {};}auto* next=stream.next_in;const auto available=stream.avail_in;
      if(inflateReset2(&stream,MAX_WBITS+16)!=Z_OK){return {};}stream.next_in=next;stream.avail_in=available;continue;
    }
    if(result!=Z_OK||(!count&&stream.avail_in==before)){return {};}
  }
}
}
Body response(const QByteArray& wire,bool eof){
  constexpr qsizetype HeaderLimit=16384,BodyLimit=128*1024;
  const auto split=wire.indexOf("\r\n\r\n");if(split<0){return {false,wire.size()>HeaderLimit||eof,{}};}
  if(split>HeaderLimit){return {false,true,{}};}const auto lines=wire.left(split).split('\n');
  if(lines.isEmpty()||(!lines[0].startsWith("HTTP/1.1 ")&&!lines[0].startsWith("HTTP/1.0 "))){return {false,true,{}};}
  bool statusOk=false;const auto code=lines[0].mid(9,3).toInt(&statusOk);if(!statusOk||lines[0].size()<13||lines[0][12]!=' '){return {false,true,{}};}
  std::optional<qulonglong> length;bool chunks=false,gzip=false;QByteArray location;std::set<QByteArray> uniqueHeaders;
  for(qsizetype i=1;i<lines.size();++i){auto line=lines[i];if(line.endsWith('\r')){line.chop(1);}const auto colon=line.indexOf(':');
    if(colon<1||line[0]==' '||line[0]=='\t'){return {false,true,{}};}const auto key=line.left(colon).toLower();const auto value=line.mid(colon+1).trimmed();
    if((key=="content-length"||key=="transfer-encoding"||key=="content-encoding"||key=="location")&&!uniqueHeaders.insert(key).second){return {false,true,{}};}
    if(key=="location"){location=value;}
    if(key=="content-length"){bool ok=false;const auto n=value.toULongLong(&ok);if(!ok||value.isEmpty()||!std::ranges::all_of(value,[](char c){return c>='0'&&c<='9';})||n>BodyLimit){return {false,true,{}};}length=n;}
    if(key=="transfer-encoding"){if(value.toLower()!="chunked"){return {false,true,{}};}chunks=true;}
    if(key=="content-encoding"){const auto encoding=value.toLower();if(encoding!="identity"&&encoding!="gzip"){return {false,true,{}};}gzip=encoding=="gzip";}
  }
  // HttpURLConnection2855 follows these ordinary same-protocol redirects.
  //305 would introduce a proxy,308 is outside the inspected JDK17 branch.
  if(code==300||code==301||code==302||code==303||code==307){if(location.isEmpty()){return {false,true,{}};}return {true,false,{},false,location};}
  if(code>=400&&code<=599){return {true,false,{},true};}if(code!=200){return {false,true,{}};}
  const auto complete=[&](QByteArray data)->Body{if(!gzip){return {true,false,std::move(data)};}auto decoded=gunzip(data);if(!decoded){return {false,true,{}};}return {true,false,std::move(*decoded)};};
  auto body=wire.mid(split+4);if(length&&chunks){return {false,true,{}};}
  if(chunks){QByteArray decoded;qsizetype at=0;for(;;){const auto end=body.indexOf("\r\n",at);if(end<0){return {false,eof,{}};}if(end-at>8){return {false,true,{}};}
      const auto sizeText=body.mid(at,end-at);bool ok=false;const auto n=sizeText.toULongLong(&ok,16);if(!ok||sizeText.isEmpty()||!std::ranges::all_of(sizeText,[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');})||n>static_cast<qulonglong>(BodyLimit-decoded.size())){return {false,true,{}};}at=end+2;
      if(n==0){if(body.size()<at+2){return {false,eof,{}};}if(body.mid(at)!="\r\n"){return {false,true,{}};}return complete(std::move(decoded));}
      if(static_cast<qulonglong>(body.size()-at)<n+2){return {false,eof,{}};}if(body.mid(at+static_cast<qsizetype>(n),2)!="\r\n"){return {false,true,{}};}
      decoded+=body.mid(at,static_cast<qsizetype>(n));at+=static_cast<qsizetype>(n)+2;
    }}
  if(length){if(static_cast<qulonglong>(body.size())>*length){return {false,true,{}};}if(static_cast<qulonglong>(body.size())!=*length){return {false,eof,{}};}return complete(std::move(body));}
  if(body.size()>BodyLimit){return {false,true,{}};}return eof?complete(std::move(body)):Body{};
}
core::QueueTrackerTerminal scrape(std::string_view encoded,std::span<const std::byte,20> hash,
    std::int64_t now,std::int64_t wall,std::int64_t started,bool singleOnly,unsigned fuzz){
  core::QueueTrackerTerminal out;out.observedMillis=now;out.observedWallMillis=wall;out.requestStartedWallMillis=started;
  auto doc=core::Document::parse(std::string(encoded),{128*1024,128*1024,8192,16});if(!doc||!unique(doc->root())||fuzz>179){return out;}
  const auto& root=doc->root();std::int32_t minimum=0;
  if(const auto* flags=root.find("flags")){if(!unique(*flags)){return out;}const auto n=number(*flags,"min_request_interval",0);if(!n){return out;}minimum=*n;}
  const auto* files=root.find("files");const core::Node* counts=nullptr;bool topLevel=false;
  if(files&&!unique(*files)){return out;}
  if(files&&!files->children.empty()){
    counts=files->find(std::string_view(reinterpret_cast<const char*>(hash.data()),20));
    if(!counts){out.definitiveFailure=true;out.regularDelayMillis=10800000;return out;}
  }else if(singleOnly&&root.find("complete")&&root.find("incomplete")){
    counts=&root;topLevel=true;const auto n=number(root,"interval",600000);if(!n){return out;}minimum=*n;
  }else if(const auto* failure=root.find("failure reason")){
    if(failure->type!=core::Node::Type::Bytes){return out;}out.definitiveFailure=true;
    out.regularDelayMillis=minimum==0?600000:std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(minimum)*1000u);return out;
  }else{out.definitiveFailure=true;out.regularDelayMillis=minimum==0?10800000:std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(minimum)*1000u);return out;}
  if(!counts||!unique(*counts)){return out;}
  const auto seeds=number(*counts,"complete",0),peers=number(*counts,"incomplete",0),downloaded=topLevel?std::optional<std::int32_t>{-1}:number(*counts,"downloaded",-1);
  if(!seeds||!peers||!downloaded){return out;}if(*seeds<0||*peers<0||*downloaded< -1){out.definitiveFailure=true;return out;}
  out.kind=core::QueueTrackerTerminalKind::Scrape;out.complete=*seeds;out.incomplete=*peers;out.downloaded=*downloaded;
  out.present=(counts->find("complete")?1u:0u)|(counts->find("incomplete")?2u:0u)|(!topLevel&&counts->find("downloaded")?4u:0u);
  out.comparable=true;out.regularDelayMillis=interval(minimum,*seeds,fuzz);return out;
}
} // namespace http_tracker_wire
} // namespace biglybtc::engine::detail

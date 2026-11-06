// SPDX-License-Identifier: GPL-2.0-or-later
#include "http_response.hpp"
#include <QList>
#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <zlib.h>

namespace biglybtc::services::http {
namespace {
// One explicitly selected Basic challenge. Digest/NTLM and ambiguous/multiple
// challenges require an authentication implementation beyond this saved cache.
std::optional<QByteArray> basicRealm(QByteArray input){
  if(input.size()<6||input.size()>512||input.first(6).compare("Basic ",Qt::CaseInsensitive)!=0){return {};}
  input=input.mid(6).trimmed();QByteArray realm;bool haveRealm=false,charset=false;
  while(!input.isEmpty()){
    const auto equal=input.indexOf('=');if(equal<=0){return {};}auto key=input.left(equal).trimmed().toLower();input=input.mid(equal+1).trimmed();QByteArray value;
    if(input.startsWith('"')){
      qsizetype at=1;bool closed=false;for(;at<input.size();++at){char c=input[at];if(c=='"'){closed=true;++at;break;}if(c=='\\'){return {};}
        if(static_cast<unsigned char>(c)<32||static_cast<unsigned char>(c)>126){return {};}value+=c;}
      if(!closed){return {};}input=input.mid(at).trimmed();
    }else{const auto comma=input.indexOf(',');value=input.left(comma<0?input.size():comma).trimmed();input=comma<0?QByteArray{}:input.mid(comma);
      if(value.isEmpty()||!std::ranges::all_of(value,[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||QByteArray("!#$%&'*+-.^_`|~").contains(static_cast<char>(c));})){return {};}}
    if(key=="realm"&&!haveRealm){if(value.size()>128||!std::ranges::all_of(value,[](unsigned char c){return c>=32&&c<=126;})){return {};}haveRealm=true;realm=value;}
    else if(key=="charset"&&!charset&&value.compare("UTF-8",Qt::CaseInsensitive)==0){charset=true;}
    else{return {};}
    if(input.isEmpty()){break;}if(!input.startsWith(',')){return {};}input=input.mid(1).trimmed();if(input.isEmpty()){return {};}
  }
  return haveRealm?std::optional<QByteArray>{realm}:std::nullopt;
}
std::optional<QByteArray> gunzip(const QByteArray& encoded,qsizetype bodyLimit){
  // zlib validates every gzip header/trailer, CRC32 and ISIZE. Accept a bounded
  // concatenation, never a partial member or trailing uninterpreted bytes.
  z_stream stream{};if(inflateInit2(&stream,MAX_WBITS+16)!=Z_OK){return {};}
  struct End{z_stream* s;~End(){inflateEnd(s);}} end{&stream};
  stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(encoded.constData()));stream.avail_in=static_cast<uInt>(encoded.size());
  QByteArray decoded;unsigned members=1;std::array<char,8192> block{};
  for(;;){stream.next_out=reinterpret_cast<Bytef*>(block.data());stream.avail_out=block.size();const auto before=stream.avail_in;
    const auto result=inflate(&stream,Z_NO_FLUSH);const auto count=block.size()-stream.avail_out;
    if(count>static_cast<std::size_t>(bodyLimit-decoded.size())){return {};}decoded.append(block.data(),static_cast<qsizetype>(count));
    if(result==Z_STREAM_END){if(!stream.avail_in){return decoded;}if(++members>8){return {};}auto* next=stream.next_in;const auto available=stream.avail_in;
      if(inflateReset2(&stream,MAX_WBITS+16)!=Z_OK){return {};}stream.next_in=next;stream.avail_in=available;continue;
    }
    if(result!=Z_OK||(!count&&stream.avail_in==before)){return {};}
  }
}
} // namespace
Response decodeResponse(const QByteArray& wire,bool eof,ResponseProfile profile){
  constexpr qsizetype HeaderLimit=16384;
  const qsizetype BodyLimit=profile==ResponseProfile::Tracker?128*1024:
      (profile==ResponseProfile::RssDocument||profile==ResponseProfile::TorrentMetainfo)?4*1024*1024:0;
  if(!BodyLimit){return {false,true,{}};}
  const auto split=wire.indexOf("\r\n\r\n");if(split<0){return {false,wire.size()>HeaderLimit||eof,{}};}
  if(split>HeaderLimit){return {false,true,{}};}const auto lines=wire.left(split).split('\n');
  if(lines.isEmpty()||(!lines[0].startsWith("HTTP/1.1 ")&&!lines[0].startsWith("HTTP/1.0 "))){return {false,true,{}};}
  bool statusOk=false;const auto code=lines[0].mid(9,3).toInt(&statusOk);if(!statusOk||lines[0].size()<13||lines[0][12]!=' '){return {false,true,{}};}
  std::optional<qulonglong> length;bool chunks=false,gzip=false;QByteArray location,authentication;std::set<QByteArray> uniqueHeaders;
  for(qsizetype i=1;i<lines.size();++i){auto line=lines[i];if(line.endsWith('\r')){line.chop(1);}const auto colon=line.indexOf(':');
    if(colon<1||line[0]==' '||line[0]=='\t'){return {false,true,{}};}const auto key=line.left(colon).toLower();const auto value=line.mid(colon+1).trimmed();
    if((key=="content-length"||key=="transfer-encoding"||key=="content-encoding"||key=="location"||key=="www-authenticate")&&!uniqueHeaders.insert(key).second){return {false,true,{}};}
    if(key=="location"){location=value;}
    if(key=="www-authenticate"){authentication=value;}
    if(key=="content-length"){bool ok=false;const auto n=value.toULongLong(&ok);if(!ok||value.isEmpty()||!std::ranges::all_of(value,[](char c){return c>='0'&&c<='9';})||n>static_cast<qulonglong>(BodyLimit)){return {false,true,{}};}length=n;}
    if(key=="transfer-encoding"){if(value.toLower()!="chunked"){return {false,true,{}};}chunks=true;}
    if(key=="content-encoding"){const auto encoding=value.toLower();if(encoding!="identity"&&encoding!="gzip"){return {false,true,{}};}gzip=encoding=="gzip";}
  }
  // HttpURLConnection2855 follows these ordinary same-protocol redirects.
  //305 would introduce a proxy,308 is outside the inspected JDK17 branch.
  if(code==300||code==301||code==302||code==303||code==307){if(location.isEmpty()){return {false,true,{}};}return {true,false,{},false,location};}
  if(code==401&&!authentication.isEmpty()){
    const auto realm=basicRealm(authentication);if(!realm){return {false,true,{}};}
    Response result;result.complete=true;result.basicRealm=*realm;result.basicAuthentication=true;return result;
  }
  if(code>=400&&code<=599){return {true,false,{},true};}if(code!=200){return {false,true,{}};}
  const auto complete=[&](QByteArray data)->Response{if(!gzip){return {true,false,std::move(data)};}auto decoded=gunzip(data,BodyLimit);if(!decoded){return {false,true,{}};}return {true,false,std::move(*decoded)};};
  auto body=wire.mid(split+4);if(length&&chunks){return {false,true,{}};}
  if(chunks){QByteArray decoded;qsizetype at=0;for(;;){const auto end=body.indexOf("\r\n",at);if(end<0){return {false,eof,{}};}if(end-at>8){return {false,true,{}};}
      const auto sizeText=body.mid(at,end-at);bool ok=false;const auto n=sizeText.toULongLong(&ok,16);if(!ok||sizeText.isEmpty()||!std::ranges::all_of(sizeText,[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');})||n>static_cast<qulonglong>(BodyLimit-decoded.size())){return {false,true,{}};}at=end+2;
      if(n==0){if(body.size()<at+2){return {false,eof,{}};}if(body.mid(at)!="\r\n"){return {false,true,{}};}return complete(std::move(decoded));}
      if(static_cast<qulonglong>(body.size()-at)<n+2){return {false,eof,{}};}if(body.mid(at+static_cast<qsizetype>(n),2)!="\r\n"){return {false,true,{}};}
      decoded+=body.mid(at,static_cast<qsizetype>(n));at+=static_cast<qsizetype>(n)+2;
    }}
  if(length){if(static_cast<qulonglong>(body.size())>*length){return {false,true,{}};}if(static_cast<qulonglong>(body.size())!=*length){return {false,eof,{}};}return complete(std::move(body));}
  if(body.size()>BodyLimit){return {false,true,{}};}return eof?complete(std::move(body)):Response{};
}
} // namespace biglybtc::services::http

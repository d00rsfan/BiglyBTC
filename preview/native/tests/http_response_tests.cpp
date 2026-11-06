// SPDX-License-Identifier: GPL-2.0-or-later
// Pure owning HTTP values: no socket, resolver, profile, grant or fake torrent.
#include "http_response.hpp"
#include <QByteArray>
#include <array>
#include <iostream>
#include <stdexcept>
#include <zlib.h>

namespace h=biglybtc::services::http;
namespace {
constexpr qsizetype TrackerBytes=128*1024,RssBytes=4*1024*1024;
void check(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
QByteArray fixed(const QByteArray& body){return "HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body;}
QByteArray compressed(const QByteArray& body){
  z_stream stream{};
  check(deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,MAX_WBITS+16,8,Z_DEFAULT_STRATEGY)==Z_OK,"Fixture gzip initialization");
  struct End{z_stream* stream;~End(){deflateEnd(stream);}} end{&stream};
  stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(body.constData()));stream.avail_in=static_cast<uInt>(body.size());
  QByteArray result;std::array<char,8192> block{};
  for(;;){stream.next_out=reinterpret_cast<Bytef*>(block.data());stream.avail_out=block.size();const int code=deflate(&stream,Z_FINISH);
    result.append(block.data(),static_cast<qsizetype>(block.size()-stream.avail_out));
    if(code==Z_STREAM_END){return result;}check(code==Z_OK,"Fixture gzip failed");}
}
QByteArray gzipWire(const QByteArray& encoded){return "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: "+QByteArray::number(encoded.size())+"\r\n\r\n"+encoded;}
void owningPartialBodies(){
  auto wire=fixed("owned");auto value=h::decodeResponse(wire,false);
  check(value.complete&&!value.unsupported&&!value.failed&&value.bytes=="owned","Complete fixed body");
  wire.fill('x');check(value.bytes=="owned","Returned bytes borrowed caller input");
  check(!h::decodeResponse("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\now",false).complete,"Incomplete body became complete");
  check(h::decodeResponse("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\now",true).unsupported,"Truncated fixed body accepted at EOF");
  check(!h::decodeResponse("HTTP/1.1 200 OK\r\n\r\nclose",false).complete,"Close-delimited body completed before EOF");
  check(h::decodeResponse("HTTP/1.1 200 OK\r\n\r\nclose",true).bytes=="close","Close-delimited EOF body lost");
}
void closedBodyProfiles(){
  const QByteArray tracker(TrackerBytes,'t');check(h::decodeResponse(fixed(tracker),true).bytes==tracker,"Default tracker128KiB boundary changed");
  const QByteArray larger(TrackerBytes+1,'t');check(h::decodeResponse(fixed(larger),true).unsupported,"Default tracker limit widened");
  check(h::decodeResponse(fixed(larger),true,h::ResponseProfile::RssDocument).bytes==larger,"RSS profile borrowed tracker cap");
  const QByteArray rss(RssBytes,'r');check(h::decodeResponse(fixed(rss),true,h::ResponseProfile::RssDocument).bytes==rss,"RSS4MiB boundary refused");
  check(h::decodeResponse(fixed(QByteArray(RssBytes+1,'r')),true,h::ResponseProfile::RssDocument).unsupported,"RSS cap raised beyond parser ceiling");
}
void strictChunkedFraming(){
  const auto chunks=QByteArray("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\n1\r\nc\r\n0\r\n\r\n");
  check(h::decodeResponse(chunks,true).bytes=="abc","Chunk order/content changed");
  check(h::decodeResponse(chunks.left(chunks.size()-1),true).unsupported,"Truncated chunk terminator accepted");
  check(h::decodeResponse(chunks+"extra",true).unsupported,"Trailing chunk data accepted");
  check(h::decodeResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;ext=x\r\na\r\n0\r\n\r\n",true).unsupported,"Unsupported chunk extension silently changed");
  const QByteArray body(RssBytes,'c');const auto wire=QByteArray("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")+QByteArray::number(body.size(),16)+"\r\n"+body+"\r\n0\r\n\r\n";
  check(h::decodeResponse(wire,true,h::ResponseProfile::RssDocument).bytes==body,"RSS exact chunk cap refused");
  check(h::decodeResponse(wire,true).unsupported,"Chunked body bypassed tracker cap");
  check(h::decodeResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n400001\r\n",false,h::ResponseProfile::RssDocument).unsupported,"Over-limit chunk allocation accepted");
}
void boundedGzipProfilesAndMembers(){
  const QByteArray rss(RssBytes,'g');const auto encoded=compressed(rss);
  check(h::decodeResponse(gzipWire(encoded),true,h::ResponseProfile::RssDocument).bytes==rss,"RSS decoded gzip cap refused");
  check(h::decodeResponse(gzipWire(encoded),true).unsupported,"Gzip expansion bypassed tracker cap");
  check(h::decodeResponse(gzipWire(compressed(QByteArray(RssBytes+1,'g'))),true,h::ResponseProfile::RssDocument).unsupported,"Gzip expansion bypassed RSS cap");
  auto corrupt=encoded;corrupt[corrupt.size()-8]=static_cast<char>(corrupt[corrupt.size()-8]^1);
  check(h::decodeResponse(gzipWire(corrupt),true,h::ResponseProfile::RssDocument).unsupported,"Gzip CRC ignored");
  check(h::decodeResponse(gzipWire(encoded.left(encoded.size()-1)),true,h::ResponseProfile::RssDocument).unsupported,"Truncated gzip member accepted");
  QByteArray members;const auto one=compressed("m");for(int i=0;i<8;++i){members+=one;}
  check(h::decodeResponse(gzipWire(members),true).bytes==QByteArray(8,'m'),"Eight gzip members refused");
  check(h::decodeResponse(gzipWire(members+one),true).unsupported,"Ninth gzip member accepted");
  check(h::decodeResponse(gzipWire(one+"trailing"),true).unsupported,"Trailing gzip bytes accepted");
}
void headerLimitsAndAmbiguity(){
  const auto prefix=QByteArray("HTTP/1.1 200 OK\r\nX-Pad: "),suffix=QByteArray("\r\nContent-Length: 0");
  const auto exact=prefix+QByteArray(16384-prefix.size()-suffix.size(),'a')+suffix;
  check(h::decodeResponse(exact+"\r\n\r\n",true).complete,"Exact16KiB header boundary refused");
  const auto over=prefix+QByteArray(16385-prefix.size()-suffix.size(),'a')+suffix;
  check(h::decodeResponse(over+"\r\n\r\n",true).unsupported,"Header cap widened");
  check(h::decodeResponse(QByteArray(16385,'a'),false).unsupported,"Unterminated header cap ignored");
  check(h::decodeResponse("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",true).unsupported,"Duplicate length accepted");
  check(h::decodeResponse("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",true).unsupported,"Ambiguous transfer framing accepted");
  check(h::decodeResponse("HTTP/1.1 200 OK\r\nContent-Encoding: br\r\nContent-Length: 0\r\n\r\n",true).unsupported,"Unsupported encoding accepted");
}
void responseStatusAndChallengeValues(){
  const auto redirect=h::decodeResponse("HTTP/1.1 302 Found\r\nLocation: /later\r\n\r\n",false);
  check(redirect.complete&&redirect.location=="/later"&&!redirect.failed,"Redirect proposal value changed");
  const auto challenge=h::decodeResponse("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"owned\"\r\n\r\n",false);
  check(challenge.complete&&challenge.basicAuthentication&&challenge.basicRealm=="owned","Basic challenge parsing changed");
  check(h::decodeResponse("HTTP/1.1 404 Missing\r\n\r\n",true).failed,"HTTP failure became empty success");
  check(h::decodeResponse("HTTP/1.1 304 Not Modified\r\n\r\n",true,h::ResponseProfile::RssDocument).unsupported,"Unsolicited304 became empty document");
}
void invalidProfileRefused(){
  const auto value=h::decodeResponse(fixed("x"),true,static_cast<h::ResponseProfile>(2));
  check(value.unsupported&&!value.complete&&value.bytes.isEmpty(),"Unknown closed profile widened body policy");
}
}
int main(){try{
  owningPartialBodies();closedBodyProfiles();strictChunkedFraming();boundedGzipProfilesAndMembers();headerLimitsAndAmbiguity();responseStatusAndChallengeValues();invalidProfileRefused();
  std::cout<<"PASS bounded HTTP response values7families\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

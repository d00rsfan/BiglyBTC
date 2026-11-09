// SPDX-License-Identifier: GPL-2.0-or-later
#include "ledger.hpp"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <filesystem>
#include <fstream>
#include <zlib.h>
namespace msg=biglybtc::services::msgsync;
namespace identity=biglybtc::plugins::msgsync;
namespace {
void require(bool ok,const char* reason){if(!ok){throw std::runtime_error(reason);}}
template<class T,class E>T take(std::expected<T,E> value){require(value.has_value(),"Required bounded operation failed");return std::move(*value);}
std::string publicKey(){
  // Public test scalar ONE and standard prime192v2 generator, already used by
  // the retained STS unit. Never an application identity or private user data.
  constexpr unsigned char generator[]{0x03,0xee,0xa2,0xba,0xe7,0xe1,0x49,0x78,0x42,0xf2,0xde,0x77,0x69,0xcf,0xe9,0xc9,0x89,0xc0,0x72,0xad,0x69,0x6f,0x48,0x03,0x4a};
  return {reinterpret_cast<const char*>(generator),sizeof(generator)};
}
msg::Message signedMessage(const identity::MessageSigner& signer,unsigned id,std::int32_t age=0){
  msg::Message m;m.uid.fill(0x41);m.id.fill(0);for(unsigned n=0;n<4;++n){m.id[7-n]=static_cast<std::uint8_t>(id>>(8*n));}
  m.ageSeconds=age;m.content=std::to_string(id);m.publicKey=std::string(signer.publicKey());m.signature=take(signer.signMessage(take(msg::signedBytes(m))));return m;
}
void messages(identity::MessageSigner& signer){
  auto m=signedMessage(signer,7);m.content=std::string("\0\xffhello",7);m.control=std::string("\0$\xff",3);m.signature=take(signer.signMessage(take(msg::signedBytes(m))));
  m.history=std::string(80,'h');m.ageSeconds=17;m.encodedContact="d1:xi7ee";
  auto bytes=take(msg::encodeMessage(m));auto round=take(msg::decodeMessage(bytes));
  require(round.content==m.content&&round.control==m.control&&round.signature==m.signature&&round.history==m.history&&round.encodedContact==m.encodedContact,"Original message bytes differ");
  auto checked=take(msg::verifyMessage(round,signer.publicKey()));require(checked.senderKey()==signer.publicKey(),"Verified candidate differs");
  // These fields are genuinely unsigned in the original protocol. They must
  // neither change authentication nor become room/contact/age authority.
  round.ageSeconds=100000;round.history="forged unsigned history";round.publicKey="wrong unsigned key hint";round.encodedContact="de";
  require(msg::verifyMessage(round,signer.publicKey()).has_value(),"Unsigned hints incorrectly entered signature");
  round.content.at(0)^=1;require(!msg::verifyMessage(round,signer.publicKey()),"Tampered content accepted");round=m;round.control->at(0)^=1;require(!msg::verifyMessage(round,signer.publicKey()),"Tampered control accepted");
  round=m;round.uid[0]^=1;require(!msg::verifyMessage(round,signer.publicKey()),"Wrong UID accepted");round=m;round.id[0]^=1;require(!msg::verifyMessage(round,signer.publicKey()),"Wrong message ID accepted");
  require(!msg::verifyMessage(m,{}),"Missing expected candidate key accepted");round=m;round.content.resize(601);require(!msg::encodeMessage(round),"Oversize normal content accepted");
  require(!msg::decodeMessage("d1:ai0e1:ai1ee"),"Duplicate map field accepted");
  round=m;round.ageSeconds=-1;require(take(msg::decodeMessage(take(msg::encodeMessage(round)))).ageSeconds==0,"Original negative age clamp differs");
  auto moved=std::move(signer);require(!signer.signMessage("x")&&signer.publicKey().empty(),"Moved signer retains capability");
  signer=std::move(moved);require(!moved.signMessage("x"),"Move assignment retained old capability");
}
void blooms(){
  auto b=take(msg::AddOnlyBloom::create(64));require(b.bits()==65&&b.count()==0,"Original odd bloom size differs");
  std::string key("\0\xff\x80\x7f" "ABCD",8);require(!b.contains(key),"Empty bloom contains key");b.add(key);require(b.contains(key)&&b.count()==1,"Bloom insert differs");b.add(key);require(b.count()==1,"Duplicate bloom insert increments count");
  auto encoded=take(b.encode());auto doc=take(biglybtc::core::Document::parse(encoded));auto decoded=take(msg::AddOnlyBloom::decode(doc.root()));require(decoded.map()==b.map()&&decoded.contains(key),"Serialized original bloom differs");
  require(!msg::AddOnlyBloom::create(0)&&!msg::AddOnlyBloom::create(8194),"Bloom bounds accepted");
  const std::vector<std::string> keys{key,key,"second bloom key","third bloom key"};auto built=take(msg::buildSyncBloom(keys));for(const auto& value:keys){require(built.filter.contains(take(msg::maskBloomKey(value,built.random))),"Built bloom lost a key");}
}
void sync(identity::MessageSigner& signer){
  auto bloom=take(msg::AddOnlyBloom::create(64));msg::SyncRequest request{5,{},std::array<std::uint8_t,6>{1,2,3,4,5,6},{},std::move(bloom),128,-1,17};request.uid.fill(7);request.random.fill(0x80);
  auto wire=take(msg::encodeSyncRequest(request));auto decoded=take(msg::decodeSyncRequest(wire));require(decoded.requestId==request.requestId&&decoded.random==request.random&&decoded.messages==128&&decoded.newMessages==-1&&decoded.oldestAgeSeconds==17,"SYNC request differs");
  msg::RequestReplay replay;require(take(replay.remember(decoded))&&!take(replay.remember(decoded)),"Request-ID duplicate not refused");request.requestId.reset();request.version=4;require(take(replay.remember(request))&&!take(replay.remember(request)),"Original random fallback differs");
  request.version=3;require(!msg::encodeSyncRequest(request),"Unsupported old SYNC accepted");request.version=5;
  for(unsigned n=0;n<513;++n){request.random.fill(0);request.random[6]=static_cast<std::uint8_t>(n>>8);request.random[7]=static_cast<std::uint8_t>(n);require(take(replay.remember(request)),"Fresh bounded request refused");}
  require(replay.size()==512,"Replay LRU exceeds bound");request.random.fill(0);require(take(replay.remember(request)),"Oldest request was not evicted");
  msg::SyncReply reply;for(unsigned n=0;n<8;++n){auto m=signedMessage(signer,n);m.content=std::string(500,'a');m.signature=take(signer.signMessage(take(msg::signedBytes(m))));reply.messages.push_back(std::move(m));}reply.moreToCome=11;
  const auto compressed=take(msg::encodeSyncReply(reply));auto top=take(biglybtc::core::Document::parse(compressed));require(top.find("z")&&!top.find("m"),"Original beneficial gzip choice missing");
  auto round=take(msg::decodeSyncReply(compressed));require(round.messages.size()==8&&round.moreToCome==11&&round.messages[0].content==reply.messages[0].content,"Gzip reply differs");
  for(const auto& m:round.messages){require(msg::verifyMessage(m,signer.publicKey()).has_value(),"Reply signature differs");}
  auto plain=take(msg::encodeSyncReply(reply,false));require(take(msg::decodeSyncReply(plain)).messages.size()==8,"Plain reply differs");
  // CRC/trailer are checked by the actual zlib gzip reader.
  std::string damaged(top.find("z")->bytes);require(damaged.size()>=18,"Missing actual gzip trailer");damaged.at(damaged.size()-8)^=1;
  require(top.replaceValue("z",std::to_string(damaged.size())+":"+damaged).has_value()&&!msg::decodeSyncReply(top.serialize()),"Invalid gzip CRC accepted");
  reply.messages.push_back(reply.messages.front());require(!msg::encodeSyncReply(reply),"Reply content target exceeded");
  const std::string huge(65537,'x');z_stream stream{};require(deflateInit2(&stream,9,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)==Z_OK,"Cannot construct bounded bomb control");
  std::string out(4096,'\0');stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(huge.data()));stream.avail_in=static_cast<uInt>(huge.size());stream.next_out=reinterpret_cast<Bytef*>(out.data());stream.avail_out=static_cast<uInt>(out.size());const auto status=deflate(&stream,Z_FINISH);const auto size=stream.total_out;deflateEnd(&stream);require(status==Z_STREAM_END,"Cannot finish bounded bomb control");out.resize(size);
  require(!msg::decodeSyncReply("d1:si1e1:ti1e1:z"+std::to_string(out.size())+":"+out+"e"),"64KiB inflate bound exceeded");
}
void ledger(identity::MessageSigner& signer){
  msg::MessageLedger l;for(const auto [id,age]:std::array<std::pair<unsigned,int>,4>{{{1,20},{2,10},{3,30},{4,20}}}){require(take(l.add(take(msg::verifyMessage(signedMessage(signer,id,age),signer.publicKey())),100000))==msg::AddOutcome::Added,"Ordered insertion refused");}
  auto rows=take(l.snapshot(100000));require(rows[0].message.content=="3"&&rows[1].message.content=="1"&&rows[2].message.content=="4"&&rows[3].message.content=="2","Original age/tie order differs");
  require(take(l.add(take(msg::verifyMessage(rows[1].message,signer.publicKey())),100000))==msg::AddOutcome::Duplicate,"Signature replay retained");
  auto older=take(l.snapshot(99000));require(older[0].message.ageSeconds==30,"Wall rollback reset differs");auto elapsed=take(l.snapshot(101000));require(elapsed[0].message.ageSeconds==32,"Elapsed age differs");
  require(!l.snapshot(-1),"Invalid wall clock accepted");l.clear();
  auto first=signedMessage(signer,0);std::optional<msg::Message> tombstoned;
  for(unsigned n=0;n<260;++n){auto m=n==0?first:signedMessage(signer,n);if(n==131){tombstoned=m;}require(take(l.add(take(msg::verifyMessage(std::move(m),signer.publicKey())),100000))==msg::AddOutcome::Added,"New signature insertion refused");}
  require(l.size()==128&&l.tombstones()==128,"Bounded live/tombstone size differs");
  auto recent=take(l.snapshot(100000));require(recent.front().message.content=="132"&&recent.back().message.content=="259","Oldest eviction differs");
  require(tombstoned&&take(l.add(take(msg::verifyMessage(*tombstoned,signer.publicKey())),100000))==msg::AddOutcome::Tombstoned,"Evicted signature replay retained");
  auto oldest=signedMessage(signer,900,1000);require(take(l.add(take(msg::verifyMessage(std::move(oldest),signer.publicKey())),100000))==msg::AddOutcome::TooOld&&l.tombstones()==128,"Ancient input flushed full tombstones");
  const auto contacts=std::array{msg::ContactBloomKey{signer.publicKey(),"/127.0.0.1:12345"}};require(l.buildBloom(contacts).has_value(),"Ledger bloom failed");
  l.clear();auto wrap=signedMessage(signer,5,std::numeric_limits<std::int32_t>::max());take(l.add(take(msg::verifyMessage(std::move(wrap),signer.publicKey())),100000));require(take(l.snapshot(101000))[0].message.ageSeconds==std::numeric_limits<std::int32_t>::min(),"Java int age wrap differs");
}
std::string readFixture(const std::filesystem::path& path){
  require(std::filesystem::is_regular_file(path)&&!std::filesystem::is_symlink(path)&&std::filesystem::file_size(path)<=65536,"Unsafe or unbounded original fixture");
  std::ifstream in(path,std::ios::binary);require(in.good(),"Cannot open original fixture");std::string bytes(std::istreambuf_iterator<char>(in),{});require(!in.bad()&&bytes.size()<=65536,"Cannot read original fixture");return bytes;
}
void writeFixture(const std::filesystem::path& path,const std::string& bytes){
  require(!std::filesystem::exists(path)&&bytes.size()<=65536,"Original output already exists or is unbounded");std::ofstream out(path,std::ios::binary);require(out.good(),"Cannot create original output");out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();require(out.good(),"Cannot write original output");
}
biglybtc::core::Node bbytes(std::string_view value){biglybtc::core::Node n(biglybtc::core::Node::Type::Bytes);n.bytes=value;return n;}
biglybtc::core::Node bint(std::int64_t value){biglybtc::core::Node n(biglybtc::core::Node::Type::Integer);n.integer=value;return n;}
void original(const std::filesystem::path& root){
  require(root.is_absolute()&&std::filesystem::is_directory(root),"Absolute owned original-fixture root required");
  auto publicBytes=readFixture(root/"public.raw");require(readFixture(root/"scalar-one.known-test-key")==std::string(1,'\1'),"Only public test scalar ONE allowed");auto signer=take(identity::MessageSigner::open(publicBytes,std::string(1,'\1')));
  for(unsigned id=0;id<4;++id){
    const auto stem="message-"+std::to_string(id);auto m=take(msg::decodeMessage(readFixture(root/(stem+".java"))));require(msg::verifyMessage(m,publicBytes).has_value(),"Actual original signature rejected");
    const auto signedBody=take(msg::signedBytes(m));m.signature=take(signer.signMessage(signedBody));writeFixture(root/(stem+".native"),take(msg::encodeMessage(m)));
    std::cout<<"PASS NATIVE VERIFIED JAVA "<<stem<<'\n';
  }
  for(const std::string mode:{"plain","gzip"}){
    const auto stem="reply-"+mode;auto r=take(msg::decodeSyncReply(readFixture(root/(stem+".java"))));require(r.messages.size()==8&&r.moreToCome==11,"Actual original reply fields differ");
    for(auto& m:r.messages){require(msg::verifyMessage(m,publicBytes).has_value(),"Actual original reply signature rejected");m.signature=take(signer.signMessage(take(msg::signedBytes(m))));}
    writeFixture(root/(stem+".native"),take(msg::encodeSyncReply(r,mode=="gzip")));std::cout<<"PASS NATIVE VERIFIED JAVA "<<stem<<'\n';
  }
  for(const std::string version:{"v4","v5"}){
    const auto stem="request-"+version;auto r=take(msg::decodeSyncRequest(readFixture(root/(stem+".java"))));require(r.version==(version=="v4"?4:5)&&r.requestId.has_value()==(version=="v5")&&r.messages==128&&r.newMessages==-1,"Actual original request fields differ");writeFixture(root/(stem+".native"),take(msg::encodeSyncRequest(r)));std::cout<<"PASS NATIVE VERIFIED JAVA "<<stem<<'\n';
  }
  auto expected=take(biglybtc::core::Document::parse(readFixture(root/"bloom.java")));auto bloom=take(msg::AddOnlyBloom::create(64));
  for(const auto& key:std::array{std::string("\0\xff\x80\x7f" "ABCD",8),std::string(25,static_cast<char>(0x80)),std::string("second bloom key")}){bloom.add(key);}
  auto actual=take(bloom.encode());require(actual==take(biglybtc::core::encodeValue(expected.root())),"Actual original bloom hashes/bytes differ");writeFixture(root/"bloom.native",actual);
  msg::MessageLedger ledger;
  for(const auto [id,age]:std::array<std::pair<unsigned,int>,4>{{{1,20},{2,10},{3,30},{4,20}}}){
    auto m=signedMessage(signer,id,age);m.id.fill(static_cast<std::uint8_t>(id));m.signature=take(signer.signMessage(take(msg::signedBytes(m))));take(ledger.add(take(msg::verifyMessage(std::move(m),publicBytes)),100000));
  }
  auto rows=take(ledger.snapshot(100000));std::vector<std::string> ids;ids.reserve(rows.size());for(const auto& row:rows){ids.emplace_back(reinterpret_cast<const char*>(row.message.id.data()),row.message.id.size());}
  biglybtc::core::Node list(biglybtc::core::Node::Type::List);for(const auto& id:ids){list.children.push_back(bbytes(id));}
  auto negative=signedMessage(signer,5,-1);auto clamped=take(msg::decodeMessage(take(msg::encodeMessage(negative))));
  auto replayBloom=take(msg::AddOnlyBloom::create(64));msg::SyncRequest req{5,{},std::array<std::uint8_t,6>{1,2,3,4,5,6},{},std::move(replayBloom),0,0,std::nullopt};msg::RequestReplay replay;
  require(take(replay.remember(req))&&!take(replay.remember(req)),"Original q replay differs");req.requestId.reset();req.random.fill(3);require(take(replay.remember(req))&&!take(replay.remember(req)),"Original r replay differs");
  biglybtc::core::Node facts(biglybtc::core::Node::Type::Dictionary);facts.children={bbytes("negative_age"),bint(clamped.ageSeconds),bbytes("order"),std::move(list),bbytes("request_ids"),bint(static_cast<std::int64_t>(replay.size()))};
  auto encoded=take(biglybtc::core::encodeValue(facts));require(encoded==readFixture(root/"bookkeeping.java"),"Original ordering/replay facts differ");writeFixture(root/"bookkeeping.native",encoded);std::cout<<"PASS NATIVE VERIFIED JAVA bloom bookkeeping\n";
}
}
int main(int argc,char** argv){try{
  if(argc==3&&std::string_view(argv[1])=="--original-interop"){original(argv[2]);return 0;}require(argc==1,"Usage: msgsync-message-tests [--original-interop OWNED_ROOT]");
  static_assert(!std::is_copy_constructible_v<identity::MessageSigner>);
  auto signer=take(identity::MessageSigner::open(publicKey(),std::string(1,'\1')));messages(signer);blooms();sync(signer);ledger(signer);signer.clear();require(!signer.signMessage("x"),"Cleared signer signs");
  std::cout<<"PASS original signed-body fields/unsigned hints, SYNC/bloom/gzip bounds, replay and age/tombstone bookkeeping\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "query_storage.hpp"
#include "database.hpp"
#include <QTest>
#include <QFile>
#include <set>
using namespace biglybtc;namespace query=services::azureus_query_storage;namespace db=services::azureus_db_runtime;namespace nodes=services::azureus_nodes;
namespace {
struct Clock final:db::lifecycle::WallClock {unsigned calls=0;std::int64_t now=1000;std::int64_t nowMilliseconds()noexcept override{++calls;return now;}};
nodes::Contact contact(unsigned n){nodes::ResolvedEndpoint ep;ep.address.bytes[0]=1;ep.address.bytes[1]=std::uint8_t(n);ep.address.bytes[3]=1;ep.address.port=4000;nodes::NodeCache cache;auto id=cache.lookup(ep,55);if(!id){throw std::runtime_error("Generated query contact");}nodes::Contact c;c.protocol=55;c.address=ep.address;c.id=id->id;return c;}
struct Fixture {Clock clock;std::unique_ptr<db::Owner> owner;nodes::Contact local=contact(1),remote=contact(2);Fixture(){auto made=db::Owner::create();if(!made){throw std::runtime_error("Generated query database");}owner=std::move(*made);if(!owner->bindLocal(local)){throw std::runtime_error("Generated query binding");}}};
query::udp::EncodeFields header(const query::udp::Header& h){query::udp::EncodeFields f;f.kind=h.kind;f.localVersion=55;f.remoteVersion=h.version;f.network=h.network;f.action=h.action;f.transaction=h.transaction;f.instance=h.instance;f.connection=h.connection;f.now=h.time;f.originator=h.originator;f.flags=h.flags;f.flags2=h.flags2;return f;}
}
class AzureusQueryStorageTests final:public QObject {Q_OBJECT
private slots:
 void actualOriginalPacketsRoundtripAndRefuseTruncation(){
  QFile file(QString::fromUtf8(QUERY_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));unsigned count=0;while(!file.atEnd()){const auto fields=file.readLine().trimmed().split('\t');QCOMPARE(fields.size(),3);const auto kind=fields[1]=="request"?query::udp::Kind::Request:query::udp::Kind::Reply;const auto raw=QByteArray::fromHex(fields[2]);const std::string wire(raw.constData(),raw.size());query::udp::Context context;context.now=1000;auto decoded=query::decode(kind,wire,context);QVERIFY2(decoded,fields[0].constData());QCOMPARE(decoded->consumed(),wire.size());QVERIFY(decoded->trailing().empty());auto f=header(decoded->header().fields());std::expected<std::string,query::Error> encoded=kind==query::udp::Kind::Request?query::encodeRequest({f,std::get<query::RequestBody>(decoded->body())}):query::encodeReply({f,std::get<query::ReplyBody>(decoded->body())});QVERIFY(encoded);QCOMPARE(*encoded,wire);
   for(std::size_t n=0;n<wire.size();++n){QVERIFY2(!query::decode(kind,wire.substr(0,n),context),fields[0].constData());}auto trailing=query::decode(kind,wire+"tail",context);QVERIFY(trailing);QCOMPARE(trailing->consumed(),wire.size());QCOMPARE(trailing->trailing(),std::string_view("tail"));++count;
  }QCOMPARE(count,34u);
 }
 void malformedCountsWidthsNetworksAndLimitsFailBeforeExpansion(){
  query::RequestFields f;f.header.connection=INT64_MIN;f.body.groups={{"abc",{"def"}}};auto wire=query::encodeRequest(f);QVERIFY(wire);auto h=query::udp::decodeHeader(query::udp::Kind::Request,*wire,{});QVERIFY(h);const auto body=h->consumed();auto bad=*wire;bad[body+1]=char(255);bad[body+2]=char(255);QVERIFY(!query::decode(query::udp::Kind::Request,bad,{}));bad=*wire;bad[body+3]=7;QVERIFY(!query::decode(query::udp::Kind::Request,bad,{}));query::udp::Context wrong;wrong.network=1;QVERIFY(!query::decode(query::udp::Kind::Request,*wire,wrong));bad=*wire;bad[16]=50;QVERIFY(!query::decode(query::udp::Kind::Request,bad,{}));
  f.body.headerLength=0;f.body.groups={{"",std::vector<std::string>(8193)}};QVERIFY(!query::encodeRequest(f));f.body.groups={{"a",{}}};QVERIFY(!query::encodeRequest(f));f.body.headerLength=6;f.body.groups={{"abc",{"bad-width"}}};QVERIFY(!query::encodeRequest(f));f.header.action=1024;QVERIFY(!query::encodeRequest(f));query::ReplyFields r;r.body.entries={std::string("bad-width")};QVERIFY(!query::encodeReply(r));r.body.entries.assign(8193,std::nullopt);QVERIFY(!query::encodeReply(r));
  query::udp::EncodeFields raw;raw.connection=INT64_MIN;raw.action=1038;std::string expansion("\0\0\1\0\x7f\xff",6);auto bomb=query::udp::encodeHeader(raw,expansion);QVERIFY(bomb);QVERIFY(!query::decode(query::udp::Kind::Request,*bomb,{}));
 }
 void bitmapSpareBytesAreOriginalWireDataAndDoNotCreateEntries(){
  query::ReplyFields f;f.header.connection=INT64_MIN;f.body.entries.resize(9);f.body.entries[0]="ab";f.body.entries[8]="cd";auto wire=query::encodeReply(f);QVERIFY(wire);auto h=query::udp::decodeHeader(query::udp::Kind::Reply,*wire,{});QVERIFY(h);QCOMPARE(wire->size()-h->consumed(),std::size_t{2+1+9+4});auto modified=*wire;modified[h->consumed()+3+8]=char(255);auto decoded=query::decode(query::udp::Kind::Reply,modified,{});QVERIFY(decoded);QCOMPARE(std::get<query::ReplyBody>(decoded->body()),f.body);
 }
 void prefixQueriesReturnTailsWithoutOrdinaryReadsOrClocks(){
  Fixture f;std::string a(20,'P');a[18]='a';a[19]='b';QVERIFY(f.owner->putLocal(a,"payload",1,f.clock));const auto before=f.clock.calls;const auto counters=f.owner->snapshot();const std::vector<std::string> prefixes{a.substr(0,6),std::string(6,'X'),a.substr(0,6)};auto rows=f.owner->queryStoredPrefixes(prefixes);QVERIFY(rows&&rows->size()==3);QCOMPARE((*rows)[0],std::optional<std::string>("ab"));QVERIFY(!(*rows)[1]);QCOMPARE((*rows)[2],(*rows)[0]);QCOMPARE(f.clock.calls,before);QCOMPARE(f.owner->snapshot()->reads,counters->reads);QCOMPARE(f.owner->snapshot()->stores,counters->stores);QVERIFY(!f.owner->queryStoredPrefixes(std::vector<std::string>{"short"}));QVERIFY(!f.owner->queryStoredPrefixes(std::vector<std::string>(8193,"abcdef")));f.owner->close();QVERIFY(!f.owner->queryStoredPrefixes(prefixes));
 }
 void collisionsUseClosestKeyAndWinnerDeletionHasNoFallback(){
  for(bool reverse:{false,true}){Fixture f;std::string near(20,'P');near[6]=char(f.local.id[6]);near[18]='a';near[19]='1';auto far=near;far[6]=char(f.local.id[6]^255);far[18]='b';far[19]='2';for(const auto& key:reverse?std::vector<std::string>{near,far}:std::vector<std::string>{far,near}){QVERIFY(f.owner->putLocal(key,"local",1,f.clock));}const std::vector<std::string> prefixes{near.substr(0,6)};QCOMPARE(f.owner->queryStoredPrefixes(prefixes)->at(0),std::optional<std::string>("a1"));QVERIFY(f.owner->removeLocal(near,2,f.clock));QVERIFY(f.owner->contains(far).value());QVERIFY(!f.owner->queryStoredPrefixes(prefixes)->at(0));QVERIFY(f.owner->removeLocal(far,2,f.clock));QVERIFY(f.owner->putLocal(far,"again",3,f.clock));QCOMPARE(f.owner->queryStoredPrefixes(prefixes)->at(0),std::optional<std::string>("b2"));}
 }

 void actualOriginalPrefixIndexTransitionsMatch(){
  QFile file(QString::fromUtf8(QUERY_DB_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));std::unique_ptr<Fixture> f;QByteArray current;std::string near,far;unsigned count=0,version=0;
  while(!file.atEnd()){const auto fields=file.readLine().trimmed().split('\t');QCOMPARE(fields.size(),3);if(fields[0]!=current){current=fields[0];f=std::make_unique<Fixture>();near=std::string(20,'P');near[6]=char(f->local.id[6]);near[18]='a';near[19]='1';far=near;far[6]=char(f->local.id[6]^255);far[18]='b';far[19]='2';version=0;}
   const auto& key=fields[1].endsWith("Near")?near:far;if(fields[1].startsWith("put")){QVERIFY(f->owner->putLocal(key,"value",++version,f->clock));}else if(fields[1].startsWith("remove")){QVERIFY(f->owner->removeLocal(key,++version,f->clock));}const std::vector<std::string> prefixes{near.substr(0,6)};auto rows=f->owner->queryStoredPrefixes(prefixes);QVERIFY(rows&&rows->size()==1);const auto actual=(*rows)[0]?QByteArray::fromStdString(*(*rows)[0]).toHex():QByteArray("-");QCOMPARE(actual,fields[2]);++count;
  }QCOMPARE(count,18u);
 }
 void expiryAndLocalRemovalRespectActualRawRemoteEntries(){
  Fixture f;const std::string key(20,'E');services::azureus_values::Value value{1,1000,"",f.remote,0,1,-1};const std::vector<services::azureus_values::Value> values{value};QVERIFY(f.owner->putLocal(key,"local",1,f.clock));QVERIFY(f.owner->store(key,f.remote,values,f.clock));QVERIFY(f.owner->removeLocal(key,2,f.clock));const std::vector<std::string> prefixes{key.substr(0,6)};QVERIFY(f.owner->queryStoredPrefixes(prefixes)->at(0));f.clock.now=10*60*60*1000;QVERIFY(f.owner->expire(f.clock,true));QVERIFY(f.owner->queryStoredPrefixes(prefixes)->at(0));QVERIFY(f.owner->expire(f.clock,true));QVERIFY(!f.owner->queryStoredPrefixes(prefixes)->at(0));
 }
};
QTEST_GUILESS_MAIN(AzureusQueryStorageTests)
#include "azureus_query_storage_tests.moc"

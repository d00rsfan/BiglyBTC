// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_nat/directory.hpp"
#include "nat_session_fixture.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QNetworkDatagram>
using namespace NatTest;
namespace nat=services::azureus_nat;
namespace {
std::unique_ptr<nat::Directory> directory(Actor& a,QObject* ctx,nat::Milliseconds* clock=nullptr){auto d=nat::Directory::open(a.transport,contact(a).external(),a.database,a.revision,hooks(a,ctx),true,clock?std::function<nat::Milliseconds()>{[clock]{return *clock;}}:std::function<nat::Milliseconds()>{});need(bool(d),"Owned DHT directory");return std::move(*d);}
}
namespace {
std::unique_ptr<Actor> distinctActor(const QHostAddress& address,bool v6,std::span<const nat::nodes::NodeId> used){
 // Original IPv6 protocol55 IDs use port%8. Several ::1 fixtures must select
 // distinct observed IDs; changing the production ID/filter rules is invalid.
 for(unsigned attempt=0;attempt<64;++attempt){auto a=std::make_unique<Actor>(address,v6);if(std::ranges::find(used,contact(*a).external().id)==used.end()){return a;}}throw std::runtime_error("Could not allocate distinct owned DHT node IDs");
}
struct DirectoryWall final:services::azureus_db_value::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}};
}
namespace {
class DirectoryShard final:public QObject {
 QUdpSocket socket_;nat::nodes::NodeCache cache_;
public:
 std::string root;services::azureus_values::Value value;unsigned rootReads=0,childReads=0;std::set<std::string> stored;std::int8_t type=2;
 DirectoryShard(){need(socket_.bind(QHostAddress("127.121.49.1"),0),"Owned shard socket");connect(&socket_,&QUdpSocket::readyRead,this,[this]{drain();});}
 udp::Peer endpoint()const{return {socket_.localAddress(),socket_.localPort()};}
 nat::RoutedContact routed(){nat::nodes::Contact c;c.protocol=55;c.address=addr(endpoint());return *nat::RoutedContact::observed(c,c.address,cache_);}
 void drain(){while(socket_.hasPendingDatagrams()){auto datagram=socket_.receiveDatagram();std::string bytes=datagram.data().toStdString();services::azureus_dht::Context ctx;ctx.network=0;ctx.localVersion=55;ctx.now=QDateTime::currentMSecsSinceEpoch();auto h=services::azureus_dht::decodeHeader(services::azureus_dht::Kind::Request,bytes,ctx);if(!h){continue;}services::azureus_dht::EncodeFields response;response.kind=services::azureus_dht::Kind::Reply;response.network=0;response.localVersion=55;response.remoteVersion=h->fields().originatorVersion;response.connection=h->fields().connection;response.transaction=h->fields().transaction;response.instance=71;response.now=ctx.now;response.receivedAt=ctx.now;std::optional<std::string> wire;
  if(h->fields().action==1028){response.action=1029;udp::find::ReplyFields f;f.header=response;f.randomId=123;udp::find::positions::Positions positions;f.positions=positions;const std::vector<nat::nodes::Snapshot> contacts;f.contacts=contacts;auto out=udp::find::encodeReply(f);need(bool(out),"Generated shard FIND_NODE encoder");if(out){wire=std::move(*out);}}
  else if(h->fields().action==1030){udp::lookup::Context context;context.header=ctx;auto decoded=udp::lookup::decode(services::azureus_dht::Kind::Request,bytes,context,cache_);const auto* q=decoded?std::get_if<udp::lookup::RequestBody>(&decoded->body()):nullptr;if(!q){continue;}response.action=1031;udp::lookup::ReplyFields f;f.header=response;std::vector<std::optional<services::azureus_values::Snapshot>> records;if(q->target==root){++rootReads;f.diversification=type;}else{++childReads;f.diversification=1;records.push_back(services::azureus_values::Snapshot{value.version,value.created,value.bytes,nat::nodes::Snapshot{true,value.originator.protocol,value.originator.address},0,0,-1});}f.values=std::span<const std::optional<services::azureus_values::Snapshot>>(records);auto out=udp::lookup::encodeReply(f);if(out){wire=std::move(*out);}}
  else if(h->fields().action==1026){udp::storage::Context context;context.header=ctx;auto decoded=udp::storage::decode(services::azureus_dht::Kind::Request,bytes,context,cache_);const auto* q=decoded?std::get_if<udp::storage::RequestBody>(&decoded->body()):nullptr;if(!q||q->keys.size()!=1){continue;}stored.insert(q->keys.front());response.action=1027;udp::storage::ReplyFields f;f.header=response;const std::string div(1,q->keys.front()==root?char(type):char(1));f.diversification=div;auto out=udp::storage::encodeReply(f);if(out){wire=std::move(*out);}}
  if(wire){socket_.writeDatagram(QByteArray::fromStdString(*wire),datagram.senderAddress(),datagram.senderPort());}
 }}
};
}
class AzureusNatDirectoryTests final:public QObject {
 Q_OBJECT
private slots:
 void derivedStoresWaitForPersistenceAndRejectFailure_data(){QTest::addColumn<bool>("saved");QTest::newRow("saved")<<true;QTest::newRow("refused")<<false;}
 void derivedStoresWaitForPersistenceAndRejectFailure(){
  QFETCH(bool,saved);DirectoryShard seed;Actor publisher(QHostAddress("127.121.50.2"),false),relay(QHostAddress("127.121.50.3"),false);const auto raw=nat::publishKey(contact(publisher).external().id);QVERIFY(raw);seed.root=QCryptographicHash::hash(QByteArray::fromStdString(*raw),QCryptographicHash::Sha1).toStdString();std::function<void(bool)> pending;std::optional<core::AzureusRemoteDiversification> record;
  auto pd=nat::Directory::open(publisher.transport,contact(publisher).external(),publisher.database,publisher.revision,hooks(publisher,this),true,{},[&](auto r,auto done){record=std::move(r);pending=std::move(done);});QVERIFY(pd);const std::array seeds{seed.routed().external()};QVERIFY((*pd)->publish(contact(relay),1,seeds));QTRY_VERIFY_WITH_TIMEOUT(bool(pending),2000);QCOMPARE(seed.stored.size(),std::size_t{1});QVERIFY(seed.stored.contains(seed.root));QVERIFY(record&&record->type==2&&record->offsets[0]!=record->offsets[1]);QCOMPARE(std::string(reinterpret_cast<const char*>(record->key.data()),20),seed.root);QVERIFY(!publisher.sourceHeld);pending(saved);QTRY_VERIFY_WITH_TIMEOUT((*pd)->facts().active==0,4000);QCOMPARE(seed.stored.size(),std::size_t(saved?11:1));
 }
 void diversifiedReadAndPublicationUseOriginalKeys_data(){QTest::addColumn<int>("type");QTest::newRow("frequency")<<2;QTest::newRow("size")<<3;}
 void diversifiedReadAndPublicationUseOriginalKeys(){
  QFETCH(int,type);DirectoryShard seed;seed.type=std::int8_t(type);Actor publisher(QHostAddress("127.121.49.2"),false),reader(QHostAddress("127.121.49.3"),false),relay(QHostAddress("127.121.49.4"),false);auto pd=directory(publisher,this),rd=directory(reader,this);const std::array seeds{seed.routed().external()};auto raw=nat::publishKey(contact(publisher).external().id);QVERIFY(raw);auto root=QCryptographicHash::hash(QByteArray::fromStdString(*raw),QCryptographicHash::Sha1);seed.root=root.toStdString();seed.value={1,QDateTime::currentMSecsSinceEpoch(),*nat::encodePublishValue(contact(relay).exportSnapshot()),contact(publisher).external(),0,0,-1};
  std::optional<nat::DirectoryOutcome> result;QVERIFY(rd->read(contact(publisher),this,[&](auto v){result.emplace(std::move(v));},seeds));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),3000);QVERIFY(*result&&(**result));QCOMPARE((***result).external().id,contact(relay).external().id);QVERIFY(seed.rootReads&&seed.childReads);
  QVERIFY(pd->publish(contact(relay),1,seeds));QTRY_VERIFY_WITH_TIMEOUT(pd->facts().active==0,4000);QCOMPARE(seed.stored.size(),std::size_t(type==2?11:3));std::set<std::string> legal{seed.root};for(unsigned i=0;i<10;++i){auto input=root;input.append(char(i));legal.insert(QCryptographicHash::hash(input,QCryptographicHash::Sha1).toStdString());}for(const auto& key:seed.stored){QVERIFY(legal.contains(key));}
  const auto prior=seed.stored;seed.stored.clear();
  // Restore original fixed offsets into a new directory as after a restart.
  std::vector<core::AzureusRemoteDiversification> records;core::AzureusRemoteDiversification saved;saved.type=std::int8_t(type);saved.expiry=QDateTime::currentMSecsSinceEpoch()+86400000;std::copy_n(reinterpret_cast<const std::uint8_t*>(root.data()),20,saved.key.begin());std::vector<unsigned> offsets;for(unsigned i=0;i<10;++i){auto input=root;input.append(char(i));if(prior.contains(QCryptographicHash::hash(input,QCryptographicHash::Sha1).toStdString())){offsets.push_back(i);}}saved.offsets=type==3?std::array<unsigned,2>{offsets[0],offsets[1]}:std::array<unsigned,2>{2,7};records.push_back(saved);pd->close();pd=directory(publisher,this);publisher.sourceHeld=true;QVERIFY(pd->restoreDiversifications(records,QDateTime::currentMSecsSinceEpoch()));publisher.sourceHeld=false;
QVERIFY(pd->publish(contact(relay),2,seeds));QTRY_VERIFY_WITH_TIMEOUT(pd->facts().active==0,4000);QCOMPARE(seed.stored,prior);
 }

 void originalKeyPublicationReadAndNegativeCache_data(){QTest::addColumn<bool>("v6");QTest::newRow("IPv4")<<false;QTest::newRow("IPv6")<<true;}
 void originalKeyPublicationReadAndNegativeCache(){
  QFETCH(bool,v6);std::vector<nat::nodes::NodeId> used;auto sa=distinctActor(QHostAddress(v6?"::1":"127.121.36.1"),v6,used);auto& seed=*sa;used.push_back(contact(seed).external().id);auto pa=distinctActor(QHostAddress(v6?"::1":"127.121.36.2"),v6,used);auto& publisher=*pa;used.push_back(contact(publisher).external().id);auto ra=distinctActor(QHostAddress(v6?"::1":"127.121.36.3"),v6,used);auto& reader=*ra;used.push_back(contact(reader).external().id);auto relayActor=distinctActor(QHostAddress(v6?"::1":"127.121.36.4"),v6,used);auto& relay=*relayActor;auto pd=directory(publisher,this);nat::Milliseconds clock=1000;auto rd=directory(reader,this,&clock);const std::array seeds{contact(seed).external()};
  QVERIFY(pd->publish(contact(relay),1,seeds));QTRY_VERIFY_WITH_TIMEOUT(pd->facts().stores>=1,3000);QCOMPARE(pd->facts().publications,quint64{1});auto raw=nat::publishKey(contact(publisher).external().id);QVERIFY(raw);auto digest=QCryptographicHash::hash(QByteArray::fromStdString(*raw),QCryptographicHash::Sha1);DirectoryWall wall;auto stored=seed.database->read(std::string(digest.constData(),20),contact(reader).external(),1,0,wall);QVERIFY(stored&&*stored&&(**stored).values.size()==1);QCOMPARE((**stored).values[0].originator.id,contact(publisher).external().id);nat::nodes::NodeCache verifyCache;auto decoded=nat::decodePublishValue((**stored).values[0].bytes,verifyCache);QVERIFY(decoded);QCOMPARE(decoded->address,contact(relay).external().address);std::optional<nat::DirectoryOutcome> result;QVERIFY(rd->read(contact(publisher),this,[&](auto v){result.emplace(std::move(v));},seeds));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),3000);QVERIFY(*result&&(**result).has_value());QCOMPARE((***result).external().id,contact(relay).external().id);QCOMPARE((***result).external().address,contact(relay).external().address);
  auto received=seed.transport.statistics().received;result.reset();QVERIFY(rd->read(contact(publisher),this,[&](auto v){result.emplace(std::move(v));},seeds));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),500);QVERIFY(*result&&(**result).has_value());QCOMPARE(seed.transport.statistics().received,received);QCOMPARE(rd->facts().hits,quint64{1});
  // A new version and own-key deletion travel through real authenticated STORE.
  QVERIFY(pd->withdraw(2,seeds));QTRY_VERIFY_WITH_TIMEOUT(pd->facts().stores>=2,3000);clock+=120000;result.reset();QVERIFY(rd->read(contact(publisher),this,[&](auto v){result.emplace(std::move(v));},seeds));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),3000);QVERIFY(*result&&!(**result));received=seed.transport.statistics().received;result.reset();QVERIFY(rd->read(contact(publisher),this,[&](auto v){result.emplace(std::move(v));},seeds));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),500);QVERIFY(*result&&!(**result));QCOMPARE(seed.transport.statistics().received,received);QCOMPARE(rd->facts().hits,quint64{2});
 }
 void pendingDedupCancellationAndSourceWithdrawalAreScoped(){
  Actor seed(QHostAddress("127.121.37.1"),false),reader(QHostAddress("127.121.37.2"),false),subject(QHostAddress("127.121.37.3"),false);auto rd=directory(reader,this);const std::array seeds{contact(seed).external()};unsigned cancelled=0,done=0;auto one=rd->read(contact(subject),this,[&](auto v){need(!v&&v.error()==nat::DirectoryError::Cancelled,"Cancelled waiter only");++cancelled;},seeds);auto two=rd->read(contact(subject),this,[&](auto v){need(bool(v)&&!*v,"Shared negative result");++done;},seeds);QVERIFY(one&&two);QCOMPARE(rd->facts().active,std::size_t{1});QVERIFY(rd->cancel(*one));QTRY_VERIFY_WITH_TIMEOUT(done==1&&cancelled==1,2000);QVERIFY(!rd->cancel(*one));
  // Cached completion must still acquire a current source permit.
  ++reader.revision;std::optional<nat::DirectoryOutcome> result;QVERIFY(rd->read(contact(subject),this,[&](auto v){result.emplace(std::move(v));},seeds));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),1000);QVERIFY(!*result&&result->error()==nat::DirectoryError::Source);QVERIFY(rd->facts().closed);QVERIFY(!rd->read(contact(subject),this,[](auto){},seeds));QVERIFY(!rd->publish(contact(seed),3,seeds));rd->close();
 }
 void closeBetweenSourceReleaseAndReceiverDeliveryRejectsCachedSuccess(){
  Actor seed(QHostAddress("127.121.41.1"),false),reader(QHostAddress("127.121.41.2"),false),subject(QHostAddress("127.121.41.3"),false);auto d=directory(reader,this);const std::array seeds{contact(seed).external()};std::optional<nat::DirectoryOutcome> first,last;QVERIFY(d->read(contact(subject),this,[&](auto v){first.emplace(std::move(v));},seeds));QTRY_VERIFY_WITH_TIMEOUT(first.has_value(),2000);QVERIFY(*first&&!(**first));
  QVERIFY(d->read(contact(subject),this,[&](auto v){last.emplace(std::move(v));},seeds));QVERIFY(QMetaObject::invokeMethod(this,[&]{d->close();},Qt::QueuedConnection));QTRY_VERIFY_WITH_TIMEOUT(last.has_value(),1000);QVERIFY(!*last&&last->error()==nat::DirectoryError::Closed);
 }
 void destructionDropsQueuedResultsWithoutBorrowingCallerLocals(){
  Actor seed(QHostAddress("127.121.39.1"),false),reader(QHostAddress("127.121.39.2"),false),subject(QHostAddress("127.121.39.3"),false);auto d=directory(reader,this);const std::array seeds{contact(seed).external()};unsigned calls=0;QVERIFY(d->read(contact(subject),this,[&](auto){++calls;},seeds));d.reset();QTest::qWait(50);QCOMPARE(calls,0U);
 }
 void malformedOrWrongOriginRecordNeverBecomesARelay(){
  Actor seed(QHostAddress("127.121.38.1"),false),reader(QHostAddress("127.121.38.2"),false),subject(QHostAddress("127.121.38.3"),false);auto rd=directory(reader,this);const std::array seeds{contact(seed).external()};auto raw=nat::publishKey(contact(subject).external().id);QVERIFY(raw);auto digest=QCryptographicHash::hash(QByteArray::fromStdString(*raw),QCryptographicHash::Sha1);struct Clock:services::azureus_db_value::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}} wall;
  // Correct key, but a different publisher's origin. It is not authority for
  // the requested node even when the contact payload itself is well formed.
  auto bytes=nat::encodePublishValue(contact(seed).exportSnapshot());QVERIFY(bytes);QVERIFY(seed.database->putLocal(std::string(digest.constData(),20),*bytes,1,wall));std::optional<nat::DirectoryOutcome> result;QVERIFY(rd->read(contact(subject),this,[&](auto v){result.emplace(std::move(v));},seeds));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),2000);QVERIFY(*result&&!(**result));auto noHandler=reader.transport.callTransfer(seed.actualEndpoint,55,std::string(nat::handlerKey()),"unregistered",this,[](auto){},std::chrono::steady_clock::now()+1s);QVERIFY(!noHandler&&noHandler.error()==udp::Failure::Encode);
 }
};
QTEST_GUILESS_MAIN(AzureusNatDirectoryTests)
#include "azureus_nat_directory_tests.moc"

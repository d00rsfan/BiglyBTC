// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_nat/directory.hpp"
#include "nat_session_fixture.hpp"
#include <QCryptographicHash>
#include <QDateTime>
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
class AzureusNatDirectoryTests final:public QObject {
 Q_OBJECT
private slots:
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

// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_db_runtime/survey_contact_state.hpp"
#include "database.hpp"
#include <QTest>
#include <QFile>
namespace state=biglybtc::services::azureus_db_runtime::survey;namespace db=biglybtc::services::azureus_db_runtime;namespace nodes=biglybtc::services::azureus_nodes;
namespace {
struct Clock final:db::lifecycle::WallClock {std::int64_t now=1000;unsigned calls=0;std::int64_t nowMilliseconds()noexcept override{++calls;return now;}};
nodes::Contact contact(unsigned n){nodes::ResolvedEndpoint ep;ep.address.bytes[0]=1;ep.address.bytes[1]=std::uint8_t(n);ep.address.bytes[3]=1;ep.address.port=4000;nodes::NodeCache cache;auto id=cache.lookup(ep,55);if(!id){throw std::runtime_error("Generated state contact");}nodes::Contact c;c.protocol=55;c.address=ep.address;c.id=id->id;return c;}
}
class AzureusSurveyStateTests final:public QObject {
 Q_OBJECT
private slots:
 void actualOriginalStateTransitionsMatch(){
  QFile file(QString::fromUtf8(STATE_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));std::optional<state::ContactState> current;QByteArray scenario;unsigned transitions=0;
  while(!file.atEnd()){
   const auto row=file.readLine().trimmed().split('\t');QCOMPARE(row.size(),11);const auto now=row[2].toLongLong();const auto action=row[1];
   if(action=="create"){auto made=state::ContactState::create(row[4].toInt(),now,row[3].toLongLong());QVERIFY(made);current=std::move(*made);scenario=row[0];}
   QVERIFY(current&&scenario==row[0]);
   if(action.startsWith("add")){QVERIFY(current->add(std::uint64_t(action.mid(3).toUInt()+1)));}
   else if(action.startsWith("remove")){current->remove(std::uint64_t(action.mid(6).toUInt()+1));}
   else if(action=="fail"){current->failure();}else if(action=="ok"){current->success();}else if(action=="instance"){current->updateInstance(row[4].toInt());}else if(action=="use"){current->updateUseTime(now);}
   unsigned mask=0;for(unsigned i=0;i<3;++i){if(current->contains(i+1)){mask|=1u<<i;}}QCOMPARE(current->instance(),row[4].toInt());QCOMPARE(current->created(),row[5].toLongLong());QCOMPARE(current->deadline(),row[6].toLongLong());QCOMPARE(current->lastUse(),row[7].toLongLong());QCOMPARE(current->failures(),row[8].toInt());QCOMPARE(mask,row[9].toUInt());QCOMPARE(current->expired(now),bool(row[10].toInt()));++transitions;
  }QCOMPARE(transitions,44u);
 }
 void membershipAndLifetimeBoundsDoNotResetOriginalState(){
  QVERIFY(!state::ContactState::create(1,0,-1));QVERIFY(!state::ContactState::create(1,0,3600000));QVERIFY(!state::ContactState::create(1,0,0,0));auto current=state::ContactState::create(1,0,0,2);QVERIFY(current);QVERIFY(!current->add(0));QVERIFY(current->add(1));QVERIFY(current->add(2));QVERIFY(!current->add(3));auto duplicate=current->add(1);QVERIFY(duplicate&&!*duplicate);QCOMPARE(current->lastUse(),std::int64_t{0});current->failure();QCOMPARE(current->known().size(),std::size_t{2});current->updateInstance(1);QCOMPARE(current->known().size(),std::size_t{2});current->updateInstance(2);QVERIFY(current->known().empty());QCOMPARE(current->failures(),1);current->updateUseTime(12600000);QVERIFY(!current->expired(12600000));QVERIFY(current->expired(12600001));
 }
 void dbIncarnationsSurviveUpdatesAndDoNotReuseDeletedRecords(){
  auto made=db::Owner::create();QVERIFY(made);auto owner=std::move(*made);Clock clock;QVERIFY(owner->bindLocal(contact(1)));const std::string key(20,'I');QVERIFY(owner->mappingIdentity(key)&&!owner->mappingIdentity(key)->has_value());QVERIFY(owner->putLocal(key,"first",1,clock));auto first=owner->mappingIdentity(key);QVERIFY(first&&*first);const auto before=clock.calls;QCOMPARE(owner->mappingIdentity(key),first);QCOMPARE(clock.calls,before);QVERIFY(owner->putLocal(key,"updated",2,clock));QCOMPARE(owner->mappingIdentity(key),first);QVERIFY(owner->removeLocal(key,3,clock));QVERIFY(!owner->mappingIdentity(key)->has_value());QVERIFY(owner->putLocal(key,"recreated",4,clock));auto second=owner->mappingIdentity(key);QVERIFY(second&&*second&&**second>**first);owner->close();QVERIFY(!owner->mappingIdentity(key));
 }
 void anEmptyLiveRecordKeepsIdentityWhenRefilled(){
  auto made=db::Owner::create();QVERIFY(made);auto owner=std::move(*made);Clock clock;QVERIFY(owner->bindLocal(contact(1)));const auto remote=contact(2);const std::string key(20,'R');const biglybtc::services::azureus_values::Value value{1,1000,"remote",remote,0,1,-1};QVERIFY(owner->store(key,remote,std::span(&value,1),clock));auto first=owner->mappingIdentity(key);QVERIFY(first&&*first);clock.now=10*60*60*1000;QVERIFY(owner->expire(clock,true));QCOMPARE(owner->snapshot()->values,std::size_t{0});QCOMPARE(owner->mappingIdentity(key),first);QVERIFY(owner->putLocal(key,"refill",2,clock));QCOMPARE(owner->mappingIdentity(key),first);QVERIFY(owner->removeLocal(key,3,clock));QVERIFY(!owner->mappingIdentity(key)->has_value());
 }
};
QTEST_GUILESS_MAIN(AzureusSurveyStateTests)
#include "azureus_survey_state_tests.moc"

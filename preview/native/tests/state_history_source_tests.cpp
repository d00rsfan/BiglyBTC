// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal_tag_namespace.hpp"
#include "tag_constraint_worker.hpp"
#include "owned_engine_fixture.hpp"
#include <QtTest>
#include <future>
#include <thread>
namespace {using namespace biglybtc;
bool member(const storage::ManualTagSnapshot& value){const auto& rows=value.tags()->tags;return rows.size()==1&&!rows.front().uniqueHashes.empty();}
}
class StateHistorySourceTests final:public QObject {
 Q_OBJECT
private slots:
 void retainedMainSource_data(){
  QTest::addColumn<int>("scenario");QTest::addColumn<bool>("expected");
  QTest::newRow("seeding-through-checking")<<0<<true;
  QTest::newRow("downloading-through-checking")<<1<<true;
  QTest::newRow("engine-epoch-replacement")<<2<<false;
  QTest::newRow("admission-replacement")<<3<<false;
  QTest::newRow("missing-observation")<<4<<false;
  QTest::newRow("foreign-engine-session")<<5<<false;
  QTest::newRow("late-first-checking")<<6<<false;
  QTest::newRow("duplicate-observation")<<7<<false;
  QTest::newRow("retired-admission")<<8<<false;
  QTest::newRow("unobserved-operation")<<9<<false;
  QTest::newRow("missing-engine-identity")<<10<<false;
 }
 void retainedMainSource(){QFETCH(int,scenario);QFETCH(bool,expected);run(scenario,expected);}
 void actualNormalWorkerActivityCadence(){run(11,true);}
 void run(int scenario,bool expected){
  test::OwnedEngineFixture fixture(false);
  libtorrent::entry config(libtorrent::entry::dictionary_type{});config["locale"]="en_GB";config["tagmanager.enable"]=1;
  test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));
  libtorrent::entry definitions(libtorrent::entry::dictionary_type{});auto& rule=definitions["3"]["1"];
  rule["n"]="Generated state consumer";rule["o"]=libtorrent::entry::list_type{};
  const std::string expression=scenario==11?"hasTag(\"Active\")":scenario==9?"hasTag(\"Checking\")":
      std::string("hasTag(\"")+(scenario==1?"Downloading":"Seeding")+"\")&&isChecking()";
  rule["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry(expression),libtorrent::entry("am=0;")};
  test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(definitions)));
  const auto inventory=test::read(fixture.profile+"/downloads.config"),state=test::read(fixture.stateFile),tags=test::read(fixture.profile+"/tag.config");
  auto result=std::async(std::launch::async,[&]{
    auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Generated history profile");
    auto mode=storage::TagManagerStartupMode::capture(*lease);test::require(bool(mode),"Actual startup source");
    auto internal=app::InternalTagNamespace::activate(*lease,88,true,*mode);test::require(bool(internal),"Actual namespace");
    auto names=(*internal)->snapshot();test::require(names&&*names,"Actual namespace observation");
    auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Actual history tag store");
    auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Actual history source");
    auto worker=app::TagConstraintWorker::activate(*lease,88,*mode);test::require(bool(worker),"Actual history worker");
    auto library=std::make_shared<ui::LibraryPresentation>();library->revision=7;
    ui::LibraryItem item;item.hash=QString::fromStdString(fixture.hash->hex());item.bytes=test::OwnedEngineFixture::PayloadBytes;
    item.providerAdmission=3;item.providerGeneration=88;library->items.push_back(item);
    app::ConstraintWorkerFrame frame;frame.session=88;frame.sequence=1;frame.sourceRevision=lease->sourceRevision();frame.library=library;
    frame.internalNamespace=**names;frame.peerEngineIdentity=engine::AccountingIdentity{9,88,2};
    engine::ManagedDownloadStatus managed;managed.hash=*fixture.hash;managed.admission=3;managed.retained=true;
    managed.bytes=test::OwnedEngineFixture::PayloadBytes;managed.completed=managed.bytes;managed.checked=true;managed.selectedComplete=true;
    managed.phase=scenario==1?engine::ManagedPhase::Downloading:engine::ManagedPhase::Seeding;
    if(scenario==6){managed.phase=engine::ManagedPhase::Checking;}
    if(scenario==11){managed.uploadRate=17;}
    frame.managed.push_back(managed);
    auto first=(*worker)->step(**store,*snapshot,frame);test::require(first&&!first->changed,"Initial history observation unexpectedly assigned membership");
    if(scenario==11){std::this_thread::sleep_for(std::chrono::milliseconds(10100));}
    else{frame.managed.front().phase=engine::ManagedPhase::Checking;frame.managed.front().checked=false;}
    if(scenario==2){++frame.peerEngineIdentity->engineEpoch;}
    if(scenario==3){++frame.managed.front().admission;++library->items.front().providerAdmission;}
    if(scenario==4){frame.managed.clear();}
    if(scenario==5){frame.peerEngineIdentity->profileSession=89;}
    if(scenario==7){frame.managed.push_back(frame.managed.front());}
    if(scenario==8){frame.managed.front().retained=false;}
    if(scenario==10){frame.peerEngineIdentity.reset();}
    ++frame.sequence;frame.sourceRevision=lease->sourceRevision();
    auto next=(*worker)->step(**store,*snapshot,frame);test::require(next&&next->changed==expected,"Retained history assignment differs");
    auto current=next->changed?next->snapshot:*snapshot;test::require(member(current)==expected,"Retained history actual membership differs");
    if(scenario==11){
      frame.managed.front().phase=engine::ManagedPhase::Stopped;frame.managed.front().uploadRate=0;++frame.sequence;frame.sourceRevision=lease->sourceRevision();
      auto stopped=(*worker)->step(**store,current,frame);test::require(stopped&&stopped->changed&&!member(stopped->snapshot),"Stopped transition failed to remove Active-driven membership");
    }
    (*worker)->close();test::require(bool((*internal)->close())&&bool((*store)->close()),"Actual history owners close");return true;
  });QVERIFY(result.get());
  QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.stateFile),state);
  if(!expected){QCOMPARE(test::read(fixture.profile+"/tag.config"),tags);}
  for(const auto& file:fixture.files){QCOMPARE(test::read(fixture.payload+'/'+file.relative),QByteArray(file.expected.size(),'\0'));}
 }
};
QTEST_GUILESS_MAIN(StateHistorySourceTests)
#include "state_history_source_tests.moc"

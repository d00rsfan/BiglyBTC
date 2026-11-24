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
 void retainedExplicitCheckingOperation(){run(12,true);}
 void retainedPrivateActivityWakesRules(){run(13,true);}
 void savedStoppedStateUsesThePrimaryPauseRecord(){run(14,true);}
 void savedQueuedStateUsesOriginalCompletion(){run(15,true);}
 void run(int scenario,bool expected){
  test::OwnedEngineFixture fixture(false);
  libtorrent::entry config(libtorrent::entry::dictionary_type{});config["locale"]="en_GB";config["tagmanager.enable"]=1;
  test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));
  libtorrent::entry definitions(libtorrent::entry::dictionary_type{});auto& rule=definitions["3"]["1"];
  rule["n"]="Generated state consumer";rule["o"]=libtorrent::entry::list_type{};
  std::string expression;
  switch (scenario) {
  case 9:
    expression = "hasTag(\"Checking\")";
    break;
  case 12:
    expression = "hasTagAge(\"Checking\")==0&&tagPosition(\"Checking\",2)==-1";
    break;
  case 11:
    expression = "hasTag(\"Active\")&&(last_active>=0)&&(last_active<5)"
                 "&&isEQ(lastactive,lastxfer)&&isEQ(last_xfer,last_active)";
    break;
  case 13:
    expression = "hasTag(\"Active\")";
    break;
  case 14:
    expression = "hasTag(\"Stopped\")&&hasTag(\"Inactive\")&&hasTag(\"Paused\")";
    break;
  case 15:
    expression = "hasTag(\"Queued for Seeding\")&&hasTag(\"Complete\")&&hasTag(\"Inactive\")";
    break;
  default:
    expression = std::string("hasTag(\"")+(scenario==1?"Downloading":"Seeding")+"\")&&isChecking()";
    break;
  }
  rule["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry(expression),libtorrent::entry("am=0;")};
  test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(definitions)));
  if (scenario == 14) {
    const auto path = fixture.profile+"/downloads.config";
    auto original = core::Document::parse(test::read(path).toStdString());
    test::require(bool(original), "Generated primary inventory");
    const auto* rows = original->find("downloads");
    test::require(rows && rows->children.size() == 1, "Generated single download");
    const auto& saved = rows->children.front();
    auto row = core::Document::parse(original->originalBytes().substr(saved.begin, saved.end-saved.begin));
    test::require(row && row->replaceValue("state", "i70e"), "Saved original stopped state");
    test::require(original->replaceValue("downloads", "l"+row->serialize()+"e")
                  && original->replaceValue("pause_data", "le"), "Original settled inventory");
    test::write(path, QByteArray::fromStdString(original->serialize()));
  }
  const auto setDndFlags = [&](const storage::ProfileLease& lease, int flags) {
    auto store = storage::TorrentStateStore::open(lease);
    test::require(bool(store), "Generated original attribute source");
    auto file = store->loadAttributes(lease, *fixture.hash);
    test::require(bool(file), "Generated original cached attributes");
    const auto* attrs = file->document().find("attributes");
    test::require(attrs && attrs->type == core::Node::Type::Dictionary, "Original attributes dictionary");
    auto section = core::Document::parse(file->document().originalBytes().substr(attrs->begin, attrs->end-attrs->begin));
    test::require(section && section->replaceValue("parameters", "d8:dndflagsi"+std::to_string(flags)+"ee"),
                  "Original saved DND flags");
    test::require(bool(store->saveAttributes(lease, *file, section->serialize())), "Save exact cached completion source");
  };
  if (scenario == 15) {
    auto lease = storage::ProfileLease::acquire(fixture.native());
    test::require(bool(lease), "Generated queued profile");
    setDndFlags(*lease, 1);
  }
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
    using Membership = core::tags::runtime::Membership;
    if (scenario == 13) {
      frame.managed.clear();
      auto& observation = frame.i2p.emplace_back();
      observation.hash = *fixture.hash;
      observation.value.admission = 3;
      observation.value.generation = 91;
      observation.value.current = true;
      observation.value.phase = app::OwnedI2pService::Phase::Active;
      observation.value.downloadTagMembers.emplace().fill(Membership::Unknown);
      library->items.front().providerGeneration = 91;
    }
    if (scenario == 14) {
      frame.managed.clear();
      auto& row = library->items.front();
      row.providerAdmission = row.providerGeneration = 0;
      row.state = "Stopped";
    }
    if (scenario == 15) {
      frame.managed.clear();
      auto& row = library->items.front();
      row.providerAdmission = row.providerGeneration = 0;
      row.state = "Queued";
      row.assumedComplete = false;
    }
    auto first=(*worker)->step(**store,*snapshot,frame);test::require(first&&!first->changed,"Initial history observation unexpectedly assigned membership");
    if(scenario==11){std::this_thread::sleep_for(std::chrono::milliseconds(10100));}
    else if(scenario==13){(*frame.i2p.front().value.downloadTagMembers)[7]=Membership::Present;}
    else if (scenario == 14) {
      auto paused = storage::ConfigFile::load(*lease, "downloads.config");
      const auto rawHash = QByteArray::fromHex(item.hash.toLatin1()).toStdString();
      test::require(paused && paused->document().replaceValue("pause_data",
          "ld5:forcei0e4:hash20:"+rawHash+"5:owneri1eee") && paused->save(*lease),
          "Save actual original pause record");
      library->items.front().state = "Paused";
    }
    else if (scenario == 15) {
      setDndFlags(*lease, 3);
      library->items.front().assumedComplete = true;
    }
    else{frame.managed.front().phase=engine::ManagedPhase::Checking;frame.managed.front().checked=false;}
    if(scenario==12){frame.managed.front().recheck.emplace();}
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
    if (scenario == 12) {
      auto& download = frame.managed.front();
      const auto turn = [&] {
        ++frame.sequence;
        frame.sourceRevision = lease->sourceRevision();
        return (*worker)->step(**store, current, frame);
      };
      download.recheck->phase = engine::ManagedRecheckPhase::Retiring;
      auto retiring = turn();
      test::require(retiring && !retiring->changed, "Unjoined recheck lost Checking membership");
      frame.managed.clear();
      frame.retiredRecheckTags.insert(item.hash);
      auto stale = turn();
      test::require(stale && !stale->changed, "Retired recheck was accepted as a live provider");
      library->items.front().providerAdmission = library->items.front().providerGeneration = 0;
      frame.peerEngineIdentity->profileSession = 89;
      auto foreign = turn();
      test::require(foreign && !foreign->changed, "Foreign engine supplied Checking retirement");
      frame.peerEngineIdentity->profileSession = 88;
      auto retired = turn();
      test::require(retired && retired->changed && !member(retired->snapshot),
                    "Exact recheck retirement failed to remove Checking-driven membership");
    }
    if (scenario == 13) {
      auto& privateOwner = frame.i2p.front().value;
      (*privateOwner.downloadTagMembers)[7] = Membership::Absent;
      ++privateOwner.generation;
      ++frame.sequence;
      frame.sourceRevision = lease->sourceRevision();
      auto stale = (*worker)->step(**store, current, frame);
      test::require(stale && !stale->changed, "A foreign I2P generation removed an Active-driven tag");
      --privateOwner.generation;
      ++frame.sequence;
      auto inactive = (*worker)->step(**store, current, frame);
      test::require(inactive && inactive->changed && !member(inactive->snapshot),
                    "Observed I2P activity change did not wake the rule on the same admission");
    }
    if (scenario == 14) {
      library->items.front().state = "Stopped";
      ++frame.sequence;
      auto cosmetic = (*worker)->step(**store, current, frame);
      test::require(cosmetic && !cosmetic->changed, "Painted state replaced the held pause record");
      auto config = storage::ConfigFile::load(*lease, "downloads.config");
      test::require(config && config->document().replaceValue("pause_data", "le")
                    && config->save(*lease), "Clear actual original pause record");
      ++frame.sequence;
      frame.sourceRevision = lease->sourceRevision();
      library->items.front().state = "Resumed";
      auto stopped = (*worker)->step(**store, current, frame);
      test::require(stopped && stopped->changed && !member(stopped->snapshot),
                    "Cleared pause record did not remove Paused-driven membership");
    }
    if (scenario == 15) {
      library->items.front().assumedComplete = false;
      ++frame.sequence;
      auto cosmetic = (*worker)->step(**store, current, frame);
      test::require(cosmetic && !cosmetic->changed, "Displayed completion replaced original DND flags");
      setDndFlags(*lease, 1);
      ++frame.sequence;
      frame.sourceRevision = lease->sourceRevision();
      library->items.front().assumedComplete.reset();
      auto incomplete = (*worker)->step(**store, current, frame);
      test::require(incomplete && incomplete->changed && !member(incomplete->snapshot),
                    "Original incomplete queue row did not remove the completion-driven tag");
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

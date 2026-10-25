#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"

#include <QtTest>
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
std::optional<engine::ManagedDownloadStatus> status(const engine::PersistentSession& owner,const core::TorrentHash& hash){
  const auto values=owner.managedDownloads();const auto found=std::ranges::find(values,hash,&engine::ManagedDownloadStatus::hash);
  return found==values.end()?std::nullopt:std::optional{*found};
}
void slowOwnedTransfer(const storage::ProfileLease& lease){
  auto config=storage::ConfigFile::load(lease,"downloads.config");test::require(config.has_value(),"Cannot load owned inventory");
  const auto* rows=config->document().find("downloads");test::require(rows&&rows->children.size()==1,"Expected one owned row");
  auto raw=core::encodeValue(rows->children[0],storage::compatibleConfigLimits());test::require(raw.has_value(),"Cannot encode owned row");
  auto entry=core::Document::parse(std::move(*raw),storage::compatibleConfigLimits());test::require(entry.has_value(),"Cannot parse owned row");
  test::require(entry->replaceValue("maxdl","i8192e").has_value(),"Cannot constrain owned transfer");
  test::require(config->document().replaceValue("downloads","l"+entry->serialize()+'e').has_value(),"Cannot save owned row");
  test::require(config->save(lease).has_value(),"Cannot publish owned transfer setting");
}
}
class ManagedCombinedProtocolTests final:public QObject {
  Q_OBJECT
  bool expected_=false;
private slots:
  void initTestCase(){
    const auto expected=qgetenv("BIGLYBTC_EXPECT_COMBINED_COUNTERS");
    QVERIFY2(expected=="0"||expected=="1","Set explicit expected ABI cohort: 1=new separately approved V2 provider; 0=old approved d367 fallback");
    expected_=expected=="1";
  }
  void actualProtocolPrefixSurvivesPauseResumeWithoutReplacingPayload_data(){
    QTest::addColumn<bool>("multifile");QTest::newRow("single")<<false;QTest::newRow("multi")<<true;
  }
  void actualProtocolPrefixSurvivesPauseResumeWithoutReplacingPayload(){
    QFETCH(bool,multifile);test::OwnedEngineFixture fixture(multifile,true);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);slowOwnedTransfer(*lease);
    auto created=engine::PersistentSession::create({801,802,803});QVERIFY2(created.has_value(),"Actual source/artifact-bound owner is required; never skip unqualified providers");
    auto owner=std::move(*created);
    QVERIFY(owner->startManaged(*lease,{.hash=*fixture.hash,.ownedLoopbackPeerPort=fixture.port(),.forceStart=true}));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=owner->poll();const auto value=status(*owner,*fixture.hash);
      return poll&&value&&!value->failure&&value->received>0&&value->phase==engine::ManagedPhase::Downloading;
    })(),30000);
    const auto before=status(*owner,*fixture.hash);QVERIFY(before);
    if(expected_){
      QVERIFY(before->combinedReceived&&before->combinedSent);
      QVERIFY(*before->combinedReceived>before->received); // Actual BT handshake/message bytes, not rate*size.
      QVERIFY(*before->combinedSent>=before->sent&&*before->combinedSent>0);
    }else{QVERIFY(!before->combinedReceived&&!before->combinedSent);}
    QVERIFY(owner->pauseManaged(*fixture.hash,true));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=owner->poll();const auto value=status(*owner,*fixture.hash);
      return poll&&value&&!value->failure&&value->phase==engine::ManagedPhase::Paused;
    })(),5000);
    const auto paused=status(*owner,*fixture.hash);QVERIFY(paused);QVERIFY(paused->received>=before->received);
    if(expected_){QVERIFY(paused->combinedReceived&&paused->combinedSent);QVERIFY(*paused->combinedReceived>=*before->combinedReceived);}
    QVERIFY(owner->pauseManaged(*fixture.hash,false));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=owner->poll();const auto value=status(*owner,*fixture.hash);
      return poll&&value&&!value->failure&&value->received>paused->received&&value->phase==engine::ManagedPhase::Downloading;
    })(),30000);
    const auto resumed=status(*owner,*fixture.hash);QVERIFY(resumed);
    if(expected_){
      QVERIFY(resumed->combinedReceived&&resumed->combinedSent);
      QVERIFY(*resumed->combinedReceived>=*paused->combinedReceived+(resumed->received-paused->received));
      QVERIFY(*resumed->combinedSent>=*paused->combinedSent&&*resumed->combinedReceived>resumed->received);
    }else{QVERIFY(!paused->combinedReceived&&!resumed->combinedReceived&&!resumed->combinedSent);}
    QVERIFY(owner->stopManaged(*fixture.hash));
    QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto poll=owner->poll();const auto value=status(*owner,*fixture.hash);
      return poll&&value&&!value->failure&&value->phase==engine::ManagedPhase::Stopped&&!value->retained;
    })(),10000);
    const auto stopped=status(*owner,*fixture.hash);QVERIFY(stopped);
    const auto closed=owner->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&closed.retirement.peerCounters);
    QVERIFY(!closed.checkpointFailure);QVERIFY(fixture.intact(false));
    const auto saved=core::Document::parse(test::read(fixture.profile+QStringLiteral("/downloads.config")).toStdString(),storage::compatibleConfigLimits());
    QVERIFY(saved);const auto* rows=saved->find("downloads");QVERIFY(rows&&rows->children.size()==1);
    const auto* received=rows->children[0].find("downloaded");QVERIFY(received&&received->type==core::Node::Type::Integer);
    // Original payload field keeps its existing payload meaning; protocol is
    // not folded into persisted lifetime payload or a GUI finality receipt.
    QCOMPARE(received->integer,stopped->received);QVERIFY(stopped->received>=resumed->received);
  }
};
QTEST_GUILESS_MAIN(ManagedCombinedProtocolTests)
#include "managed_combined_protocol_tests.moc"

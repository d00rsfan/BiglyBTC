// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal_tag_namespace.hpp"
#include "tag_constraint_worker.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#include <QFile>
#include <QtTest>
#include <chrono>
#include <future>
#include <thread>
namespace {
using namespace biglybtc;using namespace std::chrono_literals;
std::string base32(const core::TorrentHash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string result;unsigned buffer=0,bits=0;for(auto byte:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;result+=chars[(buffer>>bits)&31];}}if(bits){result+=chars[(buffer<<(5-bits))&31];}return result;}
}
class NamedTagRankSourceTests final:public QObject {
 Q_OBJECT
private slots:
 void actualQueuedEngineSource_data(){QTest::addColumn<int>("scenario");QTest::addColumn<bool>("changed");
  QTest::newRow("actual-queue-and-time-ranks")<<0<<true;
  QTest::newRow("missing-engine-receipt")<<1<<false;
  QTest::newRow("missing-position")<<2<<false;
  QTest::newRow("unsettled-queue-position")<<3<<false;
  QTest::newRow("foreign-admission")<<4<<false;
  QTest::newRow("duplicate-managed-record")<<5<<false;
  QTest::newRow("queue-position-failure")<<6<<false;
  QTest::newRow("retired-source")<<7<<false;
  QTest::newRow("zero-position-is-not-known")<<8<<false;
  QTest::newRow("queue-rank-with-unknown-time")<<9<<true;
  QTest::newRow("time-rank-with-unknown-time")<<10<<false;
  QTest::newRow("foreign-engine-profile")<<11<<false;
  QTest::newRow("position-exceeds-original-int")<<12<<false;
  QTest::newRow("missing-engine-epoch")<<13<<false;
  QTest::newRow("actual-state-group-positive")<<14<<true;
 }
 void actualQueuedEngineSource(){QFETCH(int,scenario);QFETCH(bool,changed);test::OwnedEngineFixture fixture(false);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
  libtorrent::entry config(libtorrent::entry::dictionary_type{});config["locale"]="en_GB";config["tagmanager.enable"]=1;test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));
  libtorrent::entry tags(libtorrent::entry::dictionary_type{});auto& root=tags["3"]["1"];root["n"]="Generated rank consumer";root["o"]=libtorrent::entry::list_type{};
  // Invalid observations must KEEP a current member even for an impossible
  // comparison. Any inferred numeric value would incorrectly remove it.
  std::string expression=changed?(scenario==9?"tagPosition(\"Generated metrics\")==0":"tagPosition(\"Generated metrics\")==0&&tagPosition(\"Generated metrics\",2)==0"):(scenario==10?"tagPosition(\"Generated metrics\",2)==-2":"tagPosition(\"Generated metrics\")==-2");
  if(scenario==14){expression="hasTagGroup(\"Download State\")";}if(!changed){root["o"].list().emplace_back(base32(*fixture.hash));}root["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry(expression),libtorrent::entry("am=0;")};auto& source=tags["3"]["2"];source["n"]="Generated metrics";source["o"]=libtorrent::entry::list_type{libtorrent::entry(base32(*fixture.hash))};libtorrent::entry time(libtorrent::entry::dictionary_type{});if(scenario!=9&&scenario!=10){time["a"]=1700000000;}source["p"]=libtorrent::entry::list_type{time};test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(tags)));
  auto result=std::async(std::launch::async,[&]{auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Actual rank profile");auto allocated=engine::PersistentSession::create({1,88,1});test::require(bool(allocated),"Actual queued rank engine");auto service=std::move(*allocated);core::QueuePolicy policy;policy.maximumDownloads=1;policy.maximumActive=1;policy.minimumDownloads=0;test::require(bool(service->configureQueuePolicy(policy))&&bool(service->configureExternalQueue(true)),"Actual external queue setup");test::require(bool(service->configureSourcePublication([](const core::TorrentHash&,const engine::SourceMutation& f){return f();},[](const engine::SourceMutation& f){return f();})),"Owned queue source publication");auto started=service->startManaged(*lease,{.hash=*fixture.hash,.ownedLoopbackPeerPort=fixture.port(),.queued=true});test::require(bool(started),started?"":started.error().message.c_str());
   std::optional<engine::ManagedDownloadStatus> managed;const auto end=std::chrono::steady_clock::now()+10s;while(std::chrono::steady_clock::now()<end){test::require(bool(service->poll()),"Actual queued engine poll");for(const auto& value:service->managedDownloads()){if(value.hash==*fixture.hash){managed=value;}}if(managed&&managed->checked&&managed->phase==engine::ManagedPhase::Queued&&!managed->failure){break;}std::this_thread::sleep_for(10ms);}test::require(managed&&managed->checked&&managed->retained&&!managed->failure&&managed->phase==engine::ManagedPhase::Queued&&managed->received==0,"Actual queued retained source was not reached");
   const engine::QueueBinding binding{*fixture.hash,managed->admission};const std::array positions{std::pair{binding,std::uint32_t{1}}};test::require(bool(service->applyExternalQueuePositions(positions)),"Actual committed queue position projection");auto queue=service->externalQueueSnapshot();test::require(bool(queue),"Actual queue source snapshot");core::QueueDecision decision;decision.token=binding.admission;decision.action=core::QueueAction::Preserve;const std::array decisions{engine::ExternalQueueDecision{binding,decision}};auto applied=service->applyExternalQueue(queue->sequence,decisions,0,core::QueueAction::Preserve);test::require(bool(applied),"Actual queue source phase");for(const auto& value:service->managedDownloads()){if(value.hash==*fixture.hash){managed=value;}}test::require(managed&&managed->queuePosition==1&&!managed->queuePositionFailure&&!(managed->unavailableQueueInputs&(1U<<5)),"Actual queue position is unavailable");auto receipt=service->latest();while(!receipt&&std::chrono::steady_clock::now()<end){test::require(bool(service->poll()),"Actual engine observation poll");receipt=service->latest();if(!receipt){std::this_thread::sleep_for(10ms);}}test::require(bool(receipt),"Actual engine receipt");
   auto mode=storage::TagManagerStartupMode::capture(*lease);test::require(bool(mode),"Actual rank startup source");auto scope=app::InternalTagNamespace::activate(*lease,88,true,*mode);test::require(bool(scope),"Actual rank namespace");auto metadata=(*scope)->snapshot();test::require(metadata&&*metadata,"Actual rank namespace snapshot");auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Actual rank writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Actual rank tag source");auto worker=app::TagConstraintWorker::activate(*lease,88,*mode);test::require(bool(worker),"Actual rank normal worker");auto library=std::make_shared<ui::LibraryPresentation>();library->revision=7;ui::LibraryItem item;item.hash=QString::fromStdString(fixture.hash->hex());item.bytes=test::OwnedEngineFixture::PayloadBytes;item.providerAdmission=managed->admission;item.providerGeneration=88;library->items.push_back(item);app::ConstraintWorkerFrame frame;frame.session=88;frame.sequence=1;frame.library=library;frame.sourceRevision=lease->sourceRevision();frame.internalNamespace=**metadata;frame.peerEngineIdentity=receipt->identity;frame.managed={*managed};
   if(scenario==1){frame.peerEngineIdentity.reset();}if(scenario==2){frame.managed[0].queuePosition.reset();}if(scenario==3){frame.managed[0].unavailableQueueInputs|=1U<<5;}if(scenario==4){++frame.managed[0].admission;}if(scenario==5){frame.managed.push_back(*managed);}if(scenario==6){frame.managed[0].queuePositionFailure=std::make_shared<const storage::Error>(storage::Error{storage::ErrorCode::Conflict,"Generated queue failure"});}if(scenario==7){frame.managed[0].retained=false;}if(scenario==8){frame.managed[0].queuePosition=0;}if(scenario==11){frame.peerEngineIdentity->profileSession=89;}if(scenario==12){frame.managed[0].queuePosition=std::numeric_limits<std::uint32_t>::max();}if(scenario==13){frame.peerEngineIdentity->engineEpoch=0;}
   const auto inventory=test::read(fixture.profile+"/downloads.config"),state=test::read(fixture.stateFile);auto turn=(*worker)->step(**store,*snapshot,frame);if(!turn||turn->changed!=changed){throw std::runtime_error(std::string("Actual rank result differs: ")+(turn?(turn->kept?turn->kept->detail:"no detail"):turn.error().message));}const auto current=turn->changed?turn->snapshot:*snapshot;const auto root=std::find_if(current.tags()->tags.begin(),current.tags()->tags.end(),[](const auto& tag){return tag.id==1;});test::require(root!=current.tags()->tags.end()&&root->uniqueHashes.size()==1,"Rank source lost actual/kept membership");test::require(test::read(fixture.profile+"/downloads.config")==inventory&&test::read(fixture.stateFile)==state,"Tag rank effect edited torrent state");(*worker)->close();test::require(bool((*scope)->close())&&bool((*store)->close()),"Actual rank tag owners close");const auto closed=service->close(std::chrono::steady_clock::now()+5s);test::require(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure,"Actual queued engine close");return true;
  });QVERIFY(result.get());
 }
};
QTEST_GUILESS_MAIN(NamedTagRankSourceTests)
#include "named_tag_rank_source_tests.moc"

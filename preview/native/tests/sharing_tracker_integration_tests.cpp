// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_sharing_service.hpp"
#include "embedded_tracker.hpp"
#include "persistent_session.hpp"
#include "shared_seed_announce.hpp"
#include "network_policy.hpp"
#include "torrent_intake.hpp"
#include "owned_engine_fixture.hpp"
#include <QtTest>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/peer_info.hpp>
#include <chrono>
namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace lt=libtorrent;
std::optional<engine::ManagedDownloadStatus> observed(engine::PersistentSession& owner,const core::TorrentHash& hash){
  for(const auto& row:owner.managedDownloads()){if(row.hash==hash){return row;}}return {};
}
}
class SharingTrackerIntegrationTests final:public QObject {
  Q_OBJECT
private slots:
  void sharedDataReachesAnIndependentTrackerOnlyLeecher_data(){
    QTest::addColumn<bool>("directory");QTest::newRow("file")<<false;QTest::newRow("whole-directory")<<true;
  }
  void sharedDataReachesAnIndependentTrackerOnlyLeecher(){
    QFETCH(bool,directory);
    QTemporaryDir root(QStringLiteral("/tmp/biglybtc-sharing-tracker-integration-XXXXXX"));QVERIFY(root.isValid());
    const auto profile=root.path()+"/profile",payload=root.path()+"/payload";
    QVERIFY(QDir().mkpath(profile));QVERIFY(QDir().mkpath(payload));
    test::write(profile+"/downloads.config","d9:downloadsle14:unknown.futureli7e3:rawee");
    test::write(profile+"/tables.config","de");test::write(profile+"/biglybt.config","de");
    test::write(profile+"/sharing.config","d9:resourcesle14:unknown.futureli9e3:rawee");
    test::write(profile+"/tracker.config","d8:torrentsle14:unknown.futureli8e3:rawee");
    QByteArray bytes(131072,'\0');for(qsizetype i=0;i<bytes.size();++i){bytes[i]=static_cast<char>((i*31+17)%251);}
    const auto relative=directory?QStringLiteral("bundle/sub/data.bin"):QStringLiteral("data.bin");
    test::write(payload+'/'+relative,bytes);
    const auto source=directory?payload+"/bundle":payload+"/data.bin";
    auto lease=storage::ProfileLease::acquire(profile.toStdString());QVERIFY(lease);
    auto trackerService=tracker::EmbeddedTrackerService::open(*lease,profile.toStdString(),9001,tracker::TrackerServiceMode::Normal);QVERIFY(trackerService);
    auto sharingService=sharing::SavedSharingService::open(*lease,profile.toStdString(),9001,sharing::SharingMode::Normal);QVERIFY(sharingService);
    auto configured=(*trackerService)->configure(*lease,9001,{"127.0.0.1",0},{});QVERIFY(configured);
    auto endpoint=(*trackerService)->startListener(*lease,9001,{});QVERIFY(endpoint);QVERIFY(endpoint->description().port);
    const auto kind=directory?core::SavedShareKind::Directory:core::SavedShareKind::File;
    auto review=(*sharingService)->prepareCreatePersistent(*lease,9001,source.toStdString(),kind,*endpoint);QVERIFY(review);
    auto made=(*sharingService)->confirmCreate(*lease,9001,review->key);QVERIFY(made);
    QCOMPARE(made->truth,sharing::ShareMutationTruth::Committed);QVERIFY(made->selected);
    auto catalog=(*sharingService)->catalog(*lease,9001);QVERIFY(catalog);QCOMPARE(catalog->rows.size(),std::size_t{1});
    auto handle=(*sharingService)->retainShare(*lease,9001,*made->selected,catalog->generation);QVERIFY(handle);
    auto owner=engine::PersistentSession::create({9000,9001,9002});QVERIFY(owner);
    engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.httpTrackers=true;
    QVERIFY((*owner)->configureNetwork(policy));
    // A saved cache and a caller-selected hash never constitute a live seed.
    QVERIFY(!(*trackerService)->registerShare(*lease,9001,*handle,**owner,{}));
    const auto& metadata=handle->metadata();
    std::vector<compat::FilePolicy> files;for(const auto& file:metadata.layout().files){files.push_back({file.padding,0});}
    auto imported=storage::importTorrentWithFileOptions(*lease,metadata,handle->description().intakeDestination,true,files,
        nullptr,false,{}, {},false,false,false,{}, {},true);QVERIFY(imported);QCOMPARE(*imported,handle->description().hash);
    QVERIFY(!(*owner)->validateSharedSeed(*lease,metadata,handle->description().intakeDestination));
    QVERIFY(!engine::announceSharedSeed(**owner,*lease,metadata,handle->description().intakeDestination));
    QVERIFY((*owner)->startManaged(*lease,{.hash=*imported,.verifyExisting=true}));
    auto ready=[&]{return (*owner)->poll()&&(*trackerService)->poll(*lease,9001)
        &&(*owner)->validateSharedSeed(*lease,metadata,handle->description().intakeDestination);};
    QTRY_VERIFY_WITH_TIMEOUT(ready(),30000);
    const auto before=observed(**owner,*imported);QVERIFY(before&&before->received==0&&before->complete);
    QVERIFY(!(*owner)->validateSharedSeed(*lease,metadata,root.path().toStdString()));
    const auto otherProfile=root.path()+"/other-profile";QVERIFY(QDir().mkpath(otherProfile));
    auto otherLease=storage::ProfileLease::acquire(otherProfile.toStdString());QVERIFY(otherLease);
    QVERIFY(!(*owner)->validateSharedSeed(*otherLease,metadata,handle->description().intakeDestination));
    auto registered=(*trackerService)->registerShare(*lease,9001,*handle,**owner,{});QVERIFY(registered);
    QCOMPARE(registered->rows.size(),std::size_t{1});
    auto hosted=(*trackerService)->start(*lease,9001,registered->rows.front().key,{});QVERIFY(hosted);
    QVERIFY(hosted->rows.front().serving);
    QVERIFY(engine::announceSharedSeed(**owner,*lease,metadata,handle->description().intakeDestination));
    auto announced=[&]{if(!(*owner)->poll()||!(*trackerService)->poll(*lease,9001)){return false;}
      const auto state=(*trackerService)->catalog(*lease,9001);return state&&!state->rows.empty()
          &&state->rows.front().runtime&&state->rows.front().runtime->seeds>0;};
    QTRY_VERIFY_WITH_TIMEOUT(announced(),10000);
    // Both endpoints learn peers exclusively through the generated metainfo.
    // No connect_peer, external tracker, DHT, PEX, LSD or external source.
    {
      const auto leecherPath=root.path()+"/leecher";QVERIFY(QDir().mkpath(leecherPath));
      auto settings=test::localSettings();settings.set_bool(lt::settings_pack::ssrf_mitigation,false);
      settings.set_int(lt::settings_pack::alert_mask,settings.get_int(lt::settings_pack::alert_mask)|static_cast<int>(lt::alert_category::connect));
      lt::session leecher(settings);lt::error_code error;
      const auto raw=test::read(QString::fromStdString(handle->description().cacheFile.native()));
      auto info=std::make_shared<lt::torrent_info>(lt::span<const char>(raw.constData(),static_cast<std::size_t>(raw.size())),error,lt::from_span);QVERIFY(!error);
      QCOMPARE(info->trackers().size(),std::size_t{1});
      lt::add_torrent_params options;options.ti=std::move(info);options.save_path=leecherPath.toStdString();
      options.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);
      options.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;
      auto peer=leecher.add_torrent(std::move(options),error);QVERIFY(!error&&peer.is_valid());
      bool sawNative=false,otherPeer=false;
      const auto nativePort=(*owner)->tcpListenPort();QVERIFY(nativePort&&*nativePort);
      const lt::tcp::endpoint native(lt::address_v4::loopback(),*nativePort);
      auto transferred=[&]{
        if(!(*owner)->poll()||!(*trackerService)->poll(*lease,9001)){return false;}
        std::vector<lt::alert*> alerts;leecher.pop_alerts(&alerts);
        for(const auto* alert:alerts){if(const auto* connected=lt::alert_cast<lt::peer_connect_alert>(alert)){
          if(connected->endpoint==native){sawNative=true;}else{otherPeer=true;}
        }}
        std::vector<lt::peer_info> peers;peer.get_peer_info(peers);
        for(const auto& p:peers){if(p.ip==native){sawNative=true;}else{otherPeer=true;}}
        const auto seeded=observed(**owner,*imported);const auto downloaded=peer.status();
        return seeded&&seeded->sent>before->sent&&downloaded.is_seeding&&downloaded.total_payload_download>0;
      };
      const auto transferEnd=std::chrono::steady_clock::now()+30s;bool received=false;
      while(std::chrono::steady_clock::now()<transferEnd&&!(received=transferred())){QTest::qWait(10);}
      QVERIFY2(received,"Owned tracker-only payload transfer did not complete within30seconds");QVERIFY(sawNative);QVERIFY(!otherPeer);
      QCOMPARE(test::read(leecherPath+'/'+relative),bytes);
    }
    auto live=(*trackerService)->catalog(*lease,9001);QVERIFY(live);QVERIFY(live->rows.front().runtime);
    QVERIFY(live->rows.front().runtime->announces>=2);
    // Stop/re-publish must reuse only the genuine stopped verified target and
    // exact tracker record, without duplicate rows or deletion of payload.
    auto withdrawn=(*trackerService)->stop(*lease,9001,live->rows.front().key,{});QVERIFY(withdrawn);
    QVERIFY((*owner)->stopManaged(*imported));
    const auto retired=[&]{if(!(*owner)->poll()){return false;}const auto row=observed(**owner,*imported);
      return row&&!row->retained&&row->phase==engine::ManagedPhase::Stopped;};
    QTRY_VERIFY_WITH_TIMEOUT(retired(),15000);QVERIFY((*owner)->forgetManaged(*imported));
    QVERIFY((*owner)->startManaged(*lease,{.hash=*imported,.verifyExisting=true}));
    QTRY_VERIFY_WITH_TIMEOUT(ready(),30000);
    auto repeated=(*trackerService)->registerShare(*lease,9001,*handle,**owner,{});QVERIFY(repeated);
    QCOMPARE(repeated->rows.size(),std::size_t{1});
    auto resumed=(*trackerService)->start(*lease,9001,repeated->rows.front().key,{});QVERIFY(resumed);QVERIFY(resumed->rows.front().serving);
    QVERIFY(engine::announceSharedSeed(**owner,*lease,metadata,handle->description().intakeDestination));
    QTRY_VERIFY_WITH_TIMEOUT(announced(),10000);

    QVERIFY((*owner)->pauseManaged(*imported,true));QVERIFY(!(*owner)->validateSharedSeed(*lease,metadata,handle->description().intakeDestination));
    QVERIFY(!engine::announceSharedSeed(**owner,*lease,metadata,handle->description().intakeDestination));
    auto stopped=(*trackerService)->stopListener(*lease,9001,{});QVERIFY(stopped);QVERIFY(!stopped->listener.running&&stopped->listener.joined);
    QVERIFY(!endpoint->current(*lease,9001));
    const auto closed=(*owner)->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);
    const auto joined=(*trackerService)->close();QVERIFY(joined.listenerJoined&&joined.connectionsJoined);
    QVERIFY((*sharingService)->close());
    QCOMPARE(test::read(payload+'/'+relative),bytes);
    for(const auto& filename:{"sharing.config","tracker.config","downloads.config"}){
      auto document=core::Document::parse(test::read(profile+'/'+filename).toStdString());QVERIFY(document);QVERIFY(document->find("unknown.future"));
    }
  }
};
QTEST_GUILESS_MAIN(SharingTrackerIntegrationTests)
#include "sharing_tracker_integration_tests.moc"

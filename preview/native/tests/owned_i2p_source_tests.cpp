// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "source_permit.hpp"
#include "channel.hpp"
#include <libtorrent/torrent_flags.hpp>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>
#include <fcntl.h>
#include <unistd.h>
namespace sam=biglybtc::services::i2p::provider;
namespace i2cp=biglybtc::services::i2p::i2cp;
using namespace owned_i2p_test;
class OwnedI2pSourceTests:public QObject {
  Q_OBJECT
private slots:
  void boundSourceAuthorityRefusesLegacySingleStreamPath(){
    Fixture f;auto source=sam::ProfileSource::open(*f.lease,f.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    i2cp::InitialChannel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));
    auto stream=channel.connectStream((*source)->destination().wire,{});QVERIFY(!stream);QCOMPARE(stream.error().code,biglybtc::services::i2p::ErrorCode::Unsupported);
    QCOMPARE(channel.state(),i2cp::InitialChannel::State::Idle);QVERIFY(!channel.sourceEffectsPending());
  }
  void genuinePureSourceRetainsSameLeaseAndOriginalBytes(){
    Fixture f;const auto oldConfig=read(f.path/"biglybt.config"),oldKey=read(f.key),rawInfo=f.info;
    auto source=sam::ProfileSource::open(*f.lease,f.hash);QVERIFY2(source,source?"":source.error().message.c_str());QVERIFY((*source)->lease()->sharesOwnership(*f.lease));
    f.lease.reset();auto storage=(*source)->prepareStorage();QVERIFY2(storage,storage?"":storage.error().message.c_str());QVERIFY((*source)->validate());
    QVERIFY(storage->identity().full.v1.has_value());QCOMPARE(storage->identity().originalKey,f.hash);QVERIFY(!storage->mainlinePolicy().discover);QVERIFY(!storage->mainlinePolicy().announce);
    QCOMPARE(read(f.path/"biglybt.config"),oldConfig);QCOMPARE(read(f.key),oldKey);QVERIFY(read(f.state()).find(rawInfo)!=std::string::npos);
    auto key=(*source)->takeIdentity();QVERIFY(key);QCOMPARE(key->destination()->wire,(*source)->destination().wire);QVERIFY(!(*source)->takeIdentity());
    auto params=storage->parameters();QVERIFY(bool(params.flags&libtorrent::torrent_flags::disable_dht));QVERIFY(bool(params.flags&libtorrent::torrent_flags::disable_lsd));QVERIFY(bool(params.flags&libtorrent::torrent_flags::disable_pex));
  }
  void keyReplacementAndSourceMutationWithdrawWithoutRepair(){
    Fixture f;auto source=sam::ProfileSource::open(*f.lease,f.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    const auto bytes=read(f.key);fs::rename(f.key,f.key.string()+".old");write(f.key,bytes);QVERIFY(::chmod(f.key.c_str(),0600)==0);
    QVERIFY(!(*source)->validate());QVERIFY(!(*source)->effectAuthority()->current());QCOMPARE(read(f.key),bytes);QVERIFY(fs::exists(f.key.string()+".old"));
    Fixture g;auto other=sam::ProfileSource::open(*g.lease,g.hash);QVERIFY(other);auto prepared=(*other)->prepareStorage();QVERIFY(prepared);
    write(g.path/"biglybt.config",dict({{"Plugin.azneti2phelper.enable","i0e"}}));QVERIFY(!(*other)->validate());QCOMPARE(read(g.path/"biglybt.config"),dict({{"Plugin.azneti2phelper.enable","i0e"}}));
  }
  void unsupportedNetworksAndUnsafeOriginalKeysRefuse_data(){QTest::addColumn<int>("kind");for(int i=0;i<5;++i){QTest::newRow(std::to_string(i).c_str())<<i;}}
  void unsupportedNetworksAndUnsafeOriginalKeysRefuse(){
    QFETCH(int,kind);Fixture f(kind==0);const auto original=read(f.key);
    if(kind==1){fs::rename(f.key,f.key.string()+".old");fs::create_symlink(f.key.string()+".old",f.key);}
    if(kind==2){fs::create_hard_link(f.key,f.key.string()+".alias");}
    if(kind==3){fs::remove(f.key);QVERIFY(::mkfifo(f.key.c_str(),0600)==0);}
    if(kind==4){QVERIFY(::chmod(f.key.c_str(),0644)==0);}
    QVERIFY(!sam::ProfileSource::open(*f.lease,f.hash));if(kind==0||kind==2||kind==4){QCOMPARE(read(f.key),original);}
  }
  void sourcePermitIsOneShotAndBlockedIoCannotExtendChannelDeadline(){
    Fixture f;auto source=sam::ProfileSource::open(*f.lease,f.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    QObject receiver;sam::SourcePermit permit(*source,&receiver);
    auto access=(*source)->lease()->sourceAccess();QVERIFY(access);QVERIFY(permit.request());
    QTRY_COMPARE_WITH_TIMEOUT(permit.phase(),sam::SourcePermit::Phase::Reading,2000);
    i2cp::InitialChannel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost,0));
    i2cp::InitialChannel::Options options{QHostAddress::LocalHost,server.serverPort(),"owned-test",false,100,false};QVERIFY(channel.begin(options));
    // No worker permit means no actual TCP connect or greeting. Existing
    // absolute channel deadline still closes without filesystem progress.
    QTRY_COMPARE_WITH_TIMEOUT(channel.state(),i2cp::InitialChannel::State::Failed,2000);QCOMPARE(channel.failure(),i2cp::InitialChannel::Failure::Deadline);QVERIFY(!server.hasPendingConnections());
    access=std::unexpected(biglybtc::storage::Error{biglybtc::storage::ErrorCode::Cancelled,"release owned test gate"});
    QTRY_COMPARE_WITH_TIMEOUT(permit.phase(),sam::SourcePermit::Phase::Ready,2000);QVERIFY(permit.take());QVERIFY((*source)->effectAuthority()->granted());QVERIFY(!channel.resumeAuthorizedEffects());QVERIFY(!permit.take());permit.acknowledge();QVERIFY(!(*source)->effectAuthority()->granted());
    QTRY_COMPARE_WITH_TIMEOUT(permit.phase(),sam::SourcePermit::Phase::Idle,2000);permit.stop();permit.join();
  }
};
QTEST_GUILESS_MAIN(OwnedI2pSourceTests)
#include "owned_i2p_source_tests.moc"

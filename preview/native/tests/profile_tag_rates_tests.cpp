// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_tag_rates.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#include <QtTest>
#include <future>
using namespace biglybtc;using namespace std::chrono_literals;
namespace {
std::string text(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
std::string base32(const core::TorrentHash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned bits=0,buffer=0;for(auto value:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(value);bits+=8;while(bits>=5){bits-=5;out+=chars[(buffer>>bits)&31];}}if(bits){out+=chars[(buffer<<(5-bits))&31];}return out;}
QByteArray tag(const core::TorrentHash& hash,std::string rate){return QByteArray::fromStdString("d1:3d1:7d1:cd7:rl.down"+rate+"e1:n13:Generated tag1:ol"+text(base32(hash))+"eeee");}
auto lease(const test::OwnedEngineFixture& fixture){auto held=storage::ProfileLease::acquire(fixture.native());test::require(bool(held),"Generated profile lease");return std::move(*held);}
auto owner(){auto result=engine::PersistentSession::create({901,902,903});test::require(bool(result),"Qualified actual owner");return std::move(*result);}
}
class ProfileTagRatesTests final:public QObject {
 Q_OBJECT
private slots:
 void unsupportedPreflight_data(){QTest::addColumn<QByteArray>("rate");for(const auto& rate:{QByteArray("i-1e"),QByteArray("i1e"),QByteArray("i9e"),QByteArray("i2147483647e"),QByteArray("3:bad")}){QTest::newRow(rate.constData())<<rate;}}
 void unsupportedPreflight(){
  QFETCH(QByteArray,rate);test::OwnedEngineFixture fixture(false);auto held=lease(fixture);const auto original=test::read(fixture.stateFile),inventory=test::read(fixture.profile+"/downloads.config");
  test::write(fixture.profile+"/tag.config",tag(*fixture.hash,rate.toStdString()));const auto source=test::read(fixture.profile+"/tag.config");
  auto service=owner();auto result=service->startManaged(held,{*fixture.hash,0,false,false});
  QVERIFY(!result);QCOMPARE(result.error().code,storage::ErrorCode::Unsupported);QVERIFY(service->managedDownloads().empty());
  QCOMPARE(test::read(fixture.stateFile),original);QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.profile+"/tag.config"),source);QVERIFY(fixture.intact(false));
  QVERIFY(!QFile::exists(fixture.profile+"/tag.config.bak"));QVERIFY(!QFile::exists(fixture.profile+"/tag.config.saving"));
 }
 void disabledModeAndForeignSource(){
  test::OwnedEngineFixture first(false),second(false);auto held=lease(first),foreign=lease(second);
  test::write(first.profile+"/biglybt.config","d17:tagmanager.enablei0ee");test::write(first.profile+"/tag.config","unreadable disabled source");
  auto mode=storage::TagManagerStartupMode::capture(held);QVERIFY(mode);QVERIFY(!mode->enabled());
  auto rates=engine::ProfileTagRates::open(held,*mode);QVERIFY(rates);QVERIFY(!engine::ProfileTagRates::open(foreign,*mode));
  auto wrong=std::async(std::launch::async,[&]{return (*rates)->close();});QVERIFY(!wrong.get());QVERIFY((*rates)->close());
 }
 void noncanonicalNumericRateKeysRefuseBeforeResumeMutation(){
  test::OwnedEngineFixture fixture(false);auto held=lease(fixture);auto source=tag(*fixture.hash,"i16384e");source.replace("1:7d","2:07d");test::write(fixture.profile+"/tag.config",source);const auto state=test::read(fixture.stateFile);auto service=owner();auto attempted=service->startManaged(held,{*fixture.hash,0,false,false});QVERIFY(!attempted);QCOMPARE(test::read(fixture.stateFile),state);QCOMPARE(test::read(fixture.profile+"/tag.config"),source);
 }
 void damagedSourceAndRecoveryAreNeverRepaired(){
  test::OwnedEngineFixture fixture(false);auto held=lease(fixture);auto mode=storage::TagManagerStartupMode::capture(held);QVERIFY(mode);
  const auto raw=tag(*fixture.hash,"i16384e");test::write(fixture.profile+"/tag.config.bak",raw);
  QVERIFY(!engine::ProfileTagRates::open(held,*mode));QVERIFY(!QFile::exists(fixture.profile+"/tag.config"));
  test::write(fixture.profile+"/tag.config",raw);test::write(fixture.profile+"/tag.config.saving",raw);
  QVERIFY(!engine::ProfileTagRates::open(held,*mode));QCOMPARE(test::read(fixture.profile+"/tag.config.bak"),raw);QCOMPARE(test::read(fixture.profile+"/tag.config.saving"),raw);
 }
 void idleCloseReleasesStartupModeWhileOwnerLives(){
  QTemporaryDir directory;QVERIFY(directory.isValid());auto service=owner();
  {
   auto held=storage::ProfileLease::acquire(directory.path().toStdString());QVERIFY(held);
   auto mode=storage::TagManagerStartupMode::capture(*held);QVERIFY(mode);
   QVERIFY(service->configureTagManagerStartup(*mode));QVERIFY(service->startIdle(*held));
   const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined);
  }
  auto reopened=storage::ProfileLease::acquire(directory.path().toStdString());QVERIFY(reopened);
 }
 void rateCloseReleasesSourceWhileSdkAndObjectLive(){
  QTemporaryDir directory;QVERIFY(directory.isValid());libtorrent::session sdk(test::localSettings());
  std::unique_ptr<engine::ProfileTagRates> rates;
  {
   auto held=storage::ProfileLease::acquire(directory.path().toStdString());QVERIFY(held);
   auto hash=core::TorrentHash::parse(std::string(20,'x'));QVERIFY(hash);test::write(directory.path()+"/tag.config",tag(*hash,"i16384e"));
   auto mode=storage::TagManagerStartupMode::capture(*held);QVERIFY(mode);
   auto result=engine::ProfileTagRates::open(*held,*mode);QVERIFY(result);rates=std::move(*result);
   QVERIFY(rates->prepare(*hash,sdk));QVERIFY(rates->close());
  }
  auto reopened=storage::ProfileLease::acquire(directory.path().toStdString());QVERIFY(reopened);
 }
 void positiveStartupAndSourceRecovery(){
  test::OwnedEngineFixture fixture(false);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),10000);auto actualLease=std::make_unique<storage::ProfileLease>(lease(fixture));auto& held=*actualLease;
  test::write(fixture.profile+"/tag.config",tag(*fixture.hash,"i16384e"));const auto source=test::read(fixture.profile+"/tag.config");
  auto mode=storage::TagManagerStartupMode::capture(held);QVERIFY(mode);auto service=owner();QVERIFY(service->configureTagManagerStartup(*mode));
  auto started=service->startManaged(held,{*fixture.hash,fixture.port(),false,false});QVERIFY2(started,started?"":started.error().message.c_str());
  QVERIFY(!service->configureTagManagerStartup(*mode));
  const auto sample=[&]{auto all=service->managedDownloads();return all.front();};
  const auto limitEnd=std::chrono::steady_clock::now()+3s;while(std::chrono::steady_clock::now()<limitEnd){QVERIFY(service->poll());QTest::qWait(25);}
  QVERIFY(sample().received<100000);QVERIFY(!service->tagRateFailure());
  test::write(fixture.profile+"/tag.config.saving",source);
  const auto heldBySource=[&]{if(!service->poll()){return false;}return bool(service->tagRateFailure());};QTRY_VERIFY_WITH_TIMEOUT(heldBySource(),4000);
  QVERIFY(QFile::remove(fixture.profile+"/tag.config.saving"));
  // Actual current primary change removes the group without writing any data.
  test::write(fixture.profile+"/tag.config",tag(*fixture.hash,"i0e"));
  const auto recovered=[&]{if(!service->poll()){return false;}return !service->tagRateFailure()&&sample().complete;};QTRY_VERIFY_WITH_TIMEOUT(recovered(),15000);
  const auto closed=service->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined);QVERIFY(!closed.checkpointFailure);QVERIFY(fixture.intact());
  mode=std::unexpected(storage::Error{storage::ErrorCode::Missing,"released test startup mode"});actualLease.reset();
  service.reset(); // ManagedStorage retains its separate certification lease until destruction.
  auto reopened=storage::ProfileLease::acquire(fixture.native());QVERIFY(reopened);

 }
};
QTEST_GUILESS_MAIN(ProfileTagRatesTests)
#include "profile_tag_rates_tests.moc"

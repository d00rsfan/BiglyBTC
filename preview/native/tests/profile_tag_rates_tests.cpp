// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_tag_rates.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#include <QtTest>
#include "tag_rate_capacity.hpp"
#include <libtorrent/entry.hpp>
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
 void unsupportedPreflight_data(){QTest::addColumn<QByteArray>("rate");for(const auto& rate:{QByteArray("i2147483647e"),QByteArray("3:bad")}){QTest::newRow(rate.constData())<<rate;}}
 void unsupportedPreflight(){
  QFETCH(QByteArray,rate);test::OwnedEngineFixture fixture(false);auto held=lease(fixture);const auto original=test::read(fixture.stateFile),inventory=test::read(fixture.profile+"/downloads.config");
  test::write(fixture.profile+"/tag.config",tag(*fixture.hash,rate.toStdString()));const auto source=test::read(fixture.profile+"/tag.config");
  auto service=owner();auto result=service->startManaged(held,{*fixture.hash,0,false,false});
  QVERIFY(!result);QCOMPARE(result.error().code,storage::ErrorCode::Unsupported);QVERIFY(service->managedDownloads().empty());
  QCOMPARE(test::read(fixture.stateFile),original);QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.profile+"/tag.config"),source);QVERIFY(fixture.intact(false));
  QVERIFY(!QFile::exists(fixture.profile+"/tag.config.bak"));QVERIFY(!QFile::exists(fixture.profile+"/tag.config.saving"));
 }
 void extendedSourceReadiness_data(){QTest::addColumn<int>("bytes");for(int n:{-1,1,9}){QTest::newRow(qPrintable(QString::number(n)))<<n;}}
 void extendedSourceReadiness(){
  QFETCH(int,bytes);test::OwnedEngineFixture fixture(false);auto held=lease(fixture);test::write(fixture.profile+"/tag.config",tag(*fixture.hash,"i"+std::to_string(bytes)+"e"));const auto source=test::read(fixture.profile+"/tag.config"),state=test::read(fixture.stateFile);
  auto mode=storage::TagManagerStartupMode::capture(held);QVERIFY(mode);auto rates=engine::ProfileTagRates::open(held,*mode);QVERIFY(rates);libtorrent::session sdk(test::localSettings());
  auto ready=(*rates)->prepare(*fixture.hash,sdk);QCOMPARE(bool(ready),engine::SdkTagRateGroup::supports({0,bytes}));
  if(ready){auto store=storage::ManualTagStore::open(held);QVERIFY(store);auto snapshot=(*store)->snapshot();QVERIFY(snapshot);const std::array<std::int32_t,1> ids{7};auto proof=(*rates)->prepareSource(held,**store,*snapshot,ids,sdk);QVERIFY(proof);QVERIFY(proof->matches(held,**store,*snapshot,7));}
  QVERIFY((*rates)->close());QCOMPARE(test::read(fixture.profile+"/tag.config"),source);QCOMPARE(test::read(fixture.stateFile),state);
 }
 void disabledDownloadStartupAndSourceRemoval(){
  test::OwnedEngineFixture fixture(false);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),10000);auto held=lease(fixture);test::write(fixture.profile+"/tag.config",tag(*fixture.hash,"i-1e"));auto service=owner();
  if(!engine::SdkTagRateGroup::supports({0,-1})){QVERIFY(!service->startManaged(held,{*fixture.hash,fixture.port(),false,false}));return;}
  auto started=service->startManaged(held,{*fixture.hash,fixture.port(),false,false});QVERIFY2(started,started?"":started.error().message.c_str());
  const auto end=std::chrono::steady_clock::now()+1500ms;while(std::chrono::steady_clock::now()<end){QVERIFY(service->poll());QTest::qWait(25);}QVERIFY(!service->tagRateFailure());QCOMPARE(service->managedDownloads().front().received,std::uint64_t{0});
  test::write(fixture.profile+"/tag.config",tag(*fixture.hash,"i0e"));QTRY_VERIFY_WITH_TIMEOUT(service->poll()&&!service->tagRateFailure()&&service->managedDownloads().front().complete,15000);
  const auto closed=service->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined);QVERIFY(!closed.checkpointFailure);QVERIFY(fixture.intact());
 }
 void fullQualifiedCohortRestoresBeforePayload(){
  test::OwnedEngineFixture fixture(false);auto held=lease(fixture);libtorrent::entry saved(libtorrent::entry::dictionary_type{});
  for(int id=1;id<=int(storage::MaximumTagRateGroups);++id){auto& row=saved["3"][std::to_string(id)];row["n"]="Generated capacity tag";row["c"]["rl.down"]=16384;row["o"]=libtorrent::entry::list_type{};row["o"].list().emplace_back(base32(*fixture.hash));}
  test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(saved)));const auto original=test::read(fixture.profile+"/tag.config");auto service=owner();
  auto started=service->startManaged(held,{*fixture.hash,0,true,false,true});QVERIFY2(started,started?"":started.error().message.c_str());QVERIFY(!service->tagRateFailure());QCOMPARE(service->managedDownloads().size(),std::size_t{1});QCOMPARE(service->managedDownloads().front().received,std::uint64_t{0});
  auto closed=service->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&!closed.checkpointFailure);QCOMPARE(test::read(fixture.profile+"/tag.config"),original);QVERIFY(fixture.intact(false));
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

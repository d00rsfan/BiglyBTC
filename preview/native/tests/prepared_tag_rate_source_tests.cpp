// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_tag_rates.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <array>
#include <future>
using namespace biglybtc;
namespace {
void require(bool ok,const char* reason){if(!ok){throw std::runtime_error(reason);}}
libtorrent::settings_pack isolated(){libtorrent::settings_pack p;p.set_str(libtorrent::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(libtorrent::settings_pack::dht_bootstrap_nodes,"");for(int k:{libtorrent::settings_pack::enable_dht,libtorrent::settings_pack::enable_lsd,libtorrent::settings_pack::enable_upnp,libtorrent::settings_pack::enable_natpmp}){p.set_bool(k,false);}return p;}
struct Fixture {
 QTemporaryDir directory;std::optional<storage::ProfileLease> lease;std::optional<storage::TagManagerStartupMode> mode;
 std::unique_ptr<storage::ManualTagStore> store;storage::ManualTagSnapshot source;
 std::unique_ptr<libtorrent::session> sdk;std::unique_ptr<engine::ProfileTagRates> rates;
 void write(std::string_view rate){QFile f(directory.path()+"/tag.config");const auto raw=QByteArray::fromStdString("d1:3d1:7d1:cd5:rl.upi"+std::string(rate)+"ee1:n13:Generated tag1:oleeee");require(f.open(QIODevice::WriteOnly)&&f.write(raw)==raw.size(),"Generated source write");}
 Fixture(){require(directory.isValid(),"Generated root");auto held=storage::ProfileLease::acquire(directory.path().toStdString());require(bool(held),"Actual profile lease");lease.emplace(std::move(*held));write("16384");auto atOpen=storage::TagManagerStartupMode::capture(*lease);require(bool(atOpen),"Actual startup setting");mode.emplace(std::move(*atOpen));auto opened=storage::ManualTagStore::open(*lease);require(bool(opened),"Actual manual source store");store=std::move(*opened);source=store->snapshot().value();sdk=std::make_unique<libtorrent::session>(isolated());auto groupOwner=engine::ProfileTagRates::open(*lease,*mode);require(bool(groupOwner),"Actual profile rate owner");rates=std::move(*groupOwner);}
 auto prepare(std::span<const std::int32_t> ids){return rates->prepareSource(*lease,*store,source,ids,*sdk);}
};
}
class PreparedTagRateSourceTests final:public QObject {
 Q_OBJECT
private slots:
 void actualReadySourceAndGenericWriterRefusal(){
  Fixture f;const std::array ids{7};storage::PreparedTagRateSource absent;QVERIFY(!absent.matches(*f.lease,*f.store,f.source,7));
  auto prepared=f.prepare(ids);QVERIFY2(prepared,prepared?"":prepared.error().message.c_str());QVERIFY(prepared->matches(*f.lease,*f.store,f.source,7));QVERIFY(!prepared->matches(*f.lease,*f.store,f.source,8));
  const auto hash=core::TorrentHash::parse(std::string(20,'x'));QVERIFY(hash);
  const auto result=f.store->apply(f.source,core::SavedTagAddMember{7,*hash,123});QVERIFY(!result);QVERIFY(prepared->matches(*f.lease,*f.store,f.source,7));
  QVERIFY(!QFile::exists(f.directory.path()+"/tag.config.bak"));QVERIFY(!QFile::exists(f.directory.path()+"/tag.config.saving"));
 }
 void foreignStoreProfileAndWorker(){
  Fixture f,other;const std::array ids{7};auto prepared=f.prepare(ids);QVERIFY(prepared);
  auto another=storage::ManualTagStore::open(*f.lease);QVERIFY(another);auto view=(*another)->snapshot();QVERIFY(view);
  QVERIFY(!prepared->matches(*f.lease,**another,*view,7));QVERIFY(!prepared->matches(*other.lease,*other.store,other.source,7));
  QVERIFY(!f.rates->prepareSource(*other.lease,*other.store,other.source,ids,*f.sdk));
  auto wrong=std::async(std::launch::async,[&]{return prepared->matches(*f.lease,*f.store,f.source,7);});QVERIFY(!wrong.get());
 }
 void logicalReloadAndActualSourceChange(){
  Fixture f;const std::array ids{7};auto old=f.prepare(ids);QVERIFY(old);auto reloaded=f.store->reload();QVERIFY(reloaded);f.source=*reloaded;
  QVERIFY(!old->matches(*f.lease,*f.store,f.source,7));auto current=f.prepare(ids);QVERIFY(current);QVERIFY(current->matches(*f.lease,*f.store,f.source,7));
  f.write("32768");QVERIFY(!f.prepare(ids));QVERIFY(!current->matches(*f.lease,*f.store,f.source,7));
  reloaded=f.store->reload();QVERIFY(reloaded);f.source=*reloaded;auto updated=f.prepare(ids);QVERIFY(updated);QVERIFY(updated->matches(*f.lease,*f.store,f.source,7));
  QVERIFY(f.rates->close());QVERIFY(!updated->matches(*f.lease,*f.store,f.source,7));
 }
 void requestGuardsAndStoreClose(){
  Fixture f;const std::array duplicate{7,7};const std::array missing{8};const std::array valid{7};const std::array<std::int32_t,0> empty{};
  QVERIFY(!f.prepare(duplicate));QVERIFY(!f.prepare(missing));QVERIFY(!f.prepare(empty));
  auto prepared=f.prepare(valid);QVERIFY(prepared);QVERIFY(f.store->close());QVERIFY(!prepared->matches(*f.lease,*f.store,f.source,7));
 }
 void sdkAbortRevokesLiveSourceProof(){
  Fixture f;const std::array ids{7};auto prepared=f.prepare(ids);QVERIFY(prepared);QVERIFY(prepared->matches(*f.lease,*f.store,f.source,7));
  f.sdk.reset();QVERIFY(!prepared->matches(*f.lease,*f.store,f.source,7));
 }
 void preparationKeepsNoProfileLease(){
  storage::PreparedTagRateSource kept;
  {
   Fixture f;const std::array ids{7};auto prepared=f.prepare(ids);QVERIFY(prepared);kept=*prepared;
   // Snapshot/proof remain allocated after actual controller/store/mode release.
   QVERIFY(f.rates->close());QVERIFY(f.store->close());f.mode.reset();f.lease.reset();
   auto again=storage::ProfileLease::acquire(f.directory.path().toStdString());QVERIFY(again);
  }
 }
};
QTEST_GUILESS_MAIN(PreparedTagRateSourceTests)
#include "prepared_tag_rate_source_tests.moc"

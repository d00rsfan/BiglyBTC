// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_tag_rates.hpp"
#include "sdk_shared_tag_budget.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <array>
#include <future>
using namespace biglybtc;
namespace {
void require(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
libtorrent::settings_pack isolated(){libtorrent::settings_pack p;p.set_str(libtorrent::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(libtorrent::settings_pack::dht_bootstrap_nodes,"");for(int k:{libtorrent::settings_pack::enable_dht,libtorrent::settings_pack::enable_lsd,libtorrent::settings_pack::enable_upnp,libtorrent::settings_pack::enable_natpmp}){p.set_bool(k,false);}return p;}
struct Fixture {
 QTemporaryDir directory;
 std::optional<storage::ProfileLease> lease;
 std::optional<storage::TagManagerStartupMode> mode;
 std::unique_ptr<storage::ManualTagStore> store;
 storage::ManualTagSnapshot source;
 std::unique_ptr<libtorrent::session> first=std::make_unique<libtorrent::session>(isolated());
 std::unique_ptr<libtorrent::session> second=std::make_unique<libtorrent::session>(isolated());
 std::unique_ptr<engine::ProfileTagRates> rates;
 void write(std::string_view rate){QFile f(directory.path()+"/tag.config");const auto raw=QByteArray::fromStdString("d1:3d1:7d1:cd5:rl.upi"+std::string(rate)+"ee1:n13:Generated tag1:oleeee");require(f.open(QIODevice::WriteOnly)&&f.write(raw)==raw.size(),"Generated source write");}
 Fixture(){require(directory.isValid(),"Generated profile");auto held=storage::ProfileLease::acquire(directory.path().toStdString());require(bool(held),"Actual lease");lease.emplace(std::move(*held));write("16384");auto captured=storage::TagManagerStartupMode::capture(*lease);require(bool(captured),"Actual startup mode");mode.emplace(std::move(*captured));auto opened=storage::ManualTagStore::open(*lease);require(bool(opened),"Actual manual-tag store");store=std::move(*opened);source=store->snapshot().value();auto owner=engine::ProfileTagRates::open(*lease,*mode);require(bool(owner),"Actual common source owner");rates=std::move(*owner);}
 auto prepare(libtorrent::session_handle session){const std::array ids{7};return rates->prepareSource(*lease,*store,source,ids,session);}
 bool matches(const storage::PreparedTagRateSource& proof){return proof.matches(*lease,*store,source,7);}
 auto join(){return prepare(*second);}
};
}
class ProfileCohortTests final:public QObject {
 Q_OBJECT
private slots:
 void membershipChangesRevokePreviousCompleteCohort(){
  Fixture f;auto before=f.prepare(*f.first);QVERIFY(before);QVERIFY(f.matches(*before));
  if(!engine::SdkSharedQuotaScope::supported()){QVERIFY(!f.join());QVERIFY(f.matches(*before));return;}
  auto joined=f.join();QVERIFY2(joined,joined?"":joined.error().message.c_str());QVERIFY(!f.matches(*before));QVERIFY(f.matches(*joined));
  auto noOp=f.prepare(*f.first);QVERIFY(noOp);QVERIFY(f.matches(*joined));QVERIFY(f.matches(*noOp));
  QVERIFY(f.rates->release(*f.second));QVERIFY(!f.matches(*joined));auto remaining=f.prepare(*f.first);QVERIFY(remaining);QVERIFY(f.matches(*remaining));
  auto rejoined=f.join();QVERIFY(rejoined);QVERIFY(!f.matches(*remaining));QVERIFY(f.matches(*rejoined));
 }
 void eightOwnerBoundaryRefusesBeforeChangingCohort(){
  Fixture f;auto proof=f.prepare(*f.first);QVERIFY(proof);
  if(!engine::SdkSharedQuotaScope::supported()){QVERIFY(!f.join());QVERIFY(f.matches(*proof));return;}
  proof=f.join();QVERIFY(proof);std::vector<std::unique_ptr<libtorrent::session>> extra;
  for(int n=0;n<6;++n){extra.push_back(std::make_unique<libtorrent::session>(isolated()));proof=f.prepare(*extra.back());QVERIFY(proof);QVERIFY(f.matches(*proof));}
  libtorrent::session ninth(isolated());QVERIFY(!f.prepare(ninth));QVERIFY(f.matches(*proof));
 }
 void abortOfAnySdkRevokesCompletePreparation(){
  if(!engine::SdkSharedQuotaScope::supported()){return;}
  Fixture f;QVERIFY(f.prepare(*f.first));auto complete=f.join();QVERIFY(complete);QVERIFY(f.matches(*complete));
  f.second.reset();QVERIFY(!f.matches(*complete));auto survivor=f.prepare(*f.first);QVERIFY2(survivor,survivor?"":survivor.error().message.c_str());QVERIFY(f.matches(*survivor));QVERIFY(!f.matches(*complete));
 }
 void damagedSourceHoldsAllSessionsAndRecoversOnlyOwnedPauses(){
  if(!engine::SdkSharedQuotaScope::supported()){return;}
  Fixture f;QVERIFY(f.prepare(*f.first));auto complete=f.join();QVERIFY(complete);
  f.second->pause();QVERIFY(f.second->is_paused());QFile saving(f.directory.path()+"/tag.config.saving");QVERIFY(saving.open(QIODevice::WriteOnly));saving.write("generated interrupted save");saving.close();
  QVERIFY(!f.rates->synchronize(*f.first));QVERIFY(!f.matches(*complete));QVERIFY(f.first->is_paused());QVERIFY(f.second->is_paused());
  QVERIFY(saving.remove());QVERIFY(f.rates->synchronize(*f.first));QVERIFY(!f.first->is_paused());QVERIFY(f.second->is_paused());auto restored=f.prepare(*f.first);QVERIFY(restored);QVERIFY(f.matches(*restored));
 }
 void actualLimitChangesRefreshAllSdkWitnesses(){
  Fixture f;auto old=f.prepare(*f.first);QVERIFY(old);if(engine::SdkSharedQuotaScope::supported()){old=f.join();QVERIFY(old);}
  for(const auto rate:{"-1","1","32768"}){
   f.write(rate);auto fresh=f.store->reload();QVERIFY(fresh);f.source=*fresh;QVERIFY(f.rates->synchronize(*f.first));QVERIFY(!f.matches(*old));
   auto next=f.prepare(*f.first);QVERIFY2(next,next?"":next.error().message.c_str());QVERIFY(f.matches(*next));old=std::move(next);
  }
 }
 void sourceStoreAndWorkerAreStillExact(){
  Fixture f,other;auto proof=f.prepare(*f.first);QVERIFY(proof);if(engine::SdkSharedQuotaScope::supported()){proof=f.join();QVERIFY(proof);}
  QVERIFY(!proof->matches(*other.lease,*other.store,other.source,7));auto foreign=storage::ManualTagStore::open(*f.lease);QVERIFY(foreign);auto snapshot=(*foreign)->snapshot();QVERIFY(snapshot);QVERIFY(!proof->matches(*f.lease,**foreign,*snapshot,7));
  auto wrong=std::async(std::launch::async,[&]{return f.prepare(*f.first);});QVERIFY(!wrong.get());QVERIFY(f.matches(*proof));
  auto wrongRelease=std::async(std::launch::async,[&]{return f.rates->release(*f.first);});QVERIFY(!wrongRelease.get());QVERIFY(f.matches(*proof));
 }
 void closeRevokesAllCohortPreparations(){
  Fixture f;auto proof=f.prepare(*f.first);QVERIFY(proof);if(engine::SdkSharedQuotaScope::supported()){proof=f.join();QVERIFY(proof);}
  QVERIFY(f.rates->close());QVERIFY(!f.matches(*proof));QVERIFY(f.first->is_paused());if(engine::SdkSharedQuotaScope::supported()){QVERIFY(f.second->is_paused());}
  QVERIFY(!f.prepare(*f.first));
 }
 void preparedCohortRetainsNoProfileLease(){
  Fixture f;auto proof=f.prepare(*f.first);QVERIFY(proof);if(engine::SdkSharedQuotaScope::supported()){proof=f.join();QVERIFY(proof);}
  QVERIFY(f.rates->close());QVERIFY(f.store->close());f.mode.reset();f.lease.reset();auto reopened=storage::ProfileLease::acquire(f.directory.path().toStdString());QVERIFY(reopened);
 }
};
QTEST_GUILESS_MAIN(ProfileCohortTests)
#include "profile_cohort_tests.moc"

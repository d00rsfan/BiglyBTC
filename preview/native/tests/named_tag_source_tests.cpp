// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal_tag_namespace.hpp"
#include "tag_constraint_worker.hpp"
#include "owned_engine_fixture.hpp"
#include <QFile>
#include <QtTest>
#include <chrono>
#include <future>
namespace {
using namespace biglybtc;
std::string base32(const core::TorrentHash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string result;unsigned bits=0,buffer=0;for(auto byte:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;result+=chars[(buffer>>bits)&31];}}if(bits){result+=chars[(buffer<<(5-bits))&31];}return result;}
QByteArray read(const QString& path){QFile file(path);test::require(file.open(QIODevice::ReadOnly),"Generated source read");return file.readAll();}
}
class NamedTagSourceTests final:public QObject {
 Q_OBJECT
private slots:
 void actualNormalSource_data(){QTest::addColumn<int>("scenario");QTest::addColumn<bool>("changed");
  QTest::newRow("actual-count-and-age")<<0<<true;
  QTest::newRow("actual-empty-count")<<1<<true;
  QTest::newRow("actual-absent-age")<<2<<true;
  QTest::newRow("category-alias")<<3<<false;
  QTest::newRow("internal-alias")<<4<<false;
  QTest::newRow("unknown-locale-census")<<5<<false;
  QTest::newRow("orphan-count")<<6<<false;
  QTest::newRow("orphan-does-not-hide-subject-age")<<7<<true;
  QTest::newRow("unobserved-original-rank")<<8<<false;
  QTest::newRow("missing-internal-service")<<9<<false;
  QTest::newRow("misaligned-time-properties")<<10<<false;
  QTest::newRow("duplicate-manual-name")<<11<<false;
 }
 void actualNormalSource(){QFETCH(int,scenario);QFETCH(bool,changed);
  test::OwnedEngineFixture fixture(false);libtorrent::entry config(libtorrent::entry::dictionary_type{});config["locale"]=scenario==5?"zz_ZZ":"en_GB";config["tagmanager.enable"]=1;test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));
  libtorrent::entry definitions(libtorrent::entry::dictionary_type{});auto& rule=definitions["3"]["1"];rule["n"]="Generated consumer";rule["o"]=libtorrent::entry::list_type{};
  std::string expression="countTag(\"Generated metrics\")==1&&hasTagAge(\"Generated metrics\")>=1";
  if(scenario==1){expression="countTag(\"Generated metrics\")==0";}if(scenario==2){expression="hasTagAge(\"Generated metrics\")==-1";}if(scenario==6){expression="countTag(\"Generated metrics\")>=1";}if(scenario==7){expression="hasTagAge(\"Generated metrics\")>=1";}if(scenario==8){expression="tagPosition(\"Generated metrics\")>=0";}
  rule["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry(expression),libtorrent::entry("am=0;")};
  auto& source=definitions["3"]["2"];source["n"]="Generated metrics";source["o"]=libtorrent::entry::list_type{};source["p"]=libtorrent::entry::list_type{};
  const auto wall=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  if(scenario!=1&&scenario!=2){source["o"].list().emplace_back(base32(*fixture.hash));libtorrent::entry time(libtorrent::entry::dictionary_type{});time["a"]=wall-100;source["p"].list().push_back(time);}
  if(scenario==6||scenario==7){auto orphan=core::TorrentHash::parse(std::string(20,'\x73'));test::require(bool(orphan),"Generated orphan");source["o"].list().emplace_back(base32(*orphan));libtorrent::entry time(libtorrent::entry::dictionary_type{});time["a"]=wall-100;source["p"].list().push_back(time);}
  if(scenario==10){source["p"].list().clear();}if(scenario==11){auto& alias=definitions["3"]["3"];alias["n"]="Generated metrics";alias["o"]=libtorrent::entry::list_type{};}
  if(scenario==3){libtorrent::entry categories(libtorrent::entry::dictionary_type{}),category(libtorrent::entry::dictionary_type{});category["name"]="Generated metrics";categories["categories"]=libtorrent::entry::list_type{category};test::write(fixture.profile+"/categories.config",QByteArray::fromStdString(test::encoded(categories)));}
  test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(definitions)));const auto inventory=read(fixture.profile+"/downloads.config"),state=read(fixture.stateFile);
  auto result=std::async(std::launch::async,[&]{auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Actual named source profile");auto mode=storage::TagManagerStartupMode::capture(*lease);test::require(bool(mode),"Actual named startup mode");auto scope=app::InternalTagNamespace::activate(*lease,88,true,*mode);test::require(bool(scope),"Actual internal name namespace");if(scenario==4){auto alias=(*scope)->createTag(u"Generated metrics",true);test::require(bool(alias),"Actual internal alias");}
   auto metadata=(*scope)->snapshot();test::require(metadata&&*metadata,"Actual opaque internal name source");auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Actual named writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Actual named manual source");auto worker=app::TagConstraintWorker::activate(*lease,88,*mode);test::require(bool(worker),"Actual named normal worker");auto library=std::make_shared<ui::LibraryPresentation>();library->revision=7;ui::LibraryItem item;item.hash=QString::fromStdString(fixture.hash->hex());item.bytes=test::OwnedEngineFixture::PayloadBytes;library->items.push_back(item);app::ConstraintWorkerFrame frame;frame.session=88;frame.sequence=1;frame.sourceRevision=lease->sourceRevision();frame.library=library;if(scenario!=9){frame.internalNamespace=**metadata;}
   auto turn=(*worker)->step(**store,*snapshot,frame);if(!turn){throw std::runtime_error("Actual named worker error: "+turn.error().message);}if(turn->changed!=changed){throw std::runtime_error(std::string("Actual named source result differs: ")+(turn->kept?turn->kept->detail:"no detail"));}const auto current=turn->changed?turn->snapshot:*snapshot;const auto root=std::find_if(current.tags()->tags.begin(),current.tags()->tags.end(),[](const auto& tag){return tag.id==1;});test::require(root!=current.tags()->tags.end()&&root->uniqueHashes.size()==std::size_t(changed),"Actual named root membership differs");(*worker)->close();test::require(bool((*scope)->close())&&bool((*store)->close()),"Actual named source close");return true;
  });QVERIFY(result.get());QCOMPARE(read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(read(fixture.stateFile),state);for(const auto& file:fixture.files){QCOMPARE(read(fixture.payload+'/'+file.relative),QByteArray(file.expected.size(),'\0'));}
 }
};
QTEST_GUILESS_MAIN(NamedTagSourceTests)
#include "named_tag_source_tests.moc"

// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal_tag_namespace.hpp"
#include "tag_constraint_worker.hpp"
#include "owned_engine_fixture.hpp"
#include <QtTest>
#include <future>
namespace {using namespace biglybtc;
std::string base32(const core::TorrentHash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string result;unsigned bits=0,buffer=0;for(auto byte:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;result+=chars[(buffer>>bits)&31];}}if(bits){result+=chars[(buffer<<(5-bits))&31];}return result;}}
class NegativeTagSourceTests final:public QObject {
 Q_OBJECT
private slots:
 void actualNormalSource_data(){QTest::addColumn<int>("scenario");QTest::addColumn<bool>("before");QTest::addColumn<bool>("after");
  QTest::newRow("actual-negative-add")<<0<<false<<true;
  QTest::newRow("actual-negative-remove")<<1<<true<<false;
  QTest::newRow("positive-witness-prevents-negative")<<2<<false<<false;
  QTest::newRow("missing-internal-service")<<3<<true<<true;
  QTest::newRow("unknown-stock-locale")<<4<<true<<true;
  QTest::newRow("unknown-stored-family")<<5<<true<<true;
  QTest::newRow("possible-runtime-name-override")<<6<<true<<true;
  QTest::newRow("unknown-same-name-internal-membership")<<7<<true<<true;
  QTest::newRow("unqualified-membership-property")<<8<<true<<true;
  QTest::newRow("unreadable-peer-domain")<<9<<true<<true;
  QTest::newRow("duplicate-absent-manual-name")<<10<<false<<true;
  QTest::newRow("unreadable-nondownload-swarm-name")<<11<<false<<true;
  QTest::newRow("unknown-name-of-known-nonmember")<<12<<false<<true;
  QTest::newRow("unregistered-constructor-name")<<13<<true<<true;
  QTest::newRow("disabled-startup")<<14<<true<<true;
  QTest::newRow("uncompiled-rule-current-membership")<<15<<true<<false;
  QTest::newRow("unproved-full32-member-alias")<<16<<true<<true;
 }
 void actualNormalSource(){QFETCH(int,scenario);QFETCH(bool,before);QFETCH(bool,after);test::OwnedEngineFixture fixture(false);lt::entry config(lt::entry::dictionary_type{});config["locale"]=scenario==4?"zz_ZZ":"en_GB";config["tagmanager.enable"]=scenario==14?0:1;if(scenario==9){config["speed.limit.handler.schedule.lines"]=lt::entry::list_type{lt::entry("unsupported generated schedule")};}test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));lt::entry tags(lt::entry::dictionary_type{});auto& root=tags["3"]["1"];root["n"]="Generated consumer";root["o"]=lt::entry::list_type{};if(before){root["o"].list().emplace_back(base32(*fixture.hash));}root["c"]["pp.constraint"]=lt::entry::list_type{lt::entry(before?"hasTag(\"Generated witness\")":"!hasTag(\"Generated witness\")"),lt::entry("am=0;")};auto& source=tags["3"]["2"];source["n"]=scenario==13?"Other name":"Generated witness";source["o"]=lt::entry::list_type{};if(scenario==2){source["o"].list().emplace_back(base32(*fixture.hash));}if(scenario==8){source["c"]["pp.generated.unknown"]=lt::entry::list_type{lt::entry("Generated unsupported property")};}if(scenario==15){source["c"]["pp.constraint"]=lt::entry::list_type{lt::entry("UnsupportedGeneratedFunction()")};}if(scenario==5){tags["8"]=lt::entry::dictionary_type{};}if(scenario==6){tags["2"]["0"]["n"]="Possible generated override";}if(scenario==10||scenario==12){tags["3"]["3"]["n"]=scenario==10?std::string("Generated witness"):std::string(1,'\xff');tags["3"]["3"]["o"]=lt::entry::list_type{};}if(scenario==11){tags["7"]["100"]["n"]="tag.unknown.generated.name";}if(scenario==16){std::string raw;for(auto byte:fixture.hash->bytes()){raw+=static_cast<char>(std::to_integer<unsigned char>(byte));}raw+=std::string(12,'\x31');auto alias=core::TorrentHash::parse(raw);test::require(bool(alias),"Generated full32 alias");source["o"].list().emplace_back(base32(*alias));}test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(tags)));const auto inventory=test::read(fixture.profile+"/downloads.config"),state=test::read(fixture.stateFile),raw=test::read(fixture.profile+"/tag.config");
  auto run=std::async(std::launch::async,[&]{auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Actual negative profile");auto mode=storage::TagManagerStartupMode::capture(*lease);test::require(bool(mode),"Actual negative startup");auto scope=app::InternalTagNamespace::activate(*lease,88,true,*mode);test::require(bool(scope),"Actual negative namespace");if(scenario==7){auto alias=(*scope)->createTag(u"Generated witness",true);test::require(bool(alias),"Actual internal alias");}auto metadata=(*scope)->snapshot();test::require(bool(metadata),"Actual negative namespace snapshot");auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Actual negative writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Actual negative source");auto worker=app::TagConstraintWorker::activate(*lease,88,*mode);test::require(bool(worker),"Actual negative normal worker");auto library=std::make_shared<ui::LibraryPresentation>();library->revision=7;ui::LibraryItem item;item.hash=QString::fromStdString(fixture.hash->hex());item.bytes=test::OwnedEngineFixture::PayloadBytes;library->items.push_back(item);app::ConstraintWorkerFrame frame;frame.session=88;frame.sequence=1;frame.sourceRevision=lease->sourceRevision();frame.library=library;if(scenario!=3&&*metadata){frame.internalNamespace=**metadata;}
   auto turn=(*worker)->step(**store,*snapshot,frame);if(!turn||turn->changed!=(before!=after)){throw std::runtime_error(std::string("Actual negative result differs: ")+(turn?(turn->kept?turn->kept->detail:"no detail"):turn.error().message));}const auto current=turn->changed?turn->snapshot:*snapshot;const auto root=std::find_if(current.tags()->tags.begin(),current.tags()->tags.end(),[](const auto& tag){return tag.id==1;});test::require(root!=current.tags()->tags.end()&&root->uniqueHashes.size()==std::size_t(after),"Actual negative membership differs");(*worker)->close();test::require(bool((*scope)->close())&&bool((*store)->close()),"Actual negative owners close");return true;
  });QVERIFY(run.get());QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.stateFile),state);if(before==after){QCOMPARE(test::read(fixture.profile+"/tag.config"),raw);}for(const auto& file:fixture.files){QCOMPARE(test::read(fixture.payload+'/'+file.relative),QByteArray(file.expected.size(),'\0'));}
 }
};
QTEST_GUILESS_MAIN(NegativeTagSourceTests)
#include "negative_tag_source_tests.moc"

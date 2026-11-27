// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal_tag_namespace.hpp"
#include "tag_constraint_worker.hpp"
#include "peer_set_tag_turn.hpp"
#include "owned_engine_fixture.hpp"
#include <QtTest>
#include <future>
namespace {using namespace biglybtc;
std::string base32(const core::TorrentHash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string result;unsigned bits=0,buffer=0;for(auto byte:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;result+=chars[(buffer>>bits)&31];}}if(bits){result+=chars[(buffer<<(5-bits))&31];}return result;}}
class TagGroupSourceTests final:public QObject {
 Q_OBJECT
private slots:
 void actualNormalSource_data(){QTest::addColumn<int>("scenario");QTest::addColumn<bool>("before");QTest::addColumn<bool>("after");
  QTest::newRow("known-null-group-of-present-member")<<0<<false<<true;
  QTest::newRow("actual-group-negative-remove")<<1<<true<<false;
  QTest::newRow("positive-manual-group")<<2<<false<<false;
  QTest::newRow("different-present-group")<<3<<false<<true;
  QTest::newRow("unknown-internal-group")<<4<<true<<true;
  QTest::newRow("unknown-locale")<<5<<true<<true;
  QTest::newRow("missing-internal-service")<<6<<true<<true;
  QTest::newRow("malformed-group-of-possible-member")<<7<<true<<true;
  QTest::newRow("unknown-membership-same-group")<<8<<true<<true;
  QTest::newRow("unknown-membership-other-group")<<9<<false<<true;
  QTest::newRow("peer-schedule-is-not-a-download-group-source")<<10<<false<<true;
  QTest::newRow("unknown-nondownload-swarm-group")<<11<<false<<true;
  QTest::newRow("unregistered-group-is-allowed")<<12<<false<<true;
  QTest::newRow("disabled-startup")<<13<<true<<true;
  QTest::newRow("future-family-keeps-membership")<<14<<true<<true;
  QTest::newRow("group-with-unknown-nonmember-name")<<15<<false<<true;
  QTest::newRow("actual-default-category-group")<<16<<false<<true;
  QTest::newRow("state-group-does-not-invent-membership")<<17<<true<<true;
  QTest::newRow("active-provider-group-needs-no-peer-query")<<18<<false<<true;
  QTest::newRow("unproved-full32-member-alias")<<19<<true<<true;
 }
 void actualNormalSource(){QFETCH(int,scenario);QFETCH(bool,before);QFETCH(bool,after);test::OwnedEngineFixture fixture(false);lt::entry config(lt::entry::dictionary_type{});config["locale"]=scenario==5?"zz_ZZ":"en_GB";config["tagmanager.enable"]=scenario==13?0:1;if(scenario==10){config["speed.limit.handler.schedule.lines"]=lt::entry::list_type{lt::entry("unsupported generated schedule")};}test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));lt::entry tags(lt::entry::dictionary_type{});auto& root=tags["3"]["1"];root["n"]="Generated consumer";root["o"]=lt::entry::list_type{};if(before){root["o"].list().emplace_back(base32(*fixture.hash));}std::string group=scenario==12?"Unregistered generated group":scenario==16?"Category":scenario==17?"Download State":"Generated group";const bool positive=scenario==16||scenario==17;const auto expression=std::string(before||positive?"hasTagGroup(\"":"!hasTagGroup(\"")+group+"\")";root["c"]["pp.constraint"]=lt::entry::list_type{lt::entry(expression),lt::entry("am=0;")};auto& source=tags["3"]["2"];source["n"]=scenario==15?std::string(1,'\xff'):std::string("Generated source");source["o"]=lt::entry::list_type{};if(scenario==0||scenario==2||scenario==3||scenario==7){source["o"].list().emplace_back(base32(*fixture.hash));}if(scenario==1||scenario==2||scenario==8||scenario==15){source["c"]["gr"]="Generated group";}if(scenario==3||scenario==9){source["c"]["gr"]="Different group";}if(scenario==7){source["c"]["gr"]=1;}if(scenario==8||scenario==9){source["c"]["pp.generated.unknown"]=lt::entry::list_type{lt::entry("Generated unsupported property")};}if(scenario==11){tags["7"]["100"]["n"]="Generated swarm";tags["7"]["100"]["c"]["gr"]=1;}if(scenario==14){tags["8"]=lt::entry::dictionary_type{};}if(scenario==19){source["c"]["gr"]="Generated group";std::string raw;for(auto byte:fixture.hash->bytes()){raw+=static_cast<char>(std::to_integer<unsigned char>(byte));}raw+=std::string(12,'\x31');auto alias=core::TorrentHash::parse(raw);test::require(bool(alias),"Generated full32 group alias");source["o"].list().emplace_back(base32(*alias));}test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(tags)));const auto raw=test::read(fixture.profile+"/tag.config"),inventory=test::read(fixture.profile+"/downloads.config"),state=test::read(fixture.stateFile);
  auto run=std::async(std::launch::async,[&]{auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Actual group profile");auto mode=storage::TagManagerStartupMode::capture(*lease);test::require(bool(mode),"Actual group startup mode");auto scope=app::InternalTagNamespace::activate(*lease,88,true,*mode);test::require(bool(scope),"Actual group namespace");if(scenario==4){auto tag=(*scope)->createTag(u"Generated internal",true);test::require(bool(tag),"Actual unknown internal group");}auto metadata=(*scope)->snapshot();test::require(bool(metadata),"Actual group namespace snapshot");auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Actual group writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Actual group source");auto worker=app::TagConstraintWorker::activate(*lease,88,*mode);test::require(bool(worker),"Actual group worker");auto library=std::make_shared<ui::LibraryPresentation>();library->revision=7;ui::LibraryItem item;item.hash=QString::fromStdString(fixture.hash->hex());item.bytes=test::OwnedEngineFixture::PayloadBytes;if(scenario==18){item.providerAdmission=11;item.providerGeneration=88;}library->items.push_back(item);app::ConstraintWorkerFrame frame;frame.session=88;frame.sequence=1;frame.library=library;frame.sourceRevision=lease->sourceRevision();if(scenario!=6&&*metadata){frame.internalNamespace=**metadata;}if(scenario==18){engine::ManagedDownloadStatus managed{*fixture.hash};managed.admission=11;managed.retained=managed.checked=true;managed.phase=engine::ManagedPhase::Seeding;managed.bytes=managed.completed=test::OwnedEngineFixture::PayloadBytes;managed.complete=true;frame.managed={managed};}
   // With no peer provider,any accidental peer demand would remain deferred.
   auto turn=app::runConstraintPeerTurn(**worker,**store,*snapshot,frame,*lease,nullptr);if(!turn||turn->changed!=(before!=after)||turn->peerRequest){throw std::runtime_error(std::string("Actual group result differs: ")+(turn?(turn->kept?turn->kept->detail:"no detail"):turn.error().message));}const auto current=turn->changed?turn->snapshot:*snapshot;const auto root=std::find_if(current.tags()->tags.begin(),current.tags()->tags.end(),[](const auto& tag){return tag.id==1;});test::require(root!=current.tags()->tags.end()&&root->uniqueHashes.size()==std::size_t(after),"Actual group membership differs");(*worker)->close();test::require(bool((*scope)->close())&&bool((*store)->close()),"Actual group owners close");return true;
  });QVERIFY(run.get());QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.stateFile),state);if(before==after){QCOMPARE(test::read(fixture.profile+"/tag.config"),raw);}for(const auto& file:fixture.files){QCOMPARE(test::read(fixture.payload+'/'+file.relative),QByteArray(file.expected.size(),'\0'));}
 }
 void actualTagNames_data(){
  QTest::addColumn<QString>("expression");
  QTest::addColumn<bool>("before");
  QTest::addColumn<bool>("hasOperationReceipt");
  QTest::addColumn<bool>("after");
  QTest::newRow("known-name-adds-with-incomplete-roster")<<QString("contains(tag_names,\"Generated source\")")<<false<<false<<true;
  QTest::newRow("own-name-is-blank")<<QString("contains(tagnames,\"Generated rule\")")<<true<<true<<false;
  QTest::newRow("incomplete-roster-keeps")<<QString("contains(tagnames,\"Generated rule\")")<<true<<false<<true;
 }
 void actualTagNames(){
  QFETCH(QString,expression);
  QFETCH(bool,before);
  QFETCH(bool,hasOperationReceipt);
  QFETCH(bool,after);
  test::OwnedEngineFixture fixture(false);
  lt::entry config(lt::entry::dictionary_type{});
  config["locale"]="en_GB";
  config["tagmanager.enable"]=1;
  test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));
  lt::entry tags(lt::entry::dictionary_type{});
  auto& rule=tags["3"]["1"];
  rule["n"]="Generated rule";
  rule["o"]=lt::entry::list_type{};
  if(before){rule["o"].list().emplace_back(base32(*fixture.hash));}
  rule["c"]["pp.constraint"]=lt::entry::list_type{lt::entry(expression.toStdString()),lt::entry("am=0;")};
  auto& source=tags["3"]["2"];
  source["n"]="Generated source";
  source["o"]=lt::entry::list_type{lt::entry(base32(*fixture.hash))};
  test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(tags)));

  auto run=std::async(std::launch::async,[&]{
   auto lease=storage::ProfileLease::acquire(fixture.native());
   test::require(bool(lease),"Tag names own profile");
   auto mode=storage::TagManagerStartupMode::capture(*lease);
   test::require(bool(mode),"Tag names startup mode");
   auto scope=app::InternalTagNamespace::activate(*lease,88,true,*mode);
   test::require(bool(scope),"Tag names own namespace");
   auto metadata=(*scope)->snapshot();
   test::require(metadata&&*metadata,"Tag names current namespace");
   auto store=storage::ManualTagStore::open(*lease);
   test::require(bool(store),"Tag names own writer");
   auto snapshot=(*store)->snapshot();
   test::require(bool(snapshot),"Tag names current source");
   auto worker=app::TagConstraintWorker::activate(*lease,88,*mode);
   test::require(bool(worker),"Tag names normal worker");

   auto library=std::make_shared<ui::LibraryPresentation>();
   library->revision=7;
   ui::LibraryItem item;
   item.hash=QString::fromStdString(fixture.hash->hex());
   item.bytes=test::OwnedEngineFixture::PayloadBytes;
   item.providerAdmission=11;
   item.providerGeneration=88;
   library->items.push_back(item);
   engine::ManagedDownloadStatus managed{*fixture.hash};
   managed.admission=11;
   managed.retained=managed.checked=managed.complete=managed.selectedComplete=true;
   managed.phase=engine::ManagedPhase::Stopped;
   managed.bytes=managed.completed=test::OwnedEngineFixture::PayloadBytes;
   app::ConstraintWorkerFrame frame;
   frame.session=88;
   frame.sequence=1;
   frame.library=library;
   frame.sourceRevision=lease->sourceRevision();
   frame.internalNamespace=**metadata;
   frame.managed={managed};
   frame.peerEngineIdentity=engine::AccountingIdentity{1,88,1};
   frame.retiredRecheckTags.insert(item.hash);
   frame.retiredMoveTagSubjects.insert(item.hash);
   if(hasOperationReceipt){frame.retiredDeletingTagSubjects.insert(item.hash);}

   auto result=app::runConstraintPeerTurn(**worker,**store,*snapshot,frame,*lease,nullptr);
   if(!result||result->changed!=(before!=after)||result->peerRequest){
    throw std::runtime_error(std::string("Tag names worker result differs: ")+
      (result?(result->kept?result->kept->detail:"no detail"):result.error().message));
   }
   const auto current=result->changed?result->snapshot:*snapshot;
   const auto found=std::ranges::find(current.tags()->tags,std::optional<std::int32_t>{1},&core::SavedDownloadTag::id);
   test::require(found!=current.tags()->tags.end()&&found->uniqueHashes.size()==std::size_t(after),
                "Tag names saved membership differs");
   (*worker)->close();
   test::require(bool((*scope)->close())&&bool((*store)->close()),"Tag names owners close");
   return true;
  });
  QVERIFY(run.get());
 }
};
QTEST_GUILESS_MAIN(TagGroupSourceTests)
#include "tag_group_source_tests.moc"

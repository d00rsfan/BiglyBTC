// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal_tag_namespace.hpp"
#include "tag_constraint_worker.hpp"
#include "owned_engine_fixture.hpp"
#include "original_identity_digest.hpp"
#include <QFile>
#include <QtTest>
#include <chrono>
#include <future>
#include <zlib.h>
namespace {
using namespace biglybtc;
std::string base32(const core::TorrentHash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string result;unsigned bits=0,buffer=0;for(auto byte:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;result+=chars[(buffer>>bits)&31];}}if(bits){result+=chars[(buffer<<(5-bits))&31];}return result;}
QByteArray read(const QString& path){QFile file(path);test::require(file.open(QIODevice::ReadOnly),"Generated source read");return file.readAll();}
QByteArray gzip(const std::string& input) {
  z_stream stream{};
  test::require(deflateInit2(&stream, 3, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) == Z_OK,
                "Initialize generated cache gzip");
  QByteArray output(qsizetype(input.size() + 1024), Qt::Uninitialized);
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out = reinterpret_cast<Bytef*>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());
  const auto status = deflate(&stream, Z_FINISH);
  output.resize(qsizetype(stream.total_out));
  deflateEnd(&stream);
  test::require(status == Z_STREAM_END, "Finish generated cache gzip");
  return output;
}
void setCategory(core::Document& state, std::string_view category) {
  const auto* attributes = state.find("attributes");
  test::require(attributes && attributes->type == core::Node::Type::Dictionary, "Actual attributes section");
  auto section = core::Document::parse(state.originalBytes().substr(attributes->begin, attributes->end-attributes->begin));
  test::require(section && section->replaceValue("category", std::to_string(category.size())+":"+std::string(category))
                 && state.replaceValue("attributes", section->serialize()), "Write generated category");
}
struct ExtraCategoryRow {
  core::TorrentHash hash;
  QString path;
  QByteArray bytes;
};
ExtraCategoryRow extraCategoryRow(const test::OwnedEngineFixture& fixture, std::string_view category) {
  auto state = core::Document::parse(read(fixture.stateFile).toStdString());
  test::require(bool(state), "Generated primary state");
  const auto* info = state->find("info");
  test::require(info && info->type == core::Node::Type::Dictionary, "Generated primary info");
  auto extraInfo = core::Document::parse(state->originalBytes().substr(info->begin, info->end-info->begin));
  test::require(extraInfo && extraInfo->replaceValue("name", "10:second.bin")
      && state->replaceValue("info", extraInfo->serialize()), "Assign distinct generated identity");
  setCategory(*state, category);
  const auto output = state->serialize();
  const auto extra = core::Document::parse(output);
  test::require(bool(extra), "Parse generated second torrent state");
  const auto identity = storage::digestOriginalTorrentIdentity(*extra);
  test::require(identity && identity->originalKey != *fixture.hash, "Independent original identity");

  const auto inventoryPath = fixture.profile + "/downloads.config";
  auto inventory = core::Document::parse(read(inventoryPath).toStdString());
  test::require(bool(inventory), "Actual generated inventory");
  const auto* rows = inventory->find("downloads");
  test::require(rows && rows->children.size() == 1, "Exact primary inventory row");
  const auto& first = rows->children.front();
  const auto raw = inventory->originalBytes().substr(first.begin, first.end-first.begin);
  auto saved = core::Document::parse(raw);
  const auto bytes = identity->originalKey.bytes();
  const std::string hash(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  test::require(saved && saved->replaceValue("torrent_hash", "20:"+hash)
      && saved->replaceValue("save_file", "10:second.bin")
      && inventory->replaceValue("downloads", "l"+raw+saved->serialize()+"e"), "Append the second saved download");
  test::write(inventoryPath, QByteArray::fromStdString(inventory->serialize()));
  return {identity->originalKey, fixture.profile+"/active/"+QString::fromStdString(identity->originalKey.hex())+".dat",
      QByteArray::fromStdString(output)};
}

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
  QTest::newRow("first-localized-category-absent-age")<<12<<true;
  QTest::newRow("first-localized-category-present-age")<<13<<true;
  QTest::newRow("custom-category-empty-count")<<14<<true;
  QTest::newRow("all-category-count-on-complete-library")<<15<<true;
  QTest::newRow("all-category-count-with-extra-pending-identity")<<16<<false;
  QTest::newRow("cache-category-overrides-primary")<<17<<true;
  QTest::newRow("cache-empty-attributes-remove-primary-category")<<18<<true;
  QTest::newRow("cache-without-attributes-uses-primary")<<19<<true;
  QTest::newRow("duplicate-cache-row-last-wins")<<20<<true;
  QTest::newRow("malformed-cache-keeps-current-membership")<<21<<false;
  QTest::newRow("late-cache-change-refuses-membership-cas")<<22<<false;
  QTest::newRow("user-category-count-includes-other-owner")<<23<<true;
  QTest::newRow("uncategorised-count-uses-effective-assignment")<<24<<true;
  QTest::newRow("missing-other-state-keeps-category-count-rule")<<25<<false;
  QTest::newRow("late-other-category-change-refuses-cas")<<26<<false;
 }
 void actualNormalSource(){QFETCH(int,scenario);QFETCH(bool,changed);
  test::OwnedEngineFixture fixture(false);libtorrent::entry config(libtorrent::entry::dictionary_type{});config["locale"]=scenario==5?"zz_ZZ":"en_GB";config["tagmanager.enable"]=1;test::write(fixture.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));
  libtorrent::entry definitions(libtorrent::entry::dictionary_type{});auto& rule=definitions["3"]["1"];rule["n"]="Generated consumer";rule["o"]=libtorrent::entry::list_type{};
  std::string expression="countTag(\"Generated metrics\")==1&&hasTagAge(\"Generated metrics\")>=1";
  if(scenario==1){expression="countTag(\"Generated metrics\")==0";}if(scenario==2){expression="hasTagAge(\"Generated metrics\")==-1";}if(scenario==6){expression="countTag(\"Generated metrics\")>=1";}if(scenario==7){expression="hasTagAge(\"Generated metrics\")>=1";}if(scenario==8){expression="tagPosition(\"Generated metrics\")>=0";}
  if (scenario == 12) { expression = "hasTagAge(\"All\")==-1"; }
  if (scenario == 13) { expression = "hasTagAge(\"All\")==0"; }
  if (scenario == 14) { expression = "countTag(\"All\")==0"; }
  if (scenario == 15 || scenario == 16) { expression = "countTag(\"All\")==1"; }
  if (scenario == 17 || scenario == 20 || scenario == 22) { expression = "hasTag(\"Cache Beta\")&&!hasTag(\"Cache Alpha\")"; }
  if (scenario == 18) { expression = "hasTag(\"Uncategorised\")&&!hasTag(\"Cache Alpha\")"; }
  if (scenario == 19) { expression = "hasTag(\"Cache Alpha\")"; }
  if (scenario == 23 || scenario == 25 || scenario == 26) { expression = "countTag(\"Cache Beta\")==2"; }
  if (scenario == 24) { expression = "countTag(\"Uncategorised\")==1&&countTag(\"Cache Alpha\")==1"; }
  if (scenario == 21) { expression = "!hasTag(\"Cache Alpha\")"; }
  if (scenario == 21 || scenario == 25) { rule["o"].list().emplace_back(base32(*fixture.hash)); }
  rule["c"]["pp.constraint"]=libtorrent::entry::list_type{libtorrent::entry(expression),libtorrent::entry("am=0;")};
  auto& source=definitions["3"]["2"];source["n"]="Generated metrics";source["o"]=libtorrent::entry::list_type{};source["p"]=libtorrent::entry::list_type{};
  const auto wall=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  if(scenario!=1&&scenario!=2){source["o"].list().emplace_back(base32(*fixture.hash));libtorrent::entry time(libtorrent::entry::dictionary_type{});time["a"]=wall-100;source["p"].list().push_back(time);}
  if(scenario==6||scenario==7){auto orphan=core::TorrentHash::parse(std::string(20,'\x73'));test::require(bool(orphan),"Generated orphan");source["o"].list().emplace_back(base32(*orphan));libtorrent::entry time(libtorrent::entry::dictionary_type{});time["a"]=wall-100;source["p"].list().push_back(time);}
  if(scenario==10){source["p"].list().clear();}if(scenario==11){auto& alias=definitions["3"]["3"];alias["n"]="Generated metrics";alias["o"]=libtorrent::entry::list_type{};}
  if(scenario==3){libtorrent::entry categories(libtorrent::entry::dictionary_type{}),category(libtorrent::entry::dictionary_type{});category["name"]="Generated metrics";categories["categories"]=libtorrent::entry::list_type{category};test::write(fixture.profile+"/categories.config",QByteArray::fromStdString(test::encoded(categories)));}
  if (scenario >= 12 && scenario <= 14) {
    libtorrent::entry categories(libtorrent::entry::dictionary_type{}), category(libtorrent::entry::dictionary_type{});
    category["name"] = "All";
    categories["categories"] = libtorrent::entry::list_type{category};
    test::write(fixture.profile+"/categories.config", QByteArray::fromStdString(test::encoded(categories)));
  }
  if (scenario == 13) {
    auto primary = core::Document::parse(read(fixture.stateFile).toStdString());
    test::require(bool(primary), "Read generated category assignment source");
    const auto* attributes = primary->find("attributes");
    test::require(attributes && attributes->type == core::Node::Type::Dictionary, "Original attributes dictionary");
    auto section = core::Document::parse(primary->originalBytes().substr(attributes->begin, attributes->end-attributes->begin));
    test::require(section && section->replaceValue("category", "3:All")
                  && primary->replaceValue("attributes", section->serialize()), "Save exact category assignment");
    test::write(fixture.stateFile, QByteArray::fromStdString(primary->serialize()));
  }
  const auto cachePath = fixture.profile + "/active/cache.dat";
  QByteArray cacheBytes, lateCacheBytes;
  if (scenario >= 17) {
    libtorrent::entry categories(libtorrent::entry::dictionary_type{});
    categories["categories"] = libtorrent::entry::list_type{};
    for (const auto* name : {"Cache Alpha", "Cache Beta"}) {
      libtorrent::entry value(libtorrent::entry::dictionary_type{});
      value["name"] = name;
      categories["categories"].list().push_back(std::move(value));
    }
    test::write(fixture.profile+"/categories.config", QByteArray::fromStdString(test::encoded(categories)));

    auto primary = core::Document::parse(read(fixture.stateFile).toStdString());
    test::require(bool(primary), "Read generated primary state");
    setCategory(*primary, "Cache Alpha");
    test::write(fixture.stateFile, QByteArray::fromStdString(primary->serialize()));

    libtorrent::entry cache(libtorrent::entry::dictionary_type{});
    cache["state"] = libtorrent::entry::list_type{};
    const auto addRow = [&](const char* name) {
      libtorrent::entry row(libtorrent::entry::dictionary_type{});
      const auto hash = fixture.hash->bytes();
      row["hash"] = std::string(reinterpret_cast<const char*>(hash.data()), hash.size());
      if (scenario != 19) { row["attributes"] = libtorrent::entry::dictionary_type{}; }
      if (name) { row["attributes"]["category"] = name; }
      cache["state"].list().push_back(std::move(row));
    };
    if (scenario == 20) { addRow("Cache Alpha"); }
    if (scenario == 17 || scenario == 20 || scenario == 22 || scenario == 23 || scenario == 25 || scenario == 26) {
      addRow("Cache Beta");
    }
    else { addRow(nullptr); }
    cacheBytes = scenario == 21 ? QByteArray("malformed cache") : gzip(test::encoded(cache));
    test::write(cachePath, cacheBytes);
    if (scenario == 22) {
      cache["state"].list().clear();
      addRow("Cache Alpha");
      lateCacheBytes = gzip(test::encoded(cache));
    }
  }
  std::optional<ExtraCategoryRow> extra;
  QByteArray lateCategoryBytes;
  if (scenario >= 23) {
    extra = extraCategoryRow(fixture, scenario == 24 ? "Cache Alpha" : "Cache Beta");
    if (scenario != 25) { test::write(extra->path, extra->bytes); }
    if (scenario == 26) {
      auto late = core::Document::parse(extra->bytes.toStdString());
      test::require(bool(late), "Parse secondary state for late change");
      setCategory(*late, "Cache Alpha");
      lateCategoryBytes = QByteArray::fromStdString(late->serialize());
    }
  }
  test::write(fixture.profile+"/tag.config",QByteArray::fromStdString(test::encoded(definitions)));const auto inventory=read(fixture.profile+"/downloads.config"),state=read(fixture.stateFile);
  auto result=std::async(std::launch::async,[&]{auto lease=storage::ProfileLease::acquire(fixture.native());test::require(bool(lease),"Actual named source profile");auto mode=storage::TagManagerStartupMode::capture(*lease);test::require(bool(mode),"Actual named startup mode");auto scope=app::InternalTagNamespace::activate(*lease,88,true,*mode);test::require(bool(scope),"Actual internal name namespace");if(scenario==4){auto alias=(*scope)->createTag(u"Generated metrics",true);test::require(bool(alias),"Actual internal alias");}
   auto metadata=(*scope)->snapshot();test::require(metadata&&*metadata,"Actual opaque internal name source");auto store=storage::ManualTagStore::open(*lease);test::require(bool(store),"Actual named writer");auto snapshot=(*store)->snapshot();test::require(bool(snapshot),"Actual named manual source");auto worker=app::TagConstraintWorker::activate(*lease,88,*mode);test::require(bool(worker),"Actual named normal worker");auto library=std::make_shared<ui::LibraryPresentation>();library->revision=7;ui::LibraryItem item;item.hash=QString::fromStdString(fixture.hash->hex());item.bytes=test::OwnedEngineFixture::PayloadBytes;library->items.push_back(item);app::ConstraintWorkerFrame frame;frame.session=88;frame.sequence=1;frame.sourceRevision=lease->sourceRevision();frame.library=library;if(scenario!=9){frame.internalNamespace=**metadata;}
   if (scenario == 16) {
    ui::LibraryItem pending;
    pending.hash = QString(40, QLatin1Char('A'));
    library->items.push_back(std::move(pending));
   }
   if (extra) {
    ui::LibraryItem item;
    item.hash = QString::fromStdString(extra->hash.hex());
    item.bytes = test::OwnedEngineFixture::PayloadBytes;
    library->items.push_back(std::move(item));
   }
   bool lateChange = false;
   if (scenario == 22 || scenario == 26) {
    frame.publishTagChange = [&](const app::ConstraintWorkerFrame::TagWrite& publish) {
      lateChange = true;
      if (scenario == 22) { test::write(cachePath, lateCacheBytes); }
      else { test::write(extra->path, lateCategoryBytes); }
      return publish();
    };
   }
   auto turn = (*worker)->step(**store, *snapshot, frame);
   if (scenario == 22 || scenario == 26) {
    test::require(lateChange && !turn && turn.error().code == storage::ErrorCode::Conflict,
                  "Changed category source crossed the membership CAS");
    const auto current = (*store)->snapshot();
    test::require(current && current->sameSource(*snapshot), "Stale cached category rewrote the tag file");
   } else {
    if (!turn) { throw std::runtime_error("Actual named worker error: " + turn.error().message); }
    if (turn->changed != changed) {
      throw std::runtime_error(std::string("Actual named source result differs: ")
                               + (turn->kept ? turn->kept->detail : "no detail"));
    }
    const auto& current = turn->changed ? turn->snapshot : *snapshot;
    const auto& rows = current.tags()->tags;
    const auto root = std::ranges::find_if(rows, [](const auto& tag) { return tag.id == 1; });
    test::require(root != rows.end() && root->uniqueHashes.size() == std::size_t(changed || scenario == 21 || scenario == 25),
                  "Actual named root membership differs");
   }
   (*worker)->close();
   test::require(bool((*scope)->close()) && bool((*store)->close()), "Actual named source close");
   return true;
  });
  QVERIFY(result.get());
  QCOMPARE(read(fixture.profile+"/downloads.config"), inventory);
  QCOMPARE(read(fixture.stateFile), state);
  if (scenario >= 17) { QCOMPARE(read(cachePath), scenario == 22 ? lateCacheBytes : cacheBytes); }
  if (extra && scenario != 25) { QCOMPARE(read(extra->path), scenario == 26 ? lateCategoryBytes : extra->bytes); }
  for (const auto& file : fixture.files) {
   QCOMPARE(read(fixture.payload+'/'+file.relative), QByteArray(file.expected.size(), '\0'));
  }
 }
};
QTEST_GUILESS_MAIN(NamedTagSourceTests)
#include "named_tag_source_tests.moc"

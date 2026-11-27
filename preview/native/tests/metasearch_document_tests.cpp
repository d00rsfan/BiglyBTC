// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_document.hpp"
#include "metasearch_value_fixture.hpp"
#include "metasearch_original_values.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
namespace s=biglybtc::search;namespace f=metasearch_value_fixture;
class MetasearchDocumentTests final:public QObject {
 Q_OBJECT
private slots:
 void actualOriginalValues_data(){
  QTest::addColumn<QJsonObject>("observation");const auto path=qgetenv("BIGLYBTC_METASEARCH_ORIGINAL_VALUES");QByteArray bytes;if(path.isEmpty()){bytes=QByteArray(MetasearchOriginalValues,static_cast<qsizetype>(sizeof(MetasearchOriginalValues)-1));}else{QFile file(QString::fromUtf8(path));QVERIFY(file.open(QIODevice::ReadOnly)&&file.size()<=1024*1024);bytes=file.readAll();}QCOMPARE(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),QByteArray("d8bbf52fbc2ddfc308fc6c0bace8c0164ae7ef896d1b0f97a7b316abda913ac2"));auto document=QJsonDocument::fromJson(bytes);QVERIFY(document.isObject());const auto rows=document.object().value("cases").toArray();QCOMPARE(rows.size(),qsizetype{18});for(const auto& row:rows){const auto object=row.toObject();const auto label=object.value("label").toString().toUtf8();QTest::newRow(label.constData())<<object;}
 }
 void actualOriginalValues(){
  QFETCH(QJsonObject,observation);if(observation.value("kind")=="query"){auto actual=s::expandMetasearchQuery(observation.value("template").toString().toStdString(),observation.value("query").toString().toUtf8().toStdString());QVERIFY(actual);QCOMPARE(*actual,observation.value("actual_GeneralUtils_replaceAll").toString().toStdString());return;}
  if(observation.value("native_unsupported_observation_only").toBool()){const bool expression=observation.value("label").toString().startsWith("mapping-expression");s::MetasearchRecipe recipe;recipe.kind=s::MetasearchRecipeKind::Json;recipe.mappings={{expression?"${a}":"a",1,expression?"":"never-matches"}};auto value=s::parseMetasearchDocument(recipe,observation.value("page").toString().toUtf8(),{});QVERIFY(value);const auto expected=observation.value("actual_rows").toArray();QCOMPARE(value->rows.size(),static_cast<std::size_t>(expected.size()));if(expression){QCOMPARE(value->rows[0].title,std::optional{expected[0].toObject().value("name").toString().toStdString()});}return;}
  s::MetasearchRecipe recipe;recipe.kind=s::MetasearchRecipeKind::Json;recipe.resultPath=observation.value("path").toString().toStdString();recipe.mappings={{"a",1},{"b",1},{"size",3},{"seeds",5},{"peers",4},{"date",2},{"download",102},{"hash",200}};
  auto actual=s::parseMetasearchDocument(recipe,observation.value("page").toString().toUtf8(),{observation.value("actual_root_page").toString(),observation.value("actual_base_page").toString()});if(observation.contains("actual_error")){QVERIFY(!actual);return;}QVERIFY(actual);const auto expected=observation.value("actual_rows").toArray();QCOMPARE(actual->rows.size(),static_cast<std::size_t>(expected.size()));
  for(qsizetype n=0;n<expected.size();++n){const auto reference=expected[n].toObject();const auto& row=actual->rows[static_cast<std::size_t>(n)];QCOMPARE(row.title.value_or(""),reference.value("name").toString().toUtf8().toStdString());QCOMPARE(row.size,reference.value("size").toInteger());QCOMPARE(row.seeds,reference.value("seeds").toInt());QCOMPARE(row.peers,reference.value("peers").toInt());QCOMPARE(row.hash.value_or(""),reference.value("hash").toString().toStdString());QCOMPARE(row.rawTorrentLink.value_or(""),reference.value("torrent_link_raw").toString().toStdString());QVERIFY(!row.uid&&reference.value("uid").isNull());}
 }
 void exactLongTextAndAmbiguousDecimalHaveDifferentOutcomes(){
  s::MetasearchRecipe recipe;recipe.kind=s::MetasearchRecipeKind::Json;recipe.mappings={{"title",1},{"seeds",5}};auto exact=s::parseMetasearchDocument(recipe,"[{\"title\":9007199254740993,\"seeds\":2147483648}]",{});QVERIFY(exact&&exact->rows.size()==1);QCOMPARE(exact->rows[0].title,std::optional{std::string("9007199254740993")});QCOMPARE(exact->rows[0].seeds,std::int32_t{-1});auto decimal=s::parseMetasearchDocument(recipe,"[{\"title\":1e0,\"seeds\":3.0}]",{});QVERIFY(decimal&&decimal->partial&&decimal->rows[0].incomplete);QCOMPARE(decimal->rows[0].seeds,std::int32_t{-1});
 }
 void mappedCategoriesKeepOriginalPrimaryAndOrderedUniqueTags() {
  s::MetasearchRecipe recipe;
  recipe.kind=s::MetasearchRecipeKind::Json;
  recipe.mappings={{"title",1},{"category",6}};
  const auto parsed=s::parseMetasearchDocument(recipe,R"([
    {"title":"First","category":["<b>Music</b>",{"name":"Live &amp; local"},"Music",4,{"label":"ignored"}]},
    {"title":"Second","category":" &lt;Other&gt; "},
    {"title":"Third","category":[]}
  ])",{});
  QVERIFY(parsed&&parsed->rows.size()==3);
  QCOMPARE(parsed->rows[0].category,std::optional<std::string>{"Music"});
  QCOMPARE(parsed->rows[0].categoryTags,(std::vector<std::string>{"Music","Live & local"}));
  QCOMPARE(parsed->rows[1].category,std::optional<std::string>{"<Other>"});
  QCOMPARE(parsed->rows[2].category,std::optional<std::string>{"[]"});
  QVERIFY(std::ranges::none_of(parsed->rows,[](const auto& row){return row.incomplete;}));

  recipe.kind=s::MetasearchRecipeKind::Regex;
  recipe.resultPattern="<r>(.*?)</r><category>(.*?)</category>";
  recipe.mappings={{"1",1},{"2",6}};
  const auto regex=s::parseMetasearchDocument(recipe,"<r>A</r><category><b>Books</b> &amp; magazines</category>",{});
  QVERIFY(regex&&regex->rows.size()==1);
  QCOMPARE(regex->rows[0].category,std::optional<std::string>{"Books & magazines"});
  QCOMPARE(regex->rows[0].categoryTags,(std::vector<std::string>{"Books & magazines"}));
 }
 void originalRankUsesSuperSeedsVotesAndPrivateResult() {
  s::MetasearchRecipe recipe;
  recipe.kind=s::MetasearchRecipeKind::Json;
  recipe.mappings={{"title",1},{"seeds",5},{"peers",4},{"comments",7},{"votes",10},{"superseeds",11},{"private",12},{"down",14}};
  const auto parsed=s::parseMetasearchDocument(recipe,R"([{"title":"Name","seeds":100,"peers":49,
    "comments":"<b>1,234</b>","votes":60,"superseeds":2,"private":"yes","down":1}])",{},"Name absent");
  QVERIFY(parsed&&parsed->rows.size()==1);
  const auto& row=parsed->rows.front();
  QVERIFY(!row.incomplete&&row.privateTorrent&&row.rank);
  QCOMPARE(row.comments,1234);
  QCOMPARE(row.superSeeds,2);
  QCOMPARE(row.votes,60);
  QCOMPARE(row.votesDown,1);
  QVERIFY(std::abs(*row.rank-0.13499188f)<0.000001f);

  recipe.kind=s::MetasearchRecipeKind::Regex;
  recipe.resultPattern="<r>(.*?)</r><v>(.*?)</v>";
  recipe.rankBias=0.5f;
  recipe.mappings={{"1",1},{"2",10}};
  const auto regex=s::parseMetasearchDocument(recipe,"<r>Name</r><v>6</v>",{},"Name");
  QVERIFY(regex&&regex->rows.size()==1&&regex->rows.front().rank);
  QVERIFY(std::abs(*regex->rows.front().rank-0.150515f)<0.000001f);
 }
 void mappedJsonRankUsesSavedBiasAndResponseDivisor(){
  s::MetasearchRecipe recipe;
  recipe.kind=s::MetasearchRecipeKind::Json;
  recipe.resultPath="items";
  recipe.rankDivisorPath="configuration.maximum";
  recipe.rankBias=0.5f;
  recipe.rankPreference=10.0f;
  recipe.mappings={{"title",1},{"score",201},{"fallback",201}};
  const auto actual=s::parseMetasearchDocument(recipe,
      R"({"configuration":{"maximum":2.5},"items":[{"score":1.25,"fallback":"wrong"},{"score":"5"},{"score":-5},{"score":-9},{"score":"","title":"unknown"}]})",{});
  QVERIFY(actual&&!actual->partial&&actual->rows.size()==5);
  QCOMPARE(actual->observedRankDivisor,std::optional{2.5f});
  QCOMPARE(actual->rows[0].rank,std::optional{0.3125f});
  QCOMPARE(actual->rows[1].rank,std::optional{1.0f});
  QVERIFY(!actual->rows[2].rank);
  QCOMPARE(actual->rows[3].rank,std::optional{0.0f});
  QVERIFY(!actual->rows[4].rank);
  const auto later=s::parseMetasearchDocument(recipe,R"({"items":[{"score":1.25}]})",{},"",*actual->observedRankDivisor);
  QVERIFY(later&&!later->observedRankDivisor&&later->rows.size()==1);
  QCOMPARE(later->rows[0].rank,actual->rows[0].rank);
 }
 void unmappedJsonRankUsesCountsAndQueryWords(){
  s::MetasearchRecipe recipe;
  recipe.kind=s::MetasearchRecipeKind::Json;
  recipe.mappings={{"name",1},{"seeders",5},{"leechers",4}};
  const auto body=QByteArray(R"([{"name":"Matches version 2","seeders":32,"leechers":2}])");
  const auto allFound=s::parseMetasearchDocument(recipe,body,{},"MATCHES-2");
  QVERIFY(allFound&&allFound->rows.size()==1);
  QCOMPARE(allFound->rows[0].rank,std::optional{0.4f});
  const auto missing=s::parseMetasearchDocument(recipe,body,{},"matches missing");
  QVERIFY(missing&&missing->rows.size()==1);
  QCOMPARE(missing->rows[0].rank,std::optional{0.2f});
  recipe.mappings.emplace_back("rank",201);
  const auto explicitNoRank=s::parseMetasearchDocument(recipe,body,{},"matches");
  QVERIFY(explicitNoRank&&explicitNoRank->rows.size()==1&&!explicitNoRank->rows[0].rank);
 }
 void malformedDuplicateAndXmlEncodingRefuse_data(){QTest::addColumn<QByteArray>("body");QTest::newRow("duplicate-JSON-key")<<QByteArray("[{\"a\":1,\"a\":2}]");QTest::newRow("unpaired-escaped-surrogate")<<QByteArray("[{\"a\":\"\\ud800\"}]");QTest::newRow("NUL-UTF16")<<QByteArray("<\0r\0",4);}
 void malformedDuplicateAndXmlEncodingRefuse(){QFETCH(QByteArray,body);s::MetasearchRecipe recipe;recipe.kind=s::MetasearchRecipeKind::Json;QVERIFY(!s::parseMetasearchDocument(recipe,body,{}));recipe.kind=s::MetasearchRecipeKind::Rss;QVERIFY(!s::parseMetasearchDocument(recipe,"<?xml version=\"1.0\" encoding=\"UTF-16\"?><rss/>",{}));}
 void configuredBaseIsIndependentOfRedirectAndBodyBaseIsExplicit(){
  s::MetasearchRecipe recipe;recipe.kind=s::MetasearchRecipeKind::Json;recipe.mappings={{"download",102}};auto value=s::parseMetasearchDocument(recipe,"[{\"download\":\"child.torrent\"}]",{QString("https://source.example.org"),QString("https://source.example.org/query/")});QVERIFY(value);QCOMPARE(value->rows[0].torrentLink,std::optional{std::string("https://source.example.org/query/child.torrent")});
  recipe.kind=s::MetasearchRecipeKind::Rss;auto rss=s::parseMetasearchDocument(recipe,"<rss><channel><item><title>&lt;b&gt;Literal&lt;/b&gt;</title><enclosure type=\"application/x-bittorrent\" url=\"https://files.example.org/item.torrent\"/></item></channel></rss>",{});QVERIFY(rss&&rss->rows.size()==1);
  for(const auto& href:{QByteArray("href=\"https://other.example.org/base/\""),QByteArray("href='https://other.example.org/base/'"),QByteArray("href =\"https://other.example.org/base/\"")}){auto page="<rss><channel><base "+href+"/><item><title>Owned</title><enclosure type=\"application/x-bittorrent\" url=\"child.torrent\"/></item></channel></rss>";auto parsed=s::parseMetasearchDocument(recipe,page,{QString("https://source.example.org"),QString("https://source.example.org/config/")});QVERIFY(parsed&&parsed->rows.size()==1);QCOMPARE(parsed->rows[0].torrentLink,std::optional{std::string(href.startsWith("href=\"")?"https://other.example.org/base/child.torrent":"https://source.example.org/config/child.torrent")});}
 }
};
QTEST_GUILESS_MAIN(MetasearchDocumentTests)
#include "metasearch_document_tests.moc"

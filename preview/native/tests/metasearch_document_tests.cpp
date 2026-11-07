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
  if(observation.value("native_unsupported_observation_only").toBool()){auto row=biglybtc::core::Document::parse(f::engine());QVERIFY(row);const bool expression=observation.value("label").toString().startsWith("mapping-expression");const auto maps="l"+f::mapping(expression?"${a}":"a",1,expression?std::string{}:f::b("post_filter")+f::b("never-matches"))+"e";QVERIFY(row->replaceValue("web.maps",maps));auto native=s::readMetasearchCatalog(f::catalog(row->serialize()));QVERIFY(native&&native->rows[0].issue==s::MetasearchValueError::Unsupported);return;}
  s::MetasearchRecipe recipe;recipe.kind=s::MetasearchRecipeKind::Json;recipe.resultPath=observation.value("path").toString().toStdString();recipe.mappings={{"a",1},{"b",1},{"size",3},{"seeds",5},{"peers",4},{"date",2},{"download",102},{"hash",200}};
  auto actual=s::parseMetasearchDocument(recipe,observation.value("page").toString().toUtf8(),{observation.value("actual_root_page").toString(),observation.value("actual_base_page").toString()});if(observation.contains("actual_error")){QVERIFY(!actual);return;}QVERIFY(actual);const auto expected=observation.value("actual_rows").toArray();QCOMPARE(actual->rows.size(),static_cast<std::size_t>(expected.size()));
  for(qsizetype n=0;n<expected.size();++n){const auto reference=expected[n].toObject();const auto& row=actual->rows[static_cast<std::size_t>(n)];QCOMPARE(row.title.value_or(""),reference.value("name").toString().toUtf8().toStdString());QCOMPARE(row.size,reference.value("size").toInteger());QCOMPARE(row.seeds,reference.value("seeds").toInt());QCOMPARE(row.peers,reference.value("peers").toInt());QCOMPARE(row.hash.value_or(""),reference.value("hash").toString().toStdString());QCOMPARE(row.rawTorrentLink.value_or(""),reference.value("torrent_link_raw").toString().toStdString());QVERIFY(!row.uid&&reference.value("uid").isNull());}
 }
 void exactLongTextAndAmbiguousDecimalHaveDifferentOutcomes(){
  s::MetasearchRecipe recipe;recipe.kind=s::MetasearchRecipeKind::Json;recipe.mappings={{"title",1},{"seeds",5}};auto exact=s::parseMetasearchDocument(recipe,"[{\"title\":9007199254740993,\"seeds\":2147483648}]",{});QVERIFY(exact&&exact->rows.size()==1);QCOMPARE(exact->rows[0].title,std::optional{std::string("9007199254740993")});QCOMPARE(exact->rows[0].seeds,std::int32_t{-1});auto decimal=s::parseMetasearchDocument(recipe,"[{\"title\":1e0,\"seeds\":3.0}]",{});QVERIFY(decimal&&decimal->partial&&decimal->rows[0].incomplete);QCOMPARE(decimal->rows[0].seeds,std::int32_t{-1});
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

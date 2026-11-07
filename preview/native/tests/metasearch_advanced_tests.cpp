// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_document.hpp"
#include "metasearch_value_fixture.hpp"
#include "metasearch_advanced_original_values.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <algorithm>
namespace s=biglybtc::search;namespace f=metasearch_value_fixture;
namespace {
s::MetasearchRecipe recipe(const QJsonObject& observation){
 s::MetasearchRecipe value;const auto label=observation.value("label").toString();value.kind=observation.value("kind")=="regex"?s::MetasearchRecipeKind::Regex:s::MetasearchRecipeKind::Json;value.resultPattern=observation.value("pattern").toString().toStdString();
 value.mappings=value.kind==s::MetasearchRecipeKind::Regex?std::vector<s::MetasearchMapping>{{"1",1},{"2",200}}:std::vector<s::MetasearchMapping>{{"a",1},{"b",1},{"size",3},{"seeds",5},{"peers",4},{"date",2},{"download",102},{"hash",200}};
 auto& source=value.mappings[0].name;
 if(label=="capture-interpolation"){source="Prefix ${1} suffix";}if(label=="capture-literal-replace"){source="${1,replace,-,_}";}if(label=="capture-uppercase"){source="${1,ucase}";}if(label=="capture-lowercase"){source="${1,lcase}";}if(label=="capture-url-decode"){source="${1,urldecode}";}
 if(label=="literal-postfilter-casefold"){value.mappings[0].postFilter="^alpha$";}if(label=="failed-postfilter-before-later-field"){value.mappings[0].postFilter="never";}if(label=="failed-postfilter-after-last-field"){std::reverse(value.mappings.begin(),value.mappings.end());value.mappings.back().postFilter="never";}if(label=="later-empty-capture-overwrites"||label=="later-unmatched-capture-is-skipped"){value.mappings.push_back({"3",1});}
 if(label=="json-interpolation"){source="Prefix ${a} suffix";}if(label=="json-replace"){source="${a,replace,-,_}";}if(label=="json-urldecode"){source="${a,urldecode}";}if(label=="json-missing-substitution"||label=="replacement-dollar-is-observation-only"){source="${a}";}if(label=="json-failed-filter-stays-rejected"){value.mappings[0].postFilter="never";}return value;
}
}
class MetasearchAdvancedTests final:public QObject {
 Q_OBJECT
private slots:
 void actualOriginalValuesAndExplicitCorrections_data(){
  QTest::addColumn<QJsonObject>("observation");const QByteArray bytes(MetasearchAdvancedOriginalValues,static_cast<qsizetype>(sizeof(MetasearchAdvancedOriginalValues)-1));QCOMPARE(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),QByteArray("1de27eb2365d0bb1fdddc60e77b2b288a067fafa766a21d4398cf01454ac303d"));const auto values=QJsonDocument::fromJson(bytes).object().value("cases").toArray();QCOMPARE(values.size(),qsizetype{26});int index=0;for(const auto& value:values){const auto row=value.toObject();const auto name=(row.value("label").toString(row.value("kind").toString())+QString::number(index++)).toUtf8();QTest::newRow(name.constData())<<row;}
 }
 void actualOriginalValuesAndExplicitCorrections(){
  QFETCH(QJsonObject,observation);const auto kind=observation.value("kind").toString(),label=observation.value("label").toString();
  if(kind=="zero-width-observation-only"){QCOMPARE(observation.value("actual_utf16_offsets").toArray(),(QJsonArray{0,1,2,3}));s::MetasearchRecipe value;value.kind=s::MetasearchRecipeKind::Regex;QVERIFY(!s::parseMetasearchDocument(value,QString::fromUtf8("μ 😀").toUtf8(),{}));return;}
  if(kind=="query-postfilter"){
   const auto query=observation.value("query").toString();QVERIFY(observation.contains("actual_error")||!observation.value("matches_query").toBool());s::MetasearchRecipe value;value.kind=s::MetasearchRecipeKind::Json;value.mappings={{"a",1,"^%s$"}};QVERIFY(s::validateMetasearchRecipeQuery(value,query.toUtf8().toStdString()));const auto bytes=QJsonDocument(QJsonArray{QJsonObject{{"a",query}}}).toJson();auto actual=s::parseMetasearchDocument(value,bytes,{},query.toUtf8().toStdString());QVERIFY(actual&&actual->rows.size()==1);QCOMPARE(actual->rows[0].title,std::optional{query.toUtf8().toStdString()});return;
  }
  const auto value=recipe(observation);auto actual=s::parseMetasearchDocument(value,observation.value("page").toString().toUtf8(),{QString("https://source.example.org"),QString("https://source.example.org/dir/")},observation.value("query").toString().toUtf8().toStdString());QVERIFY(actual);
  auto expected=observation.value("actual_rows").toArray();if(label=="failed-postfilter-before-later-field"){QCOMPARE(expected.size(),qsizetype{1});QVERIFY(expected[0].toObject().value("name").isNull());QVERIFY(actual->rows.empty());return;}
  QCOMPARE(actual->rows.size(),static_cast<std::size_t>(expected.size()));for(qsizetype n=0;n<expected.size();++n){const auto reference=expected[n].toObject();const auto& row=actual->rows[static_cast<std::size_t>(n)];if(label=="replacement-dollar-is-observation-only"){QCOMPARE(reference.value("name").toString(),QString("${a}"));QCOMPARE(row.title,std::optional{std::string("$1")});}else if(reference.value("name").isNull()){QVERIFY(!row.title);}else{QCOMPARE(row.title,std::optional{reference.value("name").toString().toUtf8().toStdString()});}QCOMPARE(row.size,reference.value("size").toInteger());QCOMPARE(row.seeds,reference.value("seeds").toInt());QCOMPARE(row.peers,reference.value("peers").toInt());QCOMPARE(row.hash.value_or(""),reference.value("hash").toString().toStdString());QCOMPARE(row.rawTorrentLink.value_or(""),reference.value("torrent_link_raw").toString().toStdString());}
 }
 void unsupportedSyntaxCannotBecomeAnEligibleSavedEngine_data(){QTest::addColumn<QString>("pattern");for(const auto& [name,pattern]:std::initializer_list<std::pair<const char*,const char*>>{{"backreference","(a)\\1"},{"lookahead","(?=a)a"},{"inline-flags","(?i)a"},{"PCRE-verb","(*ACCEPT)"},{"Unicode-property","\\p{L}"},{"class-intersection","[a&&b]"},{"atomic-group","(?>a)"},{"possessive","a++"},{"bad-range","[z-a]"},{"nullable","(a*)"}}){QTest::newRow(name)<<QString::fromLatin1(pattern);}}
 void unsupportedSyntaxCannotBecomeAnEligibleSavedEngine(){QFETCH(QString,pattern);auto row=biglybtc::core::Document::parse(f::engine());QVERIFY(row);QVERIFY(row->replaceValue("type","i1e"));QVERIFY(row->replaceValue("regex.pattern",f::b(pattern.toStdString())));QVERIFY(row->replaceValue("web.maps","l"+f::mapping("1",1)+"e"));const auto original=f::catalog(row->serialize());auto actual=s::readMetasearchCatalog(original);QVERIFY(actual&&actual->rows[0].issue);QCOMPARE(actual->source->originalBytes(),original);}
 void catastrophicInputAndSharedAttemptBudgetFailClosed(){
  s::MetasearchRecipe value;value.kind=s::MetasearchRecipeKind::Regex;value.resultPattern="<r>(.*?)</r>";value.mappings={{"1",1}};auto ordinary=s::parseMetasearchDocument(value,"<r>"+QByteArray(4096,'a')+"</r>",{});QVERIFY(ordinary&&ordinary->rows.size()==1);QCOMPARE(ordinary->rows[0].title->size(),std::size_t{4096});value.resultPattern="(a+)+$";value.mappings={{"1",1}};auto hard=s::parseMetasearchDocument(value,QByteArray(32768,'a')+'!',{});QVERIFY(!hard);QCOMPARE(hard.error(),s::MetasearchDocumentError::Limit);value.resultPattern="(z)";auto scans=s::parseMetasearchDocument(value,QByteArray(20000,'a'),{});QVERIFY(!scans);QCOMPARE(scans.error(),s::MetasearchDocumentError::Limit);
 }
 void unsupportedUnicodeFoldAndAmbiguousLocaleRefuse(){
  s::MetasearchRecipe value;value.kind=s::MetasearchRecipeKind::Json;value.mappings={{"a",1,"ALPHA"}};auto folded=s::parseMetasearchDocument(value,QJsonDocument(QJsonArray{QJsonObject{{"a",QString::fromUtf8("α")}}}).toJson(),{});QVERIFY(!folded);QCOMPARE(folded.error(),s::MetasearchDocumentError::Unsupported);value.mappings={{"${a,ucase}",1}};auto locale=s::parseMetasearchDocument(value,"[{\"a\":\"i\"}]",{});QVERIFY(!locale);QCOMPARE(locale.error(),s::MetasearchDocumentError::Unsupported);value.mappings={{"a",1,"^%s$"}};QVERIFY(!s::validateMetasearchRecipeQuery(value,"μ"));
 }
};
QTEST_GUILESS_MAIN(MetasearchAdvancedTests)
#include "metasearch_advanced_tests.moc"

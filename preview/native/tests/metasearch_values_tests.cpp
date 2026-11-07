// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_values.hpp"
#include "metasearch_value_fixture.hpp"
#include <QtTest>
namespace s=biglybtc::search;namespace f=metasearch_value_fixture;
class MetasearchValuesTests final:public QObject {
 Q_OBJECT
private slots:
 void literalUtf8FormEncoding_data(){QTest::addColumn<QString>("query");QTest::addColumn<QString>("encoded");QTest::newRow("space")<<QString("alpha beta")<<QString("alpha+beta");QTest::newRow("reserved")<<QString("x+y/%s")<<QString("x%2By%2F%25s");QTest::newRow("unicode")<<QString::fromUtf8("μ 雪")<<QString("%CE%BC+%E9%9B%AA");QTest::newRow("form-safe")<<QString("*~.-_")<<QString("*%7E.-_");QTest::newRow("delimiters")<<QString("a&b=c?#")<<QString("a%26b%3Dc%3F%23");QTest::newRow("replacement-not-recursive")<<QString("%s%s")<<QString("%25s%25s");}
 void literalUtf8FormEncoding(){QFETCH(QString,query);QFETCH(QString,encoded);auto value=s::expandMetasearchQuery("https://feeds.example.org/api/%s?q=%s&fixed=%2F",query.toUtf8().toStdString());QVERIFY(value);QCOMPARE(*value,("https://feeds.example.org/api/"+encoded+"?q="+encoded+"&fixed=%2F").toStdString());}
 void authorityAndUnsupportedMethodsNeverExpand_data(){QTest::addColumn<QString>("url");for(const auto& [name,url]:std::initializer_list<std::pair<const char*,const char*>>{{"scheme","%s://feeds.example.org/"},{"host","https://%s.example.org/"},{"user","https://%s@feeds.example.org/"},{"port","https://feeds.example.org:%s/"},{"POST","https://feeds.example.org/?azmethod=post"},{"unknown-macro","https://feeds.example.org/?q=%q"}}){QTest::newRow(name)<<QString::fromLatin1(url);}}
 void authorityAndUnsupportedMethodsNeverExpand(){QFETCH(QString,url);QVERIFY(!s::expandMetasearchQuery(url.toStdString(),"query"));}
 void catalogPreservesUnknownUpdaterAndDisabledRows(){
  auto supported=f::engine(7,"json",f::b("update_url")+f::b("https://updates.example.org/template"));auto disabled=biglybtc::core::Document::parse(f::engine(8,"disabled"));QVERIFY(disabled);QVERIFY(disabled->replaceValue("selected","i0e"));const auto bytes=f::catalog(supported+disabled->serialize());auto value=s::readMetasearchCatalog(bytes);QVERIFY(value);QCOMPARE(value->source->originalBytes(),bytes);QCOMPARE(value->rows.size(),std::size_t{2});QVERIFY(value->rows[0].recipe&&!value->rows[0].issue);QVERIFY(value->rows[1].issue&&!value->rows[1].diagnostic.empty());
 }
 void malformedOtherEngineCannotBorrowIdentity_data(){QTest::addColumn<bool>("sameId");QTest::newRow("duplicate-id-even-unsupported")<<true;QTest::newRow("duplicate-uid-even-unsupported")<<false;}
 void malformedOtherEngineCannotBorrowIdentity(){QFETCH(bool,sameId);auto other=biglybtc::core::Document::parse(f::engine(sameId?7:8,sameId?"other":"owned-json"));QVERIFY(other);QVERIFY(other->replaceValue("type","i1e"));auto value=s::readMetasearchCatalog(f::catalog(f::engine()+other->serialize()));QVERIFY(value);QCOMPARE(value->rows[0].issue,std::optional{s::MetasearchValueError::Ambiguous});}
 void unsupportedRecipesHaveSpecificRefusal_data(){QTest::addColumn<int>("mode");QTest::newRow("regex")<<0;QTest::newRow("auth")<<1;QTest::newRow("postfilter")<<2;QTest::newRow("expression")<<3;QTest::newRow("remap")<<4;QTest::newRow("unimplemented-field")<<5;}
 void unsupportedRecipesHaveSpecificRefusal(){QFETCH(int,mode);auto row=biglybtc::core::Document::parse(f::engine());QVERIFY(row);if(mode==0){QVERIFY(row->replaceValue("type","i1e"));}if(mode==1){QVERIFY(row->replaceValue("web.needs_auth","i1e"));}if(mode==2){QVERIFY(row->replaceValue("web.maps","l"+f::mapping("title",1,f::b("post_filter")+f::b("must-match"))+"e"));}if(mode==3){QVERIFY(row->replaceValue("web.maps","l"+f::mapping("${title}",1)+"e"));}if(mode==4){QVERIFY(row->replaceValue("l1_map","li1ee"));}if(mode==5){QVERIFY(row->replaceValue("web.maps","l"+f::mapping("rank",201)+"e"));}auto value=s::readMetasearchCatalog(f::catalog(row->serialize()));QVERIFY(value&&value->rows.size()==1&&value->rows[0].issue);QVERIFY(!value->rows[0].diagnostic.empty());}
};
QTEST_GUILESS_MAIN(MetasearchValuesTests)
#include "metasearch_values_tests.moc"

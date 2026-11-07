// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_fixture.hpp"
#include <QtTest>
#include <thread>
namespace f=metasearch_fixture;namespace s=biglybtc::search;using namespace biglybtc;
class MetasearchSourceTests final:public QObject {
 Q_OBJECT
private slots:
 void realSourceBindsQueryAndAcceptsOnlyUnrelatedRevision(){
  f::Profile area("https://feeds.example.org/dir/%41/%7E/%2f/%s?q=%s&fixed=a/b");const auto original=f::bytes(area.path+"/metasearch.config");auto value=s::SavedMetasearchSourceSnapshot::read(*area.lease,area.path.toStdString(),101,area.selector(),"μ +*/~");QVERIFY(value);QCOMPARE(value->description().literalGetUrl,std::string("https://feeds.example.org/dir/%41/%7E/%2f/%CE%BC+%2B*%2F%7E?q=%CE%BC+%2B*%2F%7E&fixed=a/b"));QCOMPARE(value->description().context.rootPage,QString("https://feeds.example.org"));QCOMPARE(value->description().context.basePage,QString("https://feeds.example.org/dir/%41/%7E/%2f/%s?q=%s&fixed=a/"));QCOMPARE(f::bytes(area.path+"/metasearch.config"),original);
  auto unrelated=storage::ConfigFile::createMissing(*area.lease,"unrelated.config","d6:futurei1ee");QVERIFY(unrelated);QVERIFY(value->current(*area.lease,101));QVERIFY(!value->current(*area.lease,102));std::optional<s::SavedRssSourceError> error;std::thread other([&]{auto read=value->current(*area.lease,101);if(!read){error=read.error();}});other.join();QCOMPARE(error,std::optional{s::SavedRssSourceError::WrongWorker});
  auto config=storage::ConfigFile::load(*area.lease,"metasearch.config");QVERIFY(config);QVERIFY(config->document().replaceValue("future_catalog","7:changed"));QVERIFY(config->save(*area.lease));QVERIFY(!value->current(*area.lease,101));
 }
 void savedIdentityCollisionsAreNeverAnEligibleOwner_data(){QTest::addColumn<int>("mode");QTest::newRow("duplicate-id")<<0;QTest::newRow("duplicate-uid")<<1;QTest::newRow("unsupported-other-kind-with-same-id")<<2;}
 void savedIdentityCollisionsAreNeverAnEligibleOwner(){QFETCH(int,mode);f::Profile area("http://feeds.example.org/?q=%s");auto other=f::engine("http://other.example.org/",mode==1?8:7,mode==1?"owned-json":"other",mode==2?1:2);area.setRows(f::engine("http://feeds.example.org/?q=%s")+other);auto value=s::SavedMetasearchSourceSnapshot::read(*area.lease,area.path.toStdString(),102,area.selector(),"query");QVERIFY(!value);QCOMPARE(value.error(),s::SavedRssSourceError::Unsupported);}
 void policyOrNamedCandidatesRefuseBeforeAnyProvider_data(){QTest::addColumn<int>("mode");QTest::newRow("proxy")<<0;QTest::newRow("browser-headers")<<1;QTest::newRow("saving-candidate")<<2;QTest::newRow("symlink-source")<<3;QTest::newRow("disabled-source")<<4;QTest::newRow("known-context-whitelist")<<5;}
 void policyOrNamedCandidatesRefuseBeforeAnyProvider(){
  QFETCH(int,mode);f::Profile area(mode==5?"https://www.biglybt.com/?q=%s":"https://feeds.example.org/?q=%s");
  if(mode==0||mode==1){auto config=storage::ConfigFile::load(*area.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().replaceValue(mode==0?"metasearch.config.proxy.type":"metasearch.web.last.headers",mode==0?"i1e":"4:raw!"));QVERIFY(config->save(*area.lease));}
  if(mode==2){f::w::write(area.path+"/metasearch.config.saving",area.primary);}if(mode==3){QVERIFY(QFile::rename(area.path+"/metasearch.config",area.path+"/retained.config"));QVERIFY(QFile::link(area.path+"/retained.config",area.path+"/metasearch.config"));}
  if(mode==4){auto row=core::Document::parse(f::engine("https://feeds.example.org/?q=%s").toStdString());QVERIFY(row);QVERIFY(row->replaceValue("selected","i0e"));area.setRows(QByteArray::fromStdString(row->serialize()));}
  QVERIFY(!s::SavedMetasearchSourceSnapshot::read(*area.lease,area.path.toStdString(),103,area.selector(),"query"));
 }
 void missingConfiguredBaseDoesNotInventOneOrRejectAbsoluteResults(){
  f::Profile area("https://feeds.example.org?q=%s");auto value=s::SavedMetasearchSourceSnapshot::read(*area.lease,area.path.toStdString(),104,area.selector(),"query");QVERIFY(value);QVERIFY(value->description().context.basePage.isEmpty());f::Profile foreign("https://other.example.org/?q=%s");QVERIFY(!value->current(*foreign.lease,104));
 }
 void actualSavedRegexRecipeRetainsUnknownBytesAndSourceGuard(){
  f::Profile area("http://feeds.example.org/?q=%s");auto row=core::Document::parse(f::engine("http://feeds.example.org/?q=%s").toStdString());QVERIFY(row);QVERIFY(row->replaceValue("type","i1e"));QVERIFY(row->replaceValue("regex.pattern",f::w::b("<r>(.*?)</r>").toStdString()));QVERIFY(row->replaceValue("web.maps",("l"+f::mapping("1",1)+"e").toStdString()));area.setRows(QByteArray::fromStdString(row->serialize()));const auto before=f::bytes(area.path+"/metasearch.config");auto source=s::SavedMetasearchSourceSnapshot::read(*area.lease,area.path.toStdString(),105,area.selector(),"actual query");QVERIFY(source);QCOMPARE(source->description().recipe->kind,s::MetasearchRecipeKind::Regex);QCOMPARE(source->description().recipe->resultPattern,std::string("<r>(.*?)</r>"));QCOMPARE(f::bytes(area.path+"/metasearch.config"),before);QVERIFY(source->current(*area.lease,105));
 }
 void queryDependentPostFilterValidatesActualQueryBeforeAnyProvider(){
  f::Profile area("http://feeds.example.org/?q=%s");auto row=core::Document::parse(f::engine("http://feeds.example.org/?q=%s").toStdString());QVERIFY(row);QVERIFY(row->replaceValue("web.maps","l"+f::dictionary({{"name",f::w::b("title")},{"field",f::integer(1)},{"post_filter",f::w::b("^%s$")}}).toStdString()+"e"));area.setRows(QByteArray::fromStdString(row->serialize()));const auto before=f::bytes(area.path+"/metasearch.config");auto refused=s::SavedMetasearchSourceSnapshot::read(*area.lease,area.path.toStdString(),106,area.selector(),"μ");QVERIFY(!refused);QCOMPARE(refused.error(),s::SavedRssSourceError::Unsupported);auto supported=s::SavedMetasearchSourceSnapshot::read(*area.lease,area.path.toStdString(),106,area.selector(),"a$b\\E");QVERIFY(supported);QCOMPARE(supported->description().query,std::string("a$b\\E"));QCOMPARE(f::bytes(area.path+"/metasearch.config"),before);
 }

};
QTEST_GUILESS_MAIN(MetasearchSourceTests)
#include "metasearch_source_tests.moc"

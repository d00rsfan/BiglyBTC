// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tracker_tag_rules.hpp>
#include <biglybtc/bencode.hpp>
#include <QFile>
#include <array>
#include <QTest>
using namespace biglybtc;
namespace tag=core::tags::tracker;
class TrackerTagRuleTests final:public QObject {
 Q_OBJECT
private slots:
 void actualOriginalHostsPropertyStartupCreationAndSync(){
  QFile file(QString::fromUtf8(TRACKER_TAG_RULE_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));
  core::Limits limits;limits.nodes=500000;auto source=core::Document::parse(file.readAll().toStdString(),limits);QVERIFY(source);unsigned cases=0;
  for(const auto& row:source->find("rows")->children){
   std::vector<tag::Endpoint> endpoints,parsed;for(const auto& e:row.find("endpoints")->children){
    endpoints.push_back({std::string(e.find("host")->bytes),std::int32_t(e.find("port")->integer),std::int32_t(e.find("default_port")->integer)});
    auto decode=tag::endpoint(e.find("url")->bytes);QVERIFY2(decode,qPrintable(QStringLiteral("Generated original case %1: %2").arg(cases).arg(QString::fromUtf8(e.find("url")->bytes.data(),qsizetype(e.find("url")->bytes.size())))));parsed.push_back(*decode);
    QCOMPARE(decode->port,endpoints.back().port);QCOMPARE(decode->defaultPort,endpoints.back().defaultPort);
    auto lower=endpoints.back().host;for(auto& c:lower){if(c>='A'&&c<='Z'){c=static_cast<char>(c+('a'-'A'));}}QCOMPARE(decode->host,lower);
   }
   std::vector<std::string> expected;for(const auto& host:row.find("hosts")->children){expected.emplace_back(host.bytes);}
   auto actual=tag::augmentedHosts(endpoints);QVERIFY(actual);QCOMPARE(*actual,expected);
   auto decoded=tag::augmentedHosts(parsed);QVERIFY(decoded);QCOMPARE(*decoded,expected);
   std::vector<std::string> property;for(const auto& name:row.find("property")->children){property.emplace_back(name.bytes);}
   auto match=tag::matches(parsed,property);QVERIFY(match);
   const auto event=row.find("event")->bytes;const auto kind=event=="startup"?tag::Event::Startup:event=="created"?tag::Event::DownloadCreated:event=="changed"?tag::Event::PropertyChanged:tag::Event::AnnounceSync;
   const bool before=row.find("before")->integer!=0;const auto decision=tag::project(kind,!property.empty(),before,*match);
   const bool after=decision==tag::Membership::Keep?before:decision==tag::Membership::Add;QCOMPARE(after,row.find("after")->integer!=0);
   const auto& trace=row.find("trace")->children;QCOMPARE(trace.size(),decision==tag::Membership::Keep?std::size_t{0}:std::size_t{1});
   if(!trace.empty()){QCOMPARE(trace.front().bytes,decision==tag::Membership::Add?std::string_view("add"):std::string_view("remove"));}++cases;
  }
  QCOMPARE(cases,2112u);
 }
 void originalAllTrackersSelectionOrigins(){
  struct Case{std::string_view url,name,selector;};
  const std::array cases{
    Case{"http://user:generated@EXAMPLE.test/announce?generated_key=abc",
        "http://example.test:80","example.test:80"},
    Case{"https://sub.example.test:80/announce","https://sub.example.test:80","sub.example.test:80"},
    Case{"https://example.test/announce","https://example.test:443","example.test:443"},
    Case{"udp://tracker.alt.test/announce","udp://tracker.alt.test:80","tracker.alt.test:80"},
    Case{"http://[2001:DB8::1]:8080/announce","http://[2001:db8::1]:8080","[2001:db8::1]:8080"},
  };
  for(const auto& example:cases){
   const auto current=tag::selectionUrl(example.url);
   QVERIFY(current&&current->origin);
   QCOMPARE(current->origin->name,example.name);
   QCOMPARE(current->origin->selector,example.selector);
  }
  const auto pseudo=tag::selectionUrl("http://unused.dht/announce");
  QVERIFY(pseudo&&!pseudo->origin);
  const auto dht=tag::selectionUrl("dht://AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA.dht/announce");
  QVERIFY(dht&&!dht->origin);
  QCOMPARE(dht->endpoint.host,std::string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.dht"));
  QCOMPARE(dht->endpoint.defaultPort,-1);
  QVERIFY(!tag::selectionUrl("ftp://example.test/announce"));
 }
 void unknownAndOverBudgetInputsNeverBecomeNegativeMatches(){
  for(const auto* input:{"http://[1:2:3]/announce","http://[1::2::3]/announce","http://[12345::1]/announce","http://[1:2:3:4:5:6:7:8:9]/announce","http://[::ffff:192.0.2.1]/announce","ftp://example.test/announce","http://example.test:-2/announce","udp://example.test:2147483648/announce","http://[2001:db8::1%eth0]/announce","http://a\\b/announce","http://a.example/space here","http://a@b@c/announce"}){QVERIFY(!tag::endpoint(input));}
  QVERIFY(!tag::endpoint(std::string("http://caf")+char(0xc3)+char(0xa9)+".test/announce"));
  const std::vector<tag::Endpoint> endpoints{{"a.example.test",-1,80}};const std::vector<std::string> names{"example.test"};
  auto tiny=tag::Limits{};tiny.hosts=1;auto hosts=tag::augmentedHosts(endpoints,tiny);QVERIFY(!hosts&&hosts.error()==tag::Error::Bounds);
  tiny={};tiny.totalBytes=1;auto match=tag::matches(endpoints,names,tiny);QVERIFY(!match&&match.error()==tag::Error::Bounds);
  tiny={};tiny.properties=0;QVERIFY(!tag::matches(endpoints,names,tiny));tiny={};tiny.endpoints=0;QVERIFY(!tag::matches(endpoints,names,tiny));
  tiny={};tiny.hosts=4097;QVERIFY(!tag::matches(endpoints,names,tiny));
  const std::vector<std::string> unknown{"example.test",std::string(1,char(0xff))};match=tag::matches(endpoints,unknown);QVERIFY(!match&&match.error()==tag::Error::Unsupported);
  const std::vector<tag::Endpoint> foreign{{std::string(1,char(0xff)),-1,80}};QVERIFY(!tag::matches(foreign,names));
  const std::vector<std::string> spaces{" example.test "};match=tag::matches(endpoints,spaces);QVERIFY(match&&!*match);
  const std::vector<tag::Endpoint> dotted{{"a..example.test.",-1,80}};const std::vector<std::string> suffix{".example.test"};match=tag::matches(dotted,suffix);QVERIFY(match&&*match);
 }
};
QTEST_GUILESS_MAIN(TrackerTagRuleTests)
#include "tracker_tag_rule_tests.moc"

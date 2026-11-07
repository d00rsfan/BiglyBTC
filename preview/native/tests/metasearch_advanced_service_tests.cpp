// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_fixture.hpp"
#include <QtTest>
namespace f=metasearch_fixture;
namespace s=biglybtc::search;
class MetasearchAdvancedServiceTests final:public QObject {
  Q_OBJECT
private slots:
  void savedRegexHttpAndActualQueryFilterMintOnlyCurrentMagnet_data(){
    QTest::addColumn<bool>("fallback");
    QTest::newRow("normal-pattern")<<false;
    QTest::newRow("dotall-fallback")<<true;
  }
  void savedRegexHttpAndActualQueryFilterMintOnlyCurrentMagnet(){
    QFETCH(bool,fallback);
    constexpr std::uint64_t session=418;
    f::w::Server server;
    const QByteArray hash(32,'C');
    server.body=QByteArray("<pre>")+(fallback?"\n":"")+"<r>Alpha</r><h>"+hash+"</h></pre>";
    f::Profile area(server.url("/find/%s?q=%s"));
    auto definition=biglybtc::core::Document::parse(f::engine(server.url("/find/%s?q=%s")).toStdString());
    QVERIFY(definition);
    QVERIFY(definition->replaceValue("type","i1e"));
    QVERIFY(definition->replaceValue("regex.pattern",f::w::b("<pre>.*?<r>([^<]+)</r><h>([A-Z2-7]{32})</h></pre>").toStdString()));
    const auto name=f::dictionary({{"name",f::w::b("1")},{"field",f::integer(1)},
        {"post_filter",f::w::b("^%s$")}});
    QVERIFY(definition->replaceValue("web.maps",(QByteArray("l")+name+f::mapping("2",200)+"e").toStdString()));
    area.setRows(QByteArray::fromStdString(definition->serialize()));
    const auto saved=area.primary;
    int made=0;
    auto service=s::SavedMetasearchService::open(*area.lease,area.path.toStdString(),
        session,s::MetasearchServiceMode::Normal,f::factory(area,made));
    QVERIFY(service);
    QVERIFY((*service)->beginSearch(*area.lease,session,{area.selector()},"Alpha"));
    std::vector<s::MetasearchSearchUpdate> out;
    QTRY_VERIFY_WITH_TIMEOUT((f::pump(**service,*area.lease,session,out),
        !out.empty()&&out.back().done),10000);
    QCOMPARE(out.size(),std::size_t{1});
    QVERIFY(!out.back().hadFailures&&!out.back().engine.failure);
    QCOMPARE(out.back().engine.rows.size(),std::size_t{1});
    const auto& row=out.back().engine.rows.front();
    QCOMPARE(row.values.title,std::optional{std::string("Alpha")});
    QCOMPARE(row.values.hash,std::optional{hash.toStdString()});
    QCOMPARE(made,1);
    QCOMPARE(server.requests.size(),qsizetype{1});
    QVERIFY(server.requests.front().startsWith("GET /find/Alpha?q=Alpha HTTP/1.1\r\n"));
    {
      std::stop_source receiver;
      auto retained=(*service)->retainMagnetResult(*area.lease,session,row.key,receiver.get_token());
      QVERIFY(retained);
      QVERIFY(retained->current(*area.lease,session));
      QVERIFY(retained->description().key==row.key);
      QCOMPARE(retained->description().query,std::string("Alpha"));
      QCOMPARE(retained->description().name,std::string("Alpha"));
      QVERIFY(row.values.rawTorrentLink&&row.values.rawTorrentLink->starts_with("magnet:?xt=urn:btih:"));
      QCOMPARE(f::bytes(area.path+"/metasearch.config"),saved);
      auto config=biglybtc::storage::ConfigFile::load(*area.lease,"biglybt.config");
      QVERIFY(config);
      QVERIFY(!config->document().find("MagnetPlugin.active.magnets"));
      QVERIFY((*service)->beginSearch(*area.lease,session,{area.selector()},"replacement"));
      QVERIFY(!retained->current(*area.lease,session));
      QVERIFY(!(*service)->retainMagnetResult(*area.lease,session,row.key,receiver.get_token()));
      QCOMPARE(made,1); // Replacing a query does not start a provider until poll.
    }
    QVERIFY((*service)->close());
    area.lease.reset();
    QVERIFY(biglybtc::storage::ProfileLease::acquire(area.path.toStdString()));
  }
};
QTEST_GUILESS_MAIN(MetasearchAdvancedServiceTests)
#include "metasearch_advanced_service_tests.moc"

// SPDX-License-Identifier: GPL-2.0-or-later
#include "cache_network_fixture.hpp"
#include "chat_owner_fixture.hpp"
#include "key_block_fixture.hpp"
using namespace generated_cache;using generated_key_blocks::Data;namespace block=services::azureus_key_block;
class AzureusIdentityOwnerTests final:public QObject{Q_OBJECT
private slots:
 void startupUsesActualSocketAndPreservesOrPurgesDirectRecords_data(){QTest::addColumn<QByteArray>("previous");QTest::addColumn<bool>("purged");QTest::newRow("same-host")<<QByteArray("127.192.91.1")<<false;QTest::newRow("changed-host")<<QByteArray("127.191.91.1")<<true;QTest::newRow("unreadable-cache-retained")<<QByteArray("unreadable")<<false;}
 void startupUsesActualSocketAndPreservesOrPurgesDirectRecords(){
  QFETCH(QByteArray,previous);QFETCH(bool,purged);Actor seed("127.193.91.1",20);Data data;auto original=core::Document::parse("d6:opaque4:keepe");QVERIFY(original);const std::array records{block::Record{data.request,data.signature,std::int32_t(QDateTime::currentSecsSinceEpoch()),true}};auto encoded=block::encodeSaved(original->root(),records);QVERIFY(encoded);const bool unreadable=previous=="unreadable";const std::string address=unreadable?previous.toStdString():"d11:most_recent"+std::to_string(previous.size())+":"+previous.toStdString()+"e";generated_chat::GeneratedClient target(QHostAddress("127.192.91.1"),seed.transport.endpoint(),false,false,0ms,0ms,{},engine::OwnedKeyBlockFixtureKey{data.modulus,data.exponent},*encoded,address);QTRY_VERIFY_WITH_TIMEOUT(target.chats->snapshot()->transports[0],5000);
  QFile blocks(target.root.path()+"/dht/net4/block.dat");QVERIFY(blocks.open(QIODevice::ReadOnly));const auto raw=blocks.readAll().toStdString();auto saved=core::Document::parse(raw);QVERIFY(saved);QCOMPARE(saved->find("opaque")->bytes,std::string_view("keep"));auto decoded=block::decodeSaved(saved->root(),QDateTime::currentMSecsSinceEpoch(),data.verifier());QVERIFY(decoded);QCOMPARE(decoded->records.size(),purged?std::size_t{0}:std::size_t{1});if(!purged){QCOMPARE(raw,*encoded);}QFile history(target.root.path()+"/dht/net4/addresses.dat");QVERIFY(history.open(QIODevice::ReadOnly));const auto historyBytes=history.readAll().toStdString();if(unreadable){QCOMPARE(historyBytes,address);}else{auto doc=core::Document::parse(historyBytes);QVERIFY(doc);QCOMPARE(doc->find("most_recent")->bytes,std::string_view("127.192.91.1"));QVERIFY(doc->find(previous.toStdString()));QVERIFY(doc->find("127.192.91.1"));}QVERIFY(!QFile::exists(target.root.path()+"/dht/net4/addresses.saving"));
 }
};
QTEST_GUILESS_MAIN(AzureusIdentityOwnerTests)
#include "azureus_identity_owner_tests.moc"

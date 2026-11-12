// SPDX-License-Identifier: GPL-2.0-or-later
#include "allocation_fixture.hpp"
#include "torrent_options.hpp"
#include <QtTest>
using namespace biglybtc;
namespace {
std::string encoded(const core::Node* node){if(!node){return {};}auto value=core::encodeValue(*node,storage::compatibleConfigLimits());return value?*value:std::string{};}
void stop(test::allocation::Fixture& f,int state=70){auto file=storage::ConfigFile::load(*f.lease,"downloads.config");if(!file){throw std::runtime_error("fixture config");}auto row=core::Document::parse(encoded(&file->document().find("downloads")->children[0]));if(!row||!row->replaceValue("state","i"+std::to_string(state)+"e")||!file->document().replaceValue("downloads","l"+row->serialize()+"e")||!file->save(*f.lease)){throw std::runtime_error("fixture stopped state");}}
}
class TorrentOptionsTests final:public QObject {Q_OBJECT
private slots:
 void compatibleEditsKeepResumeInfoUnknownFieldsAndPayload(){
  test::allocation::Fixture f(false);QVERIFY(f.import(core::TorrentAllocationMode::None));stop(f);test::allocation::write(f.path(),std::string(std::size_t(f.bytes()),'Z'));
  auto store=storage::TorrentStateStore::open(*f.lease);QVERIFY(store);auto state=store->stageUpdate(*f.lease,f.hash);QVERIFY(state);std::string flags(std::size_t((f.bytes()+16383)/16384),'\1');flags.back()='\2';QVERIFY(state->replaceSection("resume","d4:datad6:blocksde11:resume data"+test::allocation::b(flags)+"5:validi0eee"));QVERIFY(store->save(*f.lease,*state));
  const auto before=f.state();const auto inventory=f.saved();const auto payload=test::allocation::read(f.path());auto original=storage::readOriginalTorrentOptions(*f.lease,f.hash);QVERIFY(original);QVERIFY(original->stopped);
  QVERIFY(storage::saveOriginalTorrentOptions(*f.lease,f.hash,true,{original->values[0],original->values[1]},{12345,54321}));auto rates=storage::readOriginalTorrentOptions(*f.lease,f.hash);QVERIFY(rates);QCOMPARE(rates->values[0],12345);QCOMPARE(rates->values[1],54321);
  QVERIFY(storage::saveOriginalTorrentOptions(*f.lease,f.hash,false,{original->values[2],original->values[3]},{42,8}));auto limits=storage::readOriginalTorrentOptions(*f.lease,f.hash);QVERIFY(limits);QCOMPARE(limits->values[2],42);QCOMPARE(limits->values[3],8);
  const auto after=f.state();QCOMPARE(encoded(before.find("info")),encoded(after.find("info")));QCOMPARE(encoded(before.find("resume")),encoded(after.find("resume")));QCOMPARE(encoded(before.find("opaque.source")),encoded(after.find("opaque.source")));QCOMPARE(encoded(before.find("attributes")->find("opaque")),encoded(after.find("attributes")->find("opaque")));QCOMPARE(encoded(inventory.find("opaque")),encoded(f.saved().find("opaque")));QCOMPARE(test::allocation::read(f.path()),payload);
 }
 void activeStaleAndMalformedCommandsDoNotWrite(){
  test::allocation::Fixture f(false);QVERIFY(f.import(core::TorrentAllocationMode::None));stop(f,75);auto value=storage::readOriginalTorrentOptions(*f.lease,f.hash);QVERIFY(value);const auto before=f.saved().serialize();QVERIFY(!storage::saveOriginalTorrentOptions(*f.lease,f.hash,true,{value->values[0],value->values[1]},{200,300}));QCOMPARE(f.saved().serialize(),before);stop(f);value=storage::readOriginalTorrentOptions(*f.lease,f.hash);QVERIFY(value);const auto stopped=f.saved().serialize();QVERIFY(!storage::saveOriginalTorrentOptions(*f.lease,f.hash,true,{-1,-1},{200,300}));QVERIFY(!storage::saveOriginalTorrentOptions(*f.lease,f.hash,false,{value->values[2],value->values[3]},{100,1}));QCOMPARE(f.saved().serialize(),stopped);
 }
};
QTEST_GUILESS_MAIN(TorrentOptionsTests)
#include "torrent_options_tests.moc"

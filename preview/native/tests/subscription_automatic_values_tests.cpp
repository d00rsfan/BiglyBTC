// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_automatic_values.hpp"
#include "biglybtc/bencode.hpp"
#include <QtTest>
#include <limits>
using namespace biglybtc;
using namespace search::detail;
namespace {
std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
core::Document config(std::string extra={}){auto d=core::Document::parse("d"+b("Default save path")+b("/tmp/data")+extra+"e");if(!d){qFatal("Invalid value-only config fixture");}return std::move(*d);}
SubscriptionAutomaticPolicyValues policy(){auto p=readSubscriptionAutomaticPolicy(config(),"/tmp/profile");if(!p){qFatal("Value policy fixture failed");}return std::move(*p);}
std::array<std::byte,20> hash(){std::array<std::byte,20> out{};out[1]=std::byte{0x11};out[2]=std::byte{0x22};out[3]=std::byte{0x33};return out;}
}
class SubscriptionAutomaticValuesTests final:public QObject {
 Q_OBJECT
private slots:
 void actualConfigDefaultsNarrowingAndUnknownBytes(){
  const auto unknown=b("future")+"d"+b("opaque")+"li17e3:rawee";auto d=config(unknown);const auto before=d.originalBytes();auto p=readSubscriptionAutomaticPolicy(d,"/tmp/profile");QVERIFY(p);QVERIFY(p->autoStart&&!p->addHashNames&&p->saveTorrentFiles&&!p->defaultStartStopped&&!p->defaultStartAutoPause);QCOMPARE(p->torrentSaveDirectory,std::string("/tmp/profile/torrents"));QCOMPARE(d.originalBytes(),before);
  auto narrow=config(b("subscriptions.auto.start.min.mb")+"i4294967297e"+b("subscriptions.auto.start.downloads")+"i4294967296e"+b("subscriptions.auto.dl.add.hashes")+"i2e");auto n=readSubscriptionAutomaticPolicy(narrow,"/tmp/profile");QVERIFY(n);QCOMPARE(n->minStartMiB,std::int32_t{1});QVERIFY(!n->autoStart&&n->addHashNames);
 }
 void originalInclusiveSizeAndJavaIntMultiply_data(){
  QTest::addColumn<int>("min");QTest::addColumn<int>("max");QTest::addColumn<qint64>("size");QTest::addColumn<bool>("start");
  QTest::newRow("default-unbounded")<<0<<0<<qint64{0}<<true;
  QTest::newRow("below-min")<<1<<0<<qint64{1048575}<<false;
  QTest::newRow("equal-min")<<1<<0<<qint64{1048576}<<true;
  QTest::newRow("equal-max")<<0<<1<<qint64{1048576}<<true;
  QTest::newRow("above-max")<<0<<1<<qint64{1048577}<<false;
  QTest::newRow("contradictory")<<2<<1<<qint64{1048576}<<false;
  QTest::newRow("negative-normal-inactive")<<-1<<-1<<qint64{0}<<true;
  QTest::newRow("positive-min-int-product-wraps-negative")<<2097152<<0<<qint64{0}<<true;
  QTest::newRow("positive-max-int-product-wraps-negative")<<0<<2097152<<std::numeric_limits<qint64>::max()<<true;
  QTest::newRow("negative-min-int-product-wraps-positive")<<-2097153<<0<<qint64{0}<<false;
  QTest::newRow("negative-max-wrap-boundary")<<0<<-2097153<<qint64{2199022206976LL}<<true;
  QTest::newRow("negative-max-wrap-above")<<0<<-2097153<<qint64{2199022206977LL}<<false;
 }
 void originalInclusiveSizeAndJavaIntMultiply(){
  QFETCH(int,min);QFETCH(int,max);QFETCH(qint64,size);QFETCH(bool,start);auto p=policy();p.minStartMiB=min;p.maxStartMiB=max;QCOMPARE(subscriptionAutomaticShouldStart(p,size),start);p.autoStart=false;QVERIFY(!subscriptionAutomaticShouldStart(p,size));
 }
 void originalHashNameAndObservedShape_data(){
  QTest::addColumn<bool>("hashed");QTest::addColumn<bool>("multi");QTest::addColumn<int>("kind");QTest::addColumn<QString>("base");QTest::addColumn<QString>("destination");QTest::addColumn<QString>("name");
  QTest::newRow("plain-single")<<false<<false<<2<<QString("/tmp/data")<<QString("/tmp/data")<<QString("Owned");
  QTest::newRow("plain-multi")<<false<<true<<2<<QString("/tmp/data")<<QString("/tmp/data")<<QString("Owned");
  QTest::newRow("plain-multi-no-double-root")<<false<<true<<2<<QString("/tmp/data/Owned/")<<QString("/tmp/data")<<QString("Owned");
  QTest::newRow("hash-single-absent-is-file")<<true<<false<<0<<QString("/tmp/data")<<QString("/tmp/data")<<QString("Owned_00112233");
  QTest::newRow("hash-single-existing-file")<<true<<false<<1<<QString("/tmp/data")<<QString("/tmp/data")<<QString("Owned_00112233");
  QTest::newRow("hash-single-existing-directory")<<true<<false<<2<<QString("/tmp/data")<<QString("/tmp/data/Owned_00112233")<<QString("Owned");
  QTest::newRow("hash-multi-absent-container")<<true<<true<<0<<QString("/tmp/data")<<QString("/tmp/data/Owned_00112233")<<QString("Owned");
  QTest::newRow("hash-multi-existing-container")<<true<<true<<2<<QString("/tmp/data")<<QString("/tmp/data/Owned_00112233")<<QString("Owned");
 }
 void originalHashNameAndObservedShape(){
  QFETCH(bool,hashed);QFETCH(bool,multi);QFETCH(int,kind);QFETCH(QString,base);QFETCH(QString,destination);QFETCH(QString,name);auto p=policy();p.addHashNames=hashed;p.defaultSaveDirectory=base.toStdString();auto g=subscriptionAutomaticGeometry(p,"Owned",hash(),multi,1,"\"='",static_cast<AutomaticDataLocationKind>(kind));QVERIFY(g);QCOMPARE(g->destinationDirectory,destination.toStdString());QCOMPARE(g->saveName,name.toStdString());QCOMPARE(g->dataLocation,(destination+"/"+name).toStdString());QCOMPARE(g->torrentFile,std::string("/tmp/profile/torrents/Owned")+(hashed?"_00112233":"")+".torrent");QVERIFY(g->autoStart&&g->queued&&!g->paused);
 }
 void secondNameConversionAndFreshStoppedPausePolicy(){
  auto p=policy();auto g=subscriptionAutomaticGeometry(p,"b",hash(),false,1,"a=b,b=c",AutomaticDataLocationKind::Directory);QVERIFY(g);QCOMPARE(g->saveName,std::string("b"));QCOMPARE(g->torrentFile,std::string("/tmp/profile/torrents/c.torrent"));
  p.defaultStartAutoPause=true;g=subscriptionAutomaticGeometry(p,"Owned",hash(),false,1,"\"='",AutomaticDataLocationKind::Directory);QVERIFY(g&&g->queued&&!g->paused);
  p.defaultStartStopped=true;g=subscriptionAutomaticGeometry(p,"Owned",hash(),false,1,"\"='",AutomaticDataLocationKind::Directory);QVERIFY(g&&g->autoStart&&!g->queued&&g->paused);p.defaultStartStopped=false;p.autoStart=false;g=subscriptionAutomaticGeometry(p,"Owned",hash(),false,1,"\"='",AutomaticDataLocationKind::Directory);QVERIFY(g&&!g->autoStart&&!g->queued&&g->paused);
 }
 void explicitUnsupportedPolicyAndUnsafeShapes(){
  auto missing=core::Document::parse("de");QVERIFY(missing);auto p=readSubscriptionAutomaticPolicy(*missing,"/tmp/profile");QVERIFY(!p);QCOMPARE(p.error(),SubscriptionAutomaticValueError::Unsupported);
  for(const auto& extra:{b("Save Torrent Files")+"i0e",b("Delete Saved Torrent Files")+"i1e",b("subscriptions.auto.dl.mark.read.days")+"i1e",b("subscriptions.auto.start.min.mb")+b("bad")}){auto d=config(extra);QVERIFY(!readSubscriptionAutomaticPolicy(d,"/tmp/profile"));}
  auto base=policy();base.defaultSaveDirectory="../unsafe";QVERIFY(!subscriptionAutomaticGeometry(base,"Owned",hash(),false,1,"\"='",AutomaticDataLocationKind::Directory));base=policy();base.addHashNames=true;QVERIFY(!subscriptionAutomaticGeometry(base,"Owned",hash(),true,1,"\"='",AutomaticDataLocationKind::RegularFile));QVERIFY(!subscriptionAutomaticGeometry(base,"Owned",hash(),false,1,"\"='",AutomaticDataLocationKind::Unsupported));
 }
};
QTEST_GUILESS_MAIN(SubscriptionAutomaticValuesTests)
#include "subscription_automatic_values_tests.moc"

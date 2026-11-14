// SPDX-License-Identifier: GPL-2.0-or-later
#include "activity_profile.hpp"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <array>
using namespace biglybtc;
namespace {
constexpr std::int64_t Now=1760000000123LL;
using A=app::ActivityProfile;
std::string b(std::string_view v){return std::to_string(v.size())+":"+std::string(v);}
core::Document doc(std::string v){auto out=core::Document::parse(std::move(v),storage::compatibleConfigLimits());if(!out){throw std::runtime_error("Generated bencode");}return std::move(*out);}
void put(core::Document& d,std::string key,std::string v){if(!d.replaceValue(key,std::move(v))){throw std::runtime_error("Generated bencode field");}}
app::ChatActivityPost post(std::string key="Generated notification room",std::string text="Generated notification room: Generated notification text",std::int64_t time=Now){return {std::move(key),std::move(text),"View",time,false};}
std::string bytes(const storage::ProfileLease& lease){auto f=storage::ConfigFile::load(lease,A::Leaf);if(!f){throw std::runtime_error(f.error().message);}return f->document().serialize();}
void seed(const storage::ProfileLease& lease,std::string entries="le",std::string removed="le"){
 auto d=doc("de");put(d,"version","i2e");put(d,"entries",std::move(entries));put(d,"removed-entries",std::move(removed));put(d,"unrelated",b("Generated opaque root"));put(d,"LastChecks","d1:xi17ee");auto f=storage::ConfigFile::createMissing(lease,A::Leaf,d.serialize());if(!f){throw std::runtime_error(f.error().message);}
}
core::Document other(std::string id,std::int64_t ts=Now){auto d=doc("de");put(d,"id",b(id));put(d,"timestamp","i"+std::to_string(ts)+"e");put(d,"typeID",b("Generated other type"));put(d,"text",b("Generated unrelated text"));put(d,"opaque",b(std::string("\0raw\xff",5)));return d;}
const app::ActivityEntry* find(const app::ActivitySnapshot& s,std::string_view id){for(const auto& row:s.entries){if(row.source.id==id){return &row;}}return nullptr;}
}
class ActivityProfileTests final:public QObject {
 Q_OBJECT
private slots:
 void malformedUnrelatedEntriesAreRetainedAndRecoveryIsReadOnly(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto bad=other("Generated bad");put(bad,"cb_data","i7e");const auto malformed=bad.serialize();seed(*lease,"l"+malformed+"e");auto snapshot=A::post(*lease,std::array{post()},Now);QVERIFY(snapshot&&snapshot->entries.size()==1);QCOMPARE(snapshot->malformed,std::size_t(1));auto f=storage::ConfigFile::load(*lease,A::Leaf);QVERIFY(f);auto encoded=core::encodeValue(f->document().find("entries")->children[0],storage::compatibleConfigLimits());QVERIFY(encoded);QCOMPARE(*encoded,malformed);const auto primary=root.path()+"/"+A::Leaf,backup=primary+".bak";QFile::remove(backup);QVERIFY(QFile::copy(primary,backup));QFile broken(primary);QVERIFY(broken.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(broken.write("broken"),qint64(6));broken.close();auto recovered=A::read(*lease,Now);QVERIFY(recovered&&recovered->recovered&&recovered->entries.size()==1);auto refused=A::post(*lease,std::array{post("Generated new room","Generated should not publish",Now+1)},Now+1);QVERIFY(!refused);QCOMPARE(refused.error().code,storage::ErrorCode::Conflict);QVERIFY(broken.open(QIODevice::ReadOnly));QCOMPARE(broken.readAll(),QByteArray("broken"));
 }
 void allowReAddDependsOnPresenceRatherThanItsStringValue(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto value=other("Generated presence policy");auto cb=doc("de");put(cb,"allowReAdd",b("false"));put(value,"cb_data",cb.serialize());seed(*lease,"l"+value.serialize()+"e");auto snapshot=A::read(*lease,Now);QVERIFY(snapshot&&snapshot->entries.size()==1);auto removed=A::remove(*lease,std::array{snapshot->entries[0].source},Now+1);QVERIFY(removed&&removed->entries.empty());auto f=storage::ConfigFile::load(*lease,A::Leaf);QVERIFY(f);QVERIFY(f->document().find("removed-entries")->children.empty());
 }

 void missingReadAndPrivatePostsDoNotCreateAProfileFile(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto empty=A::read(*lease,Now);QVERIFY(empty&&empty->entries.empty());QVERIFY(!QFile::exists(root.path()+"/"+A::Leaf));auto secret=post();secret.privateChat=true;auto denied=A::post(*lease,std::array{secret},Now);QVERIFY(!denied);QCOMPARE(denied.error().code,storage::ErrorCode::Unsupported);QVERIFY(!QFile::exists(root.path()+"/"+A::Leaf));
 }
 void actualOriginalFactoryMapMatchesAndNewerPostResetsReadAndViewed(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto added=A::post(*lease,std::array{post()},Now);QVERIFY(added&&added->entries.size()==1);const auto original=added->entries[0].source;QVERIFY(added->entries[0].publicChat());QCOMPARE(added->entries[0].timestamp,Now);QCOMPARE(added->entries[0].readOn,std::int64_t(0));QVERIFY(!added->entries[0].viewed);
  auto f=storage::ConfigFile::load(*lease,A::Leaf);QVERIFY(f);const auto* list=f->document().find("entries");QVERIFY(list&&list->children.size()==1);auto encoded=core::encodeValue(list->children[0],storage::compatibleConfigLimits());QVERIFY(encoded);QFile expected(QString::fromUtf8(BIGLYBTC_ORIGINAL_ACTIVITY_FIXTURE));QVERIFY(expected.open(QIODevice::ReadOnly));QCOMPARE(QByteArray::fromStdString(*encoded),expected.readAll());
  auto read=A::readState(*lease,std::array{original},true,Now+1);QVERIFY(read);QCOMPARE(read->entries[0].readOn,Now+1);auto viewed=A::viewed(*lease,std::array{read->entries[0].source},Now+2);QVERIFY(viewed&&viewed->entries[0].viewed);auto stale=A::readState(*lease,std::array{original},false,Now+3);QVERIFY(!stale);QCOMPARE(stale.error().code,storage::ErrorCode::Conflict);
  auto changed=A::post(*lease,std::array{post("Generated notification room","Generated newer notification",Now+4)},Now+4);QVERIFY(changed&&changed->entries.size()==1);QCOMPARE(changed->entries[0].text,std::string("Generated newer notification"));QCOMPARE(changed->entries[0].readOn,std::int64_t(0));QVERIFY(!changed->entries[0].viewed);const auto stable=bytes(*lease);auto older=A::post(*lease,std::array{post()},Now+5);QVERIFY(older);QCOMPARE(bytes(*lease),stable);
 }
 void originalUnknownFieldsAndReadDefaultsSurviveOwnedChanges(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto foreign=other("Generated other ID");auto old=other("Generated expired",Now-A::MaximumAge-1);seed(*lease,"l"+foreign.serialize()+old.serialize()+"e");const auto before=bytes(*lease);auto read=A::read(*lease,Now);QVERIFY(read&&read->entries.size()==1);QCOMPARE(read->entries[0].readOn,Now);QCOMPARE(bytes(*lease),before);auto added=A::post(*lease,std::array{post()},Now);QVERIFY(added&&added->entries.size()==2);QVERIFY(find(*added,"Generated other ID"));auto f=storage::ConfigFile::load(*lease,A::Leaf);QVERIFY(f);QCOMPARE(f->document().find("unrelated")->bytes,std::string_view("Generated opaque root"));const auto* rows=f->document().find("entries");QCOMPARE(rows->children.size(),std::size_t(3));bool exact=false;for(const auto& row:rows->children){auto value=core::encodeValue(row,storage::compatibleConfigLimits());QVERIFY(value);exact|=*value==foreign.serialize();}QVERIFY(exact);
 }
 void deletionUsesAllowReAddPresenceAndPreservesTombstones(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto foreign=other("Public: Generated blocked room");seed(*lease,"l"+foreign.serialize()+"e");auto snapshot=A::post(*lease,std::array{post()},Now);QVERIFY(snapshot);const auto* row=find(*snapshot,"Public: Generated notification room");QVERIFY(row);auto removed=A::remove(*lease,std::array{row->source},Now+1);QVERIFY(removed&&!find(*removed,"Public: Generated notification room"));auto f=storage::ConfigFile::load(*lease,A::Leaf);QVERIFY(f);QVERIFY(f->document().find("removed-entries")->children.empty());auto restored=A::post(*lease,std::array{post("Generated notification room","Generated returned",Now+2)},Now+2);QVERIFY(restored&&find(*restored,"Public: Generated notification room"));const auto* otherRow=find(*restored,"Public: Generated blocked room");QVERIFY(otherRow);removed=A::remove(*lease,std::array{otherRow->source},Now+3);QVERIFY(removed);f=storage::ConfigFile::load(*lease,A::Leaf);QVERIFY(f);const auto* dead=f->document().find("removed-entries");QCOMPARE(dead->children.size(),std::size_t(1));QCOMPARE(dead->children[0].children.size(),std::size_t(4));auto blocked=A::post(*lease,std::array{post("Generated blocked room","Generated should stay removed",Now+4)},Now+4);QVERIFY(blocked&&!find(*blocked,"Public: Generated blocked room"));
 }
 void aStaleMemberOfABatchCannotPartiallyChangeAnotherEntry(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto snapshot=A::post(*lease,std::array{post(),post("Generated second room","Generated second text",Now+1)},Now+1);QVERIFY(snapshot&&snapshot->entries.size()==2);const auto a=find(*snapshot,"Public: Generated notification room")->source,bRef=find(*snapshot,"Public: Generated second room")->source;auto changed=A::post(*lease,std::array{post("Generated second room","Generated changed",Now+2)},Now+2);QVERIFY(changed);const auto before=bytes(*lease);auto refused=A::readState(*lease,std::array{a,bRef},true,Now+3);QVERIFY(!refused);QCOMPARE(bytes(*lease),before);auto accepted=A::readState(*lease,std::array{a},false,Now+4);QVERIFY(accepted);QCOMPARE(find(*accepted,a.id)->readOn,-(Now+4));
 }
 void unsupportedVersionRecoveryAndForeignIdCollisionNeverOverwrite(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);auto foreign=other("Public: Generated notification room");seed(*lease,"l"+foreign.serialize()+"e");const auto before=bytes(*lease);auto refused=A::post(*lease,std::array{post("Generated notification room","Generated collision attempt",Now+1)},Now+1);QVERIFY(!refused);QCOMPARE(refused.error().code,storage::ErrorCode::Conflict);QCOMPARE(bytes(*lease),before);
  auto f=storage::ConfigFile::load(*lease,A::Leaf);QVERIFY(f);QVERIFY(f->document().replaceValue("version","i3e"));QVERIFY(f->save(*lease));const auto unsupported=bytes(*lease);refused=A::post(*lease,std::array{post()},Now+2);QVERIFY(!refused);QCOMPARE(bytes(*lease),unsupported);
 }
};
QTEST_GUILESS_MAIN(ActivityProfileTests)
#include "activity_profile_tests.moc"

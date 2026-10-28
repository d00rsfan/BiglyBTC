#include "mainline_checkpoint.hpp"

#include <QtTest>

#include <algorithm>
#include <array>
#include <cerrno>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
constexpr auto Leaf = "BiglyBTC-mainline.config";
QByteArray b(const QByteArray& value) { return QByteArray::number(value.size()) + ':' + value; }
QByteArray dict(const std::map<QByteArray,QByteArray>& fields) {
  QByteArray value("d"); for (const auto& [key,encoded]:fields) { value+=b(key)+encoded; } return value+'e';
}
QByteArray list(const std::vector<QByteArray>& rows) {
  QByteArray value("l"); for (const auto& row:rows) { value+=row; } return value+'e';
}
void require(bool value,const char* message) { if (!value) { throw std::runtime_error(message); } }
void write(const QString& path,const QByteArray& bytes) {
  require(QDir().mkpath(QFileInfo(path).absolutePath()),"Cannot create generated parent");
  QFile file(path); require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Cannot write generated checkpoint fixture");
  require(file.write(bytes)==bytes.size() && file.flush(),"Generated checkpoint fixture write failed");
}
QByteArray read(const QString& path) {
  QFile file(path); require(file.open(QIODevice::ReadOnly),"Cannot read generated checkpoint fixture");
  const auto result=file.readAll(); require(file.error()==QFileDevice::NoError,"Generated checkpoint fixture read failed"); return result;
}
struct Snapshot { struct stat stat{}; QByteArray bytes; };
std::optional<Snapshot> snapshot(const QString& path) {
  struct stat value{}; const auto native=QFile::encodeName(path);
  if (::lstat(native.constData(),&value)!=0) { require(errno==ENOENT,"Cannot inspect generated pathname"); return {}; }
  QByteArray content;
  if (S_ISREG(value.st_mode)) { content=read(path); }
  if (S_ISLNK(value.st_mode)) {
    std::array<char,4096> target{}; const auto count=::readlink(native.constData(),target.data(),target.size());
    require(count>=0 && static_cast<std::size_t>(count)<target.size(),"Cannot inspect generated symlink");
    content=QByteArray(target.data(),static_cast<qsizetype>(count));
  }
  return Snapshot{value,std::move(content)};
}
bool same(const std::optional<Snapshot>& a,const std::optional<Snapshot>& b) {
  if (a.has_value()!=b.has_value()) { return false; } if (!a) { return true; }
  const auto& x=a->stat; const auto& y=b->stat;
  return x.st_dev==y.st_dev && x.st_ino==y.st_ino && x.st_mode==y.st_mode && x.st_uid==y.st_uid
      && x.st_gid==y.st_gid && x.st_nlink==y.st_nlink && x.st_size==y.st_size
      && x.st_mtim.tv_sec==y.st_mtim.tv_sec && x.st_mtim.tv_nsec==y.st_mtim.tv_nsec
      && x.st_ctim.tv_sec==y.st_ctim.tv_sec && x.st_ctim.tv_nsec==y.st_ctim.tv_nsec && a->bytes==b->bytes;
}
QByteArray state(char tag='A') {
  QByteArray id(20,tag); id+=QByteArray::fromHex("7f000001");
  return dict({{"dht state",dict({{"node-id",list({b(id)})},{"nodes",list({})},{"nodes6",list({})}})}});
}
QByteArray wrapper(const QByteArray& state,const QByteArray& directory="mldht",int version=1) {
  return dict({{"version","i"+QByteArray::number(version)+'e'}, {"pluginDirectory",b(directory)}, {"state",b(state)}});
}
struct Profile {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-mainline-checkpoint-XXXXXX")};
  QString path;
  std::optional<storage::ProfileLease> lease;
  std::map<QString,std::optional<Snapshot>> original;
  Profile() {
    require(root.isValid(),"Cannot create generated profile root"); path=root.filePath("profile");
    require(QDir().mkpath(path),"Cannot create generated profile");
    auto held=storage::ProfileLease::acquire(path.toStdString()); require(held.has_value(),"Cannot hold generated profile");
    lease.emplace(std::move(*held));
  }
  QString native(const char* suffix="") const { return path+'/'+QString::fromLatin1(Leaf)+QString::fromLatin1(suffix); }
  QString plugin(const QString& directory="mldht") const { return path+"/plugins/"+directory; }
  void originalFile(const QString& relative,const QByteArray& bytes) {
    const auto file=path+'/'+relative; write(file,bytes); original[file]=snapshot(file);
  }
  void sentinels(const QString& directory="mldht") {
    originalFile("biglybt.config",QByteArray("d6:future3:")+QByteArray::fromHex("00ff51")+'e');
    originalFile("plugins/"+directory+"/baseID.config","# retained original\n0123456789ABCDEF0123456789ABCDEF01234567\n");
  }
  bool originalsIntact() const {
    for (const auto& [path,before]:original) { if (!same(before,snapshot(path))) { return false; } } return true;
  }
};
QByteArray address4(unsigned short port) {
  auto value=QByteArray::fromHex("7f000001"); value+=static_cast<char>(port>>8); value+=static_cast<char>(port&255); return value;
}
QByteArray address6(unsigned short port) {
  auto value=QByteArray::fromHex("00000000000000000000000000000001"); value+=static_cast<char>(port>>8); value+=static_cast<char>(port&255); return value;
}
// Exact original f929 Node/KBucketEntry cache field schema; no JSON surrogate
// or claim that persisted verification is fresh network evidence.
QByteArray entry(const QByteArray& address,char id,bool verified=true) {
  return dict({{"addr",b(address)},{"id",b(QByteArray(20,id))},{"created","i800e"},
      {"lastSeen","i995e"},{"lastSend","i990e"},{"failedCount","i0e"},
      {"verified",verified?QByteArray("i1e"):QByteArray("i0e")},{"future",b(QByteArray::fromHex("00ff51"))}});
}
QByteArray cache(const std::vector<QByteArray>& main,const std::vector<QByteArray>& replacements={}) {
  return dict({{"mainEntries",list(main)},{"replacements",list(replacements)},{"oldKey",b(QByteArray(20,'R'))},
      {"timestamp","i1000e"},{"log2estimate",b(QByteArray::fromHex("4008000000000000"))}, {"opaque",dict({{"x",b("unchanged")}})}});
}
}

class MainlineCheckpointTests final : public QObject {
  Q_OBJECT
private slots:
  void absentLoadAndSameStateSaveHaveNoHiddenWrites() {
    Profile p; auto loaded=storage::MainlineCheckpoint::load(*p.lease,"mldht"); QVERIFY(loaded); QVERIFY(!loaded->state());
    QVERIFY(!QFileInfo::exists(p.native())); QVERIFY(!QFileInfo::exists(p.path+"/plugins"));
    const auto first=state(); QVERIFY(loaded->save(*p.lease,first.toStdString()));
    const auto primary=snapshot(p.native()),backup=snapshot(p.native(".bak")),saving=snapshot(p.native(".saving"));
    QVERIFY(primary); QVERIFY(!backup); QVERIFY(!saving);
    QVERIFY(loaded->save(*p.lease,first.toStdString()));
    QVERIFY(same(primary,snapshot(p.native()))); QVERIFY(same(backup,snapshot(p.native(".bak")))); QVERIFY(same(saving,snapshot(p.native(".saving"))));
  }
  void wholeCandidateCasAndOpaqueWrapperFieldsArePreserved() {
    Profile p; p.sentinels(); const auto opaque=dict({{"binary",b(QByteArray::fromHex("00ff51"))},{"list","li7e3:rawe"}});
    auto source=core::Document::parse(wrapper(state()).toStdString()); QVERIFY(source); QVERIFY(source->replaceValue("future",opaque.toStdString()));
    write(p.native(),QByteArray::fromStdString(source->serialize()));
    auto first=storage::MainlineCheckpoint::load(*p.lease,"mldht"),stale=storage::MainlineCheckpoint::load(*p.lease,"mldht");
    QVERIFY(first); QVERIFY(stale); QVERIFY(first->state() && *first->state()==state().toStdString());
    QVERIFY(first->save(*p.lease,state('B').toStdString()));
    const auto committed=snapshot(p.native()),backup=snapshot(p.native(".bak"));
    // Even an unchanged requested value must pass the captured whole-file CAS.
    const auto refused=stale->save(*p.lease,state().toStdString()); QVERIFY(!refused); QCOMPARE(refused.error().code,storage::ErrorCode::Conflict);
    QVERIFY(same(committed,snapshot(p.native()))); QVERIFY(same(backup,snapshot(p.native(".bak"))));
    const auto current=core::Document::parse(read(p.native()).toStdString()); QVERIFY(current); QVERIFY(current->find("future"));
    const auto retained=core::encodeValue(*current->find("future")); QVERIFY(retained); QCOMPARE(*retained,opaque.toStdString());
    auto reload=storage::MainlineCheckpoint::load(*p.lease,"mldht"); QVERIFY(reload); QVERIFY(reload->state() && *reload->state()==state('B').toStdString());
    QVERIFY(p.originalsIntact());
  }
  void backupRecoveryAndSavingOnlyRefusalRetainOriginalInputs() {
    Profile backup; backup.sentinels(); write(backup.native(".bak"),wrapper(state()));
    const auto captured=snapshot(backup.native(".bak")); auto restored=storage::MainlineCheckpoint::load(*backup.lease,"mldht");
    QVERIFY(restored && restored->state() && *restored->state()==state().toStdString());
    QVERIFY(!QFileInfo::exists(backup.native())); QVERIFY(same(captured,snapshot(backup.native(".bak"))));
    QVERIFY(restored->save(*backup.lease,state('B').toStdString())); QVERIFY(QFileInfo::exists(backup.native())); QVERIFY(backup.originalsIntact());
    Profile saving; saving.sentinels(); write(saving.native(".saving"),wrapper(state())); const auto retained=snapshot(saving.native(".saving"));
    auto pending=storage::MainlineCheckpoint::load(*saving.lease,"mldht"); QVERIFY(pending && pending->state());
    QVERIFY(!pending->save(*saving.lease,state('B').toStdString())); QVERIFY(!pending->save(*saving.lease,state('C').toStdString()));
    QVERIFY(!QFileInfo::exists(saving.native())); QVERIFY(same(retained,snapshot(saving.native(".saving")))); QVERIFY(saving.originalsIntact());
  }
  void malformedUnknownAndUnsafeNativeCandidatesAreNeverOverwritten_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("corrupt-primary")<<0; QTest::newRow("unknown-version")<<1; QTest::newRow("foreign-plugin-directory")<<2;
    QTest::newRow("corrupt-backup-blocks-replacement")<<3; QTest::newRow("symlink-primary")<<4;
    QTest::newRow("directory-saving")<<5; QTest::newRow("oversized-state")<<6;
  }
  void malformedUnknownAndUnsafeNativeCandidatesAreNeverOverwritten() {
    QFETCH(int,kind); Profile p; p.sentinels(); const auto external=p.root.filePath("owned-external"); write(external,"owned-unchanged");
    if (kind==0) { write(p.native(),"not-bencode"); }
    if (kind==1) { write(p.native(),wrapper(state(),"mldht",2)); }
    if (kind==2) { write(p.native(),wrapper(state(),"other-mldht")); }
    if (kind==3) { write(p.native(),wrapper(state())); write(p.native(".bak"),"not-bencode"); }
    if (kind==4) { QCOMPARE(::symlink(QFile::encodeName(external).constData(),QFile::encodeName(p.native()).constData()),0); }
    if (kind==5) { QVERIFY(QDir().mkpath(p.native(".saving"))); }
    if (kind==6) { write(p.native(),wrapper(QByteArray(static_cast<qsizetype>(storage::MainlineCheckpoint::MaximumStateBytes)+1,'x'))); }
    const auto primary=snapshot(p.native()),backup=snapshot(p.native(".bak")),saving=snapshot(p.native(".saving")),outside=snapshot(external);
    QVERIFY(!storage::MainlineCheckpoint::load(*p.lease,"mldht"));
    QVERIFY(same(primary,snapshot(p.native()))); QVERIFY(same(backup,snapshot(p.native(".bak")))); QVERIFY(same(saving,snapshot(p.native(".saving"))));
    QVERIFY(same(outside,snapshot(external))); QVERIFY(p.originalsIntact());
  }
  void missingAndExistingSnapshotsCannotPublishThroughAnotherLease() {
    Profile a,b; auto missing=storage::MainlineCheckpoint::load(*a.lease,"mldht"); QVERIFY(missing);
    QVERIFY(!missing->save(*b.lease,state().toStdString())); QVERIFY(!QFileInfo::exists(b.native()));
    auto fresh=storage::MainlineCheckpoint::load(*a.lease,"mldht"); QVERIFY(fresh); QVERIFY(fresh->save(*a.lease,state().toStdString()));
    auto captured=storage::MainlineCheckpoint::load(*a.lease,"mldht"); QVERIFY(captured); const auto before=snapshot(a.native());
    QVERIFY(!captured->save(*b.lease,state('B').toStdString())); QVERIFY(same(before,snapshot(a.native()))); QVERIFY(!QFileInfo::exists(b.native()));
  }
  void legacyCacheProjectionIsNumericBoundedAndReadOnly() {
    Profile p; const QString directory="custom-ml"; p.sentinels(directory);
    p.originalFile("plugins/"+directory+"/IPv4-table.cache",cache({entry(address4(41001),'A'),entry(address4(0),'B')},{entry(address4(41002),'C',false)}));
    p.originalFile("plugins/"+directory+"/IPv6-table.cache",cache({entry(address6(42001),'D')}));
    const auto hints=storage::readLegacyMldhtHints(*p.lease,directory.toStdString(),1000); QVERIFY(hints);
    QCOMPARE(hints->decoded,std::size_t{4}); QCOMPARE(hints->rejected,std::size_t{1}); QCOMPARE(hints->endpoints.size(),std::size_t{3});
    QVERIFY(!hints->ipv4Missing && !hints->ipv6Missing);
    const auto found=[&](const char* address,unsigned short port,bool replacement,bool verified) {
      return std::ranges::any_of(hints->endpoints,[&](const auto& item){return item.address==address && item.port==port && item.replacement==replacement && item.verified==verified && item.lastSeen==995;});
    };
    QVERIFY(found("127.0.0.1",41001,false,true)); QVERIFY(found("127.0.0.1",41002,true,false)); QVERIFY(found("::1",42001,false,true));
    QVERIFY(!hints->endpoints.front().replacement); QVERIFY(p.originalsIntact()); QVERIFY(!QFileInfo::exists(p.native()));
    const auto absent=storage::readLegacyMldhtHints(*p.lease,"different-directory",1000); QVERIFY(absent);
    QVERIFY(absent->endpoints.empty() && absent->ipv4Missing && absent->ipv6Missing); QVERIFY(!QFileInfo::exists(p.plugin("different-directory")));
  }
  void legacyUnsafeChainsAndCorruptionCannotBecomeWarmHints_data() {
    QTest::addColumn<int>("kind"); QTest::newRow("symlink-plugin-directory")<<0; QTest::newRow("symlink-table")<<1;
    QTest::newRow("corrupt-table")<<2; QTest::newRow("nonregular-table")<<3;
  }
  void legacyUnsafeChainsAndCorruptionCannotBecomeWarmHints() {
    QFETCH(int,kind); Profile p; const auto external=p.root.filePath("owned-other"); QVERIFY(QDir().mkpath(external));
    const auto externalCache=external+"/IPv4-table.cache"; write(externalCache,cache({entry(address4(41001),'A')}));
    const auto plugin=p.plugin(); const auto table=plugin+"/IPv4-table.cache";
    if (kind==0) { QVERIFY(QDir().mkpath(p.path+"/plugins")); QCOMPARE(::symlink(QFile::encodeName(external).constData(),QFile::encodeName(plugin).constData()),0); }
    else { QVERIFY(QDir().mkpath(plugin)); }
    if (kind==1) { QCOMPARE(::symlink(QFile::encodeName(externalCache).constData(),QFile::encodeName(table).constData()),0); }
    if (kind==2) { write(table,"corrupt-cache"); }
    if (kind==3) { QVERIFY(QDir().mkpath(table)); }
    const auto before=snapshot(table),outside=snapshot(externalCache),directoryBefore=snapshot(plugin);
    QVERIFY(!storage::readLegacyMldhtHints(*p.lease,"mldht",1000));
    QVERIFY(same(before,snapshot(table))); QVERIFY(same(outside,snapshot(externalCache))); QVERIFY(same(directoryBefore,snapshot(plugin)));
    QVERIFY(!QFileInfo::exists(p.native()));
  }
};
QTEST_GUILESS_MAIN(MainlineCheckpointTests)
#include "mainline_checkpoint_tests.moc"

#include "mainline_identity.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using Source = biglybtc::storage::OriginalMainlineIdentity;
using State = biglybtc::storage::OriginalMainlineIdentityState;
using Stage = biglybtc::storage::OriginalMainlineIdentityCreateStage;
using Code = biglybtc::storage::ErrorCode;
enum class Random { Normal, InterruptedShort, Fail, Zero };
Random randomMode = Random::Normal;
int randomCalls = 0;
bool failRename = false, failDirectorySync = false;
QByteArray racedPrimary;
std::function<void(Stage)> action;
std::vector<Stage> stages;
const QByteArray Key("00112233445566778899AABBCCDDEEFF01234567\n");
const QString Pending = QStringLiteral("baseID.config.BiglyBTC-pending");
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
void observe(Stage stage) { stages.push_back(stage); if (action) { action(stage); } }
QByteArray read(const QString& path) {
  QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot read generated identity");
  return file.readAll();
}
void write(const QString& path, const QByteArray& bytes) {
  QFile file(path); require(file.open(QIODevice::WriteOnly|QIODevice::NewOnly), "Cannot exclusively write fixture");
  require(file.write(bytes) == bytes.size() && file.flush(), "Cannot write complete generated identity");
  file.close(); require(file.setPermissions(QFile::ReadOwner|QFile::WriteOwner), "Cannot set fixture mode");
}
struct Snapshot { struct stat stamp{}; QByteArray bytes; };
Snapshot snapshot(const QString& path) {
  Snapshot value; require(!::lstat(QFile::encodeName(path).constData(), &value.stamp), "Cannot stat generated identity");
  if (S_ISREG(value.stamp.st_mode)) { value.bytes = read(path); }
  return value;
}
bool same(const Snapshot& a, const Snapshot& b) {
  const auto& x=a.stamp; const auto& y=b.stamp;
  return x.st_dev==y.st_dev && x.st_ino==y.st_ino && x.st_mode==y.st_mode && x.st_uid==y.st_uid
      && x.st_gid==y.st_gid && x.st_nlink==y.st_nlink && x.st_size==y.st_size
      && x.st_mtim.tv_sec==y.st_mtim.tv_sec && x.st_mtim.tv_nsec==y.st_mtim.tv_nsec
      && x.st_ctim.tv_sec==y.st_ctim.tv_sec && x.st_ctim.tv_nsec==y.st_ctim.tv_nsec && a.bytes==b.bytes;
}
struct Fixture {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-mainline-create-XXXXXX")};
  QString profile, directory;
  std::optional<biglybtc::storage::ProfileLease> lease;
  explicit Fixture(bool directories=true, const QString& plugin=QStringLiteral("mldht")) {
    require(root.isValid(), "Cannot create owned root"); profile=root.filePath("profile");
    require(QDir().mkdir(profile), "Cannot create owned profile");
    require(QFile::setPermissions(profile,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner), "Cannot secure profile");
    auto held=biglybtc::storage::ProfileLease::acquire(profile.toStdString()); require(held.has_value(), "Cannot lease profile");
    lease.emplace(std::move(*held)); directory=profile+"/plugins/"+plugin;
    if (directories) {
      require(QDir().mkpath(directory), "Cannot create generated plugin dirs");
      for (const auto& path : {profile+"/plugins",directory}) {
        require(QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner), "Cannot secure plugin dirs");
      }
    }
  }
  QString leaf() const { return directory+"/baseID.config"; }
  QString pending() const { return directory+"/"+Pending; }
};
struct OwnedChild {
  pid_t pid=-1;
  ~OwnedChild() {
    if (pid>0) { ::kill(pid,SIGKILL); int status=0; while (::waitpid(pid,&status,0)<0 && errno==EINTR) {} }
  }
  int wait() {
    for (int i=0;i<300;++i) {
      int status=0; const auto done=::waitpid(pid,&status,WNOHANG);
      if (done==pid) { pid=-1; return status; }
      if (done<0 && errno!=EINTR) { pid=-1; throw std::runtime_error("Owned child wait failed"); }
      QTest::qWait(10);
    }
    throw std::runtime_error("Owned identity child exceeded fixture deadline");
  }
};
}

extern "C" ssize_t __real_getrandom(void*,std::size_t,unsigned int);
extern "C" ssize_t __wrap_getrandom(void* buffer,std::size_t bytes,unsigned int flags) {
  ++randomCalls;
  if (flags!=GRND_NONBLOCK) { errno=EINVAL; return -1; }
  if (randomMode==Random::Fail) { errno=EIO; return -1; }
  if (randomMode==Random::Zero) { std::memset(buffer,0,bytes); return static_cast<ssize_t>(bytes); }
  if (randomMode==Random::InterruptedShort) {
    if (randomCalls==1) { errno=EINTR; return -1; }
    bytes=std::min(bytes,std::size_t{7});
  }
  return __real_getrandom(buffer,bytes,flags);
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd) {
  struct stat stamp{};
  if (failDirectorySync && !::fstat(fd,&stamp) && S_ISDIR(stamp.st_mode)) {
    failDirectorySync=false; errno=EIO; return -1;
  }
  return __real_fsync(fd);
}
extern "C" int __real_renameat2(int,const char*,int,const char*,unsigned int);
extern "C" int __wrap_renameat2(int olddir,const char* oldname,int newdir,const char* newname,unsigned int flags) {
  if (flags!=RENAME_NOREPLACE) { errno=EINVAL; return -1; }
  if (failRename) { errno=ENOSYS; return -1; }
  if (!racedPrimary.isEmpty()) {
    const int fd=::open(racedPrimary.constData(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
    if (fd<0) { errno=EIO; return -1; }
    const auto count=::write(fd,"raced-original\n",15); const int closed=::close(fd);
    if (count!=15 || closed) { errno=EIO; return -1; }
    racedPrimary.clear();
  }
  return __real_renameat2(olddir,oldname,newdir,newname,flags);
}

class MainlineIdentityCreateTests final : public QObject {
  Q_OBJECT
private slots:
  void init() { randomMode=Random::Normal; randomCalls=0; failRename=false; failDirectorySync=false; racedPrimary.clear(); action={}; stages.clear(); }
  void cleanup() { action={}; randomMode=Random::Normal; failRename=false; failDirectorySync=false; racedPrimary.clear(); }
  void actualRandomCreationReturnsFreshReaderAndNeverChangesOtherFiles() {
    Fixture p(true,"custom-mldht"); write(p.directory+"/IPv4-table.cache","unknown-original-cache");
    write(p.leaf()+".bak",Key); const auto cache=snapshot(p.directory+"/IPv4-table.cache"), backup=snapshot(p.leaf()+".bak");
    auto absent=Source::load(*p.lease,"custom-mldht"); QVERIFY(absent); QCOMPARE(absent->snapshot().state,State::Missing);
    auto created=Source::createMissing(*p.lease,"custom-mldht",true,observe);
    QVERIFY2(created,created ? "" : created.error().message.c_str()); QVERIFY(randomCalls>0);
    QCOMPARE(created->snapshot().state,State::Imported); QVERIFY(created->validate(*p.lease)); QVERIFY(!absent->validate(*p.lease));
    const auto actual=snapshot(p.leaf()); QCOMPARE(actual.bytes.size(),41); QCOMPARE(actual.bytes.back(),'\n');
    for (int i=0;i<40;++i) { QVERIFY((actual.bytes[i]>='0' && actual.bytes[i]<='9') || (actual.bytes[i]>='A' && actual.bytes[i]<='F')); }
    QCOMPARE(actual.stamp.st_uid,::geteuid()); QCOMPARE(actual.stamp.st_nlink,nlink_t{1}); QCOMPARE(actual.stamp.st_mode & 07777,mode_t{0600});
    QCOMPARE(created->snapshot().originalBytes,actual.bytes.toStdString()); QVERIFY(!QFile::exists(p.pending()));
    QVERIFY(stages==std::vector<Stage>({Stage::PendingSynced,Stage::PrimaryPublished}));
    QVERIFY(same(cache,snapshot(p.directory+"/IPv4-table.cache"))); QVERIFY(same(backup,snapshot(p.leaf()+".bak")));
    const int calls=randomCalls; auto repeated=Source::createMissing(*p.lease,"custom-mldht"); QVERIFY(!repeated);
    QCOMPARE(repeated.error().code,Code::Conflict); QCOMPARE(randomCalls,calls); QVERIFY(same(actual,snapshot(p.leaf())));
  }
  void privateModeNormalizesOnlyTheNewOwnedInodeUnderStrictUmask() {
    Fixture p;
    struct Mask {
      mode_t old;
      Mask() : old(::umask(0777)) {}
      ~Mask() { ::umask(old); }
    } mask;
    auto made=Source::createMissing(*p.lease,"mldht"); QVERIFY(made);
    const auto actual=snapshot(p.leaf()); QCOMPARE(actual.stamp.st_mode & 07777,mode_t{0600});
    QVERIFY(made->validate(*p.lease));
  }
  void interruptedShortRandomAndZeroDistributionHaveNoInventedExclusion() {
    Fixture shortRead; randomMode=Random::InterruptedShort;
    auto made=Source::createMissing(*shortRead.lease,"mldht"); QVERIFY(made); QVERIFY(randomCalls>=4);
    Fixture zero; randomMode=Random::Zero;
    auto zeroKey=Source::createMissing(*zero.lease,"mldht"); QVERIFY(zeroKey);
    QCOMPARE(read(zero.leaf()),QByteArray(40,'0')+'\n'); QVERIFY(zeroKey->snapshot().root);
    QVERIFY(*zeroKey->snapshot().root==biglybtc::storage::MainlineIdentityKey{});
  }
  void disabledMissingDirectoriesAndRandomFailureHaveNoPublication() {
    Fixture missing(false); auto noDirs=Source::createMissing(*missing.lease,"mldht"); QVERIFY(!noDirs);
    QCOMPARE(noDirs.error().code,Code::Missing); QCOMPARE(randomCalls,0); QVERIFY(!QFile::exists(missing.profile+"/plugins"));
    Fixture p; write(p.leaf(),"malformed original\n"); const auto old=snapshot(p.leaf());
    auto disabled=Source::createMissing(*p.lease,"../unsafe",false); QVERIFY(!disabled);
    QCOMPARE(disabled.error().code,Code::Unsupported); QCOMPARE(randomCalls,0); QVERIFY(same(old,snapshot(p.leaf())));
    Fixture randomFailure; randomMode=Random::Fail; auto failed=Source::createMissing(*randomFailure.lease,"mldht"); QVERIFY(!failed);
    QCOMPARE(failed.error().code,Code::Io); QVERIFY(!QFile::exists(randomFailure.leaf())); QVERIFY(!QFile::exists(randomFailure.pending()));
  }
  void existingPrimaryKindsAreNeverReplaced_data() {
    QTest::addColumn<int>("kind");
    for (int i=0;i<6;++i) { QTest::newRow(qPrintable(QString::number(i))) << i; }
  }
  void existingPrimaryKindsAreNeverReplaced() {
    QFETCH(int,kind); Fixture p; const auto encoded=QFile::encodeName(p.leaf());
    if (kind<=1) { write(p.leaf(),kind==0 ? Key : QByteArray("malformed\n")); }
    else if (kind==2) { write(p.directory+"/outside",Key); QVERIFY(!::symlink("outside",encoded.constData())); }
    else if (kind==3) { write(p.leaf(),Key); QVERIFY(!::link(encoded.constData(),QFile::encodeName(p.leaf()+".alias").constData())); }
    else if (kind==4) { QVERIFY(!::mkfifo(encoded.constData(),0600)); }
    else { QVERIFY(QDir().mkdir(p.leaf())); }
    const auto old=snapshot(p.leaf()); auto failed=Source::createMissing(*p.lease,"mldht"); QVERIFY(!failed);
    QCOMPARE(randomCalls,0); QVERIFY(same(old,snapshot(p.leaf()))); QVERIFY(!QFile::exists(p.pending()));
    if (kind==2) { QCOMPARE(read(p.directory+"/outside"),Key); }
  }
  void invalidPendingKindsArePreservedWithoutGeneratingAnotherRoot_data() {
    QTest::addColumn<int>("kind"); for (int i=0;i<7;++i) { QTest::newRow(qPrintable(QString::number(i))) << i; }
  }
  void invalidPendingKindsArePreservedWithoutGeneratingAnotherRoot() {
    QFETCH(int,kind); Fixture p; const auto encoded=QFile::encodeName(p.pending());
    if (kind<=3) { write(p.pending(),kind==0 ? Key.left(20) : kind==1 ? Key.toLower() : kind==2 ? Key : QByteArray(42,'X')); }
    if (kind==2) { QVERIFY(QFile::setPermissions(p.pending(),QFile::ReadOwner|QFile::WriteOwner|QFile::ReadGroup)); }
    else if (kind==4) { write(p.directory+"/outside",Key); QVERIFY(!::symlink("outside",encoded.constData())); }
    else if (kind==5) { write(p.pending(),Key); QVERIFY(!::link(encoded.constData(),QFile::encodeName(p.pending()+".alias").constData())); }
    else if (kind==6) { QVERIFY(!::mkfifo(encoded.constData(),0600)); }
    const auto old=snapshot(p.pending()); auto failed=Source::createMissing(*p.lease,"mldht"); QVERIFY(!failed);
    QCOMPARE(randomCalls,0); QVERIFY(same(old,snapshot(p.pending()))); QVERIFY(!QFile::exists(p.leaf()));
  }
  void canonicalPendingRecoveryPreservesExactZeroKeyAndInode() {
    Fixture p; write(p.pending(),QByteArray(40,'0')+'\n'); const auto old=snapshot(p.pending());
    randomMode=Random::Fail; auto recovered=Source::createMissing(*p.lease,"mldht",true,observe); QVERIFY(recovered);
    QCOMPARE(randomCalls,0); QCOMPARE(read(p.leaf()),old.bytes); const auto actual=snapshot(p.leaf());
    QCOMPARE(actual.stamp.st_dev,old.stamp.st_dev); QCOMPARE(actual.stamp.st_ino,old.stamp.st_ino);
    QVERIFY(!QFile::exists(p.pending())); QVERIFY(recovered->validate(*p.lease));
  }
  void actualChildCrashAtDurableStagesLeavesSameRootRecoverable_data() {
    QTest::addColumn<bool>("afterPublish"); QTest::newRow("pending-synced") << false; QTest::newRow("primary-renamed") << true;
  }
  void actualChildCrashAtDurableStagesLeavesSameRootRecoverable() {
    QFETCH(bool,afterPublish); Fixture p;
    OwnedChild child; child.pid=::fork(); QVERIFY(child.pid>=0);
    if (child.pid==0) {
      action=[afterPublish](Stage stage) { if (stage==(afterPublish ? Stage::PrimaryPublished : Stage::PendingSynced)) { ::_exit(73); } };
      auto made=Source::createMissing(*p.lease,"mldht",true,observe); ::_exit(made ? 74 : 75);
    }
    const int status=child.wait(); QVERIFY(WIFEXITED(status)); QCOMPARE(WEXITSTATUS(status),73);
    const auto durable=snapshot(afterPublish ? p.leaf() : p.pending()); QCOMPARE(durable.bytes.size(),41);
    p.lease.reset(); auto reopened=biglybtc::storage::ProfileLease::acquire(p.profile.toStdString()); QVERIFY(reopened);
    randomMode=Random::Fail;
    if (afterPublish) { auto made=Source::createMissing(*reopened,"mldht"); QVERIFY(!made); QCOMPARE(made.error().code,Code::Conflict); }
    else { auto made=Source::createMissing(*reopened,"mldht"); QVERIFY(made); QCOMPARE(made->snapshot().originalBytes,durable.bytes.toStdString()); }
    QCOMPARE(randomCalls,0); auto loaded=Source::load(*reopened,"mldht"); QVERIFY(loaded); QVERIFY(loaded->validate(*reopened));
    QCOMPARE(loaded->snapshot().originalBytes,durable.bytes.toStdString()); const auto actual=snapshot(p.leaf());
    QCOMPARE(actual.stamp.st_dev,durable.stamp.st_dev); QCOMPARE(actual.stamp.st_ino,durable.stamp.st_ino);
  }
  void primaryRaceInsideActualRenameCannotBeOverwritten() {
    Fixture p; racedPrimary=QFile::encodeName(p.leaf()); auto failed=Source::createMissing(*p.lease,"mldht"); QVERIFY(!failed);
    QCOMPARE(failed.error().code,Code::Conflict); QCOMPARE(read(p.leaf()),QByteArray("raced-original\n",15));
    QVERIFY(QFile::exists(p.pending())); const auto primary=snapshot(p.leaf()), pending=snapshot(p.pending());
    auto refused=Source::createMissing(*p.lease,"mldht"); QVERIFY(!refused);
    QVERIFY(same(primary,snapshot(p.leaf()))); QVERIFY(same(pending,snapshot(p.pending())));
  }
  void swappedPendingAndAncestorCannotPublishCapturedAuthority_data() {
    QTest::addColumn<bool>("ancestor"); QTest::newRow("pending-path") << false; QTest::newRow("plugins-path") << true;
  }
  void swappedPendingAndAncestorCannotPublishCapturedAuthority() {
    QFETCH(bool,ancestor); Fixture p; QString retained;
    action=[&](Stage stage) {
      if (stage!=Stage::PendingSynced) { return; }
      if (ancestor) {
        retained=p.profile+"/retained-plugins"; require(QDir().rename(p.profile+"/plugins",retained),"Cannot swap ancestor");
        require(QDir().mkpath(p.directory),"Cannot install replacement ancestor"); write(p.directory+"/outside","unselected");
      } else {
        retained=p.pending()+".retained"; require(QFile::rename(p.pending(),retained),"Cannot swap pending"); write(p.pending(),Key);
      }
    };
    auto failed=Source::createMissing(*p.lease,"mldht",true,observe); QVERIFY(!failed); QCOMPARE(failed.error().code,Code::Conflict);
    QVERIFY(!QFile::exists(p.leaf())); QVERIFY(QFile::exists(retained));
    if (ancestor) { QVERIFY(QFile::exists(retained+"/mldht/"+Pending)); QCOMPARE(read(p.directory+"/outside"),QByteArray("unselected")); }
    else { QCOMPARE(read(p.pending()),Key); QCOMPARE(read(retained).size(),41); }
  }
  void publishedReplacementOrDirectorySyncFailureNeverReturnsFalseAuthority_data() {
    QTest::addColumn<bool>("replace"); QTest::newRow("replace-visible-primary") << true; QTest::newRow("directory-fsync-fails") << false;
  }
  void publishedReplacementOrDirectorySyncFailureNeverReturnsFalseAuthority() {
    QFETCH(bool,replace); Fixture p; QByteArray visible;
    action=[&](Stage stage) {
      if (stage!=Stage::PrimaryPublished) { return; }
      visible=read(p.leaf());
      if (replace) { require(QFile::rename(p.leaf(),p.leaf()+".retained"),"Cannot replace publication"); write(p.leaf(),Key); }
      else { failDirectorySync=true; }
    };
    auto failed=Source::createMissing(*p.lease,"mldht",true,observe); QVERIFY(!failed);
    QCOMPARE(failed.error().code,replace ? Code::Conflict : Code::Io); QVERIFY(!QFile::exists(p.pending()));
    QCOMPARE(read(p.leaf()),replace ? Key : visible);
    if (replace) { QCOMPARE(read(p.leaf()+".retained"),visible); }
    auto loaded=Source::load(*p.lease,"mldht"); QVERIFY(loaded); QVERIFY(loaded->validate(*p.lease));
  }
  void sameInodeEditWithRestoredMtimeCannotBecomeCreationAuthority_data() {
    QTest::addColumn<bool>("published"); QTest::newRow("pending") << false; QTest::newRow("published") << true;
  }
  void sameInodeEditWithRestoredMtimeCannotBecomeCreationAuthority() {
    QFETCH(bool,published); Fixture p; Snapshot changed;
    action=[&](Stage stage) {
      if (stage!=(published ? Stage::PrimaryPublished : Stage::PendingSynced)) { return; }
      const auto path=published ? p.leaf() : p.pending(); const auto before=snapshot(path);
      auto bytes=before.bytes; bytes[0]=bytes[0]=='0' ? '1' : '0';
      QFile file(path); require(file.open(QIODevice::ReadWrite),"Cannot open exact generated inode");
      require(file.write(bytes)==bytes.size() && file.flush(),"Cannot edit generated inode"); file.close();
      const std::array<timespec,2> times{{{0,UTIME_OMIT},before.stamp.st_mtim}};
      require(!::utimensat(AT_FDCWD,QFile::encodeName(path).constData(),times.data(),AT_SYMLINK_NOFOLLOW),"Cannot restore mtime");
      changed=snapshot(path); require(changed.stamp.st_ino==before.stamp.st_ino,"Fixture inode changed");
    };
    auto failed=Source::createMissing(*p.lease,"mldht",true,observe); QVERIFY(!failed); QCOMPARE(failed.error().code,Code::Conflict);
    QVERIFY(same(changed,snapshot(published ? p.leaf() : p.pending())));
    if (!published) { QVERIFY(!QFile::exists(p.leaf())); }
  }
  void observerMayDestroyBorrowedLeaseAtEitherDurableStage_data() {
    QTest::addColumn<bool>("published"); QTest::newRow("pending-synced") << false; QTest::newRow("primary-published") << true;
  }
  void observerMayDestroyBorrowedLeaseAtEitherDurableStage() {
    QFETCH(bool,published); Fixture p; bool destroyed=false;
    action=[&](Stage stage) {
      if (stage!=(published ? Stage::PrimaryPublished : Stage::PendingSynced)) { return; }
      p.lease.reset(); destroyed=true;
      auto excluded=biglybtc::storage::ProfileLease::acquire(p.profile.toStdString());
      require(!excluded && excluded.error().code==Code::Busy,"Retained transaction lost actual profile exclusion");
    };
    auto made=Source::createMissing(*p.lease,"mldht",true,observe);
    QVERIFY2(made,made ? "" : made.error().message.c_str()); QVERIFY(destroyed); QVERIFY(!p.lease);
    QCOMPARE(made->snapshot().state,State::Imported); QVERIFY(!QFile::exists(p.pending()));
    auto reopened=biglybtc::storage::ProfileLease::acquire(p.profile.toStdString()); QVERIFY(reopened);
    QVERIFY(made->validate(*reopened)); QCOMPARE(made->snapshot().originalBytes,read(p.leaf()).toStdString());
  }
  void observerCannotRebindTransactionToReplacementLockInSameProfile() {
    Fixture p; Snapshot pending; bool rebound=false;
    action=[&](Stage stage) {
      if (stage!=Stage::PendingSynced) { return; }
      pending=snapshot(p.pending());
      require(QFile::rename(p.profile+"/.azlock",p.profile+"/.retained-lock"),"Cannot replace held lock name");
      p.lease.reset();
      auto replacement=biglybtc::storage::ProfileLease::acquire(p.profile.toStdString());
      require(replacement.has_value(),"Cannot acquire distinct replacement lock");
      p.lease.emplace(std::move(*replacement)); rebound=true;
    };
    auto refused=Source::createMissing(*p.lease,"mldht",true,observe); QVERIFY(rebound); QVERIFY(!refused);
    QCOMPARE(refused.error().code,Code::Conflict); QVERIFY(!QFile::exists(p.leaf()));
    QVERIFY(same(pending,snapshot(p.pending()))); QVERIFY(p.lease->retain());
    auto fresh=Source::load(*p.lease,"mldht"); QVERIFY(fresh); QCOMPARE(fresh->snapshot().state,State::Missing);
    QVERIFY(fresh->validate(*p.lease));
  }
  void replacedHeldLockOrWritableDirectoryCannotPublish() {
    Fixture p;
    action=[&](Stage stage) {
      if (stage!=Stage::PendingSynced) { return; }
      require(QFile::rename(p.profile+"/.azlock",p.profile+"/.retained-lock"),"Cannot replace held lock name");
      write(p.profile+"/.azlock","");
    };
    auto replaced=Source::createMissing(*p.lease,"mldht",true,observe); QVERIFY(!replaced); QCOMPARE(replaced.error().code,Code::Conflict);
    QVERIFY(!QFile::exists(p.leaf())); QVERIFY(QFile::exists(p.pending()));
    action={}; Fixture writable; QVERIFY(!::chmod(QFile::encodeName(writable.directory).constData(),0770));
    const int calls=randomCalls; auto unsafe=Source::createMissing(*writable.lease,"mldht"); QVERIFY(!unsafe);
    QCOMPARE(unsafe.error().code,Code::UnsafePath); QCOMPARE(randomCalls,calls);
    QVERIFY(!QFile::exists(writable.leaf())); QVERIFY(!QFile::exists(writable.pending()));
  }
  void successfulReaderAndFailedAttemptsRetireAllTransactionDescriptors() {
    const auto descriptors=[] { return QDir("/proc/self/fd").entryList(QDir::AllEntries|QDir::NoDotAndDotDot).size(); };
    Fixture p; const auto before=descriptors();
    {
      auto made=Source::createMissing(*p.lease,"mldht"); QVERIFY(made); QCOMPARE(descriptors(),before+3);
      Fixture other; QVERIFY(!made->validate(*other.lease)); QVERIFY(made->validate(*p.lease));
    }
    QCOMPARE(descriptors(),before);
    for (int i=0;i<32;++i) { auto refused=Source::createMissing(*p.lease,"mldht"); QVERIFY(!refused); QCOMPARE(descriptors(),before); }
    Fixture failed; const auto failedBefore=descriptors(); failRename=true;
    auto refused=Source::createMissing(*failed.lease,"mldht"); QVERIFY(!refused); QCOMPARE(descriptors(),failedBefore);
    failRename=false; const auto pending=snapshot(failed.pending()); write(failed.leaf(),Key);
    refused=Source::createMissing(*failed.lease,"mldht"); QVERIFY(!refused); QCOMPARE(descriptors(),failedBefore);
    QVERIFY(same(pending,snapshot(failed.pending())));
  }
  void unsupportedAtomicRenameRetainsCanonicalPendingForExplicitRecovery() {
    Fixture p; failRename=true; auto failed=Source::createMissing(*p.lease,"mldht"); QVERIFY(!failed);
    QCOMPARE(failed.error().code,Code::Unsupported); QVERIFY(!QFile::exists(p.leaf())); const auto pending=snapshot(p.pending());
    failRename=false; randomMode=Random::Fail; const int calls=randomCalls;
    auto recovered=Source::createMissing(*p.lease,"mldht"); QVERIFY(recovered); QCOMPARE(randomCalls,calls); QCOMPARE(read(p.leaf()),pending.bytes);
  }
};
QTEST_GUILESS_MAIN(MainlineIdentityCreateTests)
#include "mainline_identity_create_tests.moc"

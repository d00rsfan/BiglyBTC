#include "profile.hpp"
#include <QTemporaryDir>
#include <QtTest>
#include <cerrno>
#include <fcntl.h>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>
using biglybtc::storage::ProfileLease;
namespace {
struct File {int value=-1;~File(){if(value>=0){::close(value);}}};
bool posixLock(int fd){struct flock request{};request.l_type=F_WRLCK;request.l_whence=SEEK_SET;return ::fcntl(fd,F_SETLK,&request)==0;}
}
class ProfileLeaseRetentionTests final:public QObject {
 Q_OBJECT
private slots:
 void retainedAliasesKeepActualJavaPosixExclusionUntilLastClose(){
  QTemporaryDir tmp;QVERIFY(tmp.isValid());const auto path=tmp.path().toStdString();
  auto acquired=ProfileLease::acquire(path);QVERIFY(acquired);
  std::optional<ProfileLease> primary{std::move(*acquired)};
  auto first=primary->retain(),second=primary->retain();QVERIFY(first);QVERIFY(second);
  struct stat original{},retained{};QVERIFY(::fstat(primary->directoryHandle(),&original)==0);QVERIFY(::fstat((*first)->directoryHandle(),&retained)==0);
  QCOMPARE(original.st_dev,retained.st_dev);QCOMPARE(original.st_ino,retained.st_ino);
  QVERIFY((::fcntl((*first)->directoryHandle(),F_GETFD)&FD_CLOEXEC)!=0);
  File java{::openat(primary->directoryHandle(),".azlock",O_RDWR|O_CLOEXEC|O_NOFOLLOW)};QVERIFY(java.value>=0);
  QVERIFY(!posixLock(java.value));QVERIFY(errno==EAGAIN||errno==EACCES);
  primary.reset();QVERIFY(!posixLock(java.value));QVERIFY(errno==EAGAIN||errno==EACCES);
  first->reset();QVERIFY(!posixLock(java.value));QVERIFY(errno==EAGAIN||errno==EACCES);
  second->reset();QVERIFY(posixLock(java.value));
 }
 void rejectedIndependentAcquisitionDoesNotReleaseRetainedOFD(){
  QTemporaryDir tmp;QVERIFY(tmp.isValid());auto acquired=ProfileLease::acquire(tmp.path().toStdString());QVERIFY(acquired);
  auto token=acquired->retain();QVERIFY(token);
  auto moved=std::move(*acquired);QVERIFY(!acquired->valid());QVERIFY(!acquired->retain());
  auto other=ProfileLease::acquire(tmp.path().toStdString());QVERIFY(!other);QCOMPARE(other.error().code,biglybtc::storage::ErrorCode::Busy);
  QVERIFY((*token)->valid());
 }
 void replacedLockNameCannotProduceNewAuthority(){
  QTemporaryDir tmp;QVERIFY(tmp.isValid());auto acquired=ProfileLease::acquire(tmp.path().toStdString());QVERIFY(acquired);
  auto old=acquired->retain();QVERIFY(old);
  const int directory=acquired->directoryHandle();QVERIFY(::renameat(directory,".azlock",directory,"old-lock")==0);
  File replacement{::openat(directory,".azlock",O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600)};QVERIFY(replacement.value>=0);
  auto denied=acquired->retain();QVERIFY(!denied);QCOMPARE(denied.error().code,biglybtc::storage::ErrorCode::Conflict);
  QVERIFY((*old)->valid());
 }
};
QTEST_GUILESS_MAIN(ProfileLeaseRetentionTests)
#include "profile_lease_retention_tests.moc"

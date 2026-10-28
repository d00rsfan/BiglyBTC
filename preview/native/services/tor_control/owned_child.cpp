// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_child.hpp"
#include "ownership.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace biglybtc::services::tor {
namespace {
struct Fd {
  int value = -1;
  explicit Fd(int fd = -1): value(fd) {}
  ~Fd() { if (value >= 0) { ::close(value); } }
  Fd(Fd&& other) noexcept: value(std::exchange(other.value, -1)) {}
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) { if (value >= 0) { ::close(value); } value = std::exchange(other.value, -1); }
    return *this;
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
bool sameIdentity(const struct stat& a, const struct stat& b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
bool sameLeaf(const struct stat& a, const struct stat& b) {
  return S_ISREG(a.st_mode) && S_ISREG(b.st_mode) && sameIdentity(a,b)
      && a.st_mode == b.st_mode && a.st_uid == b.st_uid && a.st_gid == b.st_gid
      && a.st_nlink == b.st_nlink && a.st_size == b.st_size
      && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
      && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
bool absolute(const QString& path) {
  return path.startsWith('/') && QDir::cleanPath(path) == path && !path.contains(QChar::Null)
      && !path.contains('\n') && !path.contains('\r') && path.size() <= 4096;
}
struct Directory { Fd fd; QByteArray name; struct stat stamp{}; };
bool directoryChainCurrent(const std::vector<Directory>& chain) {
  if (chain.empty()) { return false; }
  for (std::size_t i = 0; i < chain.size(); ++i) {
    struct stat held{}, named{};
    if (::fstat(chain[i].fd.value,&held) || !S_ISDIR(held.st_mode) || !sameIdentity(held,chain[i].stamp)) { return false; }
    if (i && (::fstatat(chain[i-1].fd.value,chain[i].name.constData(),&named,AT_SYMLINK_NOFOLLOW)
        || !S_ISDIR(named.st_mode) || !sameIdentity(held,named))) { return false; }
  }
  return true;
}
bool holdDirectory(const QString& path, std::vector<Directory>& chain) {
  if (!absolute(path)) { return false; }
  Directory root{Fd(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC)),{}, {}};
  if (root.fd.value < 0 || ::fstat(root.fd.value,&root.stamp)) { return false; }
  chain.push_back(std::move(root));
  if (path == QStringLiteral("/")) { return true; }
  const auto parts = path.mid(1).split('/');
  if (parts.size() > 64) { return false; }
  for (const auto& part : parts) {
    const auto name = QFile::encodeName(part);
    Directory next{Fd(::openat(chain.back().fd.value,name.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)),name,{}};
    if (next.fd.value < 0 || ::fstat(next.fd.value,&next.stamp) || !S_ISDIR(next.stamp.st_mode)) { return false; }
    chain.push_back(std::move(next));
  }
  return directoryChainCurrent(chain);
}
struct File {
  Fd fd{};
  int parent = -1;
  QByteArray name;
  struct stat stamp{};
  bool current() const {
    struct stat held{}, named{};
    return fd.value >= 0 && !::fstat(fd.value,&held)
        && !::fstatat(parent,name.constData(),&named,AT_SYMLINK_NOFOLLOW)
        && sameLeaf(stamp,held) && sameLeaf(stamp,named);
  }
};
bool privateFile(const struct stat& s) {
  return S_ISREG(s.st_mode) && s.st_uid == ::geteuid() && s.st_nlink == 1 && !(s.st_mode & 0077);
}
enum class Read { Waiting, Ready, Unsafe };
Read readFile(int parent, const char* name, qsizetype minimum, qsizetype maximum,
              File& result, QByteArray& data) {
  struct stat named{}, held{}, after{}, finalName{};
  if (::fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)) {
    return errno == ENOENT ? Read::Waiting : Read::Unsafe;
  }
  if (!privateFile(named) || named.st_size < 0 || named.st_size > maximum) { return Read::Unsafe; }
  if (named.st_size < minimum) { return Read::Waiting; }
  Fd fd(::openat(parent,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
  if (fd.value < 0 || ::fstat(fd.value,&held) || !sameLeaf(named,held)) { return Read::Unsafe; }
  data.resize(static_cast<qsizetype>(held.st_size));
  qsizetype done = 0;
  while (done < data.size()) {
    const auto count = ::pread(fd.value,data.data()+done,static_cast<size_t>(data.size()-done),done);
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { return Read::Unsafe; }
    done += count;
  }
  if (::fstat(fd.value,&after) || ::fstatat(parent,name,&finalName,AT_SYMLINK_NOFOLLOW)) { return Read::Unsafe; }
  if (!sameLeaf(held,after) || !sameLeaf(held,finalName)) { return Read::Waiting; }
  result = File{std::move(fd),parent,QByteArray(name),after};
  return Read::Ready;
}
bool writeNew(int parent, const char* name, const QByteArray& data, File& result) {
  Fd fd(::openat(parent,name,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
  if (fd.value < 0) { return false; }
  qsizetype done = 0;
  while (done < data.size()) {
    const auto count = ::write(fd.value,data.constData()+done,static_cast<size_t>(data.size()-done));
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { return false; }
    done += count;
  }
  struct stat stamp{};
  if (::fsync(fd.value) || ::fstat(fd.value,&stamp) || !privateFile(stamp) || stamp.st_size != data.size()) { return false; }
  result = File{std::move(fd),parent,QByteArray(name),stamp};
  return result.current();
}
}

struct OwnedTorChild::Impl {
  using Clock = std::chrono::steady_clock;
  OwnedTorChild* q;
  QProcess process;
  QTimer discovery, deadline, escalation;
  QFileSystemWatcher watcher;
  std::unique_ptr<OwnershipHandshake> handshake;
  std::vector<Directory> chain, executableChain;
  Fd dataDirectory;
  File executable, config, cookie, portFile;
  OwnedTorChildOptions options;
  State state = State::Idle;
  Clock::time_point expires{};
  std::uint64_t generation = 0;
  qint64 pid = 0;
  quint16 port = 0;
  bool reaped = true, stoppingFailure = false, destroying = false;
  QString reason;
  QByteArray version;

  explicit Impl(OwnedTorChild* owner): q(owner),process(owner),discovery(owner),deadline(owner),
      escalation(owner),watcher(owner) {
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.setStandardInputFile(QProcess::nullDevice());
    discovery.setInterval(20); deadline.setSingleShot(true); escalation.setSingleShot(true);
    QObject::connect(&discovery,&QTimer::timeout,q,[this]{ inspect(); });
    QObject::connect(&deadline,&QTimer::timeout,q,[this]{ stopWithFailure(QStringLiteral("Owned Tor startup timed out")); });
    QObject::connect(&escalation,&QTimer::timeout,q,[this]{
      if (state == State::Stopping && process.state() != QProcess::NotRunning) { process.kill(); }
    });
    QObject::connect(&watcher,&QFileSystemWatcher::fileChanged,q,[this]{ changed(); });
    QObject::connect(&watcher,&QFileSystemWatcher::directoryChanged,q,[this]{ changed(); });
    QObject::connect(&process,&QProcess::started,q,[this]{
      pid = process.processId();
      if (state == State::Stopping) { process.kill(); return; }
      if (state != State::Starting || pid <= 0 || !baseCurrent()) {
        stopWithFailure(QStringLiteral("Owned Tor launch identity changed")); return;
      }
      if (change(State::Discovering)) { inspect(); }
    });
    QObject::connect(&process,&QProcess::finished,q,[this](int,QProcess::ExitStatus){ finished(); });
    QObject::connect(&process,&QProcess::errorOccurred,q,[this](QProcess::ProcessError error){
      if (error == QProcess::FailedToStart) {
        stoppingFailure = true; reason = QStringLiteral("Owned Tor executable could not start");
        if (process.state() == QProcess::NotRunning) { finished(); }
        else { stopWithFailure(reason); }
      }
    });
  }
  ~Impl() {
    destroying = true;
    QObject::disconnect(&process,nullptr,q,nullptr);
    QObject::disconnect(&watcher,nullptr,q,nullptr);
    discovery.stop(); deadline.stop(); escalation.stop();
    if (handshake) { handshake->blockSignals(true); handshake->stop(); handshake.reset(); }
    if (process.state() != QProcess::NotRunning) {
      process.kill(); process.waitForFinished(3000); // QProcess owns/reaps only its actual child.
    }
  }
  bool change(State next) {
    state = next; const auto at = generation; QPointer<OwnedTorChild> guard(q);
    emit q->stateChanged(next);
    return guard && guard->impl_.get() == this && generation == at && state == next;
  }
  bool directoriesCurrent() const {
    if (chain.size() < 2 || !directoryChainCurrent(chain) || !directoryChainCurrent(executableChain)) { return false; }
    for (const auto i : {chain.size()-2,chain.size()-1}) {
      struct stat current{};
      if (::fstat(chain[i].fd.value,&current) || current.st_uid != ::geteuid() || (current.st_mode & 0077)) { return false; }
    }
    struct stat held{}, named{};
    return !::fstat(dataDirectory.value,&held) && S_ISDIR(held.st_mode) && held.st_uid == ::geteuid() && !(held.st_mode & 0077)
        && !::fstatat(chain.back().fd.value,"data",&named,AT_SYMLINK_NOFOLLOW)
        && S_ISDIR(named.st_mode) && sameIdentity(held,named);
  }
  bool baseCurrent() const { return directoriesCurrent() && executable.current() && config.current(); }
  bool alive() const { return !reaped && pid > 0 && process.state() == QProcess::Running && process.processId() == pid; }
  bool sourceCurrent() const { return alive() && baseCurrent() && cookie.current() && portFile.current(); }
  bool prepare() {
    if (!absolute(options.executable) || !absolute(options.workingDirectory)) { return false; }
    // Pin the executable itself. Execution uses this FD, not a later pathname lookup.
    if (!holdDirectory(QFileInfo(options.executable).absolutePath(),executableChain)) { return false; }
    executable.parent = executableChain.back().fd.value;
    executable.name = QFile::encodeName(QFileInfo(options.executable).fileName());
    executable.fd = Fd(::openat(executable.parent,executable.name.constData(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    if (executable.fd.value < 0 || ::fstat(executable.fd.value,&executable.stamp)
        || !S_ISREG(executable.stamp.st_mode) || executable.stamp.st_nlink != 1
        || (executable.stamp.st_uid != ::geteuid() && executable.stamp.st_uid != 0)
        || (executable.stamp.st_mode & 0022) || !(executable.stamp.st_mode & 0111) || !executable.current()) { return false; }
    const auto parts = options.workingDirectory.mid(1).split('/');
    if (parts.isEmpty() || parts.size() > 64 || parts.back().isEmpty()) { return false; }
    chain.push_back(Directory{Fd(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC)),{}, {}});
    if (chain[0].fd.value < 0 || ::fstat(chain[0].fd.value,&chain[0].stamp)) { return false; }
    for (qsizetype i = 0; i < parts.size(); ++i) {
      const auto name = QFile::encodeName(parts[i]);
      if (name.isEmpty() || name.size() > 255 || name == "." || name == "..") { return false; }
      const int parent = chain.back().fd.value;
      if (i == parts.size()-1) {
        const auto& parentStamp = chain.back().stamp;
        if (parentStamp.st_uid != ::geteuid() || (parentStamp.st_mode & 0077)
            || ::mkdirat(parent,name.constData(),0700)) { return false; }
      }
      Directory next{Fd(::openat(parent,name.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)),name,{}};
      if (next.fd.value < 0 || ::fstat(next.fd.value,&next.stamp) || !S_ISDIR(next.stamp.st_mode)) { return false; }
      chain.push_back(std::move(next));
    }
    const int root = chain.back().fd.value;
    if (::mkdirat(root,"data",0700)) { return false; }
    dataDirectory = Fd(::openat(root,"data",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
    const QByteArray contents = "DisableNetwork 1\nSocksPort 0\nControlPort 127.0.0.1:auto\n"
        "ControlPortWriteToFile control.port\nControlPortFileGroupReadable 0\n"
        "CookieAuthentication 1\nCookieAuthFile data/control_auth_cookie\nCookieAuthFileGroupReadable 0\n"
        "DataDirectory data\nRunAsDaemon 0\n";
    return dataDirectory.value >= 0 && writeNew(root,"torrc",contents,config)
        && !::fsync(dataDirectory.value) && !::fsync(root) && directoriesCurrent();
  }
  bool start(const OwnedTorChildOptions& requested) {
    if (QThread::currentThread() != q->thread() || !reaped || process.state() != QProcess::NotRunning
        || (state != State::Idle && state != State::Stopped && state != State::Failed)) { return false; }
    if (requested.startupMilliseconds < 1 || requested.startupMilliseconds > 30000
        || requested.shutdownGraceMilliseconds < 1 || requested.shutdownGraceMilliseconds > 3000
        || generation == std::numeric_limits<std::uint64_t>::max()) { return false; }
    ++generation; options = requested; pid = 0; port = 0; stoppingFailure = false; reason.clear(); version.clear();
    const auto files = watcher.files(), dirs = watcher.directories();
    if (!files.isEmpty()) { watcher.removePaths(files); }
    if (!dirs.isEmpty()) { watcher.removePaths(dirs); }
    handshake.reset(); chain.clear(); executableChain.clear(); dataDirectory = Fd{};
    executable = File{}; config = File{}; cookie = File{}; portFile = File{};
    reaped = false; // Current attempt; a refusal below proves no child was launched.
    if (!prepare()) { stoppingFailure = true; reason = QStringLiteral("Owned Tor source or fresh directory refused"); finished(); return false; }
    expires = Clock::now()+std::chrono::milliseconds(options.startupMilliseconds);
    deadline.start(options.startupMilliseconds); discovery.start();
    const int executableFd = executable.fd.value, directoryFd = chain.back().fd.value;
    process.setProgram(QStringLiteral("/proc/self/fd/")+QString::number(executableFd));
    process.setArguments({QStringLiteral("-f"),QStringLiteral("torrc"),QStringLiteral("__OwningControllerProcess"),QString::number(QCoreApplication::applicationPid())});
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("PATH"),QStringLiteral("/usr/bin:/bin"));
    environment.insert(QStringLiteral("LANG"),QStringLiteral("C.UTF-8"));
    environment.insert(QStringLiteral("HOME"),options.workingDirectory);
    environment.insert(QStringLiteral("XDG_CONFIG_HOME"),options.workingDirectory);
    environment.insert(QStringLiteral("XDG_CACHE_HOME"),options.workingDirectory);
    environment.insert(QStringLiteral("TMPDIR"),options.workingDirectory);
    process.setProcessEnvironment(environment); // No inherited loader/Python/Tor/profile overrides.
    process.setChildProcessModifier([executableFd,directoryFd]{
      // Only async-signal-safe syscalls after fork. Script interpreters need the
      // pinned executable descriptor across exec; the work directory stays held.
      if (::fchdir(directoryFd) || ::fcntl(executableFd,F_SETFD,0) < 0) { ::_exit(126); }
    });
    if (!change(State::Starting)) {
      // A synchronous state listener may stop/delete before launch. No launch
      // occurs after cancellation, and no external PID is ever accepted.
      return true;
    }
    process.start();
    return true;
  }
  void inspect() {
    if (state != State::Starting && state != State::Discovering) { return; }
    if (Clock::now() >= expires) { stopWithFailure(QStringLiteral("Owned Tor startup timed out")); return; }
    if (!baseCurrent()) { stopWithFailure(QStringLiteral("Owned Tor source changed")); return; }
    if (state != State::Discovering || !alive()) { return; }
    QByteArray cookieBytes, portBytes;
    const auto cookieRead = readFile(dataDirectory.value,"control_auth_cookie",32,32,cookie,cookieBytes);
    const auto portRead = readFile(chain.back().fd.value,"control.port",1,128,portFile,portBytes);
    if (cookieRead == Read::Unsafe || portRead == Read::Unsafe) {
      cookieBytes.fill(0); stopWithFailure(QStringLiteral("Owned Tor discovery file refused")); return;
    }
    if (cookieRead != Read::Ready || portRead != Read::Ready || !portBytes.endsWith('\n')) { cookieBytes.fill(0); return; }
    static const QRegularExpression pattern(QStringLiteral("\\APORT=127\\.0\\.0\\.1:([1-9][0-9]{0,4})\\r?\\n\\z"));
    const auto match = pattern.match(QString::fromLatin1(portBytes));
    bool ok = false; const auto value = match.hasMatch() ? match.captured(1).toUInt(&ok) : 0;
    if (!ok || value > 65535 || !sourceCurrent()) {
      cookieBytes.fill(0); stopWithFailure(QStringLiteral("Owned Tor control endpoint refused")); return;
    }
    port = static_cast<quint16>(value); discovery.stop();
    QByteArray nonce(32,'\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()),32) != 1) {
      cookieBytes.fill(0); nonce.fill(0); stopWithFailure(QStringLiteral("Owned Tor nonce generation failed")); return;
    }
    handshake = std::make_unique<OwnershipHandshake>(q);
    const auto at = generation;
    QObject::connect(handshake.get(),&OwnershipHandshake::ready,q,[this,at]{
      if (at != generation || state != State::Authenticating) { return; }
      if (Clock::now() >= expires || !sourceCurrent()) { stopWithFailure(QStringLiteral("Owned Tor authority changed during authentication")); return; }
      version = handshake->rawVersionInfo(); deadline.stop();
      const auto unavailable = watcher.addPaths({options.workingDirectory,options.workingDirectory+QStringLiteral("/data"),
          options.workingDirectory+QStringLiteral("/torrc"),options.workingDirectory+QStringLiteral("/control.port"),
          options.workingDirectory+QStringLiteral("/data/control_auth_cookie"),options.executable});
      if (!unavailable.isEmpty() || !sourceCurrent()) { stopWithFailure(QStringLiteral("Owned Tor source watch refused")); return; }
      if (change(State::Ready)) { emit q->ready(); }
    });
    QObject::connect(handshake.get(),&OwnershipHandshake::failed,q,[this,at]{
      if (at == generation && state == State::Authenticating) { stopWithFailure(QStringLiteral("Owned Tor control setup failed")); }
    });
    if (!change(State::Authenticating)) { cookieBytes.fill(0); nonce.fill(0); return; }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(expires-Clock::now()).count();
    if (remaining <= 0) { cookieBytes.fill(0); nonce.fill(0); stopWithFailure(QStringLiteral("Owned Tor startup timed out")); return; }
    QPointer<OwnedTorChild> guard(q);
    const bool accepted = handshake->beginOwnedPeer(port,cookieBytes,nonce,static_cast<int>(remaining));
    OPENSSL_cleanse(cookieBytes.data(),static_cast<std::size_t>(cookieBytes.size()));
    OPENSSL_cleanse(nonce.data(),static_cast<std::size_t>(nonce.size()));
    if (guard && guard->impl_.get() == this && generation == at && !accepted && state == State::Authenticating) {
      stopWithFailure(QStringLiteral("Owned Tor control setup refused"));
    }
  }
  void changed() {
    if ((state == State::Ready || state == State::Authenticating) && !sourceCurrent()) {
      stopWithFailure(QStringLiteral("Owned Tor retained source changed"));
    }
  }
  void stopWithFailure(const QString& text) { stoppingFailure = true; reason = text; stop(); }
  void stop() {
    if (state == State::Stopped || state == State::Failed || state == State::Stopping) { return; }
    deadline.stop(); discovery.stop();
    if (handshake) { handshake->blockSignals(true); handshake->stop(); }
    if (!change(State::Stopping)) { return; }
    if (process.state() == QProcess::NotRunning) { finished(); return; }
    process.terminate(); escalation.start(options.shutdownGraceMilliseconds);
  }
  void finished() {
    if (destroying || (reaped && (state == State::Stopped || state == State::Failed))) { return; }
    if (process.state() != QProcess::NotRunning) { return; }
    reaped = true; deadline.stop(); discovery.stop(); escalation.stop();
    if (handshake) { handshake->blockSignals(true); handshake->stop(); }
    const auto files = watcher.files(), dirs = watcher.directories();
    if (!files.isEmpty()) { watcher.removePaths(files); }
    if (!dirs.isEmpty()) { watcher.removePaths(dirs); }
    // Retain proof only while its actual child is live. A terminal controller
    // object may remain alive without keeping executable/cookie/directory FDs.
    executable = File{}; config = File{}; cookie = File{}; portFile = File{};
    dataDirectory = Fd{}; chain.clear(); executableChain.clear();
    if (state != State::Stopping) { stoppingFailure = true; if (reason.isEmpty()) { reason = QStringLiteral("Owned Tor child exited"); } }
    QPointer<OwnedTorChild> guard(q); const auto at = generation;
    if (!change(stoppingFailure ? State::Failed : State::Stopped)) { return; }
    if (stoppingFailure) { emit q->failed(reason); }
    if (guard && guard->impl_.get() == this && generation == at) { emit q->finished(); }
  }
};

OwnedTorChild::OwnedTorChild(QObject* parent): QObject(parent),impl_(std::make_unique<Impl>(this)) {}
OwnedTorChild::~OwnedTorChild() = default;
bool OwnedTorChild::start(const OwnedTorChildOptions& options) { return impl_->start(options); }
void OwnedTorChild::stop() { if (QThread::currentThread() == thread()) { impl_->stop(); } }
OwnedTorChild::State OwnedTorChild::state() const { return impl_->state; }
bool OwnedTorChild::joined() const { return impl_->reaped && impl_->process.state() == QProcess::NotRunning; }
qint64 OwnedTorChild::processId() const { return impl_->pid; }
quint16 OwnedTorChild::controlPort() const { return impl_->state == State::Ready ? impl_->port : 0; }
QByteArray OwnedTorChild::rawVersionInfo() const { return impl_->version; }
bool OwnedTorChild::sourceCurrent() const { return impl_->sourceCurrent(); }
}

#include "minute_history_writer.hpp"
#include "file_io.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace biglybtc::storage {
namespace {
using Result = std::expected<void, Error>;
Result error(ErrorCode code, const char* message) { return std::unexpected(Error{code, message}); }
Result cancelled() { return error(ErrorCode::Cancelled, "Minute history cancelled before mutation"); }
Result domainError(core::MinuteHistoryError code) {
  using E = core::MinuteHistoryError;
  switch (code) {
    case E::UnknownProvider: return error(ErrorCode::Unsupported, "Minute history requires explicit peer and DHT observations");
    case E::Pending: return error(ErrorCode::Busy, "Minute history already has a pending plan");
    case E::Poisoned: return error(ErrorCode::Conflict, "Minute history writer is poisoned after uncertain IO; automatic retry is refused");
    case E::WrongIdentity: case E::StalePlan: case E::Active: case E::Inactive:
      return error(ErrorCode::Conflict, "Minute history session or plan does not match recorder state");
    default: return error(ErrorCode::InvalidFormat, "Minute history request is invalid, overflowing or exhausted");
  }
}
bool sameObject(const struct stat& a, const struct stat& b) {
  return (a.st_mode & S_IFMT) == (b.st_mode & S_IFMT) && a.st_dev == b.st_dev && a.st_ino == b.st_ino
      && a.st_uid == b.st_uid && a.st_gid == b.st_gid && a.st_mode == b.st_mode;
}
bool sameFile(const struct stat& a, const struct stat& b) {
  return sameObject(a, b) && a.st_nlink == b.st_nlink && a.st_size == b.st_size
      && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
      && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
bool safeFile(const struct stat& value) {
  return S_ISREG(value.st_mode) && value.st_uid == ::geteuid() && value.st_nlink == 1 && value.st_size >= 0;
}
std::string digits(unsigned value, std::size_t width) {
  std::string result(width, '0');
  for (std::size_t i = width; i; --i) { result[i - 1] = static_cast<char>('0' + value % 10); value /= 10; }
  return result;
}
struct Transaction {
  core::MinuteHistoryRecorder& recorder;
  const core::MinuteHistoryPlan& plan;
  std::stop_token stop;
  bool mutation = false, finished = false;
  ~Transaction() {
    if (!finished) {
      (void)recorder.finish(plan, mutation ? core::MinuteHistoryIoResult::Indeterminate
                                         : core::MinuteHistoryIoResult::CancelledBeforeIo);
    }
  }
  bool cancelledBeforeMutation() const { return !mutation && stop.stop_requested(); }
  bool beginMutation() {
    if (cancelledBeforeMutation()) { return false; }
    mutation = true; return true;
  }
};
struct Tree {
  const ProfileLease& lease;
  int root;
  struct stat rootIdentity{};
  std::array<std::string, 3> names;
  std::array<detail::Descriptor, 3> dirs{detail::Descriptor{-1}, detail::Descriptor{-1}, detail::Descriptor{-1}};
  std::array<struct stat, 3> identities{};
  Result verify(std::size_t count = 3) const {
    struct stat current{};
    if (!lease.valid() || lease.directoryHandle() != root || ::fstat(root, &current)
        || !sameObject(rootIdentity, current)) {
      return error(ErrorCode::Conflict, "Held minute history profile changed");
    }
    int parent = root;
    for (std::size_t i = 0; i < count; ++i) {
      struct stat named{};
      if (::fstat(dirs[i].get(), &current) || ::fstatat(parent, names[i].c_str(), &named, AT_SYMLINK_NOFOLLOW)
          || !sameObject(identities[i], current) || !sameObject(current, named)) {
        return error(ErrorCode::Conflict, "Minute history directory changed");
      }
      parent = dirs[i].get();
    }
    return {};
  }
  Result open(Transaction& transaction) {
    if (::fstat(root, &rootIdentity) || !S_ISDIR(rootIdentity.st_mode) || rootIdentity.st_uid != ::geteuid()) {
      return error(ErrorCode::UnsafePath, "Minute history requires an owned held profile directory");
    }
    int parent = root;
    for (std::size_t i = 0; i < dirs.size(); ++i) {
      if (transaction.cancelledBeforeMutation()) { return cancelled(); }
      bool created = false;
      dirs[i] = detail::Descriptor(::openat(parent, names[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
      if (dirs[i].get() < 0 && errno == ENOENT) {
        const auto stable = verify(i); if (!stable) { return stable; }
        if (!transaction.beginMutation()) { return cancelled(); }
        if (::mkdirat(parent, names[i].c_str(), 0700)) {
          return error(errno == EEXIST ? ErrorCode::Conflict : ErrorCode::Io, "Cannot exclusively create minute history directory");
        }
        created = true;
        dirs[i] = detail::Descriptor(::openat(parent, names[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
      }
      if (dirs[i].get() < 0) {
        return error(errno == ELOOP || errno == ENOTDIR ? ErrorCode::UnsafePath : ErrorCode::Io,
            "Cannot safely open minute history directory");
      }
      if (::fstat(dirs[i].get(), &identities[i]) || !S_ISDIR(identities[i].st_mode)
          || identities[i].st_uid != ::geteuid()) {
        return error(ErrorCode::UnsafePath, "Minute history directory must be owned");
      }
      if (created && (identities[i].st_mode & 0777) != 0700) {
        if (::fchmod(dirs[i].get(), 0700) || ::fstat(dirs[i].get(), &identities[i])) {
          return error(ErrorCode::Io, "Cannot set private minute history directory permissions");
        }
      }
      const auto stable = verify(i + 1); if (!stable) { return stable; }
      if (created && (::fsync(dirs[i].get()) || ::fsync(parent))) {
        return error(ErrorCode::Io, "Cannot synchronize newly created minute history directory");
      }
      parent = dirs[i].get();
    }
    return verify();
  }
};
Result projection(std::string_view bytes, core::HistoryDate date) {
  const auto valid = core::readSavedHistoryDay(bytes, date);
  if (valid) { return {}; }
  if (valid.error() == core::SavedHistoryError::UnsupportedVersion) {
    return error(ErrorCode::Unsupported, "Minute history file version is unsupported");
  }
  return error(ErrorCode::InvalidFormat, "Minute history file is invalid, overflowing or exceeds record limits");
}
Result append(const ProfileLease& lease, const core::MinuteHistoryAction& action, Transaction& transaction) {
  const auto date = action.date();
  Tree tree{lease, lease.directoryHandle(), {}, {"stats", digits(static_cast<unsigned>(date.year), 4), digits(date.month, 2)}};
  auto ready = tree.open(transaction); if (!ready) { return ready; }
  const auto leaf = digits(date.day, 2) + ".dat";
  const int parent = tree.dirs.back().get();
  if (transaction.cancelledBeforeMutation()) { return cancelled(); }
  detail::Descriptor file(::openat(parent, leaf.c_str(), O_RDWR | O_APPEND | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
  bool created = false;
  if (file.get() < 0 && errno == ENOENT) {
    ready = tree.verify(); if (!ready) { return ready; }
    if (!transaction.beginMutation()) { return cancelled(); }
    file = detail::Descriptor(::openat(parent, leaf.c_str(),
        O_RDWR | O_APPEND | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600));
    created = true;
  }
  if (file.get() < 0) {
    return error(errno == ELOOP || errno == EISDIR ? ErrorCode::UnsafePath : errno == EEXIST ? ErrorCode::Conflict : ErrorCode::Io,
        "Cannot safely open minute history append file");
  }
  struct stat before{}, named{};
  if (::fstat(file.get(), &before) || !safeFile(before)) {
    return error(ErrorCode::UnsafePath, "Minute history must be an owned single-link regular file");
  }
  if (created && (before.st_mode & 0777) != 0600) {
    if (::fchmod(file.get(), 0600) || ::fstat(file.get(), &before)) {
      return error(ErrorCode::Io, "Cannot set private minute history file permissions");
    }
  }
  if (static_cast<std::uint64_t>(before.st_size) > core::SavedHistoryMaximumBytes
      || action.bytes().size() > core::SavedHistoryMaximumBytes - static_cast<std::size_t>(before.st_size)) {
    return error(ErrorCode::InvalidFormat, "Minute history append exceeds the day-file byte limit");
  }
  std::string bytes(static_cast<std::size_t>(before.st_size), '\0');
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    if (transaction.cancelledBeforeMutation()) { return cancelled(); }
    const auto count = ::pread(file.get(), bytes.data() + offset, bytes.size() - offset, static_cast<off_t>(offset));
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { return error(ErrorCode::Io, "Minute history read before append was incomplete"); }
    offset += static_cast<std::size_t>(count);
  }
  if (!bytes.empty() && bytes.back() != '\n') {
    return error(ErrorCode::InvalidFormat, "Minute history has an unterminated tail; append refused");
  }
  auto valid = projection(bytes, date); if (!valid) { return valid; }
  bytes.append(action.bytes());
  valid = projection(bytes, date); if (!valid) { return valid; }
  struct stat observed{};
  if (::fstat(file.get(), &observed) || !sameFile(before, observed)
      || ::fstatat(parent, leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) || !sameFile(observed, named)) {
    return error(ErrorCode::Conflict, "Minute history file changed before append");
  }
  ready = tree.verify(); if (!ready) { return ready; }
  if (!transaction.beginMutation()) { return cancelled(); }
  offset = 0;
  while (offset < action.bytes().size()) {
    const auto count = ::write(file.get(), action.bytes().data() + offset, action.bytes().size() - offset);
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { return error(ErrorCode::Io, "Minute history append may be partial; recorder poisoned"); }
    offset += static_cast<std::size_t>(count);
  }
  struct stat written{};
  if (::fstat(file.get(), &written) || !sameObject(before, written) || !safeFile(written)
      || static_cast<std::uint64_t>(written.st_size) != bytes.size()) {
    return error(ErrorCode::Conflict, "Minute history file changed during append; recorder poisoned");
  }
  if (::fsync(file.get())) { return error(ErrorCode::Io, "Minute history file synchronization failed; recorder poisoned"); }
  if (created && ::fsync(parent)) {
    return error(ErrorCode::Io, "Minute history file was created but parent synchronization failed; recorder poisoned");
  }
  if (::fstat(file.get(), &observed) || !sameFile(written, observed)
      || ::fstatat(parent, leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) || !sameFile(observed, named)) {
    return error(ErrorCode::Conflict, "Minute history file changed after append; recorder poisoned");
  }
  ready = tree.verify(); if (!ready) { return ready; }
  if (::close(file.release())) { return error(ErrorCode::Io, "Minute history close failed; recorder poisoned"); }
  return {};
}
} // namespace

std::expected<void, Error> writeMinuteHistory(const ProfileLease& lease, core::MinuteHistoryRecorder& recorder,
    const core::MinuteHistoryRequest& request, std::stop_token stop, MinuteHistoryWriteReceipt* receipt) {
  if (receipt) { *receipt = {}; }
  if (stop.stop_requested()) { return cancelled(); }
  if (!lease.valid()) { return error(ErrorCode::UnsafePath, "Minute history requires a held profile lease"); }
  auto plan = recorder.prepare(request);
  if (!plan) { return domainError(plan.error()); }
  Transaction transaction{recorder, *plan, stop};
  std::size_t appended = 0;
  try {
    for (const auto& action : plan->actions()) {
      if (transaction.cancelledBeforeMutation()) { return cancelled(); }
      if (action.kind() == core::MinuteHistoryActionKind::Close) { continue; }
      auto result = append(lease, action, transaction);
      if (!result) { return result; }
      ++appended;
    }
    if (transaction.cancelledBeforeMutation()) { return cancelled(); }
    const auto committed = recorder.finish(*plan, core::MinuteHistoryIoResult::Complete);
    transaction.finished = true;
    if (!committed) { return domainError(committed.error()); }
    if (receipt) { receipt->appendedActions = appended; }
    return {};
  } catch (const std::bad_alloc&) {
    return error(ErrorCode::Io, "Insufficient memory while appending minute history");
  }
}
} // namespace biglybtc::storage

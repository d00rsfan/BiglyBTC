#include "minute_history_writer.hpp"
#include "state_file_io.hpp"

#include <array>
#include <new>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace biglybtc::storage {
namespace {
using namespace detail;
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
bool sameObjectAfterAppend(const StateStamp& a, const StateStamp& b) {
#ifdef _WIN32
  constexpr auto ModifiedByWrite = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL;
  const bool sameMode = (a.mode & ~ModifiedByWrite) == (b.mode & ~ModifiedByWrite);
#else
  const bool sameMode = a.mode == b.mode;
#endif
  return sameStateIdentity(a, b) && sameMode && a.owner == b.owner && a.group == b.group
      && a.owned == b.owned && a.regular == b.regular;
}
bool safeFile(const StateStamp& value) {
  return value.regular && value.owned && value.links == 1 && value.size >= 0;
}

Result setNewPermissions(StateHandle file, unsigned mode, StateStamp& info) {
#ifndef _WIN32
  if ((info.mode & 0777) != mode) {
    if (::fchmod(file, static_cast<mode_t>(mode)) != 0) {
      return error(ErrorCode::Io, "Cannot set private minute history permissions");
    }
    const auto updated = stateStamp(file);
    if (!updated) { return std::unexpected(updated.error()); }
    info = *updated;
  }
#else
  (void)file;
  (void)mode;
  (void)info;
#endif
  return {};
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
  StateHandle root;
  StateStamp rootIdentity{};
  std::array<std::string, 3> names;
  std::array<StateDescriptor, 3> dirs{};
  std::array<StateStamp, 3> identities{};

  Result verify(std::size_t count = 3) const {
    const auto held = stateStamp(root);
    if (!lease.valid() || lease.directoryHandle() != root || !held ||
        !sameStateDirectory(rootIdentity, *held)) {
      return error(ErrorCode::Conflict, "Held minute history profile changed");
    }
    auto parent = root;
    for (std::size_t i = 0; i < count; ++i) {
      const auto current = stateStamp(dirs[i].get());
      const auto named = stateStampAt(parent, names[i]);
      if (!current || !named || !*named || !sameStateDirectory(identities[i], *current)
          || !sameStateDirectory(*current, **named)) {
        return error(ErrorCode::Conflict, "Minute history directory changed");
      }
      parent = dirs[i].get();
    }
    return {};
  }

  Result open(Transaction& transaction) {
    const auto rootStamp = stateStamp(root);
    if (!rootStamp || !rootStamp->directory || !rootStamp->owned) {
      return error(ErrorCode::UnsafePath, "Minute history requires an owned held profile directory");
    }
    rootIdentity = *rootStamp;
    auto parent = root;
    for (std::size_t i = 0; i < dirs.size(); ++i) {
      if (transaction.cancelledBeforeMutation()) { return cancelled(); }
      auto opened = openDirectoryAt(parent, names[i]);
      const bool created = !opened && opened.error().code == ErrorCode::Missing;
      if (created) {
        const auto stable = verify(i);
        if (!stable) { return stable; }
        if (!transaction.beginMutation()) { return cancelled(); }
        opened = createDirectoryAt(parent, names[i]);
      }
      if (!opened) { return std::unexpected(opened.error()); }
      dirs[i] = std::move(*opened);
      const auto stamp = stateStamp(dirs[i].get());
      if (!stamp || !stamp->directory || !stamp->owned) {
        return error(ErrorCode::UnsafePath, "Minute history directory must be owned");
      }
      identities[i] = *stamp;
      if (created) {
        const auto mode = setNewPermissions(dirs[i].get(), 0700, identities[i]);
        if (!mode) { return mode; }
      }
      const auto stable = verify(i + 1);
      if (!stable) { return stable; }
      if (created) {
        const auto dirSync = syncStateDirectory(dirs[i].get());
        if (!dirSync) { return dirSync; }
        const auto parentSync = syncStateDirectory(parent);
        if (!parentSync) { return parentSync; }
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
  const auto parent = tree.dirs.back().get();
  if (transaction.cancelledBeforeMutation()) { return cancelled(); }
  auto file = openStateAppendFile(parent, leaf, false);
  const bool created = !file && file.error().code == ErrorCode::Missing;
  if (created) {
    ready = tree.verify();
    if (!ready) { return ready; }
    if (!transaction.beginMutation()) { return cancelled(); }
    file = openStateAppendFile(parent, leaf, true);
  }
  if (!file) { return std::unexpected(file.error()); }
  auto before = stateStamp(file->get());
  if (!before || !safeFile(*before)) {
    return error(ErrorCode::UnsafePath, "Minute history must be an owned single-link regular file");
  }
  if (created) {
    const auto permissions = setNewPermissions(file->get(), 0600, *before);
    if (!permissions) { return permissions; }
  }
  if (static_cast<std::uint64_t>(before->size) > core::SavedHistoryMaximumBytes
      || action.bytes().size() > core::SavedHistoryMaximumBytes - static_cast<std::size_t>(before->size)) {
    return error(ErrorCode::InvalidFormat, "Minute history append exceeds the day-file byte limit");
  }
  std::string bytes(static_cast<std::size_t>(before->size), '\0');
  for (std::size_t offset = 0; offset < bytes.size();) {
    if (transaction.cancelledBeforeMutation()) { return cancelled(); }
    const auto count = readStateAt(file->get(), bytes.data() + offset, bytes.size() - offset, offset);
    if (count <= 0) { return error(ErrorCode::Io, "Minute history read before append was incomplete"); }
    offset += static_cast<std::size_t>(count);
  }
  if (!bytes.empty() && bytes.back() != '\n') {
    return error(ErrorCode::InvalidFormat, "Minute history has an unterminated tail; append refused");
  }
  auto valid = projection(bytes, date); if (!valid) { return valid; }
  bytes.append(action.bytes());
  valid = projection(bytes, date); if (!valid) { return valid; }
  const auto namedCurrent = [&](const StateStamp& expected) {
    const auto held = stateStamp(file->get());
    const auto named = stateStampAt(parent, leaf);
    return held && named && *named && sameStateSnapshot(expected, *held) && sameStateSnapshot(*held, **named);
  };
  if (!namedCurrent(*before)) { return error(ErrorCode::Conflict, "Minute history file changed before append"); }
  ready = tree.verify();
  if (!ready) { return ready; }
  if (!transaction.beginMutation()) { return cancelled(); }

  const std::string_view appended = action.bytes();
  for (std::size_t offset = 0; offset < appended.size();) {
    const auto count = appendStateFile(file->get(), appended.substr(offset));
    if (count <= 0) { return error(ErrorCode::Io, "Minute history append may be partial; recorder poisoned"); }
    offset += static_cast<std::size_t>(count);
  }
  const auto written = stateStamp(file->get());
  if (!written || !sameObjectAfterAppend(*before, *written) || !safeFile(*written)
      || static_cast<std::uint64_t>(written->size) != bytes.size()) {
    return error(ErrorCode::Conflict, "Minute history file changed during append; recorder poisoned");
  }
  const auto synced = syncStateFile(file->get());
  if (!synced) { return synced; }
  if (created) {
    const auto parentSync = syncStateDirectory(parent);
    if (!parentSync) { return parentSync; }
  }
  if (!namedCurrent(*written)) {
    return error(ErrorCode::Conflict, "Minute history file changed after append; recorder poisoned");
  }
  ready = tree.verify();
  if (!ready) { return ready; }
  if (!closeStateFile(file->release())) { return error(ErrorCode::Io, "Minute history close failed; recorder poisoned"); }
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

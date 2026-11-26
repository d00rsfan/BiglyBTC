#include "history_view.hpp"
#include "state_file_io.hpp"
#include <array>
#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
Error cancelled() { return {ErrorCode::Cancelled, "Saved history read cancelled"}; }
std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}
std::string digits(unsigned number, std::size_t width) {
  std::string result(width, '0');
  for (std::size_t i = width; i > 0; --i) {
    result[i - 1] = static_cast<char>('0' + number % 10); number /= 10;
  }
  return result;
}
} // namespace

std::expected<SavedHistoryDayView, Error> readSavedHistoryDayView(
    const ProfileLease& lease, core::HistoryDate date, std::stop_token stop) {
  if (stop.stop_requested()) { return std::unexpected(cancelled()); }
  if (!lease.valid()) { return error(ErrorCode::UnsafePath, "A held profile lease is required for saved history"); }
  if (!core::validHistoryDate(date)) { return error(ErrorCode::InvalidFormat, "Saved history UTC date is invalid"); }
  try {
    using namespace detail;
    const auto root = lease.directoryHandle();
    const auto rootBefore = stateStamp(root);
    if (!rootBefore || !rootBefore->directory) {
      return error(ErrorCode::UnsafePath, "Held history profile is not a directory");
    }
    const std::array names{std::string("stats"), digits(static_cast<unsigned>(date.year), 4), digits(date.month, 2)};
    std::array<StateDescriptor, 3> directories;
    std::array<StateStamp, 3> opened{};
    auto parent = root;
    for (std::size_t i = 0; i < directories.size(); ++i) {
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      auto dir = openDirectoryAt(parent, names[i]);
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      if (!dir) {
        if (dir.error().code == ErrorCode::Missing) { return SavedHistoryDayView{}; }
        return std::unexpected(dir.error());
      }
      const auto held = stateStamp(dir->get());
      const auto named = stateStampAt(parent, names[i]);
      if (!held || !named || !*named || !sameStateDirectory(*held, **named)) {
        return error(ErrorCode::Conflict, "Saved history directory changed while opening");
      }
      opened[i] = *held;
      directories[i] = std::move(*dir);
      parent = directories[i].get();
    }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    const auto leaf = digits(date.day, 2) + ".dat";
    const auto before = stateStampAt(parent, leaf);
    if (!before) { return std::unexpected(before.error()); }
    if (!*before) { return SavedHistoryDayView{}; }
    if (!(*before)->regular) { return error(ErrorCode::UnsafePath, "Saved history day must be a regular file"); }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto bytes = readStateFile(parent, leaf, core::SavedHistoryMaximumBytes);
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!bytes) { return std::unexpected(std::move(bytes.error())); }
    if (!*bytes) { return error(ErrorCode::Conflict, "Saved history day disappeared while reading"); }
    auto snapshot = core::readSavedHistoryDay(**bytes, date, stop);
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!snapshot) {
      switch (snapshot.error()) {
        case core::SavedHistoryError::Cancelled: return std::unexpected(cancelled());
        case core::SavedHistoryError::Memory: return error(ErrorCode::Io, "Insufficient memory for saved history");
        case core::SavedHistoryError::UnsupportedVersion: return error(ErrorCode::Unsupported, "Saved history version is unsupported");
        default: return error(ErrorCode::InvalidFormat, "Saved history contains invalid, overflowing or excessive records");
      }
    }
    const auto after = stateStampAt(parent, leaf);
    if (!after || !*after || !sameStateSnapshot(**before, **after)) {
      return error(ErrorCode::Conflict, "Saved history day changed while reading");
    }
    parent = root;
    for (std::size_t i = 0; i < directories.size(); ++i) {
      const auto named = stateStampAt(parent, names[i]);
      if (!named || !*named || !sameStateDirectory(opened[i], **named)) {
        return error(ErrorCode::Conflict, "Saved history directory changed while reading");
      }
      parent = directories[i].get();
    }
    const auto rootAfter = stateStamp(root);
    if (!lease.valid() || lease.directoryHandle() != root || !rootAfter
        || !sameStateDirectory(*rootBefore, *rootAfter)) {
      return error(ErrorCode::Conflict, "Held history profile changed while reading");
    }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    SavedHistoryDayView result;
    result.snapshot.emplace(std::move(*snapshot));
    return result;
  } catch (const std::bad_alloc&) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return error(ErrorCode::Io, "Insufficient memory for saved history");
  }
}

} // namespace biglybtc::storage

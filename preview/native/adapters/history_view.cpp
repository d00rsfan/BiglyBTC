#include "history_view.hpp"

#include "file_io.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace biglybtc::storage {
namespace {
Error cancelled() { return {ErrorCode::Cancelled, "Saved history read cancelled"}; }
std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}
bool sameDirectory(const struct stat& a, const struct stat& b) {
  return S_ISDIR(a.st_mode) && S_ISDIR(b.st_mode) && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
bool sameFile(const struct stat& a, const struct stat& b) {
  return S_ISREG(a.st_mode) && S_ISREG(b.st_mode) && a.st_dev == b.st_dev && a.st_ino == b.st_ino
      && a.st_size == b.st_size && a.st_mtim.tv_sec == b.st_mtim.tv_sec
      && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec && a.st_ctim.tv_sec == b.st_ctim.tv_sec
      && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
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
    const int root = lease.directoryHandle();
    struct stat rootBefore{};
    if (::fstat(root, &rootBefore) != 0 || !S_ISDIR(rootBefore.st_mode)) {
      return error(ErrorCode::UnsafePath, "Held history profile is not a directory");
    }
    const std::array<std::string, 3> names{"stats", digits(static_cast<unsigned>(date.year), 4), digits(date.month, 2)};
    std::array<detail::Descriptor, 3> directories{detail::Descriptor{-1}, detail::Descriptor{-1}, detail::Descriptor{-1}};
    std::array<struct stat, 3> opened{};
    int parent = root;
    for (std::size_t i = 0; i < directories.size(); ++i) {
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      directories[i] = detail::Descriptor(::openat(parent, names[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
      const int openError = errno;
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      if (directories[i].get() < 0) {
        if (openError == ENOENT) { return SavedHistoryDayView{}; }
        return error(openError == ELOOP || openError == ENOTDIR ? ErrorCode::UnsafePath : ErrorCode::Io,
            "Saved history directory cannot be opened safely");
      }
      struct stat named{};
      if (::fstat(directories[i].get(), &opened[i]) != 0) {
        return error(ErrorCode::Io, "Cannot identify opened saved history directory");
      }
      if (::fstatat(parent, names[i].c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0
          || !sameDirectory(opened[i], named)) {
        return error(ErrorCode::Conflict, "Saved history directory changed while opening");
      }
      parent = directories[i].get();
    }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    const auto leaf = digits(date.day, 2) + ".dat";
    struct stat before{};
    if (::fstatat(parent, leaf.c_str(), &before, AT_SYMLINK_NOFOLLOW) != 0) {
      if (errno == ENOENT) { return SavedHistoryDayView{}; }
      return error(ErrorCode::Io, "Cannot inspect saved history day file");
    }
    if (!S_ISREG(before.st_mode)) { return error(ErrorCode::UnsafePath, "Saved history day must be a regular file"); }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto bytes = detail::readFile(parent, leaf, core::SavedHistoryMaximumBytes);
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
    struct stat after{};
    if (::fstatat(parent, leaf.c_str(), &after, AT_SYMLINK_NOFOLLOW) != 0 || !sameFile(before, after)) {
      return error(ErrorCode::Conflict, "Saved history day changed while reading");
    }
    parent = root;
    for (std::size_t i = 0; i < directories.size(); ++i) {
      if (::fstatat(parent, names[i].c_str(), &after, AT_SYMLINK_NOFOLLOW) != 0
          || !sameDirectory(opened[i], after)) {
        return error(ErrorCode::Conflict, "Saved history directory changed while reading");
      }
      parent = directories[i].get();
    }
    if (!lease.valid() || lease.directoryHandle() != root || ::fstat(root, &after) != 0
        || !sameDirectory(rootBefore, after)) {
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

#include "file_io.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace biglybtc::storage::detail {
namespace {
std::unexpected<Error> error(ErrorCode code, std::string message) {
  return std::unexpected(Error{code, std::move(message)});
}
void discardOwnedPartial(int directory, const std::string& name, int descriptor) {
  // The caller holds the profile lease and created this file with O_EXCL.
  // An existing or replaced recovery pathname must never be removed here.
  struct stat opened{}, named{};
  if (::fstat(descriptor, &opened) == 0
      && ::fstatat(directory, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0
      && S_ISREG(named.st_mode) && opened.st_dev == named.st_dev && opened.st_ino == named.st_ino
      && ::unlinkat(directory, name.c_str(), 0) == 0) {
    // Best effort: the save still reports its original write error. If directory
    // sync fails, later crash recovery must treat a reappearing partial file as
    // uncommitted; the existing primary and backup have never been replaced.
    (void)::fsync(directory);
  }
}
}

Descriptor::Descriptor(Descriptor&& other) noexcept : value_(std::exchange(other.value_, -1)) {}
Descriptor& Descriptor::operator=(Descriptor&& other) noexcept {
  if (this != &other) {
    if (value_ >= 0) { ::close(value_); }
    value_ = std::exchange(other.value_, -1);
  }
  return *this;
}
Descriptor::~Descriptor() { if (value_ >= 0) { ::close(value_); } }
int Descriptor::release() { return std::exchange(value_, -1); }

std::expected<std::optional<std::string>, Error> readFile(int directory, const std::string& name, std::size_t maximum) {
  Descriptor file(::openat(directory, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  if (file.get() < 0) {
    if (errno == ENOENT) {
      return std::nullopt;
    }
    return error(errno == ELOOP ? ErrorCode::UnsafePath : ErrorCode::Io, "Unable to open configuration safely");
  }
  struct stat before{};
  if (::fstat(file.get(), &before) != 0 || !S_ISREG(before.st_mode)) {
    return error(ErrorCode::UnsafePath, "Configuration must be a regular file");
  }
  if (before.st_size < 0 || static_cast<std::uint64_t>(before.st_size) > maximum) {
    return error(ErrorCode::InvalidFormat, "Configuration exceeds the selected input limit");
  }
  std::string data(static_cast<std::size_t>(before.st_size), '\0');
  std::size_t offset = 0;
  while (offset < data.size()) {
    const auto count = ::read(file.get(), data.data() + offset, data.size() - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      return error(ErrorCode::Io, "Configuration read was incomplete");
    }
    offset += static_cast<std::size_t>(count);
  }
  struct stat after{};
  if (::fstat(file.get(), &after) != 0 || before.st_size != after.st_size
      || before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec) {
    return error(ErrorCode::Conflict, "Configuration changed while being read");
  }
  return std::optional<std::string>(std::move(data));
}

std::expected<Descriptor, Error> writeExclusiveOwned(int directory, const std::string& name, std::string_view data, mode_t mode) {
  Descriptor file(::openat(directory, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode));
  if (file.get() < 0) {
    return error(errno == EEXIST ? ErrorCode::Conflict : ErrorCode::Io, "Unable to create exclusive saving file");
  }
  std::size_t offset = 0;
  while (offset < data.size()) {
    const auto count = ::write(file.get(), data.data() + offset, data.size() - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      discardOwnedPartial(directory, name, file.get());
      return error(ErrorCode::Io, "Unable to write complete saving file; original preserved");
    }
    offset += static_cast<std::size_t>(count);
  }
  if (::fsync(file.get()) != 0) {
    return error(ErrorCode::Io, "Unable to synchronize saving file; original preserved");
  }
  return file;
}

std::expected<void, Error> writeExclusive(int directory, const std::string& name, std::string_view data, mode_t mode) {
  auto file = writeExclusiveOwned(directory, name, data, mode);
  if (!file) { return std::unexpected(std::move(file.error())); }
  return {};
}

} // namespace biglybtc::storage::detail

// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_config_native.hpp"
#include "file_io.hpp"

#include <sys/stat.h>

namespace biglybtc::storage::detail {

std::expected<ConfigDirectoryIdentity, Error> profileConfigDirectory(const ProfileLease& lease) {
  struct stat directory{};
  if (::fstat(lease.directoryHandle(), &directory) != 0) {
    return std::unexpected(Error{ErrorCode::Io, "Unable to identify profile directory"});
  }
  return ConfigDirectoryIdentity{static_cast<std::uint64_t>(directory.st_dev),
      static_cast<std::uint64_t>(directory.st_ino)};
}

std::expected<std::optional<std::string>, Error> readProfileConfig(const ProfileLease& lease,
    const std::string& filename, std::size_t maximum) {
  return readFile(lease.directoryHandle(), filename, maximum);
}

} // namespace biglybtc::storage::detail

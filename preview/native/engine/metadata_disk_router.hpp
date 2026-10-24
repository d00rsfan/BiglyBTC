#pragma once

#include "profile.hpp"
#include <libtorrent/session_params.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace biglybtc::engine::detail {

// Owner-chosen virtual save directories only; registration is mandatory BEFORE
// add_torrent. No filesystem access, metadata parsing or engine creation here.
// Reservations last for the policy/session lifetime and are never recycled.
class MetadataDiskPolicy final {
public:
  static constexpr std::size_t MaximumMetadataPaths = 1024;
  static constexpr std::size_t MaximumStorageAdmissions = 65536;
  std::expected<void, storage::Error> registerMetadataPath(const std::filesystem::path&);
  // Pure lexical absolute normalization, including trailing separators. Throws
  // on invalid/unrepresentable input; callers must fail closed, never reroute an
  // uncertain metadata path to real storage. Ordinary admission must reject any
  // reserved directory, so selected payload cannot enter the discard backend.
  bool isMetadataPath(const std::filesystem::path&) const;
private:
  mutable std::mutex mutex_;
  std::set<std::string, std::less<>> paths_;
};

// One disk_interface in the SAME LT session: registered paths use stock disabled
// IO; all ordinary paths use stock mmap IO. Optional disk gauges cover mmap only
// (disabled IO overwrites disk_blocks_in_use). Network accounting is untouched.
libtorrent::disk_io_constructor_type metadataDiskConstructor(std::shared_ptr<MetadataDiskPolicy>);

} // namespace biglybtc::engine::detail

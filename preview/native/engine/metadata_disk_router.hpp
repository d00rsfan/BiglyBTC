#pragma once

#include "profile.hpp"
#include <libtorrent/session_params.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <map>
#include <string>

namespace biglybtc::engine::detail {

// Owner-chosen virtual save directories only; registration is mandatory BEFORE
// add_torrent. No filesystem access, metadata parsing or engine creation here.
// Reservations last for the policy/session lifetime and are never recycled.
class OriginalDiskFiles;
class MmapDiskLatency;

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
  std::expected<void,storage::Error> registerOriginalPath(const std::filesystem::path&,std::shared_ptr<OriginalDiskFiles>);
  // Optional exact full-metadata best hash + exact save root, not a parent or
  // GUI-selected alias. One live admission may claim exactly one mmap slot.
  bool registerMmapLatency(const std::filesystem::path&,const std::array<unsigned char,20>&,
      const std::shared_ptr<MmapDiskLatency>&) noexcept;
  std::shared_ptr<MmapDiskLatency> claimMmapLatency(const std::filesystem::path&,
      const std::array<unsigned char,20>&) noexcept;
  bool isOriginalPath(const std::filesystem::path&) const;
  std::shared_ptr<OriginalDiskFiles> originalFiles(const std::filesystem::path&) const;
private:
  mutable std::mutex mutex_;
  std::set<std::string, std::less<>> paths_;
  std::map<std::string,std::weak_ptr<OriginalDiskFiles>,std::less<>> originals_;
  std::map<std::pair<std::string,std::array<unsigned char,20>>,std::weak_ptr<MmapDiskLatency>> latencies_;
};

// One disk_interface in the SAME LT session: registered paths use stock disabled
// IO; all ordinary paths use stock mmap IO. Optional disk gauges cover mmap only
// (disabled IO overwrites disk_blocks_in_use). Network accounting is untouched.
libtorrent::disk_io_constructor_type metadataDiskConstructor(std::shared_ptr<MetadataDiskPolicy>);

} // namespace biglybtc::engine::detail

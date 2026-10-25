#pragma once

#include "file_io.hpp"
#include "original_file_io.hpp"
#include <libtorrent/disk_interface.hpp>
#include <libtorrent/io_context.hpp>
#include <libtorrent/torrent_info.hpp>
#include <memory>
#include <atomic>
#include <span>
#include <vector>

namespace biglybtc::engine::detail {

struct OriginalDiskFile {
  storage::detail::Descriptor descriptor;
  storage::OriginalFileIo io;
  std::function<std::expected<void,storage::Error>()> validateControl = {};
};

// One admission's authenticated descriptors and original layouts. After prepare,
// only the disk worker touches the codecs; checkpoint follows the retirement
// fence. No save-path lookup, partfile or engine-owned payload mutation exists.
class OriginalDiskFiles final {
public:
  OriginalDiskFiles(std::shared_ptr<libtorrent::torrent_info>,
      std::vector<std::optional<OriginalDiskFile>>);
  const libtorrent::torrent_info& metadata() const { return *metadata_; }
  bool matches(const libtorrent::storage_params&) const;
  std::expected<void, storage::Error> read(std::uint64_t, std::span<char>);
  std::expected<void, storage::Error> write(std::uint64_t, std::span<const char>);
  std::expected<void, storage::Error> flush();
  std::optional<storage::Error> failure() const;
  void recordFailure();
private:
  std::shared_ptr<libtorrent::torrent_info> metadata_;
  std::vector<std::optional<OriginalDiskFile>> files_;
  std::optional<storage::Error> failure_;
  std::atomic_bool failedException_ = false;
};

// Caller provides the path registry lookup; an expired reserved admission must
// fail closed instead of falling through to mmap.
using OriginalDiskLookup = std::function<std::shared_ptr<OriginalDiskFiles>(const std::string&)>;
std::unique_ptr<libtorrent::disk_interface> originalDiskConstructor(
    libtorrent::io_context&, OriginalDiskLookup);

} // namespace biglybtc::engine::detail

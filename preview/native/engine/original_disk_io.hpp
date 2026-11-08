#pragma once

#include "file_io.hpp"
#include "disk_latency.hpp"
#include "original_file_io.hpp"
#include <libtorrent/disk_interface.hpp>
#include <libtorrent/io_context.hpp>
#include <libtorrent/torrent_info.hpp>
#include <memory>
#include <atomic>
#include <mutex>
#include <span>
#include <vector>

namespace biglybtc::engine::detail {

enum class OriginalDiskAccess { ReadWrite, ReadOnly, SelectedReadOnly, RecheckReadOnly };
struct OriginalDiskFile {
  storage::detail::Descriptor descriptor;
  storage::OriginalFileIo io;
  // Control proof, or immutable named-payload proof for existing-only storage.
  std::function<std::expected<void,storage::Error>()> validateControl = {};
};

// One admission's authenticated descriptors and original layouts. After prepare,
// only the disk worker touches the codecs; checkpoint follows the retirement
// fence. No save-path lookup, partfile or engine-owned payload mutation exists.
class OriginalDiskFiles final {
public:
  OriginalDiskFiles(std::shared_ptr<libtorrent::torrent_info>,
      std::vector<std::optional<OriginalDiskFile>>,
      OriginalDiskAccess access=OriginalDiskAccess::ReadWrite,
      std::function<bool()> validatedOriginalResume = {});
  void recordHashOperation() noexcept { hashOperations_.fetch_add(1,std::memory_order_relaxed); }
  std::uint64_t hashOperations() const noexcept { return hashOperations_.load(std::memory_order_relaxed); }
  bool acceptsOriginalResume() const { return access_==OriginalDiskAccess::ReadWrite && validatedOriginalResume_ && validatedOriginalResume_(); }
  OriginalDiskAccess access() const noexcept { return access_; }
  const libtorrent::torrent_info& metadata() const { return *metadata_; }
  bool matches(const libtorrent::storage_params&) const;
  // Optional worker-only failure index is exact for the failing real fragment.
  // Missing/partial reads are errors, never hashable zero-filled data.
  std::expected<void, storage::Error> read(std::uint64_t, std::span<char>,
      libtorrent::file_index_t* failedFile=nullptr);
  std::expected<void, storage::Error> write(std::uint64_t, std::span<const char>);
  std::expected<void, storage::Error> flush();
  // Called only after retirement with actual per-file verified completeness.
  std::expected<void, storage::Error> finishComplete(std::span<const std::uint8_t>);
  std::optional<storage::Error> failure() const;
  void recordFailure();
  // Actual private disk-hash outcomes:0unobserved,1readable,2missing,3error.
  void recordRecheckHash(int piece,std::uint8_t outcome,std::int64_t readableBytes) noexcept;
  std::vector<std::uint8_t> recheckHashOutcomes() const;
  std::int64_t recheckScannedBytes() const;
  DiskLatency& diskLatency() noexcept { return diskLatency_; }
  const DiskLatency& diskLatency() const noexcept { return diskLatency_; }
private:
  std::atomic<std::uint64_t> hashOperations_{0};
  const OriginalDiskAccess access_;
  std::function<bool()> validatedOriginalResume_;
  std::shared_ptr<libtorrent::torrent_info> metadata_;
  std::vector<std::optional<OriginalDiskFile>> files_;
  std::optional<storage::Error> failure_;
  std::atomic_bool failedException_ = false;
  DiskLatency diskLatency_;
  mutable std::mutex recheckMutex_;
  std::vector<std::uint8_t> recheckOutcomes_;
  std::vector<std::int64_t> recheckBytes_;
};

// Caller provides the path registry lookup; an expired reserved admission must
// fail closed instead of falling through to mmap.
using OriginalDiskLookup = std::function<std::shared_ptr<OriginalDiskFiles>(const std::string&)>;
std::unique_ptr<libtorrent::disk_interface> originalDiskConstructor(
    libtorrent::io_context&, OriginalDiskLookup);

} // namespace biglybtc::engine::detail

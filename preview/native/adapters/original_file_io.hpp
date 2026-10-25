#pragma once
#include "profile.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
namespace biglybtc::storage {
enum class OriginalStorageKind { Linear=1, Compact=2, Reordered=3, ReorderedCompact=4 };
struct OriginalFileGeometry {std::uint64_t torrentOffset=0,fileBytes=0;std::uint32_t pieceBytes=0;};
struct OriginalMissingRange {std::uint64_t offset=0,length=0;bool operator==(const OriginalMissingRange&)const=default;};
struct OriginalReadResult {
  std::uint64_t physicalBytes=0;
  // Logical absolute ranges. Output bytes in missing ranges are zero-filled,
  // transient, and MUST NOT count as present/verified data.
  std::vector<OriginalMissingRange> missing;
  bool complete()const noexcept{return missing.empty();}
};
// Expected original bytes (nullopt means absent) -> durable CAS replacement.
// Caller owns safe fixed-name primary/backup publication and parent sync.
using OriginalControlPublisher=std::function<std::expected<void,Error>(
    const std::optional<std::string>& expected,std::string_view replacement)>;
std::expected<std::uint64_t,Error> originalMaximumPhysicalSize(OriginalFileGeometry,OriginalStorageKind);
std::string originalControlLeaf(std::size_t fileIndex,OriginalStorageKind);
class OriginalFileIo final {
public:
  // Pure control validation through the same geometry/map decoder as open().
  // No descriptors, recovery, publication or payload access.
  static std::expected<void,Error> validateControl(OriginalFileGeometry,
      OriginalStorageKind,std::string_view);

  // Borrowed authenticated owned regular descriptor; exclusive serialized use.
  // Caller retains descriptor/publisher lifetime and path/inode authorization.
  // authenticatedLinks=2 requires caller-proven exact journal aliases; default1.
  // compactExpansionPending requires caller-authenticated durable conversion
  // intent for this exact descriptor; it permits C bytes up to logical length.
  // open is read-only: new controls publish only on write/flush.
  // Explicit recovery of absent control maps an existing linear prefix and
  // requires recheck; never asserts a verified piece. No silent conversion.
  static std::expected<OriginalFileIo,Error> open(int payloadFd,OriginalFileGeometry,
      OriginalStorageKind,std::optional<std::string> control,
      OriginalControlPublisher={},bool recoverMissingControl=false,
      bool compactExpansionPending=false,unsigned authenticatedLinks=1);
  OriginalFileIo(OriginalFileIo&&)noexcept;
  OriginalFileIo& operator=(OriginalFileIo&&)noexcept;
  ~OriginalFileIo();
  OriginalFileIo(const OriginalFileIo&)=delete;
  OriginalFileIo& operator=(const OriginalFileIo&)=delete;
  std::uint64_t length()const noexcept;
  bool requiresRecheck()const noexcept;
  bool recoveryPending()const noexcept;
  const std::optional<std::string>& controlBytes()const noexcept;
  std::expected<OriginalReadResult,Error> read(std::uint64_t,std::span<std::byte>);
  std::expected<void,Error> write(std::uint64_t,std::span<const std::byte>);
  std::expected<void,Error> setLength(std::uint64_t);
  // Global torrent piece number, data restricted to this file's fragment.
  // Validates completion but retains the compatible arbitrary slot map; false
  // means no physical normalization was performed. No crash-unsafe swap.
  std::expected<bool,Error> pieceComplete(std::uint64_t,std::span<const std::byte>);
  std::expected<void,Error> flush();
  // New slots are data-synced before atomic map publication. A final-slot
  // allocation first preserves a previously allocated short tail in the new
  // slot and publishes that map before reusing its old slot for caller data.
  // Missing tail bytes stay missing. A preceding highest allocated EOF-short
  // piece is repaired first only when this same ascending write supplies its
  // entire missing suffix; otherwise hiding that suffix is refused unchanged.
  // On uncertain publisher failure, reload actual control and reopen first.
  std::expected<void,Error> recover();
  // After exact engine retirement and actual full-file verification only.
  // Put a short final piece in the highest slot so original GM's raw-size
  // gate accepts the file. Interrupted chunk redo requires native recover()
  // before any use; original resume must remain invalid until success.
  std::expected<void,Error> finishCompleteReordered();
  // Stopped handoff: normalize physically complete allocation even when hashes
  // failed. Does not certify data; incomplete physical ranges remain untouched.
  std::expected<bool,Error> finishAllocatedReordered();
  // Caller invalidates resume before conversion, then publishes original L
  // storetype and retires old compact control only AFTER this returns. Until
  // then packed prefix/tail remain intact for the original compact reader.
  // Tail copies use at most 64 KiB scratch memory, independent of piece size.
  std::expected<void,Error> convertToLinear();
  // R <-> RC changes only original st metadata; arbitrary map is preserved.
  std::expected<void,Error> setReorderedKind(OriginalStorageKind);
private:
  struct Impl;
  explicit OriginalFileIo(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}

#pragma once
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace biglybtc::storage {
enum class FileOperationKind { RemoveKeepData, RemoveDeleteData, Relocate, Relink, SetFilePolicies };
struct FileOperationRequest {
  core::TorrentHash hash;
  FileOperationKind kind = FileOperationKind::RemoveKeepData;
  // Parent save directory. Existing save_file/name remains unchanged.
  std::optional<std::filesystem::path> destinationParent{};
  // Internal selection transaction only: bounded original file_priorities_c.
  // No GUI paths or arbitrary selected-row replacement are accepted here.
  std::optional<std::string> selectionPolicyEncoding{};
  std::optional<std::string> expectedSelectionEntry{};
};
struct FileOperationEntry {
  std::size_t index = 0;
  std::filesystem::path source, destination;
  std::int64_t logicalBytes = 0, actualBytes = 0;
  bool present = false;
  std::uint64_t device = 0, inode = 0;
};
struct FileOperationPreview {
  FileOperationKind kind = FileOperationKind::RemoveKeepData;
  core::TorrentHash hash;
  std::string approvalScopeDigest; // Lowercase SHA256; excludes volatile counters/mtime.
  std::string executionCasDigest; // Exact original documents + payload metadata.
  std::filesystem::path sourceRoot, targetRoot;
  std::vector<FileOperationEntry> files;
  std::uint64_t presentFiles = 0, missingFiles = 0, presentBytes = 0;
  bool crossDevice = false, requiresRecheck = false;
};
struct FileOperationConfirmation {
  std::string approvalScopeDigest;
  bool deleteConfirmed = false;
};
enum class FileOperationStage { JournalSynced, DestinationPublished, CachePublished, StatePublished, InventoryPublished, SourceRemoved, StateRemoved, Completed };
struct FileOperationResult {
  FileOperationKind kind;
  core::TorrentHash hash;
  std::uint64_t filesMoved = 0, filesDeleted = 0;
  bool recovered = false;
};
class PreparedFileOperation final {
public:
  PreparedFileOperation(PreparedFileOperation&&) noexcept;
  PreparedFileOperation& operator=(PreparedFileOperation&&) noexcept;
  ~PreparedFileOperation();
  PreparedFileOperation(const PreparedFileOperation&) = delete;
  PreparedFileOperation& operator=(const PreparedFileOperation&) = delete;
  const FileOperationPreview& preview() const noexcept;
private:
  struct Impl;
  explicit PreparedFileOperation(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
  friend std::expected<PreparedFileOperation,Error> prepareFileOperation(const ProfileLease&,const FileOperationRequest&,std::stop_token);
  friend std::expected<FileOperationResult,Error> applyFileOperation(const ProfileLease&,PreparedFileOperation&&,const FileOperationConfirmation&,void(*)(FileOperationStage),std::stop_token);
};
// Worker-only preparation is read-only. Host may show this scope while live,
// then must stop/certify/revoke scheduler ownership and reprepare. Confirmation
// transfers only when fresh approvalScopeDigest matches the accepted preview;
// exact volatile execution CAS comes from that freshly prepared plan.
std::expected<PreparedFileOperation,Error> prepareFileOperation(
    const ProfileLease&,const FileOperationRequest&,std::stop_token = {});
std::expected<FileOperationConfirmation,Error> confirmFileOperation(
    const PreparedFileOperation&,std::string_view acceptedApprovalScopeDigest,bool deleteConfirmed);
// Borrowed lease; caller guarantees no retained handle and no concurrent profile
// work. Cancellation is observed before the first durable journal only. Errors
// after mutation require explicit recovery; never retry a consumed plan.
std::expected<FileOperationResult,Error> applyFileOperation(const ProfileLease&,PreparedFileOperation&&,
    const FileOperationConfirmation&,void(*observer)(FileOperationStage) = nullptr,std::stop_token = {});
bool hasPendingFileOperation(const ProfileLease&) noexcept;
std::expected<std::optional<FileOperationResult>,Error> recoverFileOperation(
    const ProfileLease&,void(*observer)(FileOperationStage) = nullptr);
}

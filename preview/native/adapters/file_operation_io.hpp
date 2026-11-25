#pragma once
#include "file_operations.hpp"
#include "file_operation_digest.hpp"
#include "state_file_io.hpp"
#include <array>

namespace biglybtc::storage::fileop {
using Fd=detail::StateDescriptor;
using FileHandle=detail::StateHandle;
using FileStamp=detail::StateStamp;
void sync(FileHandle);
void pathShape(const std::filesystem::path&);
Fd directory(const std::filesystem::path&,bool create=false,bool missing=false);
std::string profilePath(const ProfileLease&);
bool identity(const FileStamp&,const FileStamp&);
bool unchanged(const FileStamp&,const FileStamp&);
std::optional<FileStamp> inspect(const std::filesystem::path&);
struct Raw {std::optional<std::string> bytes;std::optional<FileStamp> stamp;};
Raw read(const std::filesystem::path&,std::size_t);
void check(const std::filesystem::path&,const Raw&,std::size_t);
void publish(const std::filesystem::path&,const Raw&,std::string_view,std::size_t);
void erase(const std::filesystem::path&,const FileStamp&);
FileStamp heldFileStamp(FileHandle);
bool validJournalFileStamp(const FileStamp&) noexcept;
bool privateEntry(FileHandle);
bool tryLinkPayload(FileHandle sourceDirectory, const std::filesystem::path& source, const FileStamp& expected,
    FileHandle destinationDirectory, const std::filesystem::path& destination);
Fd openCopyScratch(FileHandle directory, const std::filesystem::path& name, const std::optional<FileStamp>& previous);
void copyPayload(FileHandle directory, const std::filesystem::path& source, const FileStamp& expected, FileHandle output);
void renamePayload(FileHandle sourceDirectory, const std::filesystem::path& source, const FileStamp& expected,
    FileHandle destinationDirectory, const std::filesystem::path& destination);
std::optional<std::string> readAllocationToken(FileHandle directory, const std::filesystem::path& leaf, FileHandle file);
bool writeAllocationToken(FileHandle directory, const std::filesystem::path& leaf, FileHandle file, std::string_view token);
std::string digestFile(const std::filesystem::path&,const FileStamp&,std::stop_token={});
// Process-local SHA receipts. Never serialized or trusted after recovery. A hit
// requires the named inode AND its retained descriptor to match the complete
// post-hash stat (including ctime and link count). Metadata changes force a read.
struct PayloadDigest {
  Fd descriptor;
  FileStamp stamp{};
  std::string sha256;
};
class PayloadDigestCache final {
public:
  static constexpr std::size_t Capacity=8;
  std::string digest(const std::filesystem::path&,const FileStamp&,std::stop_token={});
  void clear() noexcept {for(auto& proof:proofs_){proof.reset();}next_=0;}
private:
  std::array<std::optional<PayloadDigest>,Capacity> proofs_{};
  std::size_t next_=0;
};
}

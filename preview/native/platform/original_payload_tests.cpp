// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_payload_io.hpp"
#include "state_file_io.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
namespace storage = biglybtc::storage;
namespace io = storage::detail;

constexpr auto Block = 64 * 1024;
constexpr std::uint64_t FileSize = 64 * 1024 * 1024;

void require(bool truth, const char* message) {
  if (!truth) { throw std::runtime_error(message); }
}

template<class T>
T take(std::expected<T, storage::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
  return std::move(*result);
}

void verify(std::expected<void, storage::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
}

struct TemporaryDirectory {
  const fs::path path = fs::temp_directory_path() /
      ("biglybtc-payload-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryDirectory() { require(fs::create_directory(path), "Cannot create exclusive temporary directory"); }
  ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
};

io::OriginalPayloadDescriptor openDirectory(const fs::path& path) {
#ifdef _WIN32
  const auto handle = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  require(handle != INVALID_HANDLE_VALUE, "Cannot hold the generated payload directory");
#else
  const auto handle = ::open(path.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  require(handle >= 0, "Cannot hold the generated payload directory");
#endif
  return io::OriginalPayloadDescriptor(handle);
}

std::uint64_t allocatedBytes(const fs::path& path) {
#ifdef _WIN32
  const auto attributes = ::GetFileAttributesW(path.c_str());
  require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_SPARSE_FILE),
      "Client payload has no NTFS sparse attribute");
  DWORD high = 0;
  ::SetLastError(ERROR_SUCCESS);
  const auto low = ::GetCompressedFileSizeW(path.c_str(), &high);
  require(low != INVALID_FILE_SIZE || ::GetLastError() == ERROR_SUCCESS, "Cannot measure NTFS payload allocation");
  return (std::uint64_t(high) << 32) | low;
#else
  struct stat info{};
  require(::stat(path.c_str(), &info) == 0 && info.st_blocks >= 0, "Cannot measure payload allocation");
  return static_cast<std::uint64_t>(info.st_blocks) * 512;
#endif
}

void testSparsePayload(const fs::path& path, bool setFullLength) {
  constexpr storage::OriginalFileGeometry geometry{0, FileSize, Block};
  constexpr auto Last = FileSize - Block;
  std::array<std::byte, Block> first{}, last{}, observed{};
  first.fill(std::byte{0x2b});
  last.fill(std::byte{0xd1});
  const auto directory = openDirectory(path.parent_path());
  using Access = io::OriginalPayloadAccess;

  {
    const auto missing = io::openOriginalPayloadAt(directory.get(), path.filename(), Access::ReadWrite);
    require(!missing && missing.error().code == storage::ErrorCode::Missing, "Writable reopen created new data");
    auto file = take(io::openOriginalPayloadAt(directory.get(), path.filename(), Access::CreateExclusive));
    auto io = take(storage::OriginalFileIo::open(file.get(), geometry, storage::OriginalStorageKind::Linear, {}));
    verify(io.enableSparse());
    require(io.length() == 0, "Marking a new sparse file must not reserve logical length");
    if (setFullLength) {
      verify(io.setLength(FileSize));
      require(io.length() == FileSize, "Sparse resize did not set exact logical length");
    } else {
      const auto missing = take(io.read(Last, observed));
      require(!missing.complete() && missing.physicalBytes == 0, "EOF must remain absent before a late write");
    }
    verify(io.write(Last, last));
    verify(io.write(0, first));
    verify(io.flush());
    require(io.length() == FileSize, "Client writes did not retain exact logical length");
  }

  const auto allocated = allocatedBytes(path);
  require(allocated > 0 && allocated <= 4 * 1024 * 1024, "Two small writes allocated the empty middle");
  {
    auto file = take(io::openOriginalPayloadAt(directory.get(), path.filename(), Access::ReadWrite));
    auto io = take(storage::OriginalFileIo::open(file.get(), geometry, storage::OriginalStorageKind::Linear, {}));
    require(io.length() == FileSize, "Writable reopen truncated the existing data");
    verify(io.write(Last, last));
    verify(io.flush());
  }
  {
    auto file = take(io::openOriginalPayloadAt(directory.get(), path.filename(), Access::ReadOnly));
    auto io = take(storage::OriginalFileIo::open(file.get(), geometry, storage::OriginalStorageKind::Linear, {}));
    require(io.length() == FileSize, "Reopening changed the logical file length");
    require(!io.write(0, last), "Read-only payload admitted a write");
    require(!io.enableSparse(), "Read-only payload admitted an attribute change");
    require(take(io.read(0, observed)).complete() && observed == first, "First block changed after closing");
    require(take(io.read(Last, observed)).complete() && observed == last, "Last block changed after closing");
    require(take(io.read(FileSize / 2, observed)).complete() &&
        std::ranges::all_of(observed, [](std::byte value) { return value == std::byte{}; }),
        "The hole did not read back as zero bytes");
  }
  require(allocatedBytes(path) == allocated, "Reopening or scanning a sparse payload increased physical usage");
  std::cout << (setFullLength ? "sparse resize" : "zero-length start") << ": " << FileSize
            << " logical bytes, " << allocated << " bytes allocated after write and reopen\n";
}

void testManagedPaths(const fs::path& temporary) {
  const auto path=fs::canonical(temporary);
  auto directory=take(io::openFilesystemRoot(io::filesystemRoot(path)));
  for (const auto& component:path.relative_path()) {
    directory=take(io::openDirectoryAt(directory.get(),component));
  }
  const fs::path directoryName(u8"\u03bb-storage");
  const auto missing=io::openDirectoryAt(directory.get(),directoryName);
  require(!missing&&missing.error().code==storage::ErrorCode::Missing,"Missing directory was not reported");
  require(!io::openDirectoryAt(directory.get(),".."),"Parent traversal was accepted as a relative leaf");
  const auto payloadDirectory=take(io::createDirectoryAt(directory.get(),directoryName));
  const auto actual=path/directoryName;
  const std::set<fs::path,io::FilesystemPathLess> claimed{actual/"payload",path/fs::path(u8"\u03bb-storage-other")};
  require(claimed.lower_bound(actual)!=claimed.end()&&*claimed.lower_bound(actual)==actual/"payload",
      "A neighboring path hid a descendant from the claim index");
#ifdef _WIN32
  require(claimed.contains(actual/"PAYLOAD"),"An alternate Windows filename case bypassed a payload claim");
#else
  require(!claimed.contains(actual/"PAYLOAD"),"The POSIX claim index merged distinct filename cases");
#endif
  const auto firstName=fs::path(u8"source-\u03bb");
  const auto finalName=fs::path(u8"finished-\u03bb");
  const auto occupiedName=fs::path(u8"occupied-\u03bb");
  using Access=io::OriginalPayloadAccess;
  const auto file=take(io::openOriginalPayloadAt(payloadDirectory.get(),firstName,Access::CreateExclusive,1,true));
  const auto occupied=take(io::openOriginalPayloadAt(payloadDirectory.get(),occupiedName,Access::CreateExclusive,1,true));
  constexpr std::uint64_t SecondOffset=4096;
  const std::array first{std::byte{0x19},std::byte{0x27}};
  const std::array second{std::byte{0x36},std::byte{0x45}};
  require(io::writeOriginalPayload(file.get(),0,first)==2&&
      io::writeOriginalPayload(file.get(),SecondOffset,second)==2,"Cannot write test offsets");
  auto copy=take(io::duplicateStateDirectory(file.get()));
  std::atomic_bool offsetMismatch=false;
  const auto verifyReads=[&](io::StateHandle handle,std::uint64_t offset,const auto& expected) {
    std::array<std::byte,2> bytes{};
    for (int count=0;count<500;++count) {
      if (io::readOriginalPayload(handle,offset,bytes)!=2||bytes!=expected) { offsetMismatch=true; return; }
    }
  };
  const auto writeRepeatedly=[&](io::StateHandle handle,std::uint64_t offset,const auto& bytes) {
    for (int count=0;count<500;++count) {
      if (io::writeOriginalPayload(handle,offset,bytes)!=2) { offsetMismatch=true; return; }
    }
  };
  {
    std::jthread firstWriter([&]{writeRepeatedly(file.get(),0,first);});
    std::jthread secondWriter([&]{writeRepeatedly(copy.get(),SecondOffset,second);});
  }
  {
    std::jthread firstReader([&]{verifyReads(file.get(),0,first);});
    std::jthread secondReader([&]{verifyReads(copy.get(),SecondOffset,second);});
  }
  require(!offsetMismatch,"Duplicated payload handles interfered with each other's read offsets");
  const auto collision=io::linkPayloadName(payloadDirectory.get(),file.get(),firstName,occupiedName);
  require(!collision&&collision.error().code==storage::ErrorCode::Conflict,"Completion link overwrote an existing name");
  const auto wrongSource=io::linkPayloadName(payloadDirectory.get(),file.get(),occupiedName,finalName);
  require(!wrongSource&&wrongSource.error().code==storage::ErrorCode::Conflict,"Completion accepted another file's source name");
  verify(io::linkPayloadName(payloadDirectory.get(),file.get(),firstName,finalName));
  const auto before=take(io::stateStampAt(payloadDirectory.get(),firstName));
  const auto linked=take(io::stateStampAt(payloadDirectory.get(),finalName));
  require(before&&linked&&before->links==2&&io::sameStateIdentity(*before,*linked),
      "Completion link does not name the authenticated file");
  const auto wrongRemoval=io::removePayloadName(payloadDirectory.get(),occupiedName,*before);
  require(!wrongRemoval&&wrongRemoval.error().code==storage::ErrorCode::Conflict,"Completion retired an unrelated name");
  verify(io::removePayloadName(payloadDirectory.get(),firstName,*before));
  const auto retained=take(io::stateStampAt(payloadDirectory.get(),finalName));
  require(!take(io::stateStampAt(payloadDirectory.get(),firstName))&&retained&&retained->links==1
      &&io::sameStateIdentity(*retained,*linked),"Removing the old payload name changed the retained file");
  verifyReads(copy.get(),SecondOffset,second);
  require(!offsetMismatch,"The held descriptor lost the retained payload after removing its old name");
  std::cout << "Unicode payload names, concurrent offsets and completion aliases passed\n";
}
} // namespace

int main(int argc,char** argv) {
  try {
    TemporaryDirectory directory;
    if (argc==2&&std::string_view(argv[1])=="--managed-paths") { testManagedPaths(directory.path); }
    else {
      testSparsePayload(directory.path / fs::path(u8"payload-\u03bb-lazy"), false);
      testSparsePayload(directory.path / fs::path(u8"payload-\u03bb-length"), true);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/write_resume_data.hpp>

#ifdef _WIN32
#include "original_disk_io.hpp"
#include "metadata_disk_router.hpp"
#include <libtorrent/peer_request.hpp>
#include <libtorrent/performance_counters.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace lt = libtorrent;
using namespace std::chrono_literals;
constexpr int PieceSize = 1024 * 1024;
constexpr int PieceCount = 64;
constexpr int LogicalSize = PieceSize * PieceCount;
constexpr lt::piece_index_t First{0}, Middle{PieceCount / 2}, Last{PieceCount - 1};

void require(bool condition, const char* description) {
  if (!condition) { throw std::runtime_error(description); }
}

class TestFolder {
public:
  TestFolder() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path = fs::temp_directory_path() / ("biglybtc-sparse-test-" + std::to_string(stamp));
    require(fs::create_directory(path), "Cannot create test directory");
  }
  ~TestFolder() {
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
  fs::path path;
};

lt::settings_pack settings() {
  lt::settings_pack config;
  config.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
  config.set_str(lt::settings_pack::outgoing_interfaces, "127.0.0.1");
  for (const auto option : {lt::settings_pack::enable_dht, lt::settings_pack::enable_lsd,
          lt::settings_pack::enable_upnp, lt::settings_pack::enable_natpmp}) {
    config.set_bool(option, false);
  }
  config.set_int(lt::settings_pack::alert_mask,
      int(lt::alert_category::error | lt::alert_category::storage));
  return config;
}

lt::add_torrent_params torrent(const fs::path& root) {
  lt::file_storage file;
  file.add_file("payload.bin", LogicalSize);
  lt::create_torrent builder(file, PieceSize, lt::create_torrent::v1_only);
  // The missing pieces advertise one MiB of 'M' so their zero-filled holes cannot verify.
  const auto headHash = lt::sha1_hash("\x9a\x7d\xcf\xd9\x02\x9d\xe8\x6d\xc0\x88\xee\x6e\xbb\xef\x48\xdf\x90\xe7\xc6\xcd");
  const auto middleHash = lt::sha1_hash("\xfc\x5e\x35\x0f\xa7\xcb\x2e\x3b\x49\xd0\xe0\xc8\x4b\x21\x01\x5d\x8e\x25\x96\xd1");
  const auto tailHash = lt::sha1_hash("\x4f\x21\xab\xc8\x65\x34\x9b\xd6\xfa\x8d\x2e\x1d\x64\x20\x0c\x6f\x56\x52\xe4\xc0");
  for (int piece = 0; piece < PieceCount; ++piece) {
    builder.set_hash(lt::piece_index_t{piece}, middleHash);
  }
  builder.set_hash(First, headHash);
  builder.set_hash(Last, tailHash);
  const auto bytes = builder.generate_buf();
  lt::add_torrent_params params;
  params.ti = std::make_shared<lt::torrent_info>(lt::span<char const>{bytes}, lt::from_span);
  const auto utf8 = root.u8string();
  params.save_path.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
  params.storage_mode = lt::storage_mode_sparse;
  params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
  params.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd
      | lt::torrent_flags::disable_pex;
  return params;
}

template <typename Condition>
void waitFor(Condition condition, const char* failure) {
  const auto end = std::chrono::steady_clock::now() + 30s;
  while (!condition()) {
    require(std::chrono::steady_clock::now() < end, failure);
    std::this_thread::sleep_for(10ms);
  }
}

std::vector<char> writePieces(lt::add_torrent_params params) {
  lt::session session(settings());
  auto handle = session.add_torrent(std::move(params));
  waitFor([&] { return handle.status().state == lt::torrent_status::downloading; },
      "Torrent did not become ready for piece writes");
  handle.add_piece(First, std::vector<char>(PieceSize, 'A'));
  handle.add_piece(Last, std::vector<char>(PieceSize, 'Z'));
  waitFor([&] { return handle.have_piece(First) && handle.have_piece(Last); },
      "Libtorrent did not verify both written pieces");
  require(!handle.have_piece(Middle), "An unwritten piece is marked complete");
  // Resume data contains flushed pieces. The flush_disk_cache flag schedules
  // the disk operation, so wait for its completion before taking the snapshot.
  handle.flush_cache();
  waitFor([&] {
    std::vector<lt::alert*> alerts;
    session.pop_alerts(&alerts);
    return std::ranges::any_of(alerts, [](const auto* alert) {
      return lt::alert_cast<lt::cache_flushed_alert>(alert) != nullptr;
    });
  }, "Libtorrent did not acknowledge the disk flush");
  handle.save_resume_data();
  std::vector<char> checkpoint;
  waitFor([&] {
    std::vector<lt::alert*> alerts;
    session.pop_alerts(&alerts);
    for (auto* alert : alerts) {
      if (const auto* saved = lt::alert_cast<lt::save_resume_data_alert>(alert)) {
        const auto& pieces = saved->params.have_pieces;
        require(pieces.size() == PieceCount && pieces[First] && pieces[Last] && !pieces[Middle],
            "Resume checkpoint does not describe the two written pieces");
        checkpoint = lt::write_resume_data_buf(saved->params);
        return true;
      }
      require(!lt::alert_cast<lt::save_resume_data_failed_alert>(alert), "Cannot save resume data");
    }
    return false;
  }, "Resume data did not arrive");
  return checkpoint;
}

std::uintmax_t physicalSize(const fs::path& path) {
#ifdef _WIN32
  const auto attributes = GetFileAttributesW(path.c_str());
  require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_SPARSE_FILE),
      "Libtorrent payload is missing the NTFS sparse attribute");
  DWORD high{};
  SetLastError(ERROR_SUCCESS);
  const auto low = GetCompressedFileSizeW(path.c_str(), &high);
  require(low != INVALID_FILE_SIZE || GetLastError() == ERROR_SUCCESS, "Cannot read NTFS allocation size");
  return (static_cast<std::uintmax_t>(high) << 32) | low;
#else
  struct stat info{};
  require(::stat(path.c_str(), &info) == 0, "Cannot read allocation size");
  return static_cast<std::uintmax_t>(info.st_blocks) * 512;
#endif
}

void checkPayload(const fs::path& file) {
  require(fs::file_size(file) == LogicalSize, "Incorrect logical length");
  const auto allocated = physicalSize(file);
  require(allocated > 0 && allocated <= 4 * PieceSize, "Sparse payload occupies too much physical space");
  std::ifstream input(file, std::ios::binary);
  for (const auto& [offset, expected] :
       {std::pair{0, 'A'}, std::pair{PieceSize * 32, '\0'}, std::pair{LogicalSize - PieceSize, 'Z'}}) {
    std::array<char, 128> bytes;
    input.seekg(offset);
    require(bool(input.read(bytes.data(), bytes.size())), "Cannot read payload range");
    require(std::ranges::all_of(bytes, [=](char byte) { return byte == expected; }), "Unexpected payload or hole bytes");
  }
  std::cout << "Logical bytes: " << LogicalSize << ", allocated bytes: " << allocated << '\n';
}

#ifdef _WIN32
namespace engine = biglybtc::engine::detail;
namespace storage = biglybtc::storage;
using storage::detail::OriginalPayloadAccess;

std::vector<std::optional<engine::OriginalDiskFile>> openOriginalPayload(
    const fs::path& path, OriginalPayloadAccess access) {
  const auto parent = ::CreateFileW(path.parent_path().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  require(parent != INVALID_HANDLE_VALUE, "Cannot hold the client payload directory");
  storage::detail::OriginalPayloadDescriptor directory(parent);
  if (access == OriginalPayloadAccess::CreateExclusive) {
    const auto missing = storage::detail::openOriginalPayloadAt(directory.get(), path.filename(),
        OriginalPayloadAccess::ReadWrite);
    require(!missing && missing.error().code == storage::ErrorCode::Missing && !fs::exists(path),
        "Writable reopen created a missing payload");
  }
  auto descriptor = storage::detail::openOriginalPayloadAt(directory.get(), path.filename(), access);
  if (!descriptor) { throw std::runtime_error(descriptor.error().message); }
  if (access == OriginalPayloadAccess::CreateExclusive) {
    const auto collision = storage::detail::openOriginalPayloadAt(directory.get(), path.filename(), access);
    require(!collision && collision.error().code == storage::ErrorCode::Conflict,
        "Exclusive payload create accepted a name collision");
    const auto stream = storage::detail::openOriginalPayloadAt(directory.get(), L"payload.bin:stream", access);
    require(!stream && stream.error().code == storage::ErrorCode::UnsafePath,
        "A relative payload name addressed an alternate data stream");
  }
  auto io = storage::OriginalFileIo::open(descriptor->get(), {0, LogicalSize, PieceSize},
      storage::OriginalStorageKind::Linear, {});
  if (!io) { throw std::runtime_error(io.error().message); }
  if (access == OriginalPayloadAccess::CreateExclusive) {
    const auto sparse = io->enableSparse();
    if (!sparse) { throw std::runtime_error(sparse.error().message); }
    require(io->length() == 0, "Sparse payload must start at zero length");
  } else {
    require(io->length() == LogicalSize, "Reopening changed the existing logical length");
    const auto writer = storage::detail::openOriginalPayloadAt(directory.get(), path.filename(),
        OriginalPayloadAccess::ReadWrite);
    require(!writer && writer.error().code == storage::ErrorCode::Conflict,
        "A second writer opened the held payload");
  }
  std::vector<std::optional<engine::OriginalDiskFile>> files;
  files.emplace_back(engine::OriginalDiskFile{std::move(*descriptor), std::move(*io)});
  return files;
}

void checkOriginalDisk(const fs::path& path, const lt::add_torrent_params& params, OriginalPayloadAccess opening) {
  const auto& metadata = *params.ti;
  const bool readOnly = opening == OriginalPayloadAccess::ReadOnly;
  const auto access = readOnly ? engine::OriginalDiskAccess::ReadOnly : engine::OriginalDiskAccess::ReadWrite;
  auto files = std::make_shared<engine::OriginalDiskFiles>(params.ti, openOriginalPayload(path, opening), access);
  const auto reserved = path.parent_path() / fs::path(u8"reserved-\u03bb");
  const auto encodedRoute = reserved.u8string();
  const std::string sdkRoute(encodedRoute.begin(), encodedRoute.end());
  auto policy = std::make_shared<engine::MetadataDiskPolicy>();
  require(policy->registerOriginalPath(reserved, files).has_value(), "Cannot register Unicode disk route");
  lt::io_context context;
  lt::settings_pack configuration;
  lt::counters counters;
  auto disk = engine::metadataDiskConstructor(std::move(policy))(context, configuration, counters);
  lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities;
  priorities.push_back(lt::download_priority_t{4});
  const lt::storage_params admission{metadata.files(), nullptr, sdkRoute,
      lt::storage_mode_sparse, priorities, metadata.info_hash()};
  auto holder = disk->new_torrent(admission, {});
  const auto poll = [&] { context.restart(); context.poll(); };
  if (!readOnly) {
    std::size_t completed = 0;
    bool failed = false;
    const auto writePiece = [&](lt::piece_index_t piece, char value) {
      const std::string block(lt::default_block_size, value);
      for (int offset = 0; offset < PieceSize; offset += lt::default_block_size) {
        disk->async_write(holder, {piece, offset, lt::default_block_size}, block.data(), {},
            [&](const lt::storage_error& error) { failed |= bool(error.ec); ++completed; });
      }
    };
    const bool create = opening == OriginalPayloadAccess::CreateExclusive;
    if (create) {
      writePiece(Last, 'Z');
      writePiece(First, 'A');
    } else {
      const std::string block(lt::default_block_size, 'Z');
      disk->async_write(holder, {Last, 0, lt::default_block_size}, block.data(), {},
          [&](const lt::storage_error& error) { failed |= bool(error.ec); ++completed; });
    }
    const auto writes = create ? 2 * PieceSize / lt::default_block_size : 1;
    waitFor([&] { poll(); return completed == static_cast<std::size_t>(writes); },
        "Client disk writes timed out");
    require(!failed, "Client disk backend rejected a sparse write");
  }
  std::size_t hashed = 0;
  bool failed = false;
  // The disk returns the SHA-1 of the actual zero-filled MiB for the hole.
  const auto holeHash = lt::sha1_hash("\x3b\x71\xf4\x3f\xf3\x0f\x4b\x15\xb5\xcd\x85\xdd\x9e\x95\xeb\xc7\xe8\x4e\xb5\xa3");
  for (const auto piece : {First, Middle, Last}) {
    disk->async_hash(holder, piece, {}, lt::disk_interface::v1_hash,
        [&, piece](lt::piece_index_t observed, const lt::sha1_hash& hash, const lt::storage_error& error) {
          const auto expected = piece == Middle ? holeHash : metadata.hash_for_piece(piece);
          if (error.ec || observed != piece || hash != expected) {
            std::cerr << "Original disk hash: access=" << static_cast<int>(opening)
                      << " piece=" << static_cast<int>(piece) << " observed=" << static_cast<int>(observed)
                      << " actual=" << hash << " expected=" << expected << " error=" << error.ec.message() << '\n';
            failed = true;
          }
          ++hashed;
        });
  }
  waitFor([&] { poll(); return hashed == 3; }, "Client disk hashing timed out");
  require(!failed, "Unexpected disk hash for the two pieces or the sparse hole");
  bool retired = false;
  disk->async_stop_torrent(holder, [&] { retired = true; });
  waitFor([&] { poll(); return retired; }, "Client disk flush and stop timed out");
  require(!files->failure(), "Client disk backend failed to flush the payload");
  if (readOnly) {
    const std::string block(lt::default_block_size, 'X');
    bool rejected = false;
    disk->async_write(holder, {First, 0, lt::default_block_size}, block.data(), {},
        [&](const lt::storage_error& error) { rejected = bool(error.ec); });
    waitFor([&] { poll(); return rejected; }, "Client disk backend accepted an existing-only write");
  }
  holder.reset();
  disk->abort(true);
  poll();
  require(!fs::exists(reserved), "The disk router sent a reserved path to stock storage");
}
#endif

void reopen(lt::add_torrent_params params, const char* failure) {
  lt::session session(settings());
  auto handle = session.add_torrent(std::move(params));
  try {
    waitFor([&] {
      std::vector<lt::alert*> alerts;
      session.pop_alerts(&alerts);
      for (const auto* alert : alerts) {
        if (lt::alert_cast<lt::fastresume_rejected_alert>(alert)
            || lt::alert_cast<lt::file_error_alert>(alert)) {
          std::cerr << alert->message() << '\n';
        }
      }
      const auto status = handle.status();
      if (status.errc) { throw std::runtime_error(status.errc.message()); }
      return handle.have_piece(First) && handle.have_piece(Last);
    }, failure);
  } catch (...) {
    const auto status = handle.status();
    std::cerr << "Reopen state: " << int(status.state) << ", flags: "
              << static_cast<lt::torrent_flags_t::underlying_type>(status.flags)
              << ", verified pieces: " << status.num_pieces << '\n';
    throw;
  }
  require(!handle.have_piece(Middle), "Reopening treated a hole as complete");
}
} // namespace

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) try {
  const TestFolder folder;
#ifdef _WIN32
  wchar_t filesystem[MAX_PATH]{};
  DWORD flags{};
  std::wstring volume(MAX_PATH, L'\0');
  require(GetVolumePathNameW(folder.path.c_str(), volume.data(), static_cast<DWORD>(volume.size())),
      "Cannot find the filesystem containing the test directory");
  require(GetVolumeInformationW(volume.c_str(), nullptr, 0, nullptr, nullptr, &flags, filesystem, MAX_PATH),
      "Cannot identify the filesystem containing the test directory");
  require(std::wstring_view(filesystem) == L"NTFS" && (flags & FILE_SUPPORTS_SPARSE_FILES),
      "This Windows test requires an NTFS volume with sparse-file support");
#endif
  auto params = torrent(folder.path);
#ifdef _WIN32
  if (argc == 2 && std::string_view(argv[1]) == "--original-disk") {
    const auto file = folder.path / fs::path(u8"client-payload-\u03bb.bin");
    checkOriginalDisk(file, params, OriginalPayloadAccess::CreateExclusive);
    checkPayload(file);
    checkOriginalDisk(file, params, OriginalPayloadAccess::ReadWrite);
    checkPayload(file);
    checkOriginalDisk(file, params, OriginalPayloadAccess::ReadOnly);
    checkPayload(file);
    return 0;
  }
#endif
  const auto checkpoint = writePieces(params);
  const auto file = folder.path / "payload.bin";
  checkPayload(file);
  auto resume = lt::read_resume_data(lt::span<char const>{checkpoint});
  resume.ti = params.ti;
  resume.save_path = params.save_path;
  resume.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed
      | lt::torrent_flags::stop_when_ready);
  bool success = true;
  for (auto& [input, scenario] : std::array{
           std::pair{std::move(resume), "Could not restore the written pieces from resume data"},
           std::pair{std::move(params), "Could not recheck the existing sparse file"}}) {
    try {
      reopen(std::move(input), scenario);
      checkPayload(file);
    } catch (const std::exception& error) {
      std::cerr << scenario << ": " << error.what() << '\n';
      success = false;
    }
  }
  return success ? 0 : 1;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}

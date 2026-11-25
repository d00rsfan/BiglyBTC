#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/write_resume_data.hpp>

#ifdef _WIN32
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
  // Independent SHA-1 fixtures for one MiB of each repeated byte.
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

int main() try {
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

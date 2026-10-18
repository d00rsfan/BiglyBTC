#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include "biglybtc/torrent_layout.hpp"

#include <chrono>
#include <clocale>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
namespace lt = libtorrent;
namespace fs = std::filesystem;
using namespace std::chrono_literals;
void require(bool condition, const std::string& message) { if (!condition) { throw std::runtime_error(message); } }
void owned(const fs::path& path) {
  const auto normalized = path.lexically_normal();
  auto part = normalized.begin();
  bool safe = part != normalized.end() && *part++ == "/";
  safe = safe && part != normalized.end() && *part++ == "tmp";
  safe = safe && part != normalized.end() && part->string().starts_with("biglybtc-");
  require(safe && normalized == path, "Only generated /tmp/biglybtc-* paths are accepted");
}
lt::settings_pack settings() {
  lt::settings_pack result;
  result.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
  result.set_str(lt::settings_pack::outgoing_interfaces, "127.0.0.1");
  result.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
  for (auto key : {lt::settings_pack::enable_dht, lt::settings_pack::enable_lsd,
      lt::settings_pack::enable_upnp, lt::settings_pack::enable_natpmp,
      lt::settings_pack::enable_incoming_utp, lt::settings_pack::enable_outgoing_utp}) { result.set_bool(key, false); }
  result.set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::error | lt::alert_category::status));
  return result;
}
std::shared_ptr<lt::torrent_info> metadata(const fs::path& path) {
  owned(path);
  lt::error_code error;
  auto info = std::make_shared<lt::torrent_info>(path.string(), error);
  require(!error, "Invalid generated metadata: " + error.message());
  info->clear_trackers();
  require(info->web_seeds().empty(), "Generated metadata unexpectedly has web seeds");
  return info;
}
void describe(const lt::torrent_info& info) {
  const auto& files = info.files();
  std::cout << "META files=" << files.num_files() << " size=" << info.total_size() << " pieces=" << info.num_pieces() << '\n';
  for (auto index : files.file_range()) {
    std::cout << "FILE " << static_cast<int>(index) << " size=" << files.file_size(index)
        << " offset=" << files.file_offset(index) << " pad=" << files.pad_file_at(index)
        << " path=" << std::quoted(files.file_path(index)) << '\n';
  }
}
void alerts(lt::session& session) {
  std::vector<lt::alert*> pending;
  session.pop_alerts(&pending);
  require(pending.size() <= 4096, "Unexpected local test alert flood");
  for (auto* alert : pending) {
    if (lt::alert_cast<lt::file_error_alert>(alert) || lt::alert_cast<lt::torrent_error_alert>(alert)
        || lt::alert_cast<lt::listen_failed_alert>(alert)) { throw std::runtime_error(alert->message()); }
  }
}
template<typename Predicate> void wait(lt::session& session, Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 30s;
  while (std::chrono::steady_clock::now() < deadline) {
    alerts(session);
    if (predicate()) { return; }
    std::this_thread::sleep_for(20ms);
  }
  throw std::runtime_error("Owned multifile peer verification deadline expired");
}
std::string bytes(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  require(static_cast<bool>(stream), "Generated physical file is unavailable");
  return {std::istreambuf_iterator<char>(stream), {}};
}
std::map<fs::path, std::string> snapshot(const fs::path& root) {
  std::map<fs::path, std::string> result;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    require(!entry.is_symlink(), "Generated physical fixtures cannot contain symbolic links");
    if (entry.is_regular_file()) { result.emplace(entry.path().lexically_relative(root), bytes(entry.path())); }
    else { require(entry.is_directory(), "Unexpected generated physical file type"); }
  }
  return result;
}
void run(bool seed, const fs::path& torrent, const fs::path& source, const fs::path& work) {
  owned(source);
  owned(work);
  require(fs::create_directory(work), "Multifile peer work must be new");
  fs::create_directory(work / "payload");
  const auto before = snapshot(source);
  auto info = metadata(torrent);
  describe(*info);
  const auto root = work / "payload" / info->name();
  fs::create_directories(root);
  for (const auto& [path, contents] : before) {
    const auto target = root / path;
    fs::create_directories(target.parent_path());
    std::ofstream stream(target, std::ios::binary);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    require(static_cast<bool>(stream), "Unable to copy generated payload fixture");
  }
  lt::session session(settings());
  lt::add_torrent_params params;
  params.ti = info;
  params.save_path = (work / "payload").string();
  params.flags &= ~lt::torrent_flags::paused;
  params.flags &= ~lt::torrent_flags::auto_managed;
  params.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
  if (!seed) {
    for (auto index : info->files().file_range()) {
      if (info->files().pad_file_at(index)) { continue; }
      const auto relative = fs::path(info->files().file_path(index));
      require(!relative.is_absolute() && relative.lexically_normal() == relative, "Unsafe generated metadata path");
      const auto path = work / "payload" / relative;
      if (!fs::exists(path)) {
        auto incomplete = path;
        incomplete += ".az!";
        require(fs::is_regular_file(incomplete), "Original generated file mapping is missing");
        params.renamed_files[index] = incomplete.lexically_relative(work / "payload").string();
      }
    }
  }
  lt::error_code error;
  auto handle = session.add_torrent(params, error);
  require(!error && handle.is_valid(), "Unable to open generated multifile data: " + error.message());
  wait(session, [&] {
    const auto status = handle.status();
    return status.state != lt::torrent_status::checking_files && status.state != lt::torrent_status::checking_resume_data;
  });
  const auto status = handle.status();
  std::cout << "CHECK pieces=" << status.num_pieces << " done=" << status.total_done << " wanted_done=" << status.total_wanted_done << '\n';
  require(before == snapshot(root), "Native recheck changed copied original physical data");
  require(before == snapshot(source), "Native probe changed original generated source files");
  if (seed) {
    require(status.is_seeding, "Owned full multifile seed did not verify");
    wait(session, [&] { return session.listen_port() != 0; });
    std::ofstream ready(work / "ready.saving");
    ready << session.listen_port() << '\n';
    ready.close();
    require(static_cast<bool>(ready), "Cannot publish owned multifile seed endpoint");
    fs::rename(work / "ready.saving", work / "ready");
    const auto deadline = std::chrono::steady_clock::now() + 180s;
    while (!fs::exists(work / "stop") && std::chrono::steady_clock::now() < deadline) {
      alerts(session);
      std::this_thread::sleep_for(50ms);
    }
    require(fs::exists(work / "stop"), "Owned multifile seed control deadline expired");
  }
  std::cout << "PASS original multifile physical mapping verification\n";
}
}

int main(int argc, char** argv) {
  try {
    require(std::setlocale(LC_CTYPE, "") != nullptr, "Cannot initialize the generated-file character locale");
    if (argc >= 3 && std::string_view(argv[1]) == "--original-info") {
      for (int i = 2; i < argc; ++i) {
        owned(argv[i]);
        auto document = biglybtc::core::Document::parse(bytes(argv[i]));
        require(document.has_value(), "Invalid original metadata document");
        auto layout = biglybtc::compat::originalLayout(*document);
        require(layout.has_value(), layout ? "" : layout.error().message);
        std::cout << argv[i] << " ORIGINAL files=" << layout->files.size() << " size=" << layout->bytes << '\n';
        for (std::size_t index = 0; index < layout->files.size(); ++index) {
          const auto& file = layout->files[index];
          std::cout << "ORIGINAL_FILE " << index << " size=" << file.bytes << " offset=" << file.offset
              << " pad=" << file.padding << " path=" << std::quoted(file.path) << '\n';
        }
      }
      return 0;
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--inspect") {
      for (int i = 2; i < argc; ++i) { std::cout << argv[i] << '\n'; describe(*metadata(argv[i])); }
      return 0;
    }
    require(argc == 5, "Usage: multifile-peer --inspect METADATA... | --seed|--check METADATA SOURCE_ROOT NEW_WORK");
    const std::string_view mode(argv[1]);
    require(mode == "--seed" || mode == "--check", "Unknown multifile test-peer mode");
    run(mode == "--seed", argv[2], argv[3], argv[4]);
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/write_resume_data.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace lt = libtorrent;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

void require(bool value, const std::string& message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

lt::settings_pack localSettings() {
  lt::settings_pack settings;
  settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
  settings.set_str(lt::settings_pack::outgoing_interfaces, "127.0.0.1");
  settings.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
  settings.set_bool(lt::settings_pack::enable_dht, false);
  settings.set_bool(lt::settings_pack::enable_lsd, false);
  settings.set_bool(lt::settings_pack::enable_upnp, false);
  settings.set_bool(lt::settings_pack::enable_natpmp, false);
  settings.set_bool(lt::settings_pack::enable_outgoing_utp, false);
  settings.set_bool(lt::settings_pack::enable_incoming_utp, false);
  settings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
  settings.set_int(lt::settings_pack::alert_mask,
      static_cast<int>(lt::alert_category::error | lt::alert_category::status | lt::alert_category::storage));
  return settings;
}

void alerts(lt::session& session) {
  std::vector<lt::alert*> pending;
  session.pop_alerts(&pending);
  require(pending.size() <= 1000, "Unexpected alert flood");
  for (auto* alert : pending) {
    if (lt::alert_cast<lt::listen_failed_alert>(alert)
        || lt::alert_cast<lt::file_error_alert>(alert)
        || lt::alert_cast<lt::torrent_error_alert>(alert)) {
      throw std::runtime_error(alert->message());
    }
  }
}

template <typename Predicate>
void waitFor(lt::session& session, Predicate predicate, const std::string& description) {
  const auto deadline = std::chrono::steady_clock::now() + 20s;
  while (std::chrono::steady_clock::now() < deadline) {
    alerts(session);
    if (predicate()) {
      return;
    }
    std::this_thread::sleep_for(20ms);
  }
  throw std::runtime_error("Timeout: " + description);
}

std::shared_ptr<lt::torrent_info> metadata(const fs::path& path) {
  lt::error_code error;
  auto info = std::make_shared<lt::torrent_info>(path.string(), error);
  require(!error, "Unable to read Java metadata: " + error.message());
  // The oracle's announce URL is loopback; strip every discovery source anyway.
  info->clear_trackers();
  require(info->web_seeds().empty(), "Fixture unexpectedly has web seeds");
  return info;
}

lt::torrent_handle add(lt::session& session, const std::shared_ptr<lt::torrent_info>& info,
    const fs::path& save, bool incomplete = false) {
  lt::add_torrent_params parameters;
  parameters.ti = info;
  parameters.save_path = save.string();
  parameters.flags &= ~lt::torrent_flags::paused;
  parameters.flags &= ~lt::torrent_flags::auto_managed;
  parameters.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd
      | lt::torrent_flags::disable_pex;
  parameters.trackers.clear();
  if (incomplete) {
    parameters.renamed_files[lt::file_index_t{0}] = "generated.bin.az!";
  }
  lt::error_code error;
  auto handle = session.add_torrent(parameters, error);
  require(!error && handle.is_valid(), "Unable to add fixture: " + error.message());
  return handle;
}

std::string bytes(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  require(static_cast<bool>(file), "Missing generated payload");
  return std::string(std::istreambuf_iterator<char>(file), {});
}

void variant(const fs::path& fixtures, const fs::path& work, int type) {
  const auto info = metadata(fixtures / "profile" / ("generated-" + std::to_string(type) + ".torrent"));
  require(info->num_pieces() == 8, "Unexpected piece count");
  std::ostringstream hashes;
  if (info->info_hashes().has_v1()) {
    hashes << "v1=" << info->info_hashes().v1 << ' ';
  }
  if (info->info_hashes().has_v2()) {
    hashes << "v2=" << info->info_hashes().v2;
  }
  const auto base = work / ("type-" + std::to_string(type));
  fs::create_directories(base / "seed");
  fs::create_directories(base / "receive");
  fs::create_directories(base / "partial");
  fs::copy_file(fixtures / "generated.bin", base / "seed/generated.bin");
  fs::copy_file(fixtures / "profile/generated.bin.az!", base / "partial/generated.bin.az!");
  const auto partialBefore = bytes(base / "partial/generated.bin.az!");
  {
    lt::session partial(localSettings());
    auto handle = add(partial, info, base / "partial", true);
    waitFor(partial, [&] {
      const auto status = handle.status();
      return status.state != lt::torrent_status::checking_files
          && status.state != lt::torrent_status::checking_resume_data;
    }, "partial file recheck");
    const auto status = handle.status();
    require(status.num_pieces == 1 && !status.is_seeding, "Partial payload must validate exactly one complete piece");
    handle.pause();
  }
  // The engine can allocate/extend files while checking; verified bytes must survive.
  require(bytes(base / "partial/generated.bin.az!").substr(0, partialBefore.size()) == partialBefore,
      "Partial payload bytes changed during mapped-file recheck");
  std::cout << "PASS type " << type << " partial .az! mapping and piece validation\n";

  lt::session seed(localSettings());
  auto receive = std::make_unique<lt::session>(localSettings());
  auto source = add(seed, info, base / "seed");
  auto target = add(*receive, info, base / "receive");
  waitFor(seed, [&] { return source.status().is_seeding && seed.listen_port() != 0; }, "seed verification and loopback listener");
  {
    lt::session partial(localSettings());
    auto handle = add(partial, info, base / "partial", true);
    handle.connect_peer({lt::make_address("127.0.0.1"), seed.listen_port()});
    waitFor(partial, [&] { return handle.status().is_seeding; }, "incomplete mapped-file continuation");
  }
  require(bytes(base / "partial/generated.bin.az!") == bytes(fixtures / "generated.bin"),
      "Incomplete mapped-file continuation corrupted content");
  std::cout << "PASS type " << type << " incomplete mapped-file transfer completion\n";

  fs::create_directories(base / "corrupt");
  auto damaged = bytes(fixtures / "generated.bin");
  damaged[3 * 16384 + 17] ^= 0x40;
  {
    std::ofstream file(base / "corrupt/generated.bin", std::ios::binary);
    file.write(damaged.data(), static_cast<std::streamsize>(damaged.size()));
    require(static_cast<bool>(file), "Unable to create corrupt fixture");
  }
  {
    lt::session corrupt(localSettings());
    auto handle = add(corrupt, info, base / "corrupt");
    waitFor(corrupt, [&] {
      const auto status = handle.status();
      return status.state != lt::torrent_status::checking_files
          && status.state != lt::torrent_status::checking_resume_data;
    }, "corrupt content recheck");
    require(handle.status().num_pieces == 7 && !handle.status().is_seeding,
        "A damaged piece was trusted or good pieces were lost");
    handle.connect_peer({lt::make_address("127.0.0.1"), seed.listen_port()});
    waitFor(corrupt, [&] { return handle.status().is_seeding; }, "corrupt piece repair");
  }
  require(bytes(base / "corrupt/generated.bin") == bytes(fixtures / "generated.bin"),
      "Corrupt piece repair did not restore exact content");
  std::cout << "PASS type " << type << " corruption detected and repaired\n";

  target.connect_peer({lt::make_address("127.0.0.1"), seed.listen_port()});
  waitFor(*receive, [&] { return target.status().is_seeding; }, "local content transfer");
  require(bytes(base / "receive/generated.bin") == bytes(fixtures / "generated.bin"), "Transferred payload mismatch");
  target.save_resume_data(lt::torrent_handle::save_info_dict | lt::torrent_handle::flush_disk_cache);
  std::vector<char> resume;
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (resume.empty() && std::chrono::steady_clock::now() < deadline) {
    std::vector<lt::alert*> pending;
    receive->pop_alerts(&pending);
    for (auto* alert : pending) {
      if (const auto* saved = lt::alert_cast<lt::save_resume_data_alert>(alert)) {
        resume = lt::write_resume_data_buf(saved->params);
      }
      if (const auto* failed = lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) {
        throw std::runtime_error(failed->message());
      }
    }
    std::this_thread::sleep_for(20ms);
  }
  require(!resume.empty(), "Engine did not produce resume data");
  lt::error_code error;
  auto restored = lt::read_resume_data(resume, error);
  require(!error && restored.info_hashes == info->info_hashes(), "Native resume round trip failed");
  receive.reset();
  restored.save_path = (base / "receive").string();
  restored.flags &= ~lt::torrent_flags::paused;
  restored.flags &= ~lt::torrent_flags::auto_managed;
  restored.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd
      | lt::torrent_flags::disable_pex;
  restored.trackers.clear();
  {
    lt::session restarted(localSettings());
    auto handle = restarted.add_torrent(restored, error);
    require(!error && handle.is_valid(), "Native resume restart failed");
    waitFor(restarted, [&] { return handle.status().is_seeding; }, "native resume restart");
  }
  require(bytes(base / "receive/generated.bin") == bytes(fixtures / "generated.bin"),
      "Native resume restart changed content");
  std::cout << "PASS type " << type << " Java metadata, verified local transfer, native-only resume; "
      << hashes.str() << "\n";
}

void seedFixture(const fs::path& fixtures, int type, const fs::path& work) {
  require(type == 1 || type == 2 || type == 3, "Invalid generated torrent variant");
  require(fs::create_directory(work), "Seed work directory must be new");
  fs::create_directory(work / "payload");
  fs::copy_file(fixtures / "generated.bin", work / "payload/generated.bin");
  auto info = metadata(fixtures / "profile" / ("generated-" + std::to_string(type) + ".torrent"));
  lt::session session(localSettings());
  auto handle = add(session, info, work / "payload");
  waitFor(session, [&] { return handle.status().is_seeding && session.listen_port() != 0; }, "standalone local seed");
  {
    std::ofstream ready(work / "ready.saving");
    ready << session.listen_port() << '\n';
    ready.close();
    require(static_cast<bool>(ready), "Unable to publish owned seed endpoint");
  }
  fs::rename(work / "ready.saving", work / "ready");
  const auto deadline = std::chrono::steady_clock::now() + 180s;
  while (!fs::exists(work / "stop") && std::chrono::steady_clock::now() < deadline) {
    alerts(session);
    std::this_thread::sleep_for(100ms);
  }
  require(fs::exists(work / "stop"), "Standalone seed control deadline expired");
  std::cout << "PASS owned loopback seed shut down\n";
}

void receiveFixture(const fs::path& fixtures, int type, const fs::path& work, int port) {
  require(port >= 1 && port <= 65535, "Invalid owned peer endpoint");
  require(fs::create_directory(work), "Receiver work directory must be new");
  fs::create_directory(work / "payload");
  fs::copy_file(fixtures / "profile/generated.bin.az!", work / "payload/generated.bin.az!");
  const auto original = bytes(work / "payload/generated.bin.az!");
  auto info = metadata(fixtures / "profile" / ("generated-" + std::to_string(type) + ".torrent"));
  lt::session session(localSettings());
  auto handle = add(session, info, work / "payload", true);
  waitFor(session, [&] {
    const auto status = handle.status();
    return status.state != lt::torrent_status::checking_files
        && status.state != lt::torrent_status::checking_resume_data;
  }, "standalone receiver partial-file verification");
  require(handle.status().num_pieces == 1, "Standalone receiver lost original partial piece");
  require(bytes(work / "payload/generated.bin.az!").substr(0, original.size()) == original,
      "Standalone receiver changed original partial bytes during recheck");
  handle.connect_peer({lt::make_address("127.0.0.1"), static_cast<unsigned short>(port)});
  waitFor(session, [&] { return handle.status().is_seeding; }, "original-Java/native receiver transfer");
  require(bytes(work / "payload/generated.bin.az!") == bytes(fixtures / "generated.bin"),
      "Original Java/native received content mismatch");
  std::cout << "PEER_TRANSFER_VERIFIED 131072; partial .az! recheck retained one piece\n";
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 6 && std::string_view(argv[1]) == "--receive") {
      const std::string_view type(argv[3]);
      require(type == "1" || type == "2" || type == "3", "Invalid generated torrent variant");
      const std::string port(argv[5]);
      std::size_t used = 0;
      const auto number = std::stoi(port, &used);
      require(used == port.size(), "Invalid owned peer endpoint");
      receiveFixture(argv[2], type.front() - '0', argv[4], number);
      return 0;
    }
    if (argc == 5 && std::string_view(argv[1]) == "--seed") {
      const std::string_view type(argv[3]);
      require(type == "1" || type == "2" || type == "3", "Invalid generated torrent variant");
      seedFixture(argv[2], type.front() - '0', argv[4]);
      return 0;
    }
    require(argc == 3, "Usage: biglybtc-engine-probe GENERATED_FIXTURES NEW_WORK_DIRECTORY");
    const fs::path fixtures(argv[1]);
    const fs::path work(argv[2]);
    require(fs::create_directory(work), "Work directory must be new");
    std::cout << "libtorrent " << lt::version() << "; loopback-only feasibility experiment\n";
    for (const int type : {1, 3, 2}) {
      variant(fixtures, work, type);
    }
    std::cout << "PASS all engine fixture variants; Java resume/storage parity remains pending\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

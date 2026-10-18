#include "local_session.hpp"
#include "local_support.hpp"
#include "multifile_session.hpp"
#include "file_io.hpp"
#include "torrent_state.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <langinfo.h>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace biglybtc::engine {
namespace detail {
namespace lt = libtorrent;
namespace fs = std::filesystem;
using core::Document;
using core::Node;
using storage::Error;
using storage::ErrorCode;
using storage::detail::Descriptor;
using namespace std::chrono_literals;

void require(bool condition, std::string message, ErrorCode code) {
  if (!condition) { throw Failure{{code, std::move(message)}}; }
}
void done(std::expected<void, Error> result) {
  if (!result) { throw Failure{result.error()}; }
}
std::string encoded(const Node& node) {
  auto value = core::encodeValue(node, storage::compatibleConfigLimits());
  require(value.has_value(), "State value exceeds encoding limits", ErrorCode::InvalidFormat);
  return std::move(*value);
}
Document dictionary(const Node* node) {
  auto value = Document::parse(node ? encoded(*node) : "de", storage::compatibleConfigLimits());
  require(value && value->root().type == Node::Type::Dictionary, "Invalid dictionary section", ErrorCode::InvalidFormat);
  return std::move(*value);
}
void put(Document& document, std::string_view key, std::string value) {
  require(document.replaceValue(key, std::move(value)).has_value(), "State edit exceeds limits", ErrorCode::InvalidFormat);
}
std::string stringValue(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string text(const Node* node) {
  require(node && node->type == Node::Type::Bytes && node->bytes.find('\0') == std::string_view::npos,
      "Invalid saved path or attribute", ErrorCode::InvalidFormat);
  return std::string(node->bytes);
}
std::int64_t integer(const Node* node, std::int64_t fallback) {
  if (!node) { return fallback; }
  require(node->type == Node::Type::Integer, "Invalid integer attribute", ErrorCode::InvalidFormat);
  return node->integer;
}
void observe(const LocalOptions& options, LocalStage stage) {
  if (options.observer) { options.observer(stage); }
}
Descriptor directory(const fs::path& path) {
  require(path.is_absolute() && path.lexically_normal() == path, "Payload directory must be absolute and normalized");
  Descriptor current(::open("/", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
  require(current.get() >= 0, "Cannot open filesystem root", ErrorCode::Io);
  for (const auto& part : path.relative_path()) {
    Descriptor next(::openat(current.get(), part.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY));
    require(next.get() >= 0, "Payload directory contains an inaccessible or symbolic component", ErrorCode::UnsafePath);
    current = std::move(next);
  }
  return current;
}
void requireUtf8Filesystem(const fs::path& path) {
  const auto bytes = path.native();
  if (std::ranges::none_of(bytes, [](unsigned char value) { return value >= 128; })) { return; }
  std::string encoding(::nl_langinfo(CODESET));
  std::erase_if(encoding, [](unsigned char value) { return value == '-' || value == '_'; });
  std::ranges::transform(encoding, encoding.begin(), [](unsigned char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32) : static_cast<char>(value);
  });
  require(encoding == "utf8", "These files require a UTF8 filesystem character locale");
}
void sync(int descriptor) { require(::fsync(descriptor) == 0, "Unable to synchronize payload", ErrorCode::Io); }
SavedPolicy readPolicy(const Node& entry, std::size_t files) {
  const auto original = compat::originalFilePolicy(entry, files);
  require(original.has_value(), original ? "" : original.error().message, ErrorCode::InvalidFormat);
  auto priorities = compat::linearPriorityRanks(*original);
  require(priorities.has_value(), priorities ? "" : priorities.error().message);
  auto limit = [&](std::string_view key) {
    const auto value = integer(entry.find(key));
    require(value >= 0 && value <= std::numeric_limits<int>::max(), "Disabled or invalid per-torrent rate limit requires its policy adapter");
    return value == 0 ? -1 : static_cast<int>(value);
  };
  return {std::move(*priorities), limit("maxdl"), limit("maxul")};
}

struct Mapping { fs::path parent; std::string base; std::string actual; std::string suffix; };
constexpr std::string_view intentKey = "BiglyBTC.completion";
struct CompletionIntent {
  std::string source;
  std::string target;
  std::uint64_t device;
  std::uint64_t inode;
};
bool basename(const std::string& value) {
  return !value.empty() && value != "." && value != ".."
      && fs::path(value).filename() == value && value.find('\n') == std::string::npos;
}
std::optional<CompletionIntent> completionIntent(const Node& attributes) {
  const auto* value = attributes.find(intentKey);
  if (!value) { return std::nullopt; }
  require(value->type == Node::Type::Dictionary, "Invalid native completion intent", ErrorCode::InvalidFormat);
  if (value->children.empty()) { return std::nullopt; }
  require(integer(value->find("version")) == 1, "Unknown native completion intent version");
  CompletionIntent result{text(value->find("source")), text(value->find("target")),
      static_cast<std::uint64_t>(integer(value->find("device"), -1)),
      static_cast<std::uint64_t>(integer(value->find("inode"), -1))};
  require(basename(result.source) && basename(result.target) && result.source != result.target
      && result.device <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
      && result.inode <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
      "Unsafe native completion intent", ErrorCode::InvalidFormat);
  return result;
}
void saveIntent(storage::TorrentStateFile& state, const storage::TorrentStateStore& store,
    const storage::ProfileLease& lease, const std::optional<CompletionIntent>& intent) {
  auto attributes = dictionary(state.document().find("attributes"));
  auto value = dictionary(nullptr);
  if (intent) {
    put(value, "version", "i1e");
    put(value, "source", stringValue(intent->source));
    put(value, "target", stringValue(intent->target));
    put(value, "device", "i" + std::to_string(intent->device) + "e");
    put(value, "inode", "i" + std::to_string(intent->inode) + "e");
  }
  put(attributes, intentKey, value.serialize());
  done(state.replaceSection("attributes", attributes.serialize()));
  done(store.save(lease, state));
}
Mapping mapping(const core::SavedDownload& saved, const Node& attributes) {
  require(!saved.legacySavePath && saved.saveFile.has_value(), "Legacy paths need their original mapping adapter");
  Mapping result{fs::path(saved.saveDirectory), *saved.saveFile, *saved.saveFile, {}};
  require(!result.base.empty() && fs::path(result.base).filename() == result.base
      && result.base != "." && result.base != ".." && result.base.find('\n') == std::string::npos,
      "Invalid single-file basename", ErrorCode::UnsafePath);
  const auto* stores = attributes.find("storetypes");
  require(stores && stores->type == Node::Type::List && stores->children.size() == 1
      && text(&stores->children[0]) == "L", "Only original linear storage is qualified in this runner");
  const auto* networks = attributes.find("networks");
  require(networks && networks->type == Node::Type::List && networks->children.size() == 1
      && text(&networks->children[0]) == "Public", "Private or mixed networks require their isolated adapter");
  require(!attributes.find("filelinks") && integer(attributes.find("skipfss")) == 0,
      "Legacy links or skipped-file policy need their original adapter");
  if (const auto* prefix = attributes.find("dnd_pfx")) {
    require(text(prefix).empty(), "Skipped-file prefixes require their original adapter");
  }
  if (const auto* suffix = attributes.find("incompfilesuffix")) { result.suffix = text(suffix); }
  if (const auto* links = attributes.find("filelinks2")) {
    require(links->type == Node::Type::List && links->children.size() <= 1, "Multiple file links require full mapping support");
    for (const auto& link : links->children) {
      auto value = text(&link);
      const auto first = value.find('\n');
      const auto second = value.find('\n', first == std::string::npos ? first : first + 1);
      require(first != std::string::npos && second != std::string::npos && value.substr(0, first) == "0"
          && value.find('\n', second + 1) == std::string::npos, "Unsupported original file-link entry");
      const auto source = value.substr(first + 1, second - first - 1);
      require(source.empty() || fs::path(source) == result.parent / result.base, "File-link source differs from original save location");
      const fs::path target(value.substr(second + 1));
      require(target.is_absolute() && target.parent_path() == result.parent && target.filename() != "."
          && target.filename() != ".." && !target.filename().empty(), "External file links need their qualified adapter");
      result.actual = target.filename().string();
    }
  }
  return result;
}

lt::settings_pack settings() {
  lt::settings_pack result;
  result.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
  result.set_str(lt::settings_pack::outgoing_interfaces, "127.0.0.1");
  result.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
  for (auto key : {lt::settings_pack::enable_dht, lt::settings_pack::enable_lsd,
      lt::settings_pack::enable_upnp, lt::settings_pack::enable_natpmp,
      lt::settings_pack::enable_outgoing_utp, lt::settings_pack::enable_incoming_utp}) { result.set_bool(key, false); }
  result.set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::error
      | lt::alert_category::status | lt::alert_category::storage));
  return result;
}
void errors(const std::vector<lt::alert*>& pending) {
  require(pending.size() <= 4096, "Engine alert bound exceeded", ErrorCode::Io);
  for (auto* alert : pending) {
    if (lt::alert_cast<lt::file_error_alert>(alert) || lt::alert_cast<lt::torrent_error_alert>(alert)
        || lt::alert_cast<lt::listen_failed_alert>(alert) || lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) {
      throw Failure{{ErrorCode::Io, alert->message()}};
    }
  }
}

std::shared_ptr<lt::torrent_info> metadata(const storage::TorrentStateFile& state, bool localOnly = true) {
  const auto bytes = state.document().serialize();
  lt::error_code error;
  auto info = std::make_shared<lt::torrent_info>(lt::span<const char>(bytes.data(), bytes.size()), error, lt::from_span);
  require(!error, "Original state metadata rejected by engine: " + error.message(), ErrorCode::InvalidFormat);
  require(info->num_files() <= 100000 && info->num_pieces() <= 1000000,
      "Metadata exceeds the bounded file/piece query limits", ErrorCode::InvalidFormat);
  const auto* originalInfo = state.document().find("info");
  if (localOnly) {
    require(info->num_files() == 1 && info->num_pieces() > 0 && originalInfo && !originalInfo->find("files"),
        "Multifile metadata needs full file policy support");
    require(info->web_seeds().empty(), "Web seeds require their qualified privacy and request adapter");
  }
  const auto hashes = info->info_hashes();
  const auto key = state.hash().bytes();
  const char* expected = hashes.has_v1() ? hashes.v1.data() : hashes.v2.data();
  require((key.size() == 20 || (!hashes.has_v1() && key.size() == 32))
      && std::memcmp(key.data(), expected, key.size()) == 0, "Original state hash and inventory disagree", ErrorCode::InvalidFormat);
  info->clear_trackers();
  return info;
}

void completeState(storage::TorrentStateFile& state, int pieces, std::int64_t size,
    const Mapping& files, const std::string& completed) {
  auto resume = dictionary(state.document().find("resume"));
  auto data = dictionary(resume.find("data"));
  put(data, "resume data", stringValue(std::string(static_cast<std::size_t>(pieces), '\1')));
  put(data, "blocks", "de");
  put(data, "valid", "i1e");
  put(resume, "data", data.serialize());
  done(state.replaceSection("resume", resume.serialize()));
  auto attributes = dictionary(state.document().find("attributes"));
  put(attributes, "resumecomplete", "i2e");
  auto downloaded = dictionary(attributes.find("filedownloaded"));
  put(downloaded, "downloaded", "li" + std::to_string(size) + "ee");
  put(attributes, "filedownloaded", downloaded.serialize());
  if (completed == files.base) { put(attributes, "filelinks2", "le"); }
  else {
    const auto link = "0\n" + (files.parent / files.base).string() + '\n' + (files.parent / completed).string();
    put(attributes, "filelinks2", "l" + stringValue(link) + "e");
  }
  done(state.replaceSection("attributes", attributes.serialize()));
}

LocalReport run(const storage::ProfileLease& lease, const LocalOptions& options) {
  require(options.peerPort != 0 && options.timeout > 0s && options.timeout <= 180s, "Invalid owned-loopback options");
  require(!options.stop.stop_requested(), "Local session cancelled", ErrorCode::Cancelled);
  auto namedProfile = directory(options.profile);
  struct stat named{}, held{};
  require(lease.valid() && ::fstat(namedProfile.get(), &named) == 0 && ::fstat(lease.directoryHandle(), &held) == 0
      && named.st_dev == held.st_dev && named.st_ino == held.st_ino,
      "Held lease differs from the requested profile", ErrorCode::UnsafePath);
  auto inventory = take(storage::ConfigFile::load(lease, "downloads.config"));
  auto library = core::readLibrary(inventory.document());
  require(library && library->issues.empty() && !library->legacyMap, "Invalid or unsupported saved inventory", ErrorCode::InvalidFormat);
  const core::SavedDownload* selected = nullptr;
  for (const auto& item : library->downloads) {
    if (item.hash && *item.hash == options.hash) {
      require(!selected, "Duplicate saved torrent identity", ErrorCode::InvalidFormat);
      selected = &item;
    }
  }
  require(selected && selected->persistent
      && selected->startup != core::StartupState::Stopped, "Torrent is missing, stopped or requires pending file policy");
  require(std::ranges::none_of(library->paused, [&](const auto& item) { return item.hash == options.hash; }), "Saved pause owner must be honored");
  const auto* entries = inventory.document().find("downloads");
  require(entries && entries->type == Node::Type::List, "Legacy inventory requires its update adapter");
  auto selectedEntry = dictionary(&entries->children.at(selected->sourceIndex));
  auto store = take(storage::TorrentStateStore::open(lease));
  auto original = take(store.load(lease, options.hash));
  auto info = metadata(original, false);
  require(info->num_pieces() > 0 && info->web_seeds().empty(), "Empty torrents or web seeds need their qualified adapter");
  const auto policy = readPolicy(selectedEntry.root(), static_cast<std::size_t>(info->num_files()));
  if (info->num_files() != 1 || original.document().find("info")->find("files")
      || std::filesystem::path(info->files().file_path(lt::file_index_t{0})).has_parent_path()) {
    return take(continueMultifile(lease, options, *selected, inventory, store, original, info, policy));
  }
  auto initialAttributes = dictionary(original.document().find("attributes"));
  auto files = mapping(*selected, initialAttributes.root());
  // Take cache ownership before reading authoritative attributes or touching data.
  auto state = take(store.beginUpdate(lease, options.hash));
  auto attributes = dictionary(state.document().find("attributes"));
  files = mapping(*selected, attributes.root());
  requireUtf8Filesystem(files.parent / files.actual);
  auto intent = completionIntent(attributes.root());
  require(files.parent == options.profile.parent_path() / "payload",
      "Experimental local runner requires its generated sibling payload directory", ErrorCode::UnsafePath);
  observe(options, LocalStage::StatePrepared);
  require(!options.stop.stop_requested(), "Local session cancelled", ErrorCode::Cancelled);
  auto parent = directory(files.parent);
  Descriptor payload(::openat(parent.get(), files.actual.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW));
  struct stat initial{};
  require(payload.get() >= 0 && ::fstat(payload.get(), &initial) == 0 && S_ISREG(initial.st_mode)
      && initial.st_uid == ::geteuid() && initial.st_nlink >= 1 && initial.st_nlink <= 2,
      "Original payload must be an owned regular file", ErrorCode::UnsafePath);
  if (intent) {
    require((files.actual == intent->source || files.actual == intent->target)
        && intent->device == initial.st_dev && intent->inode == initial.st_ino,
        "Completion intent differs from the current payload", ErrorCode::Conflict);
  }
  if (initial.st_nlink == 2) {
    const auto alias = intent ? (files.actual == intent->source ? intent->target : intent->source)
        : files.actual == files.base ? files.base + files.suffix : files.base;
    struct stat other{};
    require(alias != files.actual && ::fstatat(parent.get(), alias.c_str(), &other, AT_SYMLINK_NOFOLLOW) == 0
        && S_ISREG(other.st_mode) && other.st_dev == initial.st_dev && other.st_ino == initial.st_ino,
        "Unrecognized payload hard link", ErrorCode::UnsafePath);
  }
  LocalReport report{0, 0, info->total_size(), {}};
  {
    lt::session session(settings());
    SessionStatisticsSampler statistics(session, options);
    lt::add_torrent_params parameters;
    parameters.download_limit = policy.downloadLimit;
    parameters.upload_limit = policy.uploadLimit;
    for (auto priority : policy.priorities) { parameters.file_priorities.push_back(lt::download_priority_t{priority}); }
    parameters.ti = info;
    parameters.save_path = files.parent.string();
    parameters.renamed_files[lt::file_index_t{0}] = files.actual;
    parameters.flags &= ~lt::torrent_flags::paused;
    parameters.flags &= ~lt::torrent_flags::auto_managed;
    parameters.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
    lt::error_code error;
    auto handle = session.add_torrent(parameters, error);
    require(!error && handle.is_valid(), "Cannot start original payload: " + error.message(), ErrorCode::Io);
    auto nextProgress = std::chrono::steady_clock::time_point::min();
    auto publish = [&](const lt::torrent_status& status, LocalProgress::Phase phase, bool force = false) {
      const auto now = std::chrono::steady_clock::now();
      if (!options.progress || (!force && now < nextProgress)) { return; }
      options.progress({phase, status.total_done, status.download_payload_rate, status.upload_payload_rate,
          status.num_pieces, info->num_pieces()});
      nextProgress = now + 200ms;
    };
    wait(session, options, [&](const auto&) {
      const auto status = handle.status();
      publish(status, LocalProgress::Phase::Checking);
      return status.state != lt::torrent_status::checking_files && status.state != lt::torrent_status::checking_resume_data;
    }, &statistics);
    report.reusedPieces = handle.status().num_pieces;
    observe(options, LocalStage::Checked);
    require(!options.stop.stop_requested(), "Local session cancelled", ErrorCode::Cancelled);
    handle.connect_peer({lt::make_address("127.0.0.1"), options.peerPort});
    wait(session, options, [&](const auto&) {
      const auto status = handle.status();
      publish(status, LocalProgress::Phase::Downloading);
      return status.is_seeding;
    }, &statistics);
    report.received = handle.status().total_payload_download;
    publish(handle.status(), LocalProgress::Phase::Flushing, true);
    statistics.request(); // A fresh observation may arrive during the existing flush wait.
    handle.pause();
    handle.save_resume_data(lt::torrent_handle::flush_disk_cache);
    wait(session, options, [&](const auto& pending) {
      return std::ranges::any_of(pending, [](auto* alert) { return lt::alert_cast<lt::save_resume_data_alert>(alert) != nullptr; });
    }, &statistics);
  } // All engine storage/threads close before publishing Java completion flags.
  struct stat current{};
  require(::fstatat(parent.get(), files.actual.c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0
      && current.st_dev == initial.st_dev && current.st_ino == initial.st_ino && current.st_size == report.bytes,
      "Payload identity or size changed during transfer", ErrorCode::Conflict);
  sync(payload.get());
  observe(options, LocalStage::Flushed);
  auto completed = files.actual;
  bool linked = false;
  if (!intent && !files.suffix.empty() && completed.size() > files.suffix.size() && completed.ends_with(files.suffix)) {
    const auto candidate = completed.substr(0, completed.size() - files.suffix.size());
    require(basename(candidate), "Invalid completed filename", ErrorCode::UnsafePath);
    require(initial.st_dev <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        && initial.st_ino <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
        "Payload identity exceeds compatible intent limits");
    intent = CompletionIntent{files.actual, candidate, initial.st_dev, initial.st_ino};
    saveIntent(state, store, lease, intent);
  }
  if (intent) {
    // Keep the old name alive until both Java state copies reference the new one.
    // A crash at any boundary leaves a readable name for either metadata view.
    if (files.actual == intent->target) {
      completed = intent->target;
      linked = true;
    } else {
      if (::linkat(parent.get(), files.actual.c_str(), parent.get(), intent->target.c_str(), 0) == 0) {
        completed = intent->target;
        linked = true;
        sync(parent.get());
      } else if (errno == EEXIST) {
        struct stat other{};
        if (::fstatat(parent.get(), intent->target.c_str(), &other, AT_SYMLINK_NOFOLLOW) == 0
            && other.st_dev == initial.st_dev && other.st_ino == initial.st_ino) { completed = intent->target; linked = true; }
        // Original Java also retains the incomplete name when the target exists.
      } else { throw Failure{{ErrorCode::Io, "Cannot prepare completion rename; original filename retained"}}; }
    }
    if (!linked) { saveIntent(state, store, lease, std::nullopt); intent.reset(); }
  }
  observe(options, LocalStage::CompletionLinked);
  completeState(state, info->num_pieces(), report.bytes, files, completed);
  done(store.save(lease, state));
  observe(options, LocalStage::CompletionSaved);
  // Do not reset user queue/force/position/policy fields or received counters.
  const auto previous = integer(selectedEntry.find("downloaded"));
  require(previous >= 0 && report.received <= std::numeric_limits<std::int64_t>::max() - previous,
      "Saved transfer counter overflows", ErrorCode::InvalidFormat);
  put(selectedEntry, "downloaded", "i" + std::to_string(previous + report.received) + "e");
  put(selectedEntry, "completedbytes", "i" + std::to_string(report.bytes) + "e");
  put(selectedEntry, "allocated", "i1e");
  std::string updated = "l";
  for (std::size_t index = 0; index < entries->children.size(); ++index) {
    updated += index == selected->sourceIndex ? selectedEntry.serialize() : encoded(entries->children[index]);
  }
  updated += 'e';
  put(inventory.document(), "downloads", std::move(updated));
  done(inventory.save(lease));
  observe(options, LocalStage::InventorySaved);
  if (linked) {
    struct stat alias{};
    if (::fstatat(parent.get(), intent->source.c_str(), &alias, AT_SYMLINK_NOFOLLOW) == 0) {
      require(alias.st_dev == intent->device && alias.st_ino == intent->inode && S_ISREG(alias.st_mode),
          "Completion alias changed; retained without deleting", ErrorCode::Conflict);
      require(::unlinkat(parent.get(), intent->source.c_str(), 0) == 0, "Cannot finish completion rename", ErrorCode::Io);
    } else { require(errno == ENOENT, "Cannot inspect completion alias", ErrorCode::Io); }
    sync(parent.get());
    saveIntent(state, store, lease, std::nullopt);
  }
  report.payload = files.parent / completed;
  return report;
}
} // namespace detail

using namespace detail;

std::expected<LocalReport, Error> continueLocal(const LocalOptions& options) {
  try {
    require(!options.stop.stop_requested(), "Local session cancelled", ErrorCode::Cancelled);
    auto lease = take(storage::ProfileLease::acquire(options.profile));
    return run(lease, options);
  }
  catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::exception& failure) { return std::unexpected(Error{ErrorCode::Io, failure.what()}); }
}

std::expected<LocalReport, Error> continueLocal(const storage::ProfileLease& lease, const LocalOptions& options) {
  try { return run(lease, options); }
  catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::exception& failure) { return std::unexpected(Error{ErrorCode::Io, failure.what()}); }
}

std::expected<TorrentDetails, Error> queryTorrent(const storage::ProfileLease& lease, const core::TorrentHash& hash) {
  try {
    auto store = take(storage::TorrentStateStore::open(lease));
    auto state = take(store.load(lease, hash));
    auto info = metadata(state, false);
    auto inventory = take(storage::ConfigFile::load(lease, "downloads.config"));
    const auto library = core::readLibrary(inventory.document());
    require(library.has_value(), "Invalid saved library", ErrorCode::InvalidFormat);
    const core::SavedDownload* selected = nullptr;
    for (const auto& item : library->downloads) {
      if (item.hash && *item.hash == hash) {
        require(!selected, "Ambiguous original file-details identity", ErrorCode::InvalidFormat);
        selected = &item;
      }
    }
    require(selected, "Download no longer exists in the saved library", ErrorCode::Missing);
    const auto* entries = inventory.document().find("downloads");
    const auto& entry = library->legacyMap ? inventory.document().root().children.at(selected->sourceIndex * 2 + 1)
        : entries->children.at(selected->sourceIndex);
    auto geometry = compat::originalLayout(state.document());
    require(geometry.has_value(), geometry ? "" : geometry.error().message, ErrorCode::InvalidFormat);
    auto policy = compat::originalFilePolicy(entry, geometry->files.size());
    require(policy.has_value(), policy ? "" : policy.error().message, ErrorCode::InvalidFormat);
    std::optional<std::string> displayName;
    if (const auto* attributes = state.document().find("attributes")) {
      if (const auto* value = attributes->find("displayname")) {
        require(value->type == Node::Type::Bytes && value->bytes.size() <= 65536, "Invalid display name", ErrorCode::InvalidFormat);
        displayName = std::string(value->bytes);
      }
    }
    TorrentDetails result{info->name(), std::move(displayName), geometry->bytes,
        static_cast<int>(geometry->files.size()), info->num_pieces()};
    std::size_t names = 0;
    result.entries.reserve(geometry->files.size());
    for (std::size_t i = 0; i < geometry->files.size(); ++i) {
      const auto& file = geometry->files[i];
      require(file.path.size() <= 32 * 1024 * 1024 - names, "File-detail paths exceed their byte limit");
      names += file.path.size();
      result.entries.push_back({static_cast<int>(i), file.path, file.bytes, (*policy)[i].priority, (*policy)[i].skipped, file.padding});
    }
    return result;
  }
  catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::exception& failure) { return std::unexpected(Error{ErrorCode::Io, failure.what()}); }
}
} // namespace biglybtc::engine

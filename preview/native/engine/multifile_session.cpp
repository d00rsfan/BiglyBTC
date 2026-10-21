#include "multifile_session.hpp"
#include "local_support.hpp"
#include "persistent_session_support.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_status.hpp>

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <map>
#include <set>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::engine::detail {
namespace {
namespace fs = std::filesystem;
namespace lt = libtorrent;
using namespace std::chrono_literals;
constexpr std::string_view journalKey = "BiglyBTC.completion";

bool safeRelative(const fs::path& value) {
  if (value.empty() || value.is_absolute() || value.lexically_normal() != value
      || value.string().size() > 4096 || value.string().find('\n') != std::string::npos
      || value.string().find('\0') != std::string::npos) { return false; }
  for (const auto& part : value) { if (part.empty() || part == "." || part == "..") { return false; } }
  return true;
}
bool same(const struct stat& left, const struct stat& right) {
  return S_ISREG(left.st_mode) && S_ISREG(right.st_mode) && left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}
struct Slot {
  std::size_t index;
  fs::path base;
  fs::path actual;
  fs::path final;
  bool padding;
  std::int64_t bytes;
  Descriptor parent{-1};
  Descriptor data{-1};
  struct stat identity{};
};
struct Intent {
  std::size_t index;
  fs::path source;
  fs::path target;
  std::uint64_t device;
  std::uint64_t inode;
  bool linked = false;
};
std::vector<Intent> intents(const Node& attributes, std::size_t count) {
  const auto* root = attributes.find(journalKey);
  if (!root) { return {}; }
  require(root->type == Node::Type::Dictionary, "Invalid multifile completion intent", ErrorCode::InvalidFormat);
  if (root->children.empty()) { return {}; }
  require(integer(root->find("version")) == 2, "Unknown multifile completion intent version");
  const auto* list = root->find("files");
  require(list && list->type == Node::Type::List && list->children.size() <= count, "Invalid multifile intent list", ErrorCode::InvalidFormat);
  std::vector<Intent> result;
  std::set<std::size_t> seen;
  for (const auto& entry : list->children) {
    const auto index = integer(entry.find("index"), -1);
    const auto device = integer(entry.find("device"), -1);
    const auto inode = integer(entry.find("inode"), -1);
    require(index >= 0 && static_cast<std::uint64_t>(index) < count && device >= 0 && inode >= 0,
        "Multifile intent has an invalid identity", ErrorCode::InvalidFormat);
    Intent value{static_cast<std::size_t>(index), fs::path(text(entry.find("source"))),
        fs::path(text(entry.find("target"))), static_cast<std::uint64_t>(device), static_cast<std::uint64_t>(inode)};
    require(seen.insert(value.index).second && safeRelative(value.source) && safeRelative(value.target)
        && value.source != value.target && value.source.parent_path() == value.target.parent_path(),
        "Unsafe multifile completion intent", ErrorCode::UnsafePath);
    result.push_back(std::move(value));
  }
  return result;
}
void saveIntents(storage::TorrentStateFile& state, const storage::TorrentStateStore& store,
    const storage::ProfileLease& lease, const std::vector<Intent>& pending) {
  auto attributes = dictionary(state.document().find("attributes"));
  auto value = dictionary(nullptr);
  if (!pending.empty()) {
    put(value, "version", "i2e");
    std::string list = "l";
    for (const auto& item : pending) {
      auto entry = dictionary(nullptr);
      put(entry, "index", "i" + std::to_string(item.index) + "e");
      put(entry, "source", stringValue(item.source.string()));
      put(entry, "target", stringValue(item.target.string()));
      put(entry, "device", "i" + std::to_string(item.device) + "e");
      put(entry, "inode", "i" + std::to_string(item.inode) + "e");
      list += entry.serialize();
    }
    put(value, "files", list + 'e');
  }
  put(attributes, journalKey, value.serialize());
  done(state.replaceSection("attributes", attributes.serialize()));
  done(store.save(lease, state));
}
std::vector<Slot> mapping(const core::SavedDownload& saved, const Node& attributes,
    const compat::OriginalLayout& layout, const fs::path& save) {
  require(saved.saveFile.has_value() && safeRelative(fs::path(*saved.saveFile))
      && !fs::path(*saved.saveFile).has_parent_path(), "Invalid original multifile save root", ErrorCode::UnsafePath);
  const auto* networks = attributes.find("networks");
  require(networks && networks->type == Node::Type::List && networks->children.size() == 1
      && text(&networks->children[0]) == "Public", "Private or mixed networks require their isolated adapter");
  require(!attributes.find("filelinks") && integer(attributes.find("skipfss")) == 0,
      "Legacy links or skipped-file policy need their original adapter");
  if (const auto* prefix = attributes.find("dnd_pfx")) { require(text(prefix).empty(), "Skipped-file prefixes need their original adapter"); }
  const auto* stores = attributes.find("storetypes");
  require(stores && stores->type == Node::Type::List && stores->children.size() == layout.files.size(),
      "Original multifile storage type count differs from metadata");
  std::vector<Slot> result;
  for (std::size_t i = 0; i < layout.files.size(); ++i) {
    require(text(&stores->children[i]) == "L", "Compact/reordered files require their qualified disk adapter");
    const auto& file = layout.files[i];
    const auto base = fs::path(*saved.saveFile) / file.path;
    require(safeRelative(base), "Unsafe original multifile relative path", ErrorCode::UnsafePath);
    result.push_back({i, base, base, base, file.padding, file.bytes});
  }
  if (const auto* links = attributes.find("filelinks2")) {
    require(links->type == Node::Type::List && links->children.size() <= layout.files.size() * 4,
        "Original multifile link list exceeds its bound");
    for (const auto& entry : links->children) {
      const auto value = text(&entry);
      const auto first = value.find('\n');
      const auto second = first == std::string::npos ? first : value.find('\n', first + 1);
      require(first != std::string::npos && second != std::string::npos && value.find('\n', second + 1) == std::string::npos,
          "Unsupported original multifile link entry");
      std::size_t index = 0;
      const auto parsed = std::from_chars(value.data(), value.data() + first, index);
      require(parsed.ec == std::errc{} && parsed.ptr == value.data() + first && index < result.size(),
          "Original multifile link index is invalid");
      auto& slot = result[index];
      const auto from = value.substr(first + 1, second - first - 1);
      const auto to = value.substr(second + 1);
      require(from.empty() || fs::path(from) == save / slot.base, "Original file-link source differs from the saved root");
      if (to.empty() || to == from) { slot.actual = slot.base; continue; }
      const fs::path target(to);
      require(target.is_absolute() && target.parent_path() == (save / slot.base).parent_path(),
          "External multifile links need their qualified relocation adapter");
      const auto relativeTarget = target.lexically_relative(save);
      require(safeRelative(relativeTarget), "Unsafe original multifile target", ErrorCode::UnsafePath);
      slot.actual = relativeTarget;
    }
  }
  std::set<fs::path> physical;
  for (const auto& slot : result) {
    require(slot.padding || physical.insert(slot.actual).second, "Two data files alias the same physical target", ErrorCode::UnsafePath);
  }
  return result;
}
void finalState(storage::TorrentStateFile& state, const compat::OriginalLayout& layout,
    const std::vector<Slot>& slots, const fs::path& save, int pieces) {
  auto resume = dictionary(state.document().find("resume"));
  auto data = dictionary(resume.find("data"));
  put(data, "resume data", stringValue(std::string(static_cast<std::size_t>(pieces), '\1')));
  put(data, "valid", "i1e");
  put(data, "blocks", "de");
  put(resume, "data", data.serialize());
  done(state.replaceSection("resume", resume.serialize()));
  auto attributes = dictionary(state.document().find("attributes"));
  put(attributes, "resumecomplete", "i2e");
  auto downloaded = dictionary(attributes.find("filedownloaded"));
  std::string counts = "l";
  std::string links = "l";
  for (const auto& slot : slots) {
    counts += 'i' + std::to_string(slot.bytes) + 'e';
    if (!slot.padding && slot.final != slot.base) {
      links += stringValue(std::to_string(slot.index) + '\n' + (save / slot.base).string() + '\n' + (save / slot.final).string());
    }
  }
  static_cast<void>(layout);
  put(downloaded, "downloaded", counts + 'e');
  put(attributes, "filedownloaded", downloaded.serialize());
  put(attributes, "filelinks2", links + 'e');
  done(state.replaceSection("attributes", attributes.serialize()));
}

LocalReport run(const storage::ProfileLease& lease, const LocalOptions& options, const core::SavedDownload& saved,
    storage::ConfigFile& inventory, storage::TorrentStateStore& store, storage::TorrentStateFile& original,
    const std::shared_ptr<lt::torrent_info>& info, const SavedPolicy& policy, PersistentSession* owner) {
  const auto geometry = compat::originalLayout(original.document());
  require(geometry.has_value(), geometry ? "" : geometry.error().message);
  const auto& layout = *geometry;
  require(layout.multifile && layout.files.size() == static_cast<std::size_t>(info->num_files())
      && layout.bytes == info->total_size() && layout.pieceLength == info->piece_length(),
      "Original/engine multifile geometry differs; activation requires a qualified mapping");
  for (std::size_t i = 0; i < layout.files.size(); ++i) {
    const auto index = lt::file_index_t{static_cast<int>(i)};
    const auto& file = layout.files[i];
    require(file.bytes == info->files().file_size(index) && file.offset == info->files().file_offset(index)
        && file.padding == info->files().pad_file_at(index), "Original and engine file indices differ");
    if (!file.padding) {
      require(fs::path(info->files().file_path(index)) == fs::path(layout.name) / file.path,
          "Original and engine file-name codecs differ");
    }
  }
  if (const auto* encoding = original.document().find("encoding")) {
    auto name = text(encoding);
    std::erase_if(name, [](unsigned char value) { return value == '-' || value == '_' || value == ' '; });
    std::ranges::transform(name, name.begin(), [](unsigned char value) { return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32) : static_cast<char>(value); });
    require(name == "utf8" || name == "utf8keys", "Legacy filename encodings need their original codec adapter");
  }
  const fs::path save(saved.saveDirectory);
  require(save == options.profile.parent_path() / "payload", "Experimental multifile runner requires its generated sibling payload directory", ErrorCode::UnsafePath);
  auto entries = inventory.document().find("downloads");
  require(entries && entries->type == Node::Type::List && saved.sourceIndex < entries->children.size(), "Invalid saved multifile entry", ErrorCode::InvalidFormat);
  auto entry = dictionary(&entries->children[saved.sourceIndex]);
  const auto previous = integer(entry.find("downloaded"));
  require(previous >= 0 && layout.bytes <= std::numeric_limits<std::int64_t>::max() - previous, "Saved transfer counter has no safe capacity", ErrorCode::InvalidFormat);
  auto attributes = dictionary(original.document().find("attributes"));
  static_cast<void>(mapping(saved, attributes.root(), layout, save));
  auto state = take(store.beginUpdate(lease, options.hash));
  attributes = dictionary(state.document().find("attributes"));
  auto slots = mapping(saved, attributes.root(), layout, save);
  for (const auto& slot : slots) { if (!slot.padding) { requireUtf8Filesystem(save / slot.actual); } }
  auto journal = intents(attributes.root(), slots.size());
  std::map<std::size_t, Intent> byIndex;
  for (const auto& item : journal) { byIndex.emplace(item.index, item); }
  struct rlimit descriptors{};
  std::size_t budget = 8192;
  if (::getrlimit(RLIMIT_NOFILE, &descriptors) == 0 && descriptors.rlim_cur != RLIM_INFINITY) {
    budget = std::min(budget, static_cast<std::size_t>(descriptors.rlim_cur / 4));
  }
  const auto physical = static_cast<std::size_t>(std::ranges::count_if(slots, [](const auto& slot) { return !slot.padding; }));
  require(physical <= budget, "Multifile activation exceeds the current bounded descriptor budget");
  observe(options, LocalStage::StatePrepared);
  require(!options.stop.stop_requested(), "Local session cancelled", ErrorCode::Cancelled);
  for (auto& slot : slots) {
    slot.final = slot.actual;
    if (slot.padding) { require(!byIndex.contains(slot.index), "Padding cannot own a physical completion intent"); continue; }
    slot.parent = directory((save / slot.actual).parent_path());
    slot.data = Descriptor(::openat(slot.parent.get(), slot.actual.filename().c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW));
    require(slot.data.get() >= 0 && ::fstat(slot.data.get(), &slot.identity) == 0 && S_ISREG(slot.identity.st_mode)
        && slot.identity.st_uid == ::geteuid() && slot.identity.st_size >= 0 && slot.identity.st_size <= slot.bytes
        && slot.identity.st_nlink >= 1 && slot.identity.st_nlink <= 2,
        "Original multifile payload must be an owned regular file of compatible size", ErrorCode::UnsafePath);
    if (const auto found = byIndex.find(slot.index); found != byIndex.end()) {
      const auto& pending = found->second;
      require((slot.actual == pending.source || slot.actual == pending.target)
          && slot.identity.st_dev == pending.device && slot.identity.st_ino == pending.inode,
          "Multifile completion intent differs from current data", ErrorCode::Conflict);
      if (slot.identity.st_nlink == 2) {
        const auto alias = slot.actual == pending.source ? pending.target : pending.source;
        struct stat other{};
        require(::fstatat(slot.parent.get(), alias.filename().c_str(), &other, AT_SYMLINK_NOFOLLOW) == 0
            && same(slot.identity, other), "Unrecognized multifile hard link", ErrorCode::UnsafePath);
      }
    } else { require(slot.identity.st_nlink == 1, "Unrecognized multifile hard link", ErrorCode::UnsafePath); }
  }
  LocalReport report{0, 0, layout.bytes, save / *saved.saveFile};
  if (owner) {
    PersistentOperation operation(*owner, lease, options);
    lt::add_torrent_params params;
    params.download_limit = policy.downloadLimit;
    params.upload_limit = policy.uploadLimit;
    for (auto priority : policy.priorities) { params.file_priorities.push_back(lt::download_priority_t{priority}); }
    params.ti = info;
    params.save_path = save.string();
    params.flags &= ~lt::torrent_flags::paused;
    params.flags &= ~lt::torrent_flags::auto_managed;
    params.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
    for (const auto& slot : slots) {
      if (!slot.padding) { params.renamed_files[lt::file_index_t{static_cast<int>(slot.index)}] = slot.actual.string(); }
    }
    auto handle = operation.add(std::move(params));
    auto next = std::chrono::steady_clock::time_point::min();
    auto publish = [&](const lt::torrent_status& status, LocalProgress::Phase phase, bool force = false) {
      const auto now = std::chrono::steady_clock::now();
      if (!options.progress || (!force && now < next)) { return; }
      std::int64_t complete = status.total_done;
      for (const auto& file : layout.files) {
        if (!file.padding || file.bytes == 0) { continue; }
        const auto first = file.offset / layout.pieceLength;
        const auto last = (file.offset + file.bytes - 1) / layout.pieceLength;
        bool done = true;
        for (auto i = first; i <= last; ++i) {
          const auto piece = lt::piece_index_t{static_cast<int>(i)};
          if (i >= status.pieces.size() || !status.pieces[piece]) { done = false; break; }
        }
        if (done) { complete += file.bytes; }
      }
      options.progress({phase, std::min(complete, layout.bytes), status.download_payload_rate,
          status.upload_payload_rate, status.num_pieces, info->num_pieces()});
      next = now + 200ms;
    };
    operation.wait(options, [&](const auto&) {
      const auto status = handle.status();
      publish(status, LocalProgress::Phase::Checking);
      return status.state != lt::torrent_status::checking_files && status.state != lt::torrent_status::checking_resume_data;
    });
    report.reusedPieces = handle.status().num_pieces;
    observe(options, LocalStage::Checked);
    require(!options.stop.stop_requested(), "Local session cancelled", ErrorCode::Cancelled);
    handle.connect_peer({lt::make_address("127.0.0.1"), options.peerPort});
    operation.wait(options, [&](const auto&) {
      const auto status = handle.status();
      publish(status, LocalProgress::Phase::Downloading);
      return status.is_seeding;
    });
    report.received = handle.status().total_payload_download;
    publish(handle.status(), LocalProgress::Phase::Flushing, true);
    operation.request(); // A fresh observation may arrive during the existing flush wait.
    handle.pause();
    handle.save_resume_data(lt::torrent_handle::flush_disk_cache);
    operation.wait(options, [&](const auto& pending) { return operation.flushed(pending); });
    operation.retire(); // Required BEFORE the unchanged payload/state publication.
  } else {
    lt::session session(settings());
    SessionStatisticsSampler statistics(session, options);
    lt::add_torrent_params params;
    params.download_limit = policy.downloadLimit;
    params.upload_limit = policy.uploadLimit;
    for (auto priority : policy.priorities) { params.file_priorities.push_back(lt::download_priority_t{priority}); }
    params.ti = info;
    params.save_path = save.string();
    params.flags &= ~lt::torrent_flags::paused;
    params.flags &= ~lt::torrent_flags::auto_managed;
    params.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
    for (const auto& slot : slots) {
      if (!slot.padding) { params.renamed_files[lt::file_index_t{static_cast<int>(slot.index)}] = slot.actual.string(); }
    }
    lt::error_code error;
    auto handle = session.add_torrent(params, error);
    require(!error && handle.is_valid(), "Cannot open original multifile data: " + error.message(), ErrorCode::Io);
    auto next = std::chrono::steady_clock::time_point::min();
    auto publish = [&](const lt::torrent_status& status, LocalProgress::Phase phase, bool force = false) {
      const auto now = std::chrono::steady_clock::now();
      if (!options.progress || (!force && now < next)) { return; }
      std::int64_t complete = status.total_done;
      for (const auto& file : layout.files) {
        if (!file.padding || file.bytes == 0) { continue; }
        const auto first = file.offset / layout.pieceLength;
        const auto last = (file.offset + file.bytes - 1) / layout.pieceLength;
        bool done = true;
        for (auto i = first; i <= last; ++i) {
          const auto piece = lt::piece_index_t{static_cast<int>(i)};
          if (i >= status.pieces.size() || !status.pieces[piece]) { done = false; break; }
        }
        if (done) { complete += file.bytes; }
      }
      options.progress({phase, std::min(complete, layout.bytes), status.download_payload_rate,
          status.upload_payload_rate, status.num_pieces, info->num_pieces()});
      next = now + 200ms;
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
    wait(session, options, [](const auto& pending) {
      return std::ranges::any_of(pending, [](auto* value) { return lt::alert_cast<lt::save_resume_data_alert>(value) != nullptr; });
    }, &statistics);
  }
  for (auto& slot : slots) {
    if (slot.padding) { continue; }
    struct stat current{};
    require(::fstatat(slot.parent.get(), slot.actual.filename().c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0
        && same(slot.identity, current) && current.st_size == slot.bytes, "Multifile payload identity/size changed", ErrorCode::Conflict);
    sync(slot.data.get());
  }
  // Every physical byte is verified and durable, and all original names still
  // exist. Publish that fact before beginning the independent rename protocol.
  // In particular, original Java's v2 recheck can reject a correct final piece
  // when padding and an empty file make its piece map contain three entries.
  // Completion flags here describe real verified data; they never precede I/O.
  finalState(state, layout, slots, save, info->num_pieces());
  done(store.save(lease, state));
  observe(options, LocalStage::Flushed);
  std::string suffix;
  if (const auto* value = attributes.find("incompfilesuffix")) { suffix = text(value); }
  std::vector<Intent> pending;
  for (const auto& slot : slots) {
    if (slot.padding) { continue; }
    const auto found = byIndex.find(slot.index);
    if (found != byIndex.end()) { pending.push_back(found->second); continue; }
    const auto name = slot.actual.filename().string();
    if (!suffix.empty() && name.size() > suffix.size() && name.ends_with(suffix)) {
      auto target = slot.actual;
      target.replace_filename(name.substr(0, name.size() - suffix.size()));
      require(safeRelative(target), "Invalid multifile completed name", ErrorCode::UnsafePath);
      require(slot.identity.st_dev <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
          && slot.identity.st_ino <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), "Payload identity exceeds compatible intent limits");
      pending.push_back({slot.index, slot.actual, target, slot.identity.st_dev, slot.identity.st_ino});
    }
  }
  if (!pending.empty() || !journal.empty()) { saveIntents(state, store, lease, pending); }
  for (auto& item : pending) {
    auto& slot = slots[item.index];
    if (slot.actual == item.target) { item.linked = true; }
    else if (::linkat(slot.parent.get(), slot.actual.filename().c_str(), slot.parent.get(), item.target.filename().c_str(), 0) == 0) {
      item.linked = true;
      sync(slot.parent.get());
    } else if (errno == EEXIST) {
      struct stat other{};
      item.linked = ::fstatat(slot.parent.get(), item.target.filename().c_str(), &other, AT_SYMLINK_NOFOLLOW) == 0 && same(slot.identity, other);
    } else { throw Failure{{ErrorCode::Io, "Cannot prepare multifile completion rename; original names retained"}}; }
    if (item.linked) { slot.final = item.target; }
    observe(options, LocalStage::CompletionLinked);
  }
  if (pending.empty()) { observe(options, LocalStage::CompletionLinked); }
  finalState(state, layout, slots, save, info->num_pieces());
  done(store.save(lease, state));
  observe(options, LocalStage::CompletionSaved);
  require(report.received >= 0 && report.received <= std::numeric_limits<std::int64_t>::max() - previous, "Transfer counter overflows", ErrorCode::InvalidFormat);
  put(entry, "downloaded", "i" + std::to_string(previous + report.received) + "e");
  put(entry, "completedbytes", "i" + std::to_string(layout.bytes) + "e");
  put(entry, "allocated", "i1e");
  std::string updated = "l";
  for (std::size_t i = 0; i < entries->children.size(); ++i) { updated += i == saved.sourceIndex ? entry.serialize() : encoded(entries->children[i]); }
  put(inventory.document(), "downloads", updated + 'e');
  done(inventory.save(lease));
  observe(options, LocalStage::InventorySaved);
  for (const auto& item : pending) {
    if (!item.linked) { continue; }
    const auto& slot = slots[item.index];
    struct stat alias{};
    if (::fstatat(slot.parent.get(), item.source.filename().c_str(), &alias, AT_SYMLINK_NOFOLLOW) == 0) {
      require(S_ISREG(alias.st_mode) && alias.st_dev == item.device && alias.st_ino == item.inode,
          "Multifile completion alias changed; retained without deleting", ErrorCode::Conflict);
      require(::unlinkat(slot.parent.get(), item.source.filename().c_str(), 0) == 0, "Cannot finish multifile completion rename", ErrorCode::Io);
    } else { require(errno == ENOENT, "Cannot inspect multifile completion alias", ErrorCode::Io); }
    sync(slot.parent.get());
  }
  if (!pending.empty() || !journal.empty()) { saveIntents(state, store, lease, {}); }
  return report;
}
} // namespace

std::expected<LocalReport, storage::Error> continueMultifile(const storage::ProfileLease& lease,
    const LocalOptions& options, const core::SavedDownload& saved, storage::ConfigFile& inventory,
    storage::TorrentStateStore& store, storage::TorrentStateFile& original,
    const std::shared_ptr<lt::torrent_info>& metadata, const SavedPolicy& policy, PersistentSession* owner) {
  try { return run(lease, options, saved, inventory, store, original, metadata, policy, owner); }
  catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::exception& failure) { return std::unexpected(storage::Error{storage::ErrorCode::Io, failure.what()}); }
}
} // namespace biglybtc::engine::detail

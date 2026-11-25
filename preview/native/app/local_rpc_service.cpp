// SPDX-License-Identifier: GPL-2.0-or-later
#include "local_rpc_service.hpp"
#include "biglybtc/saved_dnd.hpp"

#include "application_settings.hpp"
#include <biglybtc/queue_policy.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <array>
#include <bit>
#include <limits>

namespace biglybtc::app {
namespace {
namespace rpc = plugins::rpc;
constexpr char startStopped[] = "Default Start Torrents Stopped";

std::unexpected<rpc::Failure> permission() {
  return std::unexpected(rpc::Failure{rpc::Failure::Kind::Permission, {}, {}});
}

std::unexpected<rpc::Failure> failure(QString message) {
  return std::unexpected(rpc::Failure{rpc::Failure::Kind::Textual, std::move(message), {}});
}

constexpr std::array metadataNames{
    std::pair{u"comment", u"comment"}, std::pair{u"creator", u"creator"}, std::pair{u"dateCreated", u"date_created"},
    std::pair{u"file-count", u"file_count"}, std::pair{u"isPrivate", u"is_private"},
    std::pair{u"pieceCount", u"piece_count"}, std::pair{u"pieceSize", u"piece_size"},
    std::pair{u"metadataPercentComplete", u"metadata_percent_complete"}};

constexpr std::array transferNames{
    std::pair{u"downloadedEver", u"downloaded_ever"}, std::pair{u"uploadedEver", u"uploaded_ever"},
    std::pair{u"rateDownload", u"rate_download"}, std::pair{u"rateUpload", u"rate_upload"}};

constexpr std::array rateLimitNames{
    std::pair{u"downloadLimit", u"download_limit"}, std::pair{u"downloadLimited", u"download_limited"},
    std::pair{u"uploadLimit", u"upload_limit"}, std::pair{u"uploadLimited", u"upload_limited"}};

const char16_t* fieldName(const std::pair<const char16_t*, const char16_t*>& names, bool jsonrpc2) {
  return jsonrpc2 ? names.second : names.first;
}

struct Fields {
  bool id = false;
  bool name = false;
  bool hash = false;
  bool size = false;
  bool status = false;
  bool force = false;
  bool position = false;
  bool added = false;
  bool directory = false;
  bool selectedSize = false;
  bool selectedRemaining = false;
  bool selectedPercent = false;
  bool fileStats = false;
  bool haveValid = false;
  bool haveUnchecked = false;
  bool finished = false;
  QSet<QString> metadata;
  QSet<QString> transfers;
  QSet<QString> rateLimits;
};

std::optional<Fields> fields(const rpc::Call& call) {
  const auto value = call.arguments.value(QStringLiteral("fields"));
  if (!value.isArray() || value.toArray().isEmpty() || value.toArray().size() > 24) { return std::nullopt; }
  Fields selection;
  for (const auto& field : value.toArray()) {
    const auto name = field.toString();
    if (name == u"id") { selection.id = true; }
    else if (name == u"name") { selection.name = true; }
    else if (name == (call.jsonrpc2 ? u"hash_string" : u"hashString")) { selection.hash = true; }
    else if (name == (call.jsonrpc2 ? u"total_size" : u"totalSize")) { selection.size = true; }
    else if (name == u"status") { selection.status = true; }
    else if (name == (call.jsonrpc2 ? u"is_forced" : u"isForced")) { selection.force = true; }
    else if (name == (call.jsonrpc2 ? u"queue_position" : u"queuePosition")) { selection.position = true; }
    else if (name == (call.jsonrpc2 ? u"added_date" : u"addedDate")) { selection.added = true; }
    else if (name == (call.jsonrpc2 ? u"download_dir" : u"downloadDir")) { selection.directory = true; }
    else if (name == (call.jsonrpc2 ? u"size_when_done" : u"sizeWhenDone")) { selection.selectedSize = true; }
    else if (name == (call.jsonrpc2 ? u"left_until_done" : u"leftUntilDone")) { selection.selectedRemaining = true; }
    else if (name == (call.jsonrpc2 ? u"percent_done" : u"percentDone")) { selection.selectedPercent = true; }
    else if (name == (call.jsonrpc2 ? u"file_stats" : u"fileStats")) { selection.fileStats = true; }
    else if (name == (call.jsonrpc2 ? u"have_valid" : u"haveValid")) { selection.haveValid = true; }
    else if (name == (call.jsonrpc2 ? u"have_unchecked" : u"haveUnchecked")) { selection.haveUnchecked = true; }
    else if (name == (call.jsonrpc2 ? u"is_finished" : u"isFinished")) { selection.finished = true; }
    else if (std::ranges::any_of(metadataNames, [&](const auto& pair) {
      return name == fieldName(pair, call.jsonrpc2);
    })) { selection.metadata.insert(name); }
    else if (std::ranges::any_of(transferNames, [&](const auto& pair) {
      return name == fieldName(pair, call.jsonrpc2);
    })) { selection.transfers.insert(name); }
    else if (std::ranges::any_of(rateLimitNames, [&](const auto& pair) {
      return name == fieldName(pair, call.jsonrpc2);
    })) { selection.rateLimits.insert(name); }
    else { return std::nullopt; }
  }
  return selection;
}

struct FileStatsFields {
  bool downloaded = true;
  bool wanted = true;
  bool priority = true;
};

std::optional<FileStatsFields> fileStatsFields(const rpc::Call& call) {
  const auto value = call.arguments.value(call.jsonrpc2 ? QStringLiteral("file_fields") : QStringLiteral("file-fields"));
  if (value.isUndefined() || value.isNull()) { return FileStatsFields{}; }
  if (!value.isArray() || value.toArray().size() > 32) { return std::nullopt; }
  const auto names = value.toArray();
  if (names.isEmpty()) { return FileStatsFields{}; }
  FileStatsFields selected{false, false, false};
  for (const auto& field : names) {
    if (!field.isString()) { return std::nullopt; }
    const auto name = field.toString();
    if (name == (call.jsonrpc2 ? u"bytes_completed" : u"bytesCompleted")) { selected.downloaded = true; }
    else if (name == u"wanted") { selected.wanted = true; }
    else if (name == u"priority") { selected.priority = true; }
  }
  return selected;
}

QJsonObject fileStatsRow(const engine::TorrentFileStatistics& file, FileStatsFields fields, bool rpc2) {
  QJsonObject item;
  if (fields.downloaded) {
    item.insert(rpc2 ? QStringLiteral("bytes_completed") : QStringLiteral("bytesCompleted"),
        static_cast<qint64>(file.downloaded));
  }
  if (fields.wanted) { item.insert(QStringLiteral("wanted"), file.wanted); }
  if (fields.priority) {
    item.insert(QStringLiteral("priority"), file.priority < 0 ? -1 : file.priority > 0 ? 1 : 0);
  }
  return item;
}

void addMetadata(QJsonObject& item, const std::array<QJsonValue, metadataNames.size()>& values,
    const QSet<QString>& requested, bool rpc2) {
  for (std::size_t field = 0; field < values.size(); ++field) {
    const auto name = QString::fromUtf16(fieldName(metadataNames[field], rpc2));
    if (requested.contains(name)) { item.insert(name, values[field]); }
  }
}

using TransferValues = std::array<std::optional<qint64>, transferNames.size()>;

bool addTransfers(QJsonObject& row, const TransferValues& values, const QSet<QString>& requested, bool rpc2) {
  for (std::size_t field = 0; field < values.size(); ++field) {
    const auto name = QString::fromUtf16(fieldName(transferNames[field], rpc2));
    if (!requested.contains(name)) { continue; }
    if (!values[field] || *values[field] < 0) { return false; }
    row.insert(name, *values[field]);
  }
  return true;
}

void addRateLimits(QJsonObject& row, const engine::TorrentRateLimits& limits,
    const QSet<QString>& requested, bool rpc2) {
  const auto kib = [](int value) { return value <= 0 ? value : std::max(1, value / 1024); };
  const std::array<QJsonValue, rateLimitNames.size()> values{
      kib(limits.download), limits.download > 0, kib(limits.upload), limits.upload > 0};
  for (std::size_t field = 0; field < values.size(); ++field) {
    const auto name = QString::fromUtf16(fieldName(rateLimitNames[field], rpc2));
    if (requested.contains(name)) { row.insert(name, values[field]); }
  }
}

struct Selection {
  bool all = false;
  QSet<qint64> ids;
  QSet<QString> hashes;

  bool matches(const QString& hash, qint64 id) const {
    return all || ids.contains(id) || hashes.contains(hash);
  }

  bool add(const QJsonValue& value) {
    if (value.isString()) {
      const auto hash = value.toString();
      if ((hash.size() != 40 && hash.size() != 64) || !std::ranges::all_of(hash, [](QChar c) {
            return (c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'F') || (c >= u'a' && c <= u'f');
          })) { return false; }
      hashes.insert(hash);
      return true;
    }
    const auto id = value.toInteger(0);
    if (id <= 0) { return false; }
    ids.insert(id);
    return true;
  }
};

std::optional<Selection> select(const QJsonValue& value) {
  Selection result;
  if (value.isUndefined() || value.isNull()) { result.all = true; return result; }
  if (value.isArray()) {
    const auto items = value.toArray();
    if (items.size() > 256) { return std::nullopt; }
    for (const auto& item : items) { if (!result.add(item)) { return std::nullopt; } }
    return result;
  }
  return value.isDouble() && result.add(value) ? std::optional(std::move(result)) : std::nullopt;
}

void clearAmbiguousIds(std::vector<storage::PersistedRpcIdentity>& identities) {
  QMap<qint64, unsigned> ids;
  QMap<QString, unsigned> hashes;
  for (const auto& row : identities) {
    if (row.id && *row.id > 0) { ++ids[*row.id]; }
    if (row.hash) { ++hashes[QString::fromStdString(row.hash->hex())]; }
  }
  for (auto& row : identities) {
    if (!row.hash || !row.id || *row.id <= 0 || ids.value(*row.id) > 1 ||
        hashes.value(QString::fromStdString(row.hash->hex())) > 1) {
      row.id.reset();
    }
  }
}

QJsonObject torrentFields(const Fields& requested, bool rpc2, qint64 id, const QString& hash,
    const QJsonValue& name, qint64 size) {
  QJsonObject item;
  if (requested.id) { item.insert(QStringLiteral("id"), id); }
  if (requested.name && !name.isUndefined()) { item.insert(QStringLiteral("name"), name); }
  if (requested.hash) { item.insert(rpc2 ? QStringLiteral("hash_string") : QStringLiteral("hashString"), hash); }
  if (requested.size) { item.insert(rpc2 ? QStringLiteral("total_size") : QStringLiteral("totalSize"), size); }
  return item;
}

struct TorrentStatus {
  int transmission;
  int biglybt;
};

constexpr TorrentStatus stoppedStatus{0, 70};

std::optional<TorrentStatus> torrentStatus(const ui::LibraryItem& torrent) {
  using State = core::QueueTorrentState;
  if (!torrent.publicActivity) { return std::nullopt; }
  switch (*torrent.publicActivity) {
    case State::Waiting: return TorrentStatus{1, 0};
    case State::Preparing: return TorrentStatus{1, 20};
    case State::Checking: return TorrentStatus{2, 30};
    case State::Queued:
      if (torrent.assumedComplete) { return TorrentStatus{*torrent.assumedComplete ? 5 : 3, 75}; }
      return std::nullopt;
    case State::Downloading: return TorrentStatus{4, 50};
    case State::Seeding: return TorrentStatus{6, 60};
    case State::Stopping: return TorrentStatus{0, 65};
    case State::Stopped: return stoppedStatus;
    case State::Failed: return TorrentStatus{0, 100};
  }
  return std::nullopt;
}

void addTorrentStatus(QJsonObject& row, TorrentStatus status) {
  row.insert(QStringLiteral("status"), status.transmission);
  row.insert(QStringLiteral("status.biglybt"), status.biglybt);
}

std::optional<int> originalInteger(const core::Document& config, std::string_view key, int fallback) {
  const auto* value = config.find(key);
  if (!value) { return fallback; }
  if (value->type != core::Node::Type::Integer) { return std::nullopt; }
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value->integer));
}

QJsonObject savedSessionFields(const core::Document& config) {
  QJsonObject fields;
  const auto insertInt = [&](const QString& alias, std::string_view original, int fallback) {
    if (const auto value = originalInteger(config, original, fallback)) { fields.insert(alias, *value); }
  };
  if (const auto* path = config.find("Default save path"); path && path->type == core::Node::Type::Bytes) {
    const auto name = QString::fromUtf8(path->bytes.data(), static_cast<qsizetype>(path->bytes.size()));
    fields.insert(QStringLiteral("download-dir"), name);
    fields.insert(QStringLiteral("incomplete-dir"), name);
  }
  insertInt(QStringLiteral("peer-port"), "TCP.Listen.Port", 6881);
  if (const auto random = originalInteger(config, "Listen.Port.Randomize.Enable", 0)) {
    fields.insert(QStringLiteral("peer-port-random-on-start"), *random != 0);
  }
  if (const auto encryption = originalInteger(config, "network.transport.encrypted.require", 0)) {
    fields.insert(QStringLiteral("encryption"), *encryption ? QStringLiteral("required") : QStringLiteral("preferred"));
  }
  const auto downloads = originalInteger(config, "max downloads", 4);
  const auto active = originalInteger(config, "max active torrents", 4);
  if (downloads) {
    fields.insert(QStringLiteral("download-queue-size"), *downloads);
    fields.insert(QStringLiteral("download-queue-enabled"), *downloads > 0);
  }
  if (active) { fields.insert(QStringLiteral("active-queue-size"), *active); }
  if (downloads && active) {
    const auto seeds = static_cast<std::uint32_t>(*downloads) - static_cast<std::uint32_t>(*active);
    fields.insert(QStringLiteral("seed-queue-size"), std::bit_cast<std::int32_t>(seeds));
  }
  fields.insert(QStringLiteral("seed-queue-enabled"), true);
  if (const auto limit = originalInteger(config, "Max Download Speed KBs", 0)) {
    fields.insert(QStringLiteral("speed-limit-down-enabled"), *limit != 0);
    if (*limit > 0) { fields.insert(QStringLiteral("speed-limit-down"), *limit); }
    else { insertInt(QStringLiteral("speed-limit-down"), "config.ui.speed.partitions.manual.download.last", 0); }
  }
  if (const auto rename = originalInteger(config, "Rename Incomplete Files", 0)) {
    fields.insert(QStringLiteral("rename-partial-files"), *rename != 0);
  }
  insertInt(QStringLiteral("peer-limit-global"), "Max.Peer.Connections.Total", 250);
  insertInt(QStringLiteral("peer-limit-per-torrent"), "Max.Peer.Connections.Per.Torrent", 50);
  if (const auto stopped = originalInteger(config, startStopped, 0)) {
    fields.insert(QStringLiteral("start-added-torrents"), *stopped == 0);
  }
  return fields;
}
} // namespace

LocalRpcService::LocalRpcService(const storage::ProfileLease& lease, std::string originalPluginDirectory,
    Library library, CurrentStatistics statistics, CumulativeStatistics cumulative,
    AuthorizeWrite authorizeWrite, TorrentMetadata metadata, ManagedDownloads managedDownloads,
    ControlDownloads controlDownloads, TorrentCommand recheckDownloads,
    TorrentCommand removeDownloads, SetTorrentRates setTorrentRates, AllocateIdentity allocateIdentity)
    : lease_(lease), originalPluginDirectory_(std::move(originalPluginDirectory)), library_(std::move(library)),
      currentStatistics_(std::move(statistics)), cumulativeStatistics_(std::move(cumulative)),
      authorizeWrite_(std::move(authorizeWrite)), metadata_(std::move(metadata)),
      managedDownloads_(std::move(managedDownloads)), controlDownloads_(std::move(controlDownloads)),
      recheckDownloads_(std::move(recheckDownloads)), removeDownloads_(std::move(removeDownloads)),
      setTorrentRates_(std::move(setTorrentRates)), allocateIdentity_(std::move(allocateIdentity)) {}

bool LocalRpcService::start(quint16 port, rpc::HttpCredentials credentials) {
  return lease_.valid() && library_ && listener_.start(port, std::move(credentials),
      [this](const rpc::Call& call) { return dispatch(call); });
}

void LocalRpcService::poll() {
  loop_.processEvents(QEventLoop::AllEvents, QDeadlineTimer(5));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

std::optional<qint64> LocalRpcService::captureRemovedTorrentId(
    const core::TorrentHash& hash, std::stop_token stop) const noexcept {
  try {
    const auto source = storage::readRpcProfileSnapshot(lease_, originalPluginDirectory_, stop);
    if (!source || !source->library.inventoryIssues.empty() || source->library.cacheWarning ||
        !source->library.cacheIssues.empty() || std::ranges::any_of(source->issues, [](const auto& issue) {
          return issue.scope != storage::RpcProfileIssue::Scope::Counter;
        })) { return std::nullopt; }
    const auto& identities = source->identities;
    const auto selected = std::ranges::find(identities, std::optional{hash}, &storage::PersistedRpcIdentity::hash);
    if (selected == identities.end() || selected->archived || !selected->id || *selected->id <= 0 ||
        std::ranges::count(identities, selected->hash, &storage::PersistedRpcIdentity::hash) != 1 ||
        std::ranges::count(identities, selected->id, &storage::PersistedRpcIdentity::id) != 1) {
      return std::nullopt;
    }
    return *selected->id;
  } catch (const std::exception&) {
    return std::nullopt; // An RPC observation must not prevent the approved file operation.
  }
}

void LocalRpcService::noteRemovedTorrent(qint64 id) noexcept {
  const auto end = pendingRemovals_.begin() + pendingRemovalCount_;
  if (std::find(pendingRemovals_.begin(), end, id) != end) { return; }
  if (pendingRemovalCount_ == pendingRemovals_.size()) {
    std::shift_left(pendingRemovals_.begin(), end, 1);
  } else {
    ++pendingRemovalCount_;
  }
  pendingRemovals_[pendingRemovalCount_ - 1] = id;
}

rpc::DispatchResult LocalRpcService::sessionStatistics(const rpc::Call& call) const {
  const auto currentKey = call.jsonrpc2 ? QStringLiteral("current_stats") : QStringLiteral("current-stats");
  const auto cumulativeKey = call.jsonrpc2 ? QStringLiteral("cumulative_stats") : QStringLiteral("cumulative-stats");
  const auto selection = call.arguments.value(QStringLiteral("fields")).toArray();
  if (selection.isEmpty() || selection.size() > 16 || !std::ranges::all_of(selection,
      [&](const QJsonValue& field) { return field.toString() == currentKey || field.toString() == cumulativeKey; })) {
    return failure(QStringLiteral("Select the supported current or cumulative session statistics fields."));
  }
  const auto filesKey = call.jsonrpc2 ? QStringLiteral("files_added") : QStringLiteral("filesAdded");
  const auto values = [&](const TransferStatistics& data) {
    return QJsonObject{
        {call.jsonrpc2 ? QStringLiteral("uploaded_bytes") : QStringLiteral("uploadedBytes"), data.uploadedBytes},
        {call.jsonrpc2 ? QStringLiteral("downloaded_bytes") : QStringLiteral("downloadedBytes"), data.downloadedBytes},
        {call.jsonrpc2 ? QStringLiteral("seconds_active") : QStringLiteral("secondsActive"), data.secondsActive}};
  };
  QJsonObject result;
  if (selection.contains(currentKey)) {
    const auto data = currentStatistics_ ? currentStatistics_() : std::nullopt;
    if (!data) { return failure(QStringLiteral("The current transfer statistics are unavailable.")); }
    const auto ratio = data->downloadedBytes == 0 ?
        (data->uploadedBytes == 0 ? 1.0f : std::numeric_limits<float>::max()) :
        static_cast<float>(data->uploadedBytes) / static_cast<float>(data->downloadedBytes);
    auto current = values(*data);
    current.insert(QStringLiteral("ratio"), static_cast<double>(ratio));
    current.insert(filesKey, 0);
    result.insert(currentKey, current);
  }
  if (selection.contains(cumulativeKey)) {
    const auto data = cumulativeStatistics_ ? cumulativeStatistics_() : std::nullopt;
    const auto config = storage::ConfigFile::load(lease_, "biglybt.config", storage::ApplicationSettingsLimits);
    if (!data || !config || config->recoverySource() != storage::RecoverySource::Primary) {
      return failure(QStringLiteral("The cumulative transfer statistics are unavailable."));
    }
    const auto sessionCount = originalInteger(config->document(), "locale.set.complete.count", 0);
    auto historyCount = data->filesAdded;
    if (!historyCount) {
      const auto active = originalInteger(config->document(), "download.history.active.size", 0);
      const auto dead = originalInteger(config->document(), "download.history.dead.size", 0);
      if (active && dead) {
        const auto total = static_cast<std::uint32_t>(*active) + static_cast<std::uint32_t>(*dead);
        historyCount = std::bit_cast<std::int32_t>(total);
      }
    }
    if (!sessionCount || !historyCount) {
      return failure(QStringLiteral("Original cumulative statistics settings are unavailable."));
    }
    auto cumulative = values(data->totals);
    cumulative.insert(filesKey, *historyCount);
    cumulative.insert(call.jsonrpc2 ? QStringLiteral("session_count") : QStringLiteral("sessionCount"), *sessionCount);
    result.insert(cumulativeKey, cumulative);
  }
  return result;
}

rpc::DispatchResult LocalRpcService::sessionSettings(const rpc::Call& call) const {
  const auto fields = call.arguments.value(QStringLiteral("fields")).toArray();
  if (fields.isEmpty() || fields.size() > 32) {
    return failure(QStringLiteral("Select the supported saved session settings fields."));
  }
  const auto original = storage::ConfigFile::load(lease_, "biglybt.config", storage::ApplicationSettingsLimits);
  if (!original || original->recoverySource() != storage::RecoverySource::Primary) {
    return failure(QStringLiteral("The original session settings are unavailable."));
  }
  const auto values = savedSessionFields(original->document());
  QJsonObject selected;
  for (const auto& field : fields) {
    const auto name = field.toString();
    auto originalName = name;
    if (call.jsonrpc2 && !name.contains(u'-')) { originalName.replace(u'_', u'-'); }
    else if (call.jsonrpc2) { return failure(QStringLiteral("Select the supported saved session settings fields.")); }
    const auto found = values.constFind(originalName);
    if (found == values.constEnd()) { return failure(QStringLiteral("A requested session setting is unsupported or unavailable.")); }
    selected.insert(name, found.value());
  }
  return selected;
}

std::optional<storage::ConfigFile> LocalRpcService::authorizedSettings() const {
  if (!authorizeWrite_) { return std::nullopt; }
  auto file = storage::ConfigFile::load(lease_, "biglybt.config", storage::ApplicationSettingsLimits);
  if (!file || file->recoverySource() != storage::RecoverySource::Primary || !authorizeWrite_(*file)) {
    return std::nullopt;
  }
  return std::move(*file);
}

rpc::DispatchResult LocalRpcService::writeSessionSettings(const rpc::Call& call) const {
  const auto access = lease_.sourceAccess(true);
  if (!access) { return permission(); }
  auto file = authorizedSettings();
  if (!file) { return permission(); }
  const auto key = call.jsonrpc2 ? QStringLiteral("start_added_torrents") : QStringLiteral("start-added-torrents");
  const auto value = call.arguments.value(key);
  if (call.arguments.size() != 1 || !value.isBool()) {
    return failure(QStringLiteral("Supply the supported start-added-torrents boolean setting."));
  }
  const auto prior = originalInteger(file->document(), startStopped, 0);
  if (!prior) { return failure(QStringLiteral("The original start-added-torrents setting is unavailable.")); }
  const bool stopped = !value.toBool();
  if ((*prior != 0) == stopped) { return QJsonObject{}; }
  if (!file->document().replaceValue(startStopped, stopped ? "i1e" : "i0e") || !file->save(lease_)) {
    return failure(QStringLiteral("The original start-added-torrents setting could not be saved."));
  }
  return QJsonObject{};
}

std::expected<void, storage::Error> LocalRpcService::refreshInventory(quint64 revision, bool force) {
  if (!force && revision_ == revision) { return {}; }
  auto saved = storage::readRpcProfileSnapshot(lease_, originalPluginDirectory_);
  if (!saved) { return std::unexpected(saved.error()); }
  clearAmbiguousIds(saved->identities);
  persistedIds_.clear();
  persistedForce_.clear();
  persistedAdded_.clear();
  archived_.clear();
  for (auto& identity : saved->identities) {
    if (identity.archived) {
      if (!saved->hideArchived && identity.archive) { archived_.push_back(std::move(identity)); }
    } else if (identity.hash && identity.id) {
      const auto hash = QString::fromStdString(identity.hash->hex());
      persistedIds_.insert(hash, *identity.id);
      if (identity.addedMillis) { persistedAdded_.insert(hash, *identity.addedMillis); }
    }
  }
  for (const auto& row : saved->library.items) {
    if (!row.saved.hash) { continue; }
    const auto hash = QString::fromStdString(row.saved.hash->hex());
    if (persistedIds_.contains(hash)) { persistedForce_.insert(hash, row.saved.forceStart); }
  }
  revision_ = revision;
  return {};
}

rpc::DispatchResult LocalRpcService::controlTorrents(const rpc::Call& call, core::DownloadAction action) {
  if (!controlDownloads_) { return permission(); }
  return applyTorrentCommand(call, [this, action](ui::DownloadTargets targets) {
    return controlDownloads_(std::move(targets), action);
  }, action == core::DownloadAction::Stop ? QString{} :
      QStringLiteral("Restore the selected archived torrent before starting it."));
}

rpc::DispatchResult LocalRpcService::applyTorrentCommand(const rpc::Call& call,
    const TorrentCommand& command, QString archivedFailure, qsizetype arguments) {
  if (!command || !authorizedSettings()) { return permission(); }
  const auto selected = select(call.arguments.value(QStringLiteral("ids")));
  if (call.method == u"torrent-remove") {
    const auto key = call.jsonrpc2 ? QStringLiteral("delete_local_data") : QStringLiteral("delete-local-data");
    if (const auto setting = call.arguments.constFind(key); setting != call.arguments.constEnd()) {
      if (!setting->isBool() || setting->toBool()) {
        return failure(QStringLiteral("RPC removal supports keeping the local data."));
      }
      ++arguments;
    }
  }
  if (!selected || selected->all || call.arguments.size() != arguments) {
    return failure(QStringLiteral("Supply explicit torrent IDs for this command."));
  }
  const auto current = library_();
  if (!current || !current->revision || current->items.size() > 256) {
    return failure(QStringLiteral("The current local RPC inventory is unavailable."));
  }
  const auto loaded = refreshInventory(current->revision, true);
  if (!loaded) { return failure(QString::fromStdString(loaded.error().message).left(512)); }
  ui::DownloadTargets targets;
  for (const auto& torrent : current->items) {
    const auto id = persistedIds_.value(torrent.hash, 0);
    if (!selected->matches(torrent.hash, id)) { continue; }
    if (id <= 0) { return failure(QStringLiteral("A selected torrent needs a unique current RPC ID.")); }
    targets.push_back({torrent.hash, current->revision});
  }
  if (!archivedFailure.isEmpty() && std::ranges::any_of(archived_, [&](const auto& archive) {
        return archive.hash && selected->matches(QString::fromStdString(archive.hash->hex()), archive.id.value_or(0));
      })) {
    return failure(std::move(archivedFailure));
  }
  if (targets.empty()) { return QJsonObject{}; }
  const auto applied = command(std::move(targets));
  if (!applied) { return failure(QString::fromStdString(applied.error().message).left(512)); }
  return QJsonObject{};
}

rpc::DispatchResult LocalRpcService::setTorrentRateLimits(const rpc::Call& call) {
  if (!setTorrentRates_ || !authorizedSettings()) { return permission(); }
  RateChanges changes;
  qsizetype arguments = 1;
  for (std::size_t direction = 0; direction < changes.size(); ++direction) {
    const auto limit = call.arguments.value(QString::fromUtf16(fieldName(rateLimitNames[direction * 2], call.jsonrpc2)));
    const auto flag = call.arguments.value(QString::fromUtf16(fieldName(rateLimitNames[direction * 2 + 1], call.jsonrpc2)));
    const auto value = limit.toInteger(-1);
    if ((!limit.isUndefined() && (value < 0 || value > std::numeric_limits<int>::max())) ||
        (!flag.isUndefined() && !flag.isBool())) {
      return failure(QStringLiteral("Supply supported integer torrent rate limits and boolean flags."));
    }
    arguments += !limit.isUndefined();
    arguments += !flag.isUndefined();
    // The original setter accepts bytes per second. A disabled flag clears
    // the direction even when the request omits its size.
    if (flag.isBool() && !flag.toBool()) { changes[direction] = 0; }
    else if (flag.toBool() && !limit.isUndefined()) { changes[direction] = static_cast<int>(value); }
  }
  return applyTorrentCommand(call, [this, &changes](ui::DownloadTargets targets) {
    return setTorrentRates_(std::move(targets), changes);
  }, {}, arguments);
}

rpc::DispatchResult LocalRpcService::dispatch(const rpc::Call& call) {
  if (call.method == u"session-set") { return writeSessionSettings(call); }
  if (call.method == u"torrent-stop") { return controlTorrents(call, core::DownloadAction::Stop); }
  if (call.method == u"torrent-start") { return controlTorrents(call, core::DownloadAction::Start); }
  if (call.method == u"torrent-start-now") { return controlTorrents(call, core::DownloadAction::ForceStart); }
  if (call.method == u"torrent-set") { return setTorrentRateLimits(call); }
  if (call.method == u"torrent-remove") {
    return applyTorrentCommand(call, removeDownloads_,
        QStringLiteral("Restore the selected archived torrent before removing it."));
  }
  if (call.method == u"torrent-verify") {
    return applyTorrentCommand(call, recheckDownloads_,
        QStringLiteral("Restore the selected archived torrent before verifying it."));
  }
  if (call.method == u"session-get") { return sessionSettings(call); }
  if (call.method == u"session-stats") { return sessionStatistics(call); }
  if (call.method != u"torrent-get") { return permission(); }
  const auto requested = fields(call);
  const auto ids = call.arguments.value(QStringLiteral("ids"));
  const bool recent = ids == QStringLiteral("recently-active") || ids == QStringLiteral("recently_active");
  const auto selected = select(recent ? QJsonValue(QJsonValue::Null) : ids);
  const auto format = call.arguments.value(QStringLiteral("format"));
  const bool table = format.toString().compare(u"table", Qt::CaseInsensitive) == 0;
  if (!requested || !selected ||
      (!table && !format.isUndefined() && format.toString().compare(u"objects", Qt::CaseInsensitive) != 0)) {
    return failure(QStringLiteral("Select supported torrent fields and IDs."));
  }
  const auto current = library_();
  if (!current || !current->revision || current->items.size() > 256) {
    return failure(QStringLiteral("The current local RPC inventory is unavailable."));
  }
  const auto loaded = refreshInventory(current->revision, false);
  if (!loaded) { return failure(QString::fromStdString(loaded.error().message).left(512)); }
  if (selected->hashes.size() == 1 && selected->ids.isEmpty() && allocateIdentity_) {
    const auto& hash = *selected->hashes.cbegin();
    using Member = storage::RpcIdentityMember;
    const auto archiveHasHash = [&](const auto& row) { return row.hash && QString::fromStdString(row.hash->hex()) == hash; };
    std::optional<Member> member;
    const auto torrent = std::ranges::find(current->items, hash, &ui::LibraryItem::hash);
    if (torrent != current->items.end() && !persistedIds_.contains(hash)) { member = Member::Stopped; }
    else if (const auto archived = std::ranges::find_if(archived_, archiveHasHash);
        archived != archived_.end() && !archived->id) { member = Member::Archived; }

    if (member && authorizedSettings()) {
      const auto id = allocateIdentity_({hash, current->revision}, originalPluginDirectory_, *member);
      if (!id) { return failure(QString::fromStdString(id.error().message).left(512)); }
      const auto refreshed = refreshInventory(current->revision, true);
      const auto persistent = *member == Member::Stopped ? persistedIds_.value(hash) == *id :
          std::ranges::any_of(archived_, [&](const auto& row) { return archiveHasHash(row) && row.id == *id; });
      if (!refreshed || !persistent) { return failure(QStringLiteral("The saved RPC identity could not be confirmed.")); }
    }
  }
  const auto forceName = call.jsonrpc2 ? QStringLiteral("is_forced") : QStringLiteral("isForced");
  const auto positionName = call.jsonrpc2 ? QStringLiteral("queue_position") : QStringLiteral("queuePosition");
  const auto addedName = call.jsonrpc2 ? QStringLiteral("added_date") : QStringLiteral("addedDate");
  const auto directoryName = call.jsonrpc2 ? QStringLiteral("download_dir") : QStringLiteral("downloadDir");
  const auto selectedSizeName = call.jsonrpc2 ? QStringLiteral("size_when_done") : QStringLiteral("sizeWhenDone");
  const auto selectedRemainingName = call.jsonrpc2 ? QStringLiteral("left_until_done") : QStringLiteral("leftUntilDone");
  const auto selectedPercentName = call.jsonrpc2 ? QStringLiteral("percent_done") : QStringLiteral("percentDone");
  const bool wantsProgress = requested->selectedSize || requested->selectedRemaining || requested->selectedPercent;
  const auto haveValidName = call.jsonrpc2 ? QStringLiteral("have_valid") : QStringLiteral("haveValid");
  const auto haveUncheckedName = call.jsonrpc2 ? QStringLiteral("have_unchecked") : QStringLiteral("haveUnchecked");
  const auto finishedName = call.jsonrpc2 ? QStringLiteral("is_finished") : QStringLiteral("isFinished");
  const bool wantsFileState = wantsProgress || requested->fileStats || requested->haveValid;
  const auto fileStatsName = call.jsonrpc2 ? QStringLiteral("file_stats") : QStringLiteral("fileStats");
  const auto fileFields = requested->fileStats ? fileStatsFields(call) : std::optional{FileStatsFields{}};
  if (!fileFields) { return failure(QStringLiteral("File statistics fields must be a list.")); }
  const bool wantsLimits = !requested->rateLimits.isEmpty();
  const bool needsManaged = wantsFileState || wantsLimits || requested->force || !requested->transfers.isEmpty();
  const auto managed = needsManaged && managedDownloads_ ? managedDownloads_() :
      std::vector<engine::ManagedDownloadStatus>{};
  QMap<QString, const engine::ManagedDownloadStatus*> activeDownloads;
  for (const auto& item : managed) {
    if (item.retained && item.admission) { activeDownloads.insert(QString::fromStdString(item.hash.hex()), &item); }
  }

  int remainingProgressPieces = 2000000;
  std::size_t remainingFileRows = 10000;
  std::vector<std::pair<qint64, QJsonObject>> rows;
  for (const auto& torrent : current->items) {
    const auto id = persistedIds_.value(torrent.hash, 0);
    if (!selected->matches(torrent.hash, id)) { continue; }
    if (id <= 0) { return failure(QStringLiteral("A selected torrent needs a unique current RPC ID.")); }
    if (requested->size && (!torrent.bytes || *torrent.bytes < 0)) {
      return failure(QStringLiteral("A selected torrent size is unavailable."));
    }
    auto item = torrentFields(*requested, call.jsonrpc2, id, torrent.hash, torrent.name, torrent.bytes.value_or(0));
    if (requested->haveUnchecked) { item.insert(haveUncheckedName, 0); }
    if (requested->finished) { item.insert(finishedName, false); }
    if (requested->status) {
      const auto status = torrentStatus(torrent);
      if (!status) { return failure(QStringLiteral("A selected torrent's current status is unavailable.")); }
      addTorrentStatus(item, *status);
    }
    const auto* observed = activeDownloads.value(torrent.hash, nullptr);
    if (needsManaged && (observed || torrent.providerAdmission) &&
        (!observed || observed->admission != torrent.providerAdmission || observed->failure)) {
      return failure(QStringLiteral("A selected torrent's current observation is unavailable."));
    }
    if (requested->force) {
      const auto saved = persistedForce_.constFind(torrent.hash);
      if (!observed && saved == persistedForce_.cend()) {
        return failure(QStringLiteral("A selected torrent's force flag is unavailable."));
      }
      item.insert(forceName, observed ? observed->forceStart : *saved);
    }
    if (requested->position) {
      if (!torrent.savedPosition || *torrent.savedPosition < 0) {
        return failure(QStringLiteral("A selected torrent's queue position is unavailable."));
      }
      item.insert(positionName, *torrent.savedPosition);
    }
    if (!requested->transfers.isEmpty()) {
      const auto values = observed ? TransferValues{observed->cumulativeReceived, observed->cumulativeSent,
          observed->downloadRate, observed->uploadRate} : TransferValues{torrent.received, torrent.sent, 0, 0};
      if (!addTransfers(item, values, requested->transfers, call.jsonrpc2)) {
        return failure(QStringLiteral("A selected torrent's transfer totals are unavailable."));
      }
    }
    if (requested->added) {
      const auto added = persistedAdded_.constFind(torrent.hash);
      if (added == persistedAdded_.cend()) {
        return failure(QStringLiteral("The selected torrent's saved addition time is unavailable."));
      }
      item.insert(addedName, *added / 1000);
    }
    if (requested->directory || wantsFileState || wantsLimits || !requested->metadata.isEmpty()) {
      const auto identity = core::TorrentHash::parse(QByteArray::fromHex(torrent.hash.toLatin1()).toStdString());
      if (wantsFileState && observed) {
        if (observed->totalPieces <= 0 || observed->totalPieces > remainingProgressPieces) {
          return failure(QStringLiteral("The selected piece census exceeds the RPC progress bound."));
        }
        remainingProgressPieces -= observed->totalPieces;
      }
      const engine::CurrentTorrentFileQuery query{
          wantsProgress && observed, requested->fileStats, requested->haveValid && observed, wantsLimits};
      const auto currentDetails = identity && metadata_ ? metadata_(*identity, query) : std::nullopt;
      if (!currentDetails) { return failure(QStringLiteral("The selected torrent's current metadata is unavailable.")); }
      const auto* details = &currentDetails->details;
      if ((wantsFileState || wantsLimits) && currentDetails->admission != torrent.providerAdmission) {
        return failure(QStringLiteral("The selected torrent's current file observation is unavailable."));
      }
      if (wantsLimits) {
        if (!details->rateLimits) {
          return failure(QStringLiteral("The selected torrent's current rate limits are unavailable."));
        }
        addRateLimits(item, *details->rateLimits, requested->rateLimits, call.jsonrpc2);
      }
      if (requested->fileStats) {
        if (!details->fileStatistics || details->fileStatistics->size() > remainingFileRows) {
          return failure(QStringLiteral("The selected file statistics are unavailable or exceed the reply bound."));
        }
        remainingFileRows -= details->fileStatistics->size();
        QJsonArray values;
        for (const auto& file : *details->fileStatistics) {
          values.push_back(fileStatsRow(file, *fileFields, call.jsonrpc2));
        }
        item.insert(fileStatsName, std::move(values));
      }
      const bool savedValid = requested->haveValid && !currentDetails->admission;
      if (requested->haveValid && currentDetails->admission) {
        if (!currentDetails->verifiedWantedBytes) {
          return failure(QStringLiteral("The selected torrent's verified piece bytes are unavailable."));
        }
        item.insert(haveValidName, static_cast<qint64>(*currentDetails->verifiedWantedBytes));
      }
      if (wantsProgress || savedValid) {
        std::optional<qint64> selectedSize, remaining;
        if (currentDetails->progress) {
          selectedSize = currentDetails->progress->size;
          remaining = currentDetails->progress->remaining;
        } else if (!currentDetails->admission) {
          if (torrent.savedSelected) {
            selectedSize = torrent.savedSelected->bytes;
            remaining = torrent.savedSelected->remaining;
          }
          const bool allWanted = std::ranges::none_of(details->entries, &engine::TorrentFileDetails::skipped);
          if (!selectedSize) {
            selectedSize = 0;
            for (const auto& file : details->entries) {
              if (!file.skipped) { *selectedSize += file.bytes; }
            }
          }
          if (!remaining && *selectedSize == 0) { remaining = 0; }
          if (!remaining && allWanted && torrent.completed && *torrent.completed <= *selectedSize) {
            remaining = *selectedSize - *torrent.completed;
          }
        }
        if (!selectedSize || ((requested->selectedRemaining || requested->selectedPercent || savedValid) && !remaining)) {
          return failure(QStringLiteral("The selected torrent's exact wanted-file progress is unavailable."));
        }
        if (savedValid) { item.insert(haveValidName, std::max<qint64>(0, *selectedSize - *remaining)); }
        if (requested->selectedSize) { item.insert(selectedSizeName, *selectedSize); }
        if (requested->selectedRemaining) { item.insert(selectedRemainingName, *remaining); }
        if (requested->selectedPercent) {
          const auto percent = core::selectedProgressThousandths(*selectedSize, *remaining);
          if (!percent) { return failure(QStringLiteral("The selected torrent's current progress is invalid.")); }
          item.insert(selectedPercentName, *percent / 1000.0);
        }
      }
      if (requested->directory) {
        if (!torrent.savePath.startsWith(u'/')) {
          return failure(QStringLiteral("The selected torrent's original save path is unavailable."));
        }
        const auto directory = details->originalMultifile ? torrent.savePath : QFileInfo(torrent.savePath).path();
        item.insert(directoryName, directory);
      }
      addMetadata(item, {QString::fromStdString(details->comment), QString::fromStdString(details->creator),
          static_cast<qint64>(details->created), details->files, details->privateTorrent, details->pieces,
          details->pieceLength, 1.0}, requested->metadata, call.jsonrpc2);
    }
    rows.emplace_back(id, std::move(item));
  }
  for (const auto& identity : archived_) {
    const auto hash = QString::fromStdString(identity.hash->hex());
    const auto id = identity.id.value_or(0);
    if (!selected->matches(hash, id)) { continue; }
    if (id <= 0) { return failure(QStringLiteral("A selected torrent needs a unique current RPC ID.")); }
    if (rows.size() == 256) { return failure(QStringLiteral("The selected RPC inventory exceeds its torrent limit.")); }
    const auto& archive = *identity.archive;
    const auto name = archive.name ? QJsonValue(QString::fromStdString(*archive.name)) :
        QJsonValue(table ? QJsonValue::Null : QJsonValue::Undefined);
    auto item = torrentFields(*requested, call.jsonrpc2, id, hash, name, archive.size);
    if (requested->status) { addTorrentStatus(item, stoppedStatus); }
    if (requested->position) { item.insert(positionName, 0); }
    if (requested->selectedSize) { item.insert(selectedSizeName, static_cast<qint64>(archive.size)); }
    if (requested->selectedRemaining) { item.insert(selectedRemainingName, 0); }
    if (requested->selectedPercent) { item.insert(selectedPercentName, 1.0); }
    if (requested->haveValid) { item.insert(haveValidName, static_cast<qint64>(archive.size)); }
    if (requested->haveUnchecked) { item.insert(haveUncheckedName, 0); }
    if (requested->finished) { item.insert(finishedName, true); }
    if (requested->fileStats) {
      if (!archive.fileLengths || archive.fileLengths->size() > remainingFileRows) {
        return failure(QStringLiteral("The selected archived file statistics are unavailable or exceed the reply bound."));
      }
      remainingFileRows -= archive.fileLengths->size();
      QJsonArray values;
      for (const auto length : *archive.fileLengths) {
        values.push_back(fileStatsRow({std::max<std::int64_t>(length, 0), length >= 0, 0}, {}, call.jsonrpc2));
      }
      item.insert(fileStatsName, std::move(values));
    }
    if (requested->force && table) { item.insert(forceName, QJsonValue::Null); }
    if (requested->added) {
      item.insert(addedName, static_cast<qint64>(identity.addedMillis.value_or(0) / 1000));
    }
    if (requested->directory && (archive.savePath || table)) {
      item.insert(directoryName, archive.savePath ? QJsonValue(QString::fromStdString(*archive.savePath)) :
          QJsonValue(QJsonValue::Null));
    }
    if (!requested->transfers.isEmpty()) { addTransfers(item, {0, 0, 0, 0}, requested->transfers, call.jsonrpc2); }
    if (wantsLimits) { addRateLimits(item, {0, 0}, requested->rateLimits, call.jsonrpc2); }
    addMetadata(item, {QStringLiteral("Download Archived"), QStringLiteral(""), 0, static_cast<qint64>(archive.files),
        false, 1, static_cast<qint64>(archive.size == 0 ? 1 : archive.size), 1.0}, requested->metadata, call.jsonrpc2);
    rows.emplace_back(id, std::move(item));
  }
  if (recent) {
    // The loopback listener issues a single session token. Ordinary reads
    // leave that session's last observed rows untouched.
    QMap<qint64, QJsonObject> observed;
    std::erase_if(rows, [&](const auto& entry) {
      const auto& [id, row] = entry;
      observed.insert(id, row);
      const auto previous = recentRows_.constFind(id);
      return previous != recentRows_.cend() && *previous == row;
    });
    recentRows_.swap(observed);
  }
  QJsonArray torrents;
  if (table && !rows.empty()) {
    auto keys = rows.front().second.keys();
    if (requested->status) { keys.move(keys.indexOf(QStringLiteral("status.biglybt")), keys.indexOf(QStringLiteral("status"))); }
    torrents.push_back(QJsonArray::fromStringList(keys));
    for (const auto& entry : rows) {
      QJsonArray row;
      for (const auto& key : keys) { row.push_back(entry.second.value(key)); }
      torrents.push_back(row);
    }
  } else {
    for (const auto& entry : rows) { torrents.push_back(entry.second); }
  }
  QJsonObject result{{QStringLiteral("torrents"), torrents}};
  if (recent && pendingRemovalCount_) {
    QJsonArray removed;
    for (std::size_t i = 0; i < pendingRemovalCount_; ++i) { removed.push_back(pendingRemovals_[i]); }
    result.insert(QStringLiteral("removed"), removed);
    pendingRemovalCount_ = 0;
  }
  return result;
}

} // namespace biglybtc::app

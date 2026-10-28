#pragma once

#include "biglybtc/library.hpp"
#include "biglybtc/bencode.hpp"
#include "biglybtc/saved_statistics.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace biglybtc::test {
namespace lt = libtorrent;
inline void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
inline std::string encoded(const lt::entry& value) {
  std::string result; lt::bencode(std::back_inserter(result), value); return result;
}
inline void write(const QString& path, const QByteArray& bytes) {
  require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot prepare generated file parent");
  QFile file(path); require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot write generated file");
  require(file.write(bytes) == bytes.size() && file.flush(), "Generated file write failed");
}
inline QByteArray read(const QString& path) {
  QFile file(path);
  const auto failure = [&](const char* stage, int savedErrno) {
    return std::runtime_error("Cannot read generated file: path=\"" + path.toUtf8().toStdString()
        + "\" stage=" + stage + " qfileError=" + std::to_string(static_cast<int>(file.error()))
        + " errno=" + std::to_string(savedErrno) + " errorString=\""
        + file.errorString().toUtf8().toStdString() + "\"");
  };
  if (!file.open(QIODevice::ReadOnly)) {
    const int savedErrno = errno;
    throw failure("open", savedErrno);
  }
  const auto bytes = file.readAll();
  const int savedErrno = errno;
  if (file.error() != QFileDevice::NoError) { throw failure("readAll", savedErrno); }
  return bytes;
}
inline lt::settings_pack localSettings() {
  lt::settings_pack settings;
  settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
  settings.set_str(lt::settings_pack::outgoing_interfaces, "127.0.0.1");
  settings.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
  for (auto key : {lt::settings_pack::enable_dht, lt::settings_pack::enable_lsd,
      lt::settings_pack::enable_upnp, lt::settings_pack::enable_natpmp,
      lt::settings_pack::enable_outgoing_utp, lt::settings_pack::enable_incoming_utp}) { settings.set_bool(key, false); }
  settings.set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::error
      | lt::alert_category::status | lt::alert_category::storage));
  return settings;
}

// All paths, data, sessions and peers are created here. No ordinary profile,
// original Java service, tracker, DNS or discovery source is used.
class OwnedEngineFixture final {
public:
  struct File { QString relative; QByteArray expected; struct stat before{}; };
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-engine-statistics-XXXXXX")};
  QString profile;
  QString payload;
  QString stateFile;
  std::optional<core::TorrentHash> hash;
  std::vector<File> files;
  const QByteArray statistics{"d3:alld10:downloadedi999eee"};
  static constexpr int PayloadBytes = 131072;

  explicit OwnedEngineFixture(bool multifile, bool partialSeed = false) : partialSeed_(partialSeed) {
    require(root.isValid(), "Cannot create generated owned fixture");
    profile = root.path() + QStringLiteral("/profile"); payload = root.path() + QStringLiteral("/payload");
    const auto seedPath = root.path() + QStringLiteral("/seed");
    require(QDir().mkpath(profile + QStringLiteral("/active")), "Cannot create generated active directory");
    QByteArray all(PayloadBytes, '\0');
    for (qsizetype i = 0; i < all.size(); ++i) { all[i] = static_cast<char>((i * 31 + 17) % 251); }
    if (multifile) {
      files.push_back({QStringLiteral("bundle/first.bin"), all.first(PayloadBytes / 2)});
      files.push_back({QStringLiteral("bundle/Sub/second.bin"), all.last(PayloadBytes / 2)});
    } else { files.push_back({QStringLiteral("sample.bin"), all}); }
    for (auto& file : files) {
      auto seedBytes = file.expected;
      // The cancellation/teardown peer lacks the final piece, so those
      // transfers cannot complete before their intended cancellation.
      if (partialSeed && &file == &files.back()) { std::fill(seedBytes.end() - 16384, seedBytes.end(), '\0'); }
      write(seedPath + '/' + file.relative, seedBytes);
      const auto path = payload + '/' + file.relative;
      write(path, QByteArray(file.expected.size(), '\0'));
      require(::stat(path.toStdString().c_str(), &file.before) == 0, "Cannot stat generated payload");
    }
    lt::entry info(lt::entry::dictionary_type{});
    info["name"] = multifile ? "bundle" : "sample.bin";
    info["piece length"] = 16384;
    std::string pieces;
    for (qsizetype offset = 0; offset < all.size(); offset += 16384) {
      pieces += QCryptographicHash::hash(all.mid(offset, 16384), QCryptographicHash::Sha1).toStdString();
    }
    info["pieces"] = pieces;
    if (multifile) {
      lt::entry::list_type entries;
      for (const auto& file : files) {
        lt::entry value(lt::entry::dictionary_type{}); value["length"] = static_cast<std::int64_t>(file.expected.size());
        lt::entry::list_type parts;
        const auto relative = file.relative.mid(QStringLiteral("bundle/").size());
        for (const auto& part : relative.split('/')) { parts.emplace_back(part.toStdString()); }
        value["path"] = std::move(parts); entries.push_back(std::move(value));
      }
      info["files"] = std::move(entries);
    } else { info["length"] = PayloadBytes; }
    const auto rawHash = QCryptographicHash::hash(QByteArray::fromStdString(encoded(info)), QCryptographicHash::Sha1);
    const auto parsedHash = core::TorrentHash::parse(std::string_view(rawHash.constData(), static_cast<std::size_t>(rawHash.size())));
    require(parsedHash.has_value(), "Generated hash was rejected"); hash = *parsedHash;
    stateFile = profile + QStringLiteral("/active/") + QString::fromStdString(hash->hex()) + QStringLiteral(".dat");
    lt::entry torrent(lt::entry::dictionary_type{}); torrent["info"] = info;
    torrent["opaque.fixture"] = std::string("\0\xffQ", 3);
    lt::entry::list_type stores;
    for (std::size_t i = 0; i < files.size(); ++i) { stores.emplace_back("L"); }
    torrent["attributes"]["storetypes"] = std::move(stores);
    torrent["attributes"]["networks"] = lt::entry::list_type{lt::entry("Public")};
    torrent["attributes"]["opaque.fixture"] = std::string("\0\xffQ", 3);
    write(stateFile, QByteArray::fromStdString(encoded(torrent)));
    lt::entry download(lt::entry::dictionary_type{});
    download["torrent_hash"] = rawHash.toStdString(); download["torrent"] = stateFile.toStdString();
    download["save_dir"] = payload.toStdString(); download["save_file"] = multifile ? "bundle" : "sample.bin";
    download["persistent"] = 1; download["state"] = 75; download["position"] = 1;
    download["completedbytes"] = 0; download["downloaded"] = 0; download["uploaded"] = 0;
    download["maxdl"] = 65536; // Several real observation intervals on a local transfer.
    download["file_priorities"] = lt::entry::list_type(files.size(), lt::entry(1));
    lt::entry inventory(lt::entry::dictionary_type{});
    inventory["downloads"] = lt::entry::list_type{download};
    write(profile + QStringLiteral("/downloads.config"), QByteArray::fromStdString(encoded(inventory)));
    write(profile + QStringLiteral("/tables.config"), QByteArray("de"));
    write(profile + QStringLiteral("/BiglyBT.statistics"), statistics);

    const auto raw = encoded(torrent); lt::error_code error;
    auto metadata = std::make_shared<lt::torrent_info>(lt::span<const char>(raw.data(), raw.size()), error, lt::from_span);
    require(!error, "Generated torrent metadata was rejected"); metadata->clear_trackers();
    seed_ = std::make_unique<lt::session>(localSettings());
    lt::add_torrent_params options; options.ti = std::move(metadata); options.save_path = seedPath.toStdString();
    options.flags &= ~lt::torrent_flags::paused; options.flags &= ~lt::torrent_flags::auto_managed;
    options.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
    handle_ = seed_->add_torrent(options, error); require(!error && handle_.is_valid(), "Cannot create owned local seed");
  }
  bool ready() const {
    const auto status = handle_.status();
    return seed_->listen_port() != 0 && (partialSeed_
        ? status.state != lt::torrent_status::checking_files && status.state != lt::torrent_status::checking_resume_data
            && status.num_pieces == PayloadBytes / 16384 - 1 && !status.is_seeding
        : status.is_seeding);
  }
  unsigned short port() const { return seed_->listen_port(); }
  std::filesystem::path native() const { return profile.toStdString(); }
  bool intact(bool complete = true) const {
    for (const auto& file : files) {
      const auto path = payload + '/' + file.relative; struct stat current{};
      if (::stat(path.toStdString().c_str(), &current) != 0 || current.st_dev != file.before.st_dev
          || current.st_ino != file.before.st_ino || (complete && read(path) != file.expected)) { return false; }
    }
    return read(profile + QStringLiteral("/BiglyBT.statistics")) == statistics
        && !QFile::exists(profile + QStringLiteral("/BiglyBT.statistics.bak"))
        && !QFile::exists(profile + QStringLiteral("/BiglyBT.statistics.saving"));
  }
  // Explicit persisted-accounting contract. intact() above remains unchanged
  // for offline/unbound/destructor cases that must preserve the whole original.
  bool accountingIntact(bool complete = true) const {
    for (const auto& file : files) {
      const auto path = payload + '/' + file.relative; struct stat current{};
      if (::stat(path.toStdString().c_str(), &current) != 0 || current.st_dev != file.before.st_dev
          || current.st_ino != file.before.st_ino || (complete && read(path) != file.expected)) { return false; }
    }
    const auto primary = profile + QStringLiteral("/BiglyBT.statistics");
    const auto regular = [](const QString& path) {
      const QFileInfo info(path); return info.isFile() && !info.isSymLink();
    };
    if (!regular(primary) || QFileInfo::exists(primary + QStringLiteral(".saving"))
        || QFileInfo(primary + QStringLiteral(".saving")).isSymLink()) { return false; }
    const auto original = core::readSavedStatistics(std::string_view(statistics.constData(), static_cast<std::size_t>(statistics.size())));
    if (!original || !original->hasAll) { return false; }
    const auto rawEqual = [](const core::Document& before, const core::Node& a,
                             const core::Document& after, const core::Node* b) {
      return b && before.originalBytes().substr(a.begin, a.end - a.begin)
          == after.originalBytes().substr(b->begin, b->end - b->begin);
    };
    const auto valid = [&](const QByteArray& bytes) {
      if (bytes == statistics) { return true; } // Failed/no-op save preserves the original whole map.
      const auto parsed = core::readSavedStatistics(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
      if (!parsed || !parsed->hasAll) { return false; }
      for (const auto& field : parsed->values) {
        if (!field.value || field.invalidType) { return false; }
      }
      const auto& oldRoot = original->source->root();
      for (std::size_t i = 0; i < oldRoot.children.size(); i += 2) {
        const auto key = oldRoot.children[i].bytes;
        if (key != "all" && !rawEqual(*original->source, oldRoot.children[i + 1], *parsed->source, parsed->source->find(key))) { return false; }
      }
      const auto* oldAll = original->source->find("all");
      const auto* newAll = parsed->source->find("all");
      for (std::size_t i = 0; i < oldAll->children.size(); i += 2) {
        const auto key = oldAll->children[i].bytes;
        if (std::find(core::SavedStatisticKeys.begin(), core::SavedStatisticKeys.end(), key) == core::SavedStatisticKeys.end()
            && !rawEqual(*original->source, oldAll->children[i + 1], *parsed->source, newAll->find(key))) { return false; }
      }
      return true;
    };
    if (!valid(read(primary))) { return false; }
    const auto backup = primary + QStringLiteral(".bak"); const QFileInfo backupInfo(backup);
    return !backupInfo.isSymLink() && (!backupInfo.exists() || (regular(backup) && valid(read(backup))));
  }
private:
  std::unique_ptr<lt::session> seed_;
  lt::torrent_handle handle_;
  bool partialSeed_;
};
} // namespace biglybtc::test

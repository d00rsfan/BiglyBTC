// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive.hpp"
#include "state_file_io.hpp"

#include <bit>
#include <new>

namespace biglybtc::storage {
namespace {
using core::Node;

std::optional<std::string> text(const Node& row, std::string_view key) {
  const auto* value = row.find(key);
  if (!value || value->type != Node::Type::Bytes) { return {}; }
  return std::string(value->bytes);
}

std::int64_t integer(const Node& row, std::string_view key, std::int64_t fallback = 0) {
  const auto* value = row.find(key);
  return value && value->type == Node::Type::Integer ? value->integer : fallback;
}

core::ArchivedDownloadMetadata readMetadata(const Node& row) {
  core::ArchivedDownloadMetadata out;
  out.name = text(row, "name");
  out.savePath = text(row, "l");
  out.category = text(row, "cat");
  out.tracker = text(row, "tr");
  out.size = integer(row, "s");
  out.archivedMillis = integer(row, "dt");
  out.completedMillis = integer(row, "dc");
  out.shareRatio = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(integer(row, "sr", -1)));
  if (const auto* tags = row.find("t"); tags && tags->type == Node::Type::List) {
    for (const auto& tag : tags->children) {
      if (tag.type == Node::Type::Bytes) { out.tags.emplace_back(tag.bytes); }
    }
  }
  if (const auto* files = row.find("files")) {
    if (files->type != Node::Type::List) {
      out.fileLengths.reset();
      return out;
    }
    out.files = files->children.size();
    out.fileLengths->reserve(out.files);
    out.fileEntries.reserve(out.files);
    for (const auto& file : files->children) {
      core::ArchivedFile entry;
      entry.path = text(file, "file");
      if (!entry.path) {
        entry.path = text(file, "rel");
        entry.relative = entry.path.has_value();
      }
      const auto* length = file.find("len");
      if (length && length->type == Node::Type::Integer) {
        entry.length = length->integer;
        if (out.fileLengths) { out.fileLengths->push_back(*entry.length); }
      } else {
        out.fileLengths.reset();
      }
      out.fileEntries.push_back(std::move(entry));
    }
  }
  return out;
}
} // namespace

std::expected<DownloadArchive, Error> readDownloadArchive(const ProfileLease& lease, std::stop_token stop) try {
  constexpr std::size_t maximumStubs = 100000;
  const Error cancelled{ErrorCode::Cancelled, "Archive read cancelled"};
  if (stop.stop_requested()) { return std::unexpected(cancelled); }
  const auto access = lease.sourceAccess();
  if (!access) { return std::unexpected(access.error()); }
  auto source = ConfigFile::load(lease, DownloadArchiveFile);
  if (stop.stop_requested()) { return std::unexpected(cancelled); }
  if (!source) {
    if (source.error().code != ErrorCode::Missing) { return std::unexpected(source.error()); }
    for (const auto* suffix : {"", ".bak", ".saving"}) {
      const auto candidate = detail::stateStampAt(lease.directoryHandle(), std::string(DownloadArchiveFile) + suffix);
      if (!candidate) { return std::unexpected(candidate.error()); }
      if (*candidate) { return std::unexpected(Error{ErrorCode::InvalidFormat, "No complete archive candidate could be read"}); }
    }
    return DownloadArchive{};
  }
  DownloadArchive out{std::move(*source), {}};
  const auto* rows = out.source->document().find("stubs");
  if (!rows) { return out; }
  if (rows->type != Node::Type::List) {
    return std::unexpected(Error{ErrorCode::InvalidFormat, "Original archive stub list has an invalid type"});
  }
  if (rows->children.size() > maximumStubs) {
    return std::unexpected(Error{ErrorCode::Unsupported, "Original archive exceeds its torrent limit"});
  }
  out.stubs.reserve(rows->children.size());
  for (const auto& row : rows->children) {
    if (stop.stop_requested()) { return std::unexpected(cancelled); }
    core::ArchivedDownload stub;
    const auto* hash = row.find("hash");
    if (row.type == Node::Type::Dictionary && hash && hash->type == Node::Type::Bytes) {
      if (auto parsed = core::TorrentHash::parse(hash->bytes)) {
        stub.hash = *parsed;
        stub.metadata = readMetadata(row);
      }
    }
    out.stubs.push_back(std::move(stub));
  }
  return out;
} catch (const std::bad_alloc&) {
  return std::unexpected(Error{ErrorCode::Io, "Insufficient memory to read the archive"});
}

} // namespace biglybtc::storage

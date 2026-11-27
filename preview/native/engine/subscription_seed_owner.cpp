// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_seed_owner.hpp"
#include "../platform/utf8_path.hpp"
#include "metadata_disk_router.hpp"
#include "original_disk_io.hpp"

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <algorithm>
#include <limits>

namespace biglybtc::engine::detail {
namespace {
namespace lt = libtorrent;
namespace state = storage::detail;
using namespace std::chrono_literals;

// Bound the extra disk checks and sockets independently of the library queue.
constexpr std::size_t ActiveLimit = 4;
constexpr auto IdleTimeout = 5min;
constexpr auto RevalidationInterval = 5s;

template <typename Metadata>
concept AcceptsSubscriptionHash = requires(Metadata& info, const lt::sha1_hash& hash) {
  info.use_biglybt_subscription_hash(hash);
};

template <typename Metadata>
bool bindSubscriptionHash(Metadata& info, const lt::sha1_hash& hash) {
  if constexpr (AcceptsSubscriptionHash<Metadata>) {
    return info.use_biglybt_subscription_hash(hash);
  }
  return false;
}

std::string_view bytes(const lt::sha1_hash& hash) { return {hash.data(), 20}; }
} // namespace

std::optional<PreparedSubscriptionTorrent> prepareSubscriptionTorrent(const lt::sha1_hash& hash,
    const SubscriptionSeedSource& source, const std::filesystem::path& slot, MetadataDiskPolicy& disks,
    std::function<bool()> sourceCurrent) {
  if (!source.directoryHandle || source.metainfo.empty()) { return {}; }
  const auto parent = source.directoryHandle->get();
  const auto resolved = state::stateDirectoryPath(parent);
  if (!resolved || *resolved != source.directory) { return {}; }
  lt::error_code error;
  const auto data = lt::span<const char>{source.metainfo.data(),
      static_cast<std::ptrdiff_t>(source.metainfo.size())};
  auto metadata = std::make_shared<lt::torrent_info>(data, error, lt::from_span);
  if (error || !bindSubscriptionHash(*metadata, hash)) { return {}; }

  const auto index = lt::file_index_t{0};
  const auto fileName = metadata->files().file_name(index);
  const auto leaf = platform::pathFromUtf8({fileName.data(), fileName.size()});
  auto descriptor = state::openOriginalPayloadAt(parent, leaf, state::OriginalPayloadAccess::ReadOnly);
  if (!descriptor) { return {}; }
  const auto fd = descriptor->get();
  const auto stamp = state::stateStamp(fd);
  const auto payloadSize = metadata->files().file_size(index);
  if (!stamp || payloadSize <= 0 || stamp->size != payloadSize || !stamp->owned || !stamp->regular) { return {}; }
  const auto verifyFile = [parent = source.directoryHandle, leaf, fd, accepted = *stamp,
                           sourceCurrent = std::move(sourceCurrent)]()
      -> std::expected<void, storage::Error> {
    if (sourceCurrent && !sourceCurrent()) {
      return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Subscription seed source withdrew"});
    }
    const auto held = state::stateStamp(fd);
    const auto named = state::stateStampAt(parent->get(), leaf);
    if (!held || !named || !*named || !state::sameStateSnapshot(*held, accepted)
        || !state::sameStateSnapshot(**named, accepted)) {
      return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Versioned subscription payload changed"});
    }
    return {};
  };
  if (!verifyFile()) { return {}; }
  auto io = storage::OriginalFileIo::open(fd, {0, static_cast<std::uint64_t>(payloadSize),
      static_cast<std::uint32_t>(metadata->piece_length())}, storage::OriginalStorageKind::Linear, {});
  if (!io) { return {}; }
  std::vector<std::optional<OriginalDiskFile>> payload(1);
  payload.front().emplace(OriginalDiskFile{std::move(*descriptor), std::move(*io), verifyFile});
  auto files = std::make_shared<OriginalDiskFiles>(metadata, std::move(payload), OriginalDiskAccess::ReadOnly);

  lt::add_torrent_params request;
  request.ti = std::move(metadata);
  request.save_path = platform::pathToUtf8(slot);
  request.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
  // Let the SDK verify all pieces. Its stop-when-ready gate prevents uploads
  // between completion of the check and the owning worker's authorization.
  request.flags |= lt::torrent_flags::upload_mode | lt::torrent_flags::stop_when_ready
      | lt::torrent_flags::duplicate_is_error | lt::torrent_flags::override_trackers
      | lt::torrent_flags::override_web_seeds | lt::torrent_flags::disable_dht
      | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
  if (!disks.registerOriginalPath(slot, files)) { return {}; }
  return PreparedSubscriptionTorrent{std::move(request), std::move(files)};
}

bool SubscriptionSeedOwner::supported() noexcept { return AcceptsSubscriptionHash<lt::torrent_info>; }

void SubscriptionSeedOwner::withdraw(lt::session& session, const lt::info_hash_t& hash) {
  if (!hash.has_v1()) { return; }
  const auto seed = active_.find(hash.v1);
  if (seed != active_.end()) {
    if (seed->second.handle.is_valid()) { session.remove_torrent(seed->second.handle); }
    active_.erase(seed);
  }
}

void SubscriptionSeedOwner::clear(lt::session& session) {
  if (listening()) { demand_->replace({}); }
  candidates_.clear();
  for (const auto& [hash, seed] : active_) {
    static_cast<void>(hash);
    if (seed.handle.is_valid()) { session.remove_torrent(seed.handle); }
  }
  active_.clear();
}

void SubscriptionSeedOwner::refresh(lt::session& session, MetadataDiskPolicy& disks,
    std::span<const SubscriptionSeedHash> hashes,
    const SubscriptionSeedResolver& resolve) {
  if (!supported() || hashes.empty() || !resolve) { clear(session); return; }

  std::vector<lt::sha1_hash> candidates;
  candidates.reserve(hashes.size());
  for (const auto& hash : hashes) { candidates.emplace_back(lt::span<const char>{hash}); }
  if (!demand_) {
    auto demand = std::make_shared<DormantSeedDemand>();
    session.add_extension(demand);
    demand_ = std::move(demand);
  }
  if (candidates != candidates_) {
    demand_->replace(candidates);
    candidates_ = std::move(candidates);
  }

  const auto now = Clock::now();
  for (auto it = active_.begin(); it != active_.end();) {
    auto& [hash, seed] = *it;
    if (!seed.handle.is_valid()) { it = active_.erase(it); continue; }
    const auto status = seed.handle.status({});
    const bool checking = !seed.ready && (status.flags & lt::torrent_flags::stop_when_ready);
    if (status.errc || now >= seed.expires || std::ranges::find(candidates_, hash) == candidates_.end()
        || (!checking && !status.is_seeding)) {
      session.remove_torrent(seed.handle);
      it = active_.erase(it);
      continue;
    }
    if (checking) { ++it; continue; }
    if (now >= seed.revalidateAt) {
      const auto current = resolve(bytes(hash), SubscriptionSeedRead::Revalidate);
      if (!current || !current->sameVersion(seed.source)) {
        session.remove_torrent(seed.handle);
        it = active_.erase(it);
        continue;
      }
      seed.revalidateAt = now + RevalidationInterval;
    }
    if (!seed.ready) {
      seed.handle.resume();
      seed.ready = true;
    }
    if (status.num_peers > 0) { seed.expires = now + IdleTimeout; }
    ++it;
  }
  for (const auto& hash : demand_->takePeerDemand(ActiveLimit - active_.size())) {
    if (active_.contains(hash) || session.find_torrent(hash).is_valid()) { continue; }
    auto source = resolve(bytes(hash), SubscriptionSeedRead::Prepare);
    if (!source || nextStorageSlot_ == std::numeric_limits<std::uint64_t>::max()) { continue; }
    const auto slot = source->directory / (".BiglyBTC-subscription-seed-" + std::to_string(++nextStorageSlot_));
    auto prepared = prepareSubscriptionTorrent(hash, *source, slot, disks);
    if (!prepared) { continue; }
    auto [seed, inserted] = active_.try_emplace(hash, Active{{}, {}, now + IdleTimeout, now});
    if (!inserted) { continue; }
    lt::error_code error;
    seed->second.handle = session.add_torrent(std::move(prepared->parameters), error);
    seed->second.source = std::move(*source);
    if (error || !seed->second.handle.is_valid()) { active_.erase(seed); }
  }
}
} // namespace biglybtc::engine::detail

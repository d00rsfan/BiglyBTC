#include "saved_download_tags_view.hpp"
#include "biglybtc/queue_policy.hpp"
#include <new>

namespace biglybtc::storage {
namespace {
Error cancelled() { return {ErrorCode::Cancelled, "Saved download-tag read cancelled"}; }
Error projectionError(core::SavedTagError error) {
  switch (error) {
    case core::SavedTagError::Limit: return {ErrorCode::Unsupported, "Saved download tags exceed projection limits"};
    case core::SavedTagError::Memory: return {ErrorCode::Io, "Insufficient memory for saved download tags"};
    default: return {ErrorCode::InvalidFormat, "Invalid saved manual download-tag data"};
  }
}
}
std::expected<SavedDownloadTagsView, Error> readSavedDownloadTagsView(const ProfileLease& lease, std::stop_token stop) {
  try {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto file = ConfigFile::load(lease, "tag.config", core::Limits{});
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!file) {
      if (file.error().code != ErrorCode::Missing) { return std::unexpected(file.error()); }
      for (const char* name : {"tag.config", "tag.config.bak", "tag.config.saving"}) {
        if (stop.stop_requested()) { return std::unexpected(cancelled()); }
        auto bytes = detail::readStateFile(lease.directoryHandle(), name, core::Limits{}.inputBytes);
        if (stop.stop_requested()) { return std::unexpected(cancelled()); }
        if (!bytes) { return std::unexpected(bytes.error()); }
        if (bytes->has_value()) { return std::unexpected(Error{ErrorCode::InvalidFormat, "A tag configuration candidate exists but none was read completely"}); }
      }
      return SavedDownloadTagsView{};
    }
    auto tags = core::readSavedDownloadTags(file->document().originalBytes());
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!tags) { return std::unexpected(projectionError(tags.error())); }
    SavedDownloadTagsView result{std::move(*tags), file->recoverySource()};
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return result;
  } catch (const std::bad_alloc&) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return std::unexpected(Error{ErrorCode::Io, "Insufficient memory for saved download tags"});
  }
}

std::shared_ptr<const core::SavedTagQueueRules> SavedTagQueueReader::read(
    const ProfileLease& lease, const ProfileSourceAccess& access) noexcept {
  const auto unavailable = [this]() -> std::shared_ptr<const core::SavedTagQueueRules> {
    stamps_.reset();
    priorities_.reset();
    return {};
  };
  if (!lease.authorizesObservation(access)) { return unavailable(); }
  try {
    const auto capture = [&]() -> std::optional<Stamps> {
      constexpr std::array names{"tag.config", "tag.config.bak", "tag.config.saving"};
      Stamps result;
      for (std::size_t i = 0; i < names.size(); ++i) {
        const auto observed = detail::stateStampAt(lease.directoryHandle(), names[i]);
        if (!observed || (*observed && (!(*observed)->owned || !(*observed)->regular
            || (*observed)->links != 1))) { return {}; }
        result[i] = *observed;
      }
      return result;
    };
    const auto same = [](const Stamps& a, const Stamps& b) {
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (bool(a[i]) != bool(b[i]) || (a[i] && !detail::sameStateSnapshot(*a[i], *b[i]))) { return false; }
      }
      return true;
    };
    const auto before = capture();
    if (!before || before->back()) { return unavailable(); }
    if (stamps_ && same(*stamps_, *before)) { return priorities_; }
    const auto saved = readSavedDownloadTagsView(lease);
    if (!saved) { return unavailable(); }
    auto priorities = std::make_shared<const core::SavedTagQueueRules>(
        saved->snapshot ? core::savedTagQueueRules(*saved->snapshot) : core::SavedTagQueueRules{});
    const auto after = capture();
    if (!after || !same(*before, *after)) { return unavailable(); }
    stamps_ = *after;
    priorities_ = std::move(priorities);
    return priorities_;
  } catch (...) {
    return unavailable();
  }
}

void applySavedTagQueueRules(const core::SavedTagQueueRules* priorities, std::string_view hash,
    core::QueueTorrentFacts& facts) {
  if (!priorities) { return; }
  const auto found = priorities->find(hash);
  if (found == priorities->end()) { return; }
  facts.externallyFirstPriority |= found->second.first;
  facts.externallyNotFirstPriority |= found->second.notFirst;
  facts.tagActivityLimits = found->second.activityLimits;
}
} // namespace biglybtc::storage

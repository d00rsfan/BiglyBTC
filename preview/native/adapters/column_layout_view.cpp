#include "column_layout_view.hpp"

#include "file_io.hpp"

#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
// Original saved-layout source: BiglyBT
// cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, TableColumnManager.
// This adapter uses only the existing read-only directory-fd ConfigFile reader;
// it does not initialize the original manager. Native-only preference saving
// uses the existing ConfigFile transaction without editing original table keys.
Error cancelled() { return {ErrorCode::Cancelled, "Column-layout read cancelled"}; }
Error projectionError(core::ColumnLayoutError error) {
  switch (error) {
    case core::ColumnLayoutError::Limit:
      return {ErrorCode::Unsupported, "Column-layout data exceeds the bounded preview projection limits"};
    case core::ColumnLayoutError::Memory:
      return {ErrorCode::Io, "Insufficient memory for column-layout data"};
    case core::ColumnLayoutError::Root:
      return {ErrorCode::InvalidFormat, "Invalid column-layout document structure"};
    case core::ColumnLayoutError::EditedDocument:
      return {ErrorCode::InvalidFormat, "Edited column-layout document is not an original saved snapshot"};
  }
  return {ErrorCode::InvalidFormat, "Unsupported column-layout projection result"};
}

Error preferenceError(core::ClassicPreferenceError error) {
  switch (error) {
    case core::ClassicPreferenceError::Invalid:
      return {ErrorCode::InvalidFormat, "Invalid native classic column preferences"};
    case core::ClassicPreferenceError::Unsupported:
      return {ErrorCode::Unsupported, "Unsupported native classic column preferences"};
    case core::ClassicPreferenceError::Limit:
      return {ErrorCode::Unsupported, "Native classic column preferences exceed the bounded limits"};
    case core::ClassicPreferenceError::Memory:
      return {ErrorCode::Io, "Insufficient memory for native classic column preferences"};
    case core::ClassicPreferenceError::EditedDocument:
      return {ErrorCode::InvalidFormat, "Edited column-layout document is not a pristine saved baseline"};
  }
  return {ErrorCode::InvalidFormat, "Unsupported native classic preference result"};
}

// The existing loader can report Missing when the primary is absent but its
// recovery candidates exist and are malformed. Confirm true absence instead
// of presenting malformed saved settings as an empty/missing layout. These
// separate observations do not promise an atomic view against external edits.
std::expected<bool, Error> confirmAbsent(const ProfileLease& lease, std::stop_token stop) {
  for (const char* name : {"tables.config", "tables.config.bak", "tables.config.saving"}) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto bytes = detail::readFile(lease.directoryHandle(), name, core::Limits{}.inputBytes);
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!bytes) { return std::unexpected(bytes.error()); }
    if (bytes->has_value()) { return false; }
  }
  return true;
}
} // namespace

std::expected<ColumnLayoutsView, Error> readColumnLayoutsView(
    const ProfileLease& lease, std::stop_token stop) {
  try {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto file = ConfigFile::load(lease, "tables.config", core::Limits{});
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!file) {
      if (file.error().code != ErrorCode::Missing) { return std::unexpected(file.error()); }
      const auto absent = confirmAbsent(lease, stop);
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      if (!absent) { return std::unexpected(absent.error()); }
      if (!*absent) {
        return std::unexpected(Error{ErrorCode::InvalidFormat,
            "A column-layout candidate exists but no complete compatible document was read"});
      }
      return ColumnLayoutsView{};
    }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto snapshot = core::readColumnLayouts(file->document());
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!snapshot) { return std::unexpected(projectionError(snapshot.error())); }
    auto preferences = core::readClassicPreferences(file->document());
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!preferences) { return std::unexpected(preferenceError(preferences.error())); }
    ColumnLayoutsView result;
    result.recoverySource = file->recoverySource();
    result.snapshot.emplace(std::move(*snapshot));
    if (preferences->has_value()) { result.nativePreferences.emplace(std::move(**preferences)); }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return result;
  } catch (const std::bad_alloc&) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return std::unexpected(Error{ErrorCode::Io, "Insufficient memory for column-layout data"});
  }
}

std::expected<std::shared_ptr<const core::Document>, Error> saveClassicPreferencesView(
    const ProfileLease& lease, const core::Document& baseline,
    RecoverySource baselineSource, const core::ClassicPreferences& preferences,
    std::stop_token stop, void (*observer)(SaveStage)) {
  try {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (baselineSource != RecoverySource::Primary) {
      return std::unexpected(Error{ErrorCode::Conflict, "Only an unchanged primary column-layout baseline can be saved"});
    }
    // The qualified reader validates caller tree/raw-byte consistency before
    // trusting byte views. Discard its projection before loading the held file.
    {
      auto pristine = core::readColumnLayouts(baseline);
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      if (!pristine) { return std::unexpected(projectionError(pristine.error())); }
    }
    auto file = ConfigFile::load(lease, "tables.config", core::Limits{});
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!file) { return std::unexpected(file.error()); }
    if (file->recoverySource() != RecoverySource::Primary) {
      return std::unexpected(Error{ErrorCode::Conflict, "Recovery column-layout data must be resolved before saving"});
    }
    if (file->document().originalBytes() != baseline.originalBytes()) {
      return std::unexpected(Error{ErrorCode::Conflict, "Column-layout configuration changed since it was read"});
    }
    {
      auto existing = core::readClassicPreferences(file->document());
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      if (!existing) { return std::unexpected(preferenceError(existing.error())); }
    }
    auto edited = core::writeClassicPreferences(file->document(), preferences);
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!edited) { return std::unexpected(preferenceError(edited.error())); }
    // Prepare the successful baseline before any publication. In particular,
    // allocation after a committed save must not hide success behind bad_alloc.
    auto reparsed = core::Document::parse(file->document().serialize(), core::Limits{});
    if (!reparsed) {
      return std::unexpected(Error{ErrorCode::InvalidFormat, "Edited column-layout configuration exceeds ordinary limits"});
    }
    auto savedBaseline = std::make_shared<const core::Document>(std::move(*reparsed));
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    const auto saved = file->save(lease, observer);
    if (!saved) { return std::unexpected(saved.error()); }
    // No cancellation checkpoint or allocating success construction after save.
    return savedBaseline;
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error{ErrorCode::Io, "Insufficient memory while saving native classic column preferences"});
  }
}


std::expected<std::shared_ptr<const core::Document>, Error> createClassicPreferencesView(
    const ProfileLease& lease, const core::ClassicPreferences& preferences,
    std::stop_token stop, void (*observer)(SaveStage)) {
  try {
    if (stop.stop_requested()) { return std::unexpected(Error{ErrorCode::Cancelled, "Native classic column preference creation cancelled"}); }
    if (!lease.valid()) {
      return std::unexpected(Error{ErrorCode::UnsafePath, "Held profile lease required to create native classic column preferences"});
    }
    const auto valid = core::validateClassicPreferences(preferences);
    if (!valid) { return std::unexpected(preferenceError(valid.error())); }
    auto empty = core::Document::parse("de", core::Limits{});
    if (!empty) {
      return std::unexpected(Error{ErrorCode::InvalidFormat, "Unable to prepare empty column-layout configuration"});
    }
    const auto edited = core::writeClassicPreferences(*empty, preferences);
    if (!edited) { return std::unexpected(preferenceError(edited.error())); }
    auto pristine = core::Document::parse(empty->serialize(), core::Limits{});
    if (!pristine) {
      return std::unexpected(Error{ErrorCode::InvalidFormat, "New column-layout configuration exceeds ordinary limits"});
    }
    auto baseline = std::make_shared<const core::Document>(std::move(*pristine));
    if (stop.stop_requested()) { return std::unexpected(Error{ErrorCode::Cancelled, "Native classic column preference creation cancelled"}); }
    auto created = ConfigFile::createMissing(
        lease, "tables.config", baseline->originalBytes(), core::Limits{}, observer);
    if (!created) { return std::unexpected(std::move(created.error())); }
    // No fallible successful-result preparation or cancellation after publication.
    return baseline;
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error{ErrorCode::Io, "Insufficient memory while creating native classic column preferences"});
  }
}

} // namespace biglybtc::storage

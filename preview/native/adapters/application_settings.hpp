#pragma once
#include "profile.hpp"
#include "biglybtc/application_settings.hpp"
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace biglybtc::storage {
inline constexpr core::Limits ApplicationSettingsLimits{}; //4MiB/100k/depth64
enum class SettingState { Defaulted, Stored, InvalidType, OutOfRange, UnavailableDefault };
struct ApplicationSetting {
  core::SettingId id;
  SettingState state;
  // Wrong-type values have no typed value. OutOfRange retains the actual typed
  // original value for display; no malformed field is silently defaulted.
  std::optional<core::SettingValue> value;
};
struct ApplicationSettingsSnapshot {
  std::shared_ptr<const core::Document> source;
  std::optional<RecoverySource> recoverySource;
  std::vector<ApplicationSetting> fields;
  bool missing() const noexcept { return !source && !recoverySource; }
};
std::expected<ApplicationSettingsSnapshot, Error> readApplicationSettings(
    const ProfileLease&, std::stop_token = {});
class PreparedApplicationSettings final {
public:
  const core::Document& document() const noexcept { return *document_; }
  bool changed() const noexcept { return changed_; }
private:
  PreparedApplicationSettings(std::shared_ptr<const core::Document> baseline,
      std::optional<RecoverySource> source, std::shared_ptr<const core::Document> document, bool changed)
      : baseline_(std::move(baseline)), source_(source), document_(std::move(document)), changed_(changed) {}
  std::shared_ptr<const core::Document> baseline_;
  std::optional<RecoverySource> source_;
  std::shared_ptr<const core::Document> document_;
  bool changed_;
  friend std::expected<PreparedApplicationSettings, Error> prepareApplicationSettings(
      const ApplicationSettingsSnapshot&, std::span<const core::SettingChange>);
  friend std::expected<ApplicationSettingsSnapshot, Error> saveApplicationSettings(
      const ProfileLease&, const ApplicationSettingsSnapshot&, const PreparedApplicationSettings&,
      std::stop_token, void (*)(SaveStage));
};
// Sparse edits only. Duplicate/invalid IDs, types or editor-range values reject;
// OriginalOnly changes may repair incompatible saved settings but have no native
// live effect. Caller validates cross-field network/queue policy before save.
std::expected<PreparedApplicationSettings, Error> prepareApplicationSettings(
    const ApplicationSettingsSnapshot&, std::span<const core::SettingChange>);
// Fixed biglybt.config (pinned original ConfigurationManager.CONFIG_FILENAME).
// Whole original snapshot CAS; no defaults materialization on unchanged save.
// Recovery remains explicit: read Backup/Saving for diagnosis, save requires a
// pristine Primary or verified all-absent baseline. Raw storage errors retained.
// Preallocate successful return before publication; cancellation only precommit.
std::expected<ApplicationSettingsSnapshot, Error> saveApplicationSettings(
    const ProfileLease&, const ApplicationSettingsSnapshot&, const PreparedApplicationSettings&,
    std::stop_token = {}, void (*observer)(SaveStage) = nullptr);
} // namespace biglybtc::storage

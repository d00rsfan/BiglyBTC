// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_backup_scheduler.hpp"
#include "../platform/utf8_path.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>

namespace biglybtc::storage {
namespace {
constexpr char Configuration[] = "biglybt.config";
constexpr char AttemptFile[] = "BiglyBTC.backup-schedule.config";
constexpr char LastDay[] = "br.backup.auto.last_backup_day";
constexpr char LastHour[] = "br.backup.auto.last_backup_hour";
constexpr char LastTime[] = "last_attempt_millis";

template<class T>
T take(std::expected<T, Error> value) {
  if (!value) { throw std::move(value.error()); }
  if constexpr (!std::is_void_v<T>) { return std::move(*value); }
}

std::int64_t integer(const core::Document& document, const char* key, std::int64_t fallback = 0) {
  const auto* value = document.find(key);
  return value && value->type == core::Node::Type::Integer ? value->integer : fallback;
}

std::int32_t interval(const core::Document& document, const char* key, std::int32_t fallback) {
  const auto value = integer(document, key, fallback);
  return static_cast<std::int32_t>(std::clamp<std::int64_t>(value,
      std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
}

std::optional<ConfigFile> readConfiguration(const ProfileLease& lease, const char* leaf, const char* message) {
  auto file = ConfigFile::load(lease, leaf, compatibleConfigLimits());
  if (!file && file.error().code == ErrorCode::Missing) { return {}; }
  if (!file) { throw std::move(file.error()); }
  if (file->recoverySource() != RecoverySource::Primary) { throw Error{ErrorCode::Conflict, message}; }
  return std::move(*file);
}

void recordAttempt(const ProfileLease& lease, std::optional<ConfigFile> previous,
    const core::ProfileBackupScheduleResult& plan, std::int64_t nowUtcMs) {
  auto result = previous ? previous->document() : core::Document::parse("de").value();
  for (const auto& [key, value] : {std::pair{LastDay, plan.localDay}, std::pair{LastHour, plan.localHour},
           std::pair{LastTime, nowUtcMs}}) {
    if (!result.replaceValue(key, 'i' + std::to_string(value) + 'e')) {
      throw Error{ErrorCode::InvalidFormat, "Cannot record the automatic backup attempt in the profile"};
    }
  }
  // A separate state file keeps the timer from replacing the active runtime's
  // main configuration while its network sources still hold it.
  if (previous) {
    previous->document() = std::move(result);
    take(previous->save(lease));
  } else {
    take(ConfigFile::createMissing(lease, AttemptFile, result.serialize(), compatibleConfigLimits()));
  }
}
} // namespace

std::expected<AutomaticProfileBackup, Error> pollAutomaticProfileBackup(const ProfileLease& lease,
    core::ProfileBackupClock clock, std::string_view localDate, std::stop_token stop) try {
  if (stop.stop_requested()) { return std::unexpected(Error{ErrorCode::Cancelled, "Automatic profile backup cancelled"}); }
  auto access = take(lease.sourceAccess());
  const auto configuration = readConfiguration(lease, Configuration, "Recover the main configuration before automatic backup");
  if (!configuration) { return AutomaticProfileBackup{}; }
  const auto& document = configuration->document();
  core::ProfileBackupSchedule preferences;
  preferences.enabled = integer(document, "br.backup.auto.enable") != 0;
  if (!preferences.enabled) { return AutomaticProfileBackup{}; }
  preferences.everyDays = interval(document, "br.backup.auto.everydays", preferences.everyDays);
  preferences.everyHours = interval(document, "br.backup.auto.everyhours", preferences.everyHours);
  preferences.startupDelayMinutes = interval(document, "br.backup.auto.init.delay.mins", preferences.startupDelayMinutes);
  preferences.lastBackupDay = integer(document, LastDay);
  preferences.lastBackupHour = integer(document, LastHour);
  auto previous = readConfiguration(lease, AttemptFile, "Recover the automatic backup schedule before trying again");
  if (previous) {
    preferences.lastBackupDay = std::max(preferences.lastBackupDay, integer(previous->document(), LastDay));
    preferences.lastBackupHour = std::max(preferences.lastBackupHour, integer(previous->document(), LastHour));
    const auto observedAttempt = integer(previous->document(), LastTime);
    if (observedAttempt <= clock.nowUtcMillis) {
      clock.lastAttemptUtcMillis = std::max(clock.lastAttemptUtcMillis, observedAttempt);
    }
  }
  const auto plan = core::profileBackupSchedule(preferences, clock);
  AutomaticProfileBackup result{plan, {}, integer(document, "br.backup.notify") != 0, {}};
  if (plan.action != core::ProfileBackupAction::Run) { return result; }
  if (stop.stop_requested()) { return std::unexpected(Error{ErrorCode::Cancelled, "Automatic profile backup cancelled"}); }
  recordAttempt(lease, std::move(previous), plan, clock.nowUtcMillis);
  const auto* path = document.find("br.backup.auto.dir");
  if (!path || path->type != core::Node::Type::Bytes || path->bytes.empty() || path->bytes.contains('\0')) {
    return std::unexpected(Error{ErrorCode::InvalidFormat, "Set an existing folder for automatic profile backups"});
  }
  result.completed = take(backupSavedProfile(lease, platform::pathFromUtf8(path->bytes), localDate, stop));
  const auto retain = std::max(1, interval(document, "br.backup.auto.retain", 5));
  const auto rotated = rotateSavedProfileBackups(lease, *result.completed, static_cast<std::uint32_t>(retain), stop);
  if (!rotated && rotated.error().code != ErrorCode::Cancelled) { result.retentionError = rotated.error(); }
  return result;
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Automatic profile backup did not finish"});
}

} // namespace biglybtc::storage

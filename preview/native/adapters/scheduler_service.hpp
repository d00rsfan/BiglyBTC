#pragma once

#include "profile.hpp"
#include "biglybtc/library.hpp"
#include "biglybtc/plugin_host.hpp"
#include "scheduler.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <stop_token>

namespace biglybtc::storage {
namespace scheduler = plugins::speed_scheduler;
struct SchedulerSettings {
  bool enabled = false, useTags = false, overrideForced = false;
  int pollMilliseconds = 10000;
};
struct SchedulerSettingsChanges {
  std::optional<bool> enabled{}, useTags{}, overrideForced{};
  std::optional<int> pollMilliseconds{};
};
struct SchedulerScheduleUpdate { std::size_t sourceIndex; scheduler::Schedule value; };
struct SchedulerXmlChanges {
  struct Defaults { int upload = 0, download = 0; bool enforce = true; };
  std::optional<Defaults> defaults{};
  std::vector<SchedulerScheduleUpdate> update{};
  std::vector<std::size_t> remove{};
  std::vector<scheduler::Schedule> append{};
};
struct SchedulerSnapshot {
  std::uint64_t revision = 0;
  std::string directoryLeaf;
  SchedulerSettings settings;
  std::optional<scheduler::Configuration> configuration;
  std::string originalXml; // Complete raw source, including unknown bytes/encoding.
  std::string ownershipBytes;
  bool interruptedAction = false;
  bool inheritedOwnershipUnverified = false;
  std::string diagnostic;
};
struct SchedulerTorrent {
  std::uint64_t token = 0, manualRevision = 0;
  core::TorrentHash hash;
  bool complete = false, forceStarted = false, paused = false;
  scheduler::TorrentState state = scheduler::TorrentState::Stopped;
  // Original category (Uncategorized if absent), or actual manual tags. A
  // filtered schedule cannot use an unavailable membership provider as empty.
  bool membershipsAvailable = true;
  std::vector<std::string> memberships{};
};
struct SchedulerAction {
  std::uint64_t plan = 0, ordinal = 0, token = 0, manualRevision = 0;
  core::TorrentHash hash;
  scheduler::ActionKind kind;
  bool operator==(const SchedulerAction&) const = default;
};
struct SchedulerPlan {
  std::uint64_t id = 0, revision = 0;
  std::optional<int> uploadKiB, downloadKiB;
  bool nonDefaultUpload = false, nonDefaultDownload = false;
  std::vector<SchedulerAction> actions;
};
enum class SchedulerActionResult { Succeeded, FailedBeforeEffect, Indeterminate };

// Worker-confined. Caller authenticates the module artifact BEFORE loading it;
// this API does not treat a raw filesystem path as authenticated native code.
// Borrowed held profile; no new lock, engine, timer, GUI or network owner.
class SchedulerService final {
public:
  static std::expected<std::unique_ptr<SchedulerService>, Error> open(
      const ProfileLease&, plugins::Module&& alreadyAuthenticatedModule, std::stop_token = {});
  ~SchedulerService();
  SchedulerService(const SchedulerService&) = delete;
  SchedulerService& operator=(const SchedulerService&) = delete;
  const SchedulerSnapshot& snapshot() const noexcept;
  std::expected<void, Error> reload(const ProfileLease&, std::stop_token = {});
  // Each method CAS-saves ONE original file; no cross-file atomicity claim.
  // Edit schedules first, then enable settings if a UI changes both.
  std::expected<void, Error> saveSchedules(const ProfileLease&, std::uint64_t revision,
      const SchedulerXmlChanges&, std::stop_token = {});
  std::expected<void, Error> saveSettings(const ProfileLease&, std::uint64_t revision,
      const SchedulerSettingsChanges&, std::stop_token = {});
  std::expected<SchedulerPlan, Error> evaluate(scheduler::LocalMinute,
      std::span<const SchedulerTorrent>, int actualUploadKiB, int actualDownloadKiB);
  // Durable intent BEFORE Host changes original pause_data + actual engine.
  // Host rechecks current token/manualRevision and calls finish only after the
  // real saved+engine outcome. Uncertain effect keeps an interrupted journal.
  std::expected<void, Error> beginAction(const ProfileLease&, const SchedulerAction&,
      const SchedulerTorrent& current, std::stop_token = {});
  std::expected<void, Error> finishAction(const ProfileLease&, const SchedulerAction&, SchedulerActionResult);
  std::expected<void, Error> acknowledgeRates(std::uint64_t plan, bool uploadApplied, bool downloadApplied);
  // Call BEFORE every explicit user action. In-memory authority is revoked even
  // if IO fails; caller still honors the user's command and surfaces the error.
  std::expected<void, Error> revokeForManual(const ProfileLease&, const core::TorrentHash&);
  // After a committed move/relink and actual paused re-admission only: transfer
  // existing acknowledged same-session pause authority to the new admission.
  // False means no authority exists (legacy records never mint authority).
  // Tokens must advance; journal/source CAS or identity mismatches reject. No
  // original ownership bytes are written; all outstanding plans become stale.
  std::expected<bool, Error> rebindOwnedPause(const ProfileLease&, const core::TorrentHash&,
      std::uint64_t oldToken, std::uint64_t newToken, std::uint64_t manualRevision);
private:
  struct Impl;
  explicit SchedulerService(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
struct SchedulerModuleApproval {
  std::string sha256;
  std::uint64_t bytes = 0;
  std::string version;
};
// Approval is trusted compiled/installer metadata, never values read from a
// profile. Authenticates the retained inode before executing native module code.
std::expected<std::unique_ptr<SchedulerService>, Error> loadApprovedSchedulerService(
    const ProfileLease&, const std::filesystem::path&, const SchedulerModuleApproval&, std::stop_token = {});
// Lossless known-field XML edits. Unsupported/ambiguous structure rejects;
// never serialize unknown XML away. No IO or module/engine calls.
std::expected<std::string, Error> patchSchedulerXml(std::string_view original, const SchedulerXmlChanges&);
} // namespace biglybtc::storage

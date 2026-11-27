// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_backup.hpp"
#include "download_archive.hpp"
#include "file_operations.hpp"
#include "linear_to_compact.hpp"
#include "profile_file_snapshot.hpp"
#include "profile_transaction_pending.hpp"
#include "state_directory_listing.hpp"
#include "../platform/utf8_path.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <limits>
#include <map>
#include <type_traits>
#include <utility>
#include <vector>

namespace biglybtc::storage {
namespace {
using namespace detail;
namespace fs = std::filesystem;
constexpr char Configuration[] = "biglybt.config";
constexpr char ConfigurationStage[] = "biglybt.config.saving";
constexpr std::size_t MaximumEntries = 100000;
constexpr unsigned MaximumDepth = 32;
constexpr unsigned MaximumSuffix = 99;

void require(bool valid, ErrorCode code, const char* message) {
  if (!valid) { throw Error{code, message}; }
}

template<class T>
T take(std::expected<T, Error> value) {
  if (!value) { throw std::move(value.error()); }
  if constexpr (!std::is_void_v<T>) { return std::move(*value); }
}

void checkCancellation(std::stop_token stop) {
  require(!stop.stop_requested(), ErrorCode::Cancelled, "Profile backup cancelled");
}

std::optional<unsigned> unsignedValue(std::string_view text) {
  unsigned value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? std::optional{value} : std::nullopt;
}

bool validDate(std::string_view text) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') { return false; }
  const auto year = unsignedValue(text.substr(0, 4));
  const auto month = unsignedValue(text.substr(5, 2));
  const auto day = unsignedValue(text.substr(8, 2));
  using namespace std::chrono;
  return year && *year > 0 && month && day
      && year_month_day{std::chrono::year(static_cast<int>(*year)), std::chrono::month(*month), std::chrono::day(*day)}.ok();
}

bool containsPath(const fs::path& root, const fs::path& path) {
  auto component = path.begin();
  for (const auto& expected : root) {
    if (component == path.end() || !sameFilesystemComponent(expected, *component)) { return false; }
    ++component;
  }
  return true;
}

unsigned nextSuffix(StateHandle parent, std::string_view date, std::stop_token stop) {
  unsigned next = 0;
  for (const auto& leaf : take(listStateDirectoryNames(parent, MaximumEntries, stop))) {
    const auto name = platform::pathToUtf8(leaf);
    if (name == date) { next = std::max(next, 1U); }
    else if (name.size() > date.size() + 1 && name.starts_with(date) && name[date.size()] == '.') {
      if (const auto suffix = unsignedValue(std::string_view(name).substr(date.size() + 1)); suffix && *suffix <= MaximumSuffix) {
        next = std::max(next, *suffix + 1);
      }
    }
  }
  return next;
}

bool skipped(const fs::path& relative, std::string_view name, const StateStamp& stamp, bool includePlugins) {
  if (relative.empty()) {
    if (stamp.directory) {
      constexpr std::array<std::string_view, 5> transient{"cache", "tmp", "logs", "updates", "debug"};
      return std::ranges::find(transient, name) != transient.end() || (name == "plugins" && !includePlugins);
    }
    return name == Configuration || name == ".lock" || name == ".azlock"
        || name == "update.properties" || name.ends_with(".log");
  }
  const auto parent = platform::pathToUtf8(relative.filename());
  return stamp.directory && ((parent == "aznettorbrowser" && name.starts_with("browser_"))
      || (parent == "azwebtorrent" && (name == "data" || name.starts_with("browser_"))));
}

class BackupWriter {
public:
  BackupWriter(StateHandle parent, fs::path name, StateDescriptor output, std::stop_token stop)
      : parent_(parent), name_(std::move(name)), output_(std::move(output)), outputStamp_(take(stateStamp(output_.get()))), stop_(stop) {}

  ~BackupWriter() {
    if (published_) { return; }
    for (auto it = created_.rbegin(); it != created_.rend(); ++it) {
      try {
        const auto& [path, stamp] = *it;
        const auto parent = path.parent_path();
        auto directory = parent.empty() ? duplicateStateDirectory(output_.get()) : openCreatedDirectory(parent);
        if (!directory) { continue; }
        const auto current = take(stateStampAt(directory->get(), path.filename()));
        if (!current || !sameStateIdentity(*current, stamp)) { continue; }
        if (stamp.directory) { take(retireEmptyDirectoryAt(directory->get(), path.filename(), *current)); }
        else { take(retireStateFile(directory->get(), path.filename(), *current)); }
      } catch (...) { }
    }
    output_ = {};
    try { static_cast<void>(retireEmptyDirectoryAt(parent_, name_, outputStamp_)); } catch (...) { }
  }

  ProfileBackup write(StateHandle source, const fs::path& directory, const ProfileFileSnapshot& configuration,
      std::string bytes, bool includePlugins) {
    copyTree(source, output_.get(), {}, includePlugins, 1);
    const auto last = take(captureProfileFile(source, Configuration, compatibleConfigLimits().inputBytes));
    require(configuration.matches(last), ErrorCode::Conflict, "Original profile configuration changed while backing up");
    require(bytes.size() <= std::numeric_limits<std::uint64_t>::max() - bytes_, ErrorCode::Unsupported, "Profile backup size is too large");
    checkCancellation(stop_);
    auto config = take(stageStateFile(output_.get(), ConfigurationStage, bytes, 0600));
    const auto stamp = take(stateStamp(config.get()));
    created_.emplace_back(ConfigurationStage, stamp);
    take(publishStateFile(output_.get(), ConfigurationStage, Configuration, config.get(), false, &stamp));
    published_ = true;
    take(syncStateDirectory(output_.get()));
    take(syncStateDirectory(parent_));
    return {directory / name_, files_ + 1, bytes_ + bytes.size()};
  }

private:
  std::expected<StateDescriptor, Error> openCreatedDirectory(const fs::path& path) {
    auto directory = take(duplicateStateDirectory(output_.get()));
    fs::path traversed;
    for (const auto& part : path) {
      traversed /= part;
      const auto found = directories_.find(traversed);
      if (found == directories_.end()) { return std::unexpected(Error{ErrorCode::Conflict, "Unknown backup directory"}); }
      auto child = take(openDirectoryAt(directory.get(), part));
      if (!sameStateDirectory(take(stateStamp(child.get())), found->second)) {
        return std::unexpected(Error{ErrorCode::Conflict, "Backup directory changed"});
      }
      directory = std::move(child);
    }
    return directory;
  }

  void copyFile(StateHandle source, StateHandle destination, const fs::path& name,
      const fs::path& path, const StateStamp& expected) {
    const auto input = take(openStateFile(source, name));
    require(expected.size >= 0 && sameStateSnapshot(take(stateStamp(input.get())), expected),
        ErrorCode::Conflict, "Saved file changed before backup");
    auto output = take(openStateAppendFile(destination, name, true));
    created_.emplace_back(path, take(stateStamp(output.get())));
    take(protectPrivateStateFile(output.get()));
    std::array<char, 64 * 1024> buffer;
    const auto size = static_cast<std::uint64_t>(expected.size);
    std::uint64_t copied = 0;
    while (true) {
      checkCancellation(stop_);
      const auto received = readStateSequential(input.get(), buffer.data(), buffer.size());
      require(received >= 0, ErrorCode::Io, "Cannot read saved profile file");
      if (received == 0) { break; }
      const auto count = static_cast<std::size_t>(received);
      require(count <= size - copied, ErrorCode::Conflict, "Saved file grew during backup");
      auto block = std::string_view(buffer.data(), count);
      while (!block.empty()) {
        const auto written = appendStateFile(output.get(), block);
        require(written > 0, ErrorCode::Io, "Cannot write profile backup file");
        block.remove_prefix(static_cast<std::size_t>(written));
      }
      copied += count;
    }
    const auto current = take(stateStampAt(source, name));
    require(copied == size && current && sameStateSnapshot(*current, expected)
        && sameStateSnapshot(take(stateStamp(input.get())), expected), ErrorCode::Conflict, "Saved file changed during backup");
    take(setStateMtime(output.get(), expected.modified));
    take(syncStateFile(output.get()));
    require(size <= std::numeric_limits<std::uint64_t>::max() - bytes_, ErrorCode::Unsupported, "Profile backup size is too large");
    bytes_ += size;
    ++files_;
  }

  void copyTree(StateHandle source, StateHandle destination, const fs::path& relative, bool includePlugins, unsigned depth) {
    require(depth <= MaximumDepth, ErrorCode::Unsupported, "Profile backup directory is too deeply nested");
    const auto before = take(stateStamp(source));
    require(!sameStateDirectory(before, outputStamp_), ErrorCode::UnsafePath, "Backup destination is inside the profile");
    for (const auto& name : take(listStateDirectoryNames(source, MaximumEntries, stop_))) {
      checkCancellation(stop_);
      if (name == "." || name == "..") { continue; }
      const auto expected = take(stateStampAt(source, name));
      require(expected.has_value(), ErrorCode::Conflict, "Saved profile directory changed during backup");
      if (skipped(relative, platform::pathToUtf8(name), *expected, includePlugins)) { continue; }
      require(expected->directory || expected->regular, ErrorCode::UnsafePath, "Profile backup requires ordinary files and directories");
      const auto path = relative / name;
      if (expected->directory) {
        const auto from = take(openDirectoryAt(source, name));
        require(sameStateSnapshot(take(stateStamp(from.get())), *expected), ErrorCode::Conflict, "Saved directory changed before backup");
        auto to = take(createDirectoryAt(destination, name));
        const auto stamp = take(stateStamp(to.get()));
        created_.emplace_back(path, stamp);
        directories_.emplace(path, stamp);
        copyTree(from.get(), to.get(), path, includePlugins, depth + 1);
        const auto current = take(stateStampAt(source, name));
        require(current && sameStateSnapshot(*current, *expected), ErrorCode::Conflict, "Saved directory changed during backup");
      } else { copyFile(source, destination, name, path, *expected); }
    }
    require(sameStateSnapshot(take(stateStamp(source)), before), ErrorCode::Conflict, "Saved directory changed during backup");
    take(syncStateDirectory(destination));
  }

  StateHandle parent_;
  fs::path name_;
  StateDescriptor output_;
  StateStamp outputStamp_;
  std::stop_token stop_;
  std::vector<std::pair<fs::path, StateStamp>> created_;
  std::map<fs::path, StateStamp, FilesystemPathLess> directories_;
  std::uint64_t files_ = 0;
  std::uint64_t bytes_ = 0;
  bool published_ = false;
};
} // namespace

std::expected<ProfileBackup, Error> backupSavedProfile(const ProfileLease& lease,
    const fs::path& destination, std::string_view date, std::stop_token stop) try {
  require(validDate(date), ErrorCode::InvalidFormat, "Invalid profile backup date");
  checkCancellation(stop);
  auto access = take(lease.sourceAccess());
  require(!hasPendingArchiveRestore(lease) && !hasPendingFileOperation(lease) && !hasPendingOriginalCompaction(lease)
      && !hasPendingCategoryDeletion(lease) && !hasPendingTorrentIntake(lease), ErrorCode::Conflict,
      "Finish the pending profile recovery before backing up");
  take(requireNoPendingSubscriptionAcquisition(lease));
  const auto source = lease.directoryHandle();
  const auto sourcePath = take(stateDirectoryPath(source));
  require(destination.is_absolute(), ErrorCode::UnsafePath, "Choose an absolute backup destination");
  std::error_code pathError;
  const auto resolved = fs::canonical(destination, pathError);
  require(!pathError, ErrorCode::UnsafePath, "Cannot resolve the chosen backup destination");
  const auto parent = take(openStatePathDirectory(resolved));
  const auto parentPath = take(stateDirectoryPath(parent.get()));
  require(!containsPath(sourcePath, parentPath) && !sameStateDirectory(take(stateStamp(source)), take(stateStamp(parent.get()))),
      ErrorCode::UnsafePath, "Choose a backup destination outside the profile");
  require(!take(stateStampAt(source, ConfigurationStage)), ErrorCode::Conflict, "Original profile configuration save is pending");
  const auto configuration = take(captureProfileFile(source, Configuration, compatibleConfigLimits().inputBytes));
  require(configuration.bytes.has_value(), ErrorCode::Missing, "Original profile configuration is unavailable");
  auto document = core::Document::parse(*configuration.bytes, compatibleConfigLimits());
  require(document && document->root().type == core::Node::Type::Dictionary, ErrorCode::InvalidFormat, "Original profile configuration is unreadable");
  const auto* pluginSetting = document->find("br.restore.doplugins");
  const bool includePlugins = !pluginSetting || pluginSetting->type != core::Node::Type::Integer || pluginSetting->integer != 0;
  const auto sourceName = platform::pathToUtf8(sourcePath);
  require(document->replaceValue("azureus.user.directory", std::to_string(sourceName.size()) + ':' + sourceName).has_value(),
      ErrorCode::InvalidFormat, "Cannot save the original profile directory in the backup");
  for (unsigned suffix = nextSuffix(parent.get(), date, stop); suffix <= MaximumSuffix; ++suffix) {
    checkCancellation(stop);
    const fs::path leaf = suffix ? std::string(date) + '.' + std::to_string(suffix) : std::string(date);
    auto output = createDirectoryAt(parent.get(), leaf);
    if (!output && output.error().code == ErrorCode::Conflict) { continue; }
    BackupWriter writer(parent.get(), leaf, take(std::move(output)), stop);
    return writer.write(source, parentPath, configuration, document->serialize(), includePlugins);
  }
  return std::unexpected(Error{ErrorCode::Conflict, "No date slot is available for the profile backup"});
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Profile backup did not finish"});
}

} // namespace biglybtc::storage

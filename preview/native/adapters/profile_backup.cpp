// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_backup.hpp"
#include "profile_backup_relocation.hpp"
#include "profile_restore_journal.hpp"
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
#include <set>
#include <span>
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

bool absoluteSavedPath(std::string_view value) {
  if (value.empty() || value.contains('\0')) { return false; }
  return value.starts_with('/') || value.starts_with("\\\\")
      || (value.size() >= 3 && value[1] == ':' && (value[2] == '/' || value[2] == '\\'));
}

void requireEmptyProfile(StateHandle target, std::stop_token stop) {
  for (const auto& name : take(listStateDirectoryNames(target, MaximumEntries, stop))) {
    require(name == "." || name == ".." || name == ".azlock", ErrorCode::Conflict,
        "Choose an empty directory for the restored profile");
  }
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

bool skipped(const fs::path& relative, std::string_view name, const StateStamp& stamp,
    bool includePlugins, bool restoring) {
  if (relative.empty()) {
    if (name==RestoreDirectory || name==RestoreRequest || name==RestoreRequestSaving
        || name==RestorePrevious || name==RestorePrior || name==RestorePriorSaving || name==RestoreApplied) { return true; }
    if (stamp.directory) {
      if (restoring) { return false; }
      constexpr std::array<std::string_view, 5> transient{"cache", "tmp", "logs", "updates", "debug"};
      return std::ranges::find(transient, name) != transient.end() || (name == "plugins" && !includePlugins);
    }
    return name == Configuration || name == ".lock" || name == ".azlock"
        || (!restoring && (name == "update.properties" || name.ends_with(".log")));
  }
  const auto parent = platform::pathToUtf8(relative.filename());
  return stamp.directory && ((parent == "aznettorbrowser" && name.starts_with("browser_"))
      || (parent == "azwebtorrent" && (name == "data" || name.starts_with("browser_"))));
}

struct VersionedPluginArchive {
  std::string family;
  std::vector<int> version;
};

std::optional<VersionedPluginArchive> versionedPluginArchive(std::string name) {
  for (char& c : name) {
    if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c + ('a' - 'A')); }
  }
  constexpr std::size_t extensionSize = 4;
  if (!name.ends_with(".jar") && !name.ends_with(".zip")) { return {}; }
  const auto separator = name.rfind('_');
  const auto extension = name.size() - extensionSize;
  if (separator == std::string::npos || separator + 1 >= extension) { return {}; }
  auto text = std::string_view(name).substr(separator + 1, extension - separator - 1);
  std::vector<int> version;
  while (true) {
    const auto dot = text.find('.');
    const auto token = text.substr(0, dot);
    const auto part = unsignedValue(token);
    if (!part || *part > std::numeric_limits<int>::max()) { return {}; }
    version.push_back(static_cast<int>(*part));
    if (dot == std::string_view::npos) { break; }
    text.remove_prefix(dot + 1);
  }
  while (!version.empty() && version.back() == 0) { version.pop_back(); }
  return VersionedPluginArchive{name.substr(0, separator) + name.substr(extension), std::move(version)};
}

using PluginArchiveNames = std::set<fs::path, FilesystemPathLess>;

PluginArchiveNames supersededPluginArchives(StateHandle source, std::span<const fs::path> entries,
    std::stop_token stop) {
  std::map<std::string, std::pair<std::vector<int>, fs::path>> latest;
  PluginArchiveNames superseded;
  for (const auto& entry : entries) {
    checkCancellation(stop);
    auto archive = versionedPluginArchive(platform::pathToUtf8(entry));
    if (!archive) { continue; }
    const auto stamp = take(stateStampAt(source, entry));
    require(stamp.has_value(), ErrorCode::Conflict, "Saved plugin directory changed during backup");
    if (!stamp->regular || !stamp->owned || stamp->links != 1) { continue; }

    auto [kept, inserted] = latest.try_emplace(std::move(archive->family), std::move(archive->version), entry);
    if (inserted) { continue; }
    auto& [version, name] = kept->second;
    if (version < archive->version) {
      superseded.insert(std::move(name));
      version = std::move(archive->version);
      name = entry;
    } else { superseded.insert(entry); }
  }
  return superseded;
}

using BackupSlot = std::pair<std::string, unsigned>;
constexpr std::string_view RetiredBackupPrefix = ".BiglyBTC.retired-backup-";
constexpr char RetiredConfiguration[] = "BiglyBTC.retired-backup.config";

std::optional<BackupSlot> backupSlot(std::string_view name) {
  constexpr std::size_t dateSize = 10;
  if (name.size() < dateSize || !validDate(name.substr(0, dateSize))) { return {}; }
  const auto date = std::string(name.substr(0, dateSize));
  if (name.size() == dateSize) { return BackupSlot{date, 0}; }
  if (name[dateSize] != '.') { return {}; }
  const auto suffix = unsignedValue(name.substr(dateSize + 1));
  if (!suffix || *suffix == 0 || *suffix > MaximumSuffix) { return {}; }
  if (name.substr(dateSize + 1) != std::to_string(*suffix)) { return {}; }
  return BackupSlot{date, *suffix};
}

struct BackupCandidate {
  fs::path name;
  BackupSlot slot;
  StateStamp directory;
  StateStamp configuration;
  bool retired = false;
  bool unpublished = false;
};

std::optional<BackupCandidate> inspectBackup(StateHandle parent, const fs::path& name,
    const fs::path& profile, std::stop_token stop) {
  checkCancellation(stop);
  const auto text = platform::pathToUtf8(name);
  const bool retired = text.starts_with(RetiredBackupPrefix);
  const auto slot = backupSlot(std::string_view(text).substr(retired ? RetiredBackupPrefix.size() : 0));
  if (!slot) { return {}; }

  const auto revision = take(stateStampAt(parent, name));
  if (!revision || !revision->directory || !revision->owned) { return {}; }
  const auto folder = openDirectoryAt(parent, name);
  if (!folder || !sameStateSnapshot(take(stateStamp(folder->get())), *revision)) { return {}; }
  if (take(stateStampAt(folder->get(), ".azlock")) || take(stateStampAt(folder->get(), ConfigurationStage))) {
    return {};
  }
  const auto unpublished = take(stateStampAt(folder->get(), RetiredConfiguration));
  if (unpublished && (!retired || take(stateStampAt(folder->get(), Configuration)))) { return {}; }

  const auto entry = captureProfileFile(folder->get(), unpublished ? RetiredConfiguration : Configuration,
      compatibleConfigLimits().inputBytes);
  if (!entry || !entry->bytes || !entry->stamp) { return {}; }
  const auto document = core::Document::parse(*entry->bytes, compatibleConfigLimits());
  const auto* origin = document ? document->find("azureus.user.directory") : nullptr;
  if (!origin || origin->type != core::Node::Type::Bytes || !absoluteSavedPath(origin->bytes)) { return {}; }
  const auto original = platform::pathFromUtf8(origin->bytes).lexically_normal();
  if (!original.is_absolute() || !sameFilesystemComponent(profile, original)) { return {}; }
  return BackupCandidate{name, *slot, *revision, *entry->stamp, retired, unpublished.has_value()};
}

void visitBackupFiles(StateHandle folder, std::uint64_t device, std::uint64_t mount, const fs::path& preserve,
    std::size_t& visited, unsigned depth, bool remove, std::stop_token stop) {
  require(depth <= MaximumDepth, ErrorCode::Unsupported, "Profile backup tree exceeds the retention depth limit");
  for (const auto& leaf : take(listStateDirectoryNames(folder, MaximumEntries, stop))) {
    checkCancellation(stop);
    if (leaf == "." || leaf == ".." || leaf == preserve) { continue; }
    require(depth != 0 || (leaf != ".azlock" && leaf != ConfigurationStage), ErrorCode::Conflict,
        "The profile backup became active during retirement");
    require(++visited <= MaximumEntries, ErrorCode::Unsupported, "Profile backup contains too many files for retention");
    const auto stamp = take(stateStampAt(folder, leaf));
    require(stamp && stamp->owned && stamp->device == device && (stamp->directory || (stamp->regular && stamp->links == 1)),
        ErrorCode::UnsafePath, "A saved backup contains an unsafe entry");
    if (stamp->directory) {
      {
        const auto child = take(openDirectoryAt(folder, leaf));
        require(sameStateSnapshot(take(stateStamp(child.get())), *stamp),
            ErrorCode::Conflict, "A saved backup folder changed before retirement");
        require(take(stateMountId(child.get())) == mount, ErrorCode::UnsafePath, "A saved backup crosses a mount point");
        visitBackupFiles(child.get(), device, mount, {}, visited, depth + 1, remove, stop);
      }
      if (remove) { take(retireEmptyDirectoryAt(folder, leaf, *stamp)); }
    } else if (remove) { take(retireStateFile(folder, leaf, *stamp)); }
  }
  if (remove) { take(syncStateDirectory(folder)); }
}

void retireSavedBackup(StateHandle parent, BackupCandidate backup, std::stop_token stop) {
  checkCancellation(stop);
  const auto mount = take(stateMountId(parent));
  auto configuration = fs::path(backup.unpublished ? RetiredConfiguration : Configuration);
  const auto validate = [&](StateHandle folder, bool renamed) {
    require(take(stateMountId(folder)) == mount, ErrorCode::UnsafePath, "A saved backup is a mount point");
    const auto stamp = take(stateStamp(folder));
    require(renamed ? sameStateDirectory(stamp, backup.directory) : sameStateSnapshot(stamp, backup.directory),
        ErrorCode::Conflict, "Profile backup directory changed before retirement");
    const auto configured = take(stateStampAt(folder, configuration));
    require(configured && sameStateSnapshot(*configured, backup.configuration)
        && !take(stateStampAt(folder, ".azlock")) && !take(stateStampAt(folder, ConfigurationStage)),
        ErrorCode::Conflict, "The profile backup is in use or has changed");
  };
  {
    const auto folder = take(openDirectoryAt(parent, backup.name));
    validate(folder.get(), backup.retired);
    std::size_t visited = 0;
    visitBackupFiles(folder.get(), backup.directory.device, mount, configuration, visited, 0, false, stop);
  }
  if (!backup.retired) {
    const auto quarantine = fs::path(std::string(RetiredBackupPrefix) + platform::pathToUtf8(backup.name));
    take(moveStateDirectoryNoReplace(parent, backup.name, quarantine, backup.directory));
    take(syncStateDirectory(parent));
    backup.name = quarantine;
  }
  {
    const auto folder = take(openDirectoryAt(parent, backup.name));
    validate(folder.get(), true);
    if (!backup.unpublished) {
      take(moveStateFileNoReplace(folder.get(), configuration, folder.get(), RetiredConfiguration, backup.configuration));
      take(syncStateDirectory(folder.get()));
      configuration = RetiredConfiguration;
    }
    std::size_t retired = 0;
    visitBackupFiles(folder.get(), backup.directory.device, mount, configuration, retired, 0, true, stop);
    const auto marker = take(stateStampAt(folder.get(), configuration));
    require(marker && marker->owned && marker->regular && marker->links == 1
        && sameStateIdentity(*marker, backup.configuration), ErrorCode::Conflict, "Backup retirement marker changed");
    take(retireStateFile(folder.get(), configuration, *marker));
    take(syncStateDirectory(folder.get()));
  }
  take(retireEmptyDirectoryAt(parent, backup.name, backup.directory));
  take(syncStateDirectory(parent));
}

struct DirectoryRelocation { std::string previous, destination; bool pauseDownloads=false; };

struct PreparedProfileRestore {
  ProfileFileSnapshot configuration;
  DirectoryRelocation relocation;
  std::string bytes;
};

PreparedProfileRestore prepareRestore(const ProfileLease& current, StateHandle source, const fs::path& destination) {
  require(!take(stateStampAt(source, ConfigurationStage)), ErrorCode::Conflict, "Backup configuration is still being written");
  auto configuration = take(captureProfileFile(source, Configuration, compatibleConfigLimits().inputBytes));
  require(configuration.bytes.has_value(), ErrorCode::Missing, "Select a completed profile backup containing biglybt.config");
  const auto sourceConfig = core::Document::parse(*configuration.bytes, compatibleConfigLimits());
  const auto* original = sourceConfig ? sourceConfig->find("azureus.user.directory") : nullptr;
  require(original && original->type == core::Node::Type::Bytes && absoluteSavedPath(original->bytes),
      ErrorCode::InvalidFormat, "Backup does not contain the original profile directory");
  const auto currentConfig=ConfigFile::load(current,Configuration,compatibleConfigLimits());
  const auto* pause=currentConfig ? currentConfig->document().find("br.restore.autopause") : nullptr;
  const bool pauseDownloads=pause&&pause->type==core::Node::Type::Integer
      && static_cast<std::uint32_t>(pause->integer)!=0;
  DirectoryRelocation relocation{std::string(original->bytes),platform::pathToUtf8(destination),pauseDownloads};
  auto bytes = take(relocateBackupConfiguration(*configuration.bytes, relocation.previous, relocation.destination));
  auto restoredConfig = core::Document::parse(std::move(bytes), compatibleConfigLimits());
  require(restoredConfig && restoredConfig->replaceValue("azureus.user.directory",
      std::to_string(relocation.destination.size()) + ':' + relocation.destination).has_value(), ErrorCode::InvalidFormat,
      "Cannot save the restored profile directory");
  if (pauseDownloads) {
    require(restoredConfig->replaceValue("br.restore.autopause","i1e").has_value(),
        ErrorCode::InvalidFormat,"Cannot save the restore pause preference");
  }
  return {std::move(configuration),std::move(relocation),restoredConfig->serialize()};
}

class BackupWriter {
public:
  BackupWriter(StateHandle parent, fs::path name, StateDescriptor output, std::stop_token stop,
      std::optional<DirectoryRelocation> relocation = std::nullopt)
      : parent_(parent), name_(std::move(name)), output_(std::move(output)), outputStamp_(take(stateStamp(output_.get()))),
        stop_(stop), relocation_(std::move(relocation)) {}

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
    if (parent_ != InvalidStateHandle) {
      try { static_cast<void>(retireEmptyDirectoryAt(parent_, name_, outputStamp_)); } catch (...) { }
    }
  }

  void discard() noexcept { published_=false; }

  ProfileBackup write(StateHandle source, fs::path directory, const ProfileFileSnapshot& configuration,
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
    created_.back().first=Configuration;
    published_ = true;
    take(syncStateDirectory(output_.get()));
    if (parent_ != InvalidStateHandle) { take(syncStateDirectory(parent_)); }
    return {std::move(directory), files_ + 1, bytes_ + bytes.size()};
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

  void recordCopy(std::uint64_t size) {
    require(size <= std::numeric_limits<std::uint64_t>::max() - bytes_, ErrorCode::Unsupported, "Profile backup size is too large");
    bytes_ += size;
    ++files_;
  }

  bool rewriteConfiguration(StateHandle source, StateHandle destination, const fs::path& name,
      const fs::path& path, const StateStamp& expected) {
    if (!relocation_ || path.has_parent_path()) { return false; }
    const auto leaf = platform::pathToUtf8(name);
    if (!leaf.contains(".config")) { return false; }
    const auto original = take(captureProfileFile(source, leaf, compatibleConfigLimits().inputBytes));
    require(original.bytes && original.stamp && sameStateSnapshot(*original.stamp, expected) && original.stillHeld(),
        ErrorCode::Conflict, "Backup configuration changed while restoring");
    auto relocated = relocateBackupConfiguration(*original.bytes, relocation_->previous, relocation_->destination);
    if (!relocated && relocated.error().code == ErrorCode::InvalidFormat && (leaf.contains(".bad") || leaf.contains(".bak"))) {
      return false;
    }
    auto output = take(std::move(relocated));
    if (relocation_->pauseDownloads && (leaf=="downloads.config" || leaf=="downloads.config.bak" || leaf=="downloads.config.saving")) {
      output=take(pauseRestoredProfileDownloads(std::move(output),stop_));
    }
    if (output == *original.bytes) { return false; }
    auto snapshot = take(stageStateFile(destination, name, output, 0600));
    created_.emplace_back(path, take(stateStamp(snapshot.get())));
    take(setStateMtime(snapshot.get(), expected.modified));
    take(syncStateFile(snapshot.get()));
    const auto current = take(captureProfileFile(source, leaf, compatibleConfigLimits().inputBytes));
    require(original.matches(current), ErrorCode::Conflict, "Backup configuration changed while restoring");
    recordCopy(output.size());
    return true;
  }

  void copyFile(StateHandle source, StateHandle destination, const fs::path& name,
      const fs::path& path, const StateStamp& expected) {
    if (rewriteConfiguration(source, destination, name, path, expected)) { return; }
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
    recordCopy(size);
  }

  void copyTree(StateHandle source, StateHandle destination, const fs::path& relative, bool includePlugins, unsigned depth) {
    require(depth <= MaximumDepth, ErrorCode::Unsupported, "Profile backup directory is too deeply nested");
    const auto before = take(stateStamp(source));
    require(!sameStateDirectory(before, outputStamp_), ErrorCode::UnsafePath, "Backup destination is inside the profile");
    const auto entries = take(listStateDirectoryNames(source, MaximumEntries, stop_));
    const auto superseded = !relocation_ && relative.parent_path() == "plugins"
        ? supersededPluginArchives(source, entries, stop_) : PluginArchiveNames{};
    for (const auto& name : entries) {
      checkCancellation(stop_);
      if (name == "." || name == "..") { continue; }
      const auto expected = take(stateStampAt(source, name));
      require(expected.has_value(), ErrorCode::Conflict, "Saved profile directory changed during backup");
      if (skipped(relative, platform::pathToUtf8(name), *expected, includePlugins, relocation_.has_value())) { continue; }
      if (expected->regular && expected->owned && expected->links == 1 && superseded.contains(name)) { continue; }
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
  std::optional<DirectoryRelocation> relocation_;
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
    return writer.write(source, parentPath / leaf, configuration, document->serialize(), includePlugins);
  }
  return std::unexpected(Error{ErrorCode::Conflict, "No date slot is available for the profile backup"});
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Profile backup did not finish"});
}

std::expected<fs::path, Error> restoreProfileBackup(const ProfileLease& current,
    const fs::path& backup, const fs::path& destination, std::stop_token stop) try {
  checkCancellation(stop);
  require(backup.is_absolute() && destination.is_absolute(), ErrorCode::UnsafePath,
      "Choose an absolute backup path and an absolute restore destination");
  auto currentAccess = take(current.sourceAccess());
  const auto currentPath = take(stateDirectoryPath(current.directoryHandle()));
  const auto source = take(openStatePathDirectory(fs::canonical(backup)));
  const auto sourcePath = take(stateDirectoryPath(source.get()));
  const auto output = take(openStatePathDirectory(fs::canonical(destination)));
  const auto targetPath = take(stateDirectoryPath(output.get()));
  require(!containsPath(sourcePath, targetPath) && !containsPath(targetPath, sourcePath)
      && !containsPath(currentPath, targetPath) && !containsPath(targetPath, currentPath)
      && !sameStateDirectory(take(stateStamp(source.get())), take(stateStamp(output.get())))
      && !sameStateDirectory(take(stateStamp(current.directoryHandle())), take(stateStamp(output.get()))),
      ErrorCode::UnsafePath, "Keep the restored profile separate from the backup and the current profile");
  requireEmptyProfile(output.get(), stop);
  auto prepared=prepareRestore(current,source.get(),targetPath);
  checkCancellation(stop);
  const auto target = take(ProfileLease::acquire(targetPath));
  require(sameStateDirectory(take(stateStamp(output.get())), take(stateStamp(target.directoryHandle()))),
      ErrorCode::Conflict, "Restore destination changed while opening it");
  requireEmptyProfile(target.directoryHandle(), stop);
  BackupWriter writer(InvalidStateHandle, {}, take(duplicateStateDirectory(target.directoryHandle())),
      stop, std::move(prepared.relocation));
  return writer.write(source.get(), targetPath, prepared.configuration, std::move(prepared.bytes), true).directory;
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::filesystem::filesystem_error&) {
  return std::unexpected(Error{ErrorCode::UnsafePath, "Cannot resolve the backup or restore destination"});
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Profile restoration did not finish"});
}

std::expected<void, Error> stageProfileRestore(const ProfileLease& current,
    const fs::path& backup, std::stop_token stop) try {
  checkCancellation(stop);
  require(backup.is_absolute(),ErrorCode::UnsafePath,"Choose an absolute path to the backup");
  auto access=take(current.sourceAccess(true));
  const auto profile=current.directoryHandle();
  const auto path=take(stateDirectoryPath(profile));
  const auto source=take(openStatePathDirectory(fs::canonical(backup)));
  const auto backupPath=take(stateDirectoryPath(source.get()));
  require(!containsPath(path,backupPath)&&!containsPath(backupPath,path)
      &&!sameStateDirectory(take(stateStamp(profile)),take(stateStamp(source.get()))),
      ErrorCode::UnsafePath,"Choose a backup separate from the current profile");
  require(!take(stateStampAt(profile,RestoreDirectory))&&!take(stateStampAt(profile,RestoreRequest))
      &&!take(stateStampAt(profile,RestoreRequestSaving))&&!take(stateStampAt(profile,RestoreApplied)),ErrorCode::Busy,"A saved profile restore is already pending");
  require(!hasPendingArchiveRestore(current)&&!hasPendingFileOperation(current)&&!hasPendingOriginalCompaction(current)
      &&!hasPendingCategoryDeletion(current)&&!hasPendingTorrentIntake(current),
      ErrorCode::Conflict,"Finish the pending profile recovery before staging a restore");
  take(requireNoPendingSubscriptionAcquisition(current));
  auto prepared=prepareRestore(current,source.get(),path);
  checkCancellation(stop);
  auto directory=take(createDirectoryAt(profile,RestoreDirectory));
  const auto identity=take(stateStamp(directory.get()));
  BackupWriter writer(profile,RestoreDirectory,std::move(directory),stop,std::move(prepared.relocation));
  static_cast<void>(writer.write(source.get(),path/RestoreDirectory,prepared.configuration,std::move(prepared.bytes),true));
  const auto published=publishProfileRestoreRequest(profile,identity,stop);
  if (!published) {
    const auto ready=stateStampAt(profile,RestoreRequest);
    const auto saving=stateStampAt(profile,RestoreRequestSaving);
    if (ready&&!*ready&&saving&&!*saving) { writer.discard(); }
  }
  return published;
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::filesystem::filesystem_error&) {
  return std::unexpected(Error{ErrorCode::UnsafePath,"Cannot resolve the backup directory"});
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io,"Cannot stage the restore for the next profile startup"});
}

std::expected<std::size_t, Error> rotateSavedProfileBackups(const ProfileLease& lease,
    const ProfileBackup& completed, std::uint32_t retain, std::stop_token stop) try {
  checkCancellation(stop);
  auto access = take(lease.sourceAccess());
  const auto profile = take(stateDirectoryPath(lease.directoryHandle()));
  const auto parent = take(openStatePathDirectory(completed.directory.parent_path()));
  const auto parentPath = take(stateDirectoryPath(parent.get()));
  require(!containsPath(profile, parentPath), ErrorCode::UnsafePath, "Backup retention requires a folder outside the profile");
  const auto completedName = completed.directory.filename();
  const auto justWritten = inspectBackup(parent.get(), completedName, profile, stop);
  require(justWritten && !justWritten->retired, ErrorCode::Conflict, "The new profile backup is unavailable for retention");
  const auto parentStamp = take(stateStamp(parent.get()));
  require(parentStamp.device == justWritten->directory.device, ErrorCode::UnsafePath, "The profile backup is on a separate volume");

  std::vector<BackupCandidate> backups;
  std::vector<BackupCandidate> interrupted;
  for (const auto& name : take(listStateDirectoryNames(parent.get(), MaximumEntries, stop))) {
    if (sameFilesystemComponent(name, completedName)) { continue; }
    auto backup = inspectBackup(parent.get(), name, profile, stop);
    if (!backup || backup->directory.device != parentStamp.device) { continue; }
    auto& candidates = backup->retired ? interrupted : backups;
    candidates.push_back(std::move(*backup));
  }
  std::ranges::sort(backups, std::greater<>{}, &BackupCandidate::slot);
  auto othersToKeep = std::max(std::uint32_t{1}, retain) - 1;
  std::size_t removed = 0;
  for (auto& backup : interrupted) {
    retireSavedBackup(parent.get(), std::move(backup), stop);
    ++removed;
  }
  for (auto& backup : backups) {
    if (othersToKeep) { --othersToKeep; continue; }
    retireSavedBackup(parent.get(), std::move(backup), stop);
    ++removed;
  }
  return removed;
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Profile backup retention did not finish"});
}

} // namespace biglybtc::storage

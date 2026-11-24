// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_plugin_jar.hpp"

#include "plugin_release_disk.hpp"

#include <QString>
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <cerrno>
#include <charconv>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace biglybtc::update::detail {
namespace {
using storage::Error;
using storage::ErrorCode;
constexpr std::size_t maximumEntries = 10000;
constexpr std::int64_t maximumJar = 128 * 1024 * 1024;
constexpr std::int64_t maximumProperties = 64 * 1024;
constexpr std::string_view digits = "0123456789";

std::unexpected<Error> fail(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}

std::optional<std::vector<int>> versionNumbers(std::string_view version) {
  std::vector<int> result;
  while (!version.empty()) {
    const auto begin = version.find_first_of(digits);
    if (begin == std::string_view::npos) { break; }
    version.remove_prefix(begin);
    const auto end = version.find_first_not_of(digits);
    const auto next = version.substr(0, end);
    int part = 0;
    if (std::from_chars(next.data(), next.data() + next.size(), part).ec != std::errc{}) { return std::nullopt; }
    result.push_back(part);
    version.remove_prefix(next.size());
  }
  while (!result.empty() && result.back() == 0) { result.pop_back(); }
  return result;
}

struct JarSelection {
  std::vector<std::string> names;
  std::optional<std::string> fallbackId;
};

std::expected<JarSelection, Error> originalJars(int directory, std::string_view directoryName) {
  const auto fd = ::openat(directory, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) { return fail(ErrorCode::Io, "Cannot list the installed Java plugin"); }
  const auto close = [](DIR* stream) { ::closedir(stream); };
  const std::unique_ptr<DIR, decltype(close)> stream(::fdopendir(fd), close);
  if (!stream) {
    ::close(fd);
    return fail(ErrorCode::Io, "Cannot read the installed Java plugin directory");
  }
  struct Candidate { std::string name; std::vector<int> version; };
  std::map<std::string, Candidate, std::less<>> versioned;
  std::vector<std::string> unversioned;
  std::size_t count = 0;
  for (;;) {
    errno = 0;
    const auto* entry = ::readdir(stream.get());
    if (!entry) {
      if (errno) { return fail(ErrorCode::Io, "Cannot list the installed Java plugin"); }
      break;
    }
    if (++count > maximumEntries) { return fail(ErrorCode::InvalidFormat, "The Java plugin directory is too large"); }
    const auto name = QString::fromUtf8(entry->d_name).toLower().toStdString();
    if (!name.ends_with(".jar")) { continue; }
    std::string_view base(name.data(), name.size() - 4);
    if (base.ends_with("_cvs")) { base.remove_suffix(4); }
    const auto sep = base.find_last_of('_');
    if (sep == std::string_view::npos || sep + 1 == base.size() || digits.find(base[sep + 1]) == digits.npos) {
      unversioned.emplace_back(entry->d_name);
      continue;
    }
    auto version = versionNumbers(base.substr(sep + 1));
    if (!version) { return fail(ErrorCode::InvalidFormat, "An installed Java JAR version is invalid"); }
    auto prefix = std::string(base.substr(0, sep));
    const auto current = versioned.find(prefix);
    if (current == versioned.end() || current->second.version < *version) {
      versioned.insert_or_assign(std::move(prefix), Candidate{entry->d_name, std::move(*version)});
    }
  }
  if (versioned.empty()) { return JarSelection{std::move(unversioned), std::string(directoryName)}; }
  if (versioned.contains("azrating")) { versioned.erase("rating"); }
  JarSelection result;
  if (versioned.size() == 1) { result.fallbackId = versioned.begin()->first; }
  for (auto& candidate : versioned) { result.names.push_back(std::move(candidate.second.name)); }
  return result;
}

std::expected<std::optional<OriginalJarDescriptor>, Error> descriptorInJar(int directory, const std::string& name) {
  Descriptor file(::openat(directory, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  struct stat before{}, after{};
  if (file.get() < 0 || ::fstat(file.get(), &before) != 0 || !S_ISREG(before.st_mode) ||
      before.st_size <= 0 || before.st_size > maximumJar || !heldAt(directory, name, file.get())) {
    return fail(ErrorCode::UnsafePath, "The installed Java JAR is unavailable or unsafe");
  }
  const std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
  if (!reader || archive_read_support_format_zip_seekable(reader.get()) != ARCHIVE_OK ||
      archive_read_open_fd(reader.get(), file.get(), 16 * 1024) != ARCHIVE_OK) {
    return fail(ErrorCode::InvalidFormat, "Cannot read the installed Java JAR");
  }
  std::optional<std::string> properties;
  std::size_t count = 0;
  archive_entry* entry = nullptr;
  int state = ARCHIVE_OK;
  while ((state = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
    if (++count > maximumEntries) { return fail(ErrorCode::InvalidFormat, "The installed Java JAR has too many members"); }
    const auto* path = archive_entry_pathname(entry);
    if (path && std::string_view(path) == "plugin.properties") {
      const auto length = archive_entry_size(entry);
      if (properties || archive_entry_filetype(entry) != AE_IFREG || archive_entry_symlink(entry) ||
          archive_entry_hardlink(entry) || archive_entry_is_encrypted(entry) != 0 ||
          length <= 0 || length > maximumProperties) {
        return fail(ErrorCode::InvalidFormat, "The Java JAR plugin descriptor is invalid or ambiguous");
      }
      properties.emplace(static_cast<std::size_t>(length), '\0');
      std::size_t offset = 0;
      while (offset < properties->size()) {
        const auto bytes = archive_read_data(reader.get(), properties->data() + offset, properties->size() - offset);
        if (bytes <= 0) { return fail(ErrorCode::InvalidFormat, "The Java JAR plugin descriptor is incomplete"); }
        offset += static_cast<std::size_t>(bytes);
      }
      char tail;
      if (archive_read_data(reader.get(), &tail, 1) != 0) {
        return fail(ErrorCode::InvalidFormat, "The Java JAR plugin descriptor failed its size or archive checks");
      }
    }
    if (archive_read_data_skip(reader.get()) != ARCHIVE_OK) {
      return fail(ErrorCode::InvalidFormat, "Cannot finish reading the installed Java JAR");
    }
  }
  if (state != ARCHIVE_EOF || ::fstat(file.get(), &after) != 0 || !heldAt(directory, name, file.get()) ||
      before.st_size != after.st_size || before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
      before.st_ctim.tv_nsec != after.st_ctim.tv_nsec || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
      before.st_mtim.tv_nsec != after.st_mtim.tv_nsec) {
    return fail(ErrorCode::Conflict, "The installed Java JAR changed or its directory is invalid");
  }
  if (!properties) { return std::nullopt; }
  return OriginalJarDescriptor{std::move(*properties), name + ':' + std::to_string(before.st_dev) + ':' +
      std::to_string(before.st_ino) + ':' + std::to_string(before.st_ctim.tv_sec) + ':' +
      std::to_string(before.st_ctim.tv_nsec), {}};
}
} // namespace

std::expected<std::optional<std::string>, Error> originalPluginIdFallback(
    int directory, std::string_view directoryName) {
  auto jars = originalJars(directory, directoryName);
  if (!jars) { return std::unexpected(jars.error()); }
  return std::move(jars->fallbackId);
}

std::expected<std::optional<OriginalJarDescriptor>, Error> readOriginalJarDescriptor(
    int directory, std::string_view directoryName) {
  auto jars = originalJars(directory, directoryName);
  if (!jars) { return std::unexpected(jars.error()); }
  std::optional<OriginalJarDescriptor> descriptor;
  for (const auto& name : jars->names) {
    if (!name.ends_with(".jar") || (jars->names.size() > 1 && name.starts_with("i18nPlugin_"))) { continue; }
    auto candidate = descriptorInJar(directory, name);
    if (!candidate) { return std::unexpected(candidate.error()); }
    if (!*candidate) { continue; }
    if (descriptor) { return fail(ErrorCode::Conflict, "More than one selected Java JAR supplies a plugin descriptor"); }
    descriptor = std::move(*candidate);
  }
  if (descriptor) { descriptor->fallbackId = std::move(jars->fallbackId); }
  return descriptor;
}

} // namespace biglybtc::update::detail

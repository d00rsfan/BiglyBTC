// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_plugin_jar.hpp"

#include "../../platform/utf8_path.hpp"
#include "state_directory_listing.hpp"

#include <QString>
#include <algorithm>
#include <array>
#include <cstdio>
#include <archive.h>
#include <archive_entry.h>
#include <charconv>
#include <map>
#include <memory>
#include <vector>

namespace biglybtc::update::detail {
namespace {
using storage::Error;
using storage::ErrorCode;
namespace files = storage::detail;
using files::StateHandle;
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

std::expected<JarSelection, Error> originalJars(StateHandle directory, std::string_view directoryName) {
  const auto entries = files::listStateDirectoryNames(directory, maximumEntries);
  if (!entries) {
    if (entries.error().code == ErrorCode::Unsupported) {
      return fail(ErrorCode::InvalidFormat, "The Java plugin directory is too large");
    }
    return std::unexpected(entries.error());
  }
  struct Candidate { std::string name; std::vector<int> version; };
  std::map<std::string, Candidate, std::less<>> versioned;
  std::vector<std::string> unversioned;
  for (const auto& entry : *entries) {
    const auto filename = platform::pathToUtf8(entry);
    const auto name = QString::fromUtf8(filename).toLower().toStdString();
    if (!name.ends_with(".jar")) { continue; }
    std::string_view base(name.data(), name.size() - 4);
    if (base.ends_with("_cvs")) { base.remove_suffix(4); }
    const auto sep = base.find_last_of('_');
    if (sep == std::string_view::npos || sep + 1 == base.size() || digits.find(base[sep + 1]) == digits.npos) {
      unversioned.push_back(filename);
      continue;
    }
    auto version = versionNumbers(base.substr(sep + 1));
    if (!version) { return fail(ErrorCode::InvalidFormat, "An installed Java JAR version is invalid"); }
    auto prefix = std::string(base.substr(0, sep));
    const auto current = versioned.find(prefix);
    if (current == versioned.end() || current->second.version < *version) {
      versioned.insert_or_assign(std::move(prefix), Candidate{filename, std::move(*version)});
    }
  }
  if (versioned.empty()) { return JarSelection{std::move(unversioned), std::string(directoryName)}; }
  if (versioned.contains("azrating")) { versioned.erase("rating"); }
  JarSelection result;
  if (versioned.size() == 1) { result.fallbackId = versioned.begin()->first; }
  for (auto& candidate : versioned) { result.names.push_back(std::move(candidate.second.name)); }
  return result;
}

struct JarInput {
  StateHandle file;
  la_int64_t size;
  la_int64_t offset = 0;
  std::array<char, 16 * 1024> bytes;

  static la_ssize_t read(archive*, void* data, const void** output) {
    auto& input = *static_cast<JarInput*>(data);
    *output = input.bytes.data();
    if (input.offset == input.size) { return 0; }
    const auto count = files::readStateAt(input.file, input.bytes.data(),
        static_cast<std::size_t>(std::min<la_int64_t>(input.bytes.size(), input.size - input.offset)), input.offset);
    if (count <= 0) { return -1; }
    input.offset += count;
    return count;
  }

  static la_int64_t seek(archive*, void* data, la_int64_t position, int origin) {
    auto& input = *static_cast<JarInput*>(data);
    la_int64_t base = 0;
    switch (origin) {
      case SEEK_SET: break;
      case SEEK_CUR: base = input.offset; break;
      case SEEK_END: base = input.size; break;
      default: return ARCHIVE_FATAL;
    }
    if (position < -base || position > input.size - base) { return ARCHIVE_FATAL; }
    input.offset = base + position;
    return input.offset;
  }
};

std::expected<std::optional<OriginalJarDescriptor>, Error> descriptorInJar(StateHandle directory, const std::string& name) {
  const auto leaf = platform::pathFromUtf8(name);
  const auto file = files::openStateFile(directory, leaf);
  if (!file) { return fail(ErrorCode::UnsafePath, "The installed Java JAR is unavailable or unsafe"); }
  const auto before = files::stateStamp(file->get());
  const auto initialName = files::stateStampAt(directory, leaf);
  if (!before || !before->regular || before->size <= 0 || before->size > maximumJar
      || !initialName || !*initialName || !files::sameStateSnapshot(*before, **initialName)) {
    return fail(ErrorCode::UnsafePath, "The installed Java JAR is unavailable or unsafe");
  }
  JarInput input{file->get(), before->size, 0, {}};
  const std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
  if (!reader || archive_read_support_format_zip_seekable(reader.get()) != ARCHIVE_OK
      || archive_read_set_seek_callback(reader.get(), JarInput::seek) != ARCHIVE_OK
      || archive_read_open(reader.get(), &input, nullptr, JarInput::read, nullptr) != ARCHIVE_OK) {
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
  const auto after = files::stateStamp(file->get());
  const auto currentName = files::stateStampAt(directory, leaf);
  if (state != ARCHIVE_EOF || !after || !currentName || !*currentName
      || !files::sameStateSnapshot(*before, *after) || !files::sameStateSnapshot(*before, **currentName)) {
    return fail(ErrorCode::Conflict, "The installed Java JAR changed or its directory is invalid");
  }
  if (!properties) { return std::nullopt; }
  return OriginalJarDescriptor{std::move(*properties), name + ':' + std::to_string(before->device) + ':' +
      std::to_string(before->inode) + ':' + std::to_string(before->changed.tv_sec) + ':' +
      std::to_string(before->changed.tv_nsec), {}};
}
} // namespace

std::expected<std::optional<std::string>, Error> originalPluginIdFallback(
    StateHandle directory, std::string_view directoryName) {
  auto jars = originalJars(directory, directoryName);
  if (!jars) { return std::unexpected(jars.error()); }
  return std::move(jars->fallbackId);
}

std::expected<std::optional<OriginalJarDescriptor>, Error> readOriginalJarDescriptor(
    StateHandle directory, std::string_view directoryName) {
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

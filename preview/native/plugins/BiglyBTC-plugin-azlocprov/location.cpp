// SPDX-License-Identifier: GPL-2.0-or-later
// Native port of LocationProviderImpl, Copyright 2013 Azureus Software, Inc.
// Source: 8e8224030ac772fcc8fcb22e80954342d6a66be1. No GeoIP data/assets bundled.
#include "biglybtc/plugin.h"
#include <maxminddb.h>
#include <archive.h>
#include <archive_entry.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::size_t MaximumFlag = 1024 * 1024;
struct Fd {
  int value = -1;
  explicit Fd(int fd = -1) : value(fd) {}
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  ~Fd() { if (value >= 0) { ::close(value); } }
};
int openFile(int directory, const std::string& name) {
  const int result = ::openat(directory, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  struct stat info{};
  if (result >= 0 && (::fstat(result, &info) != 0 || !S_ISREG(info.st_mode)
      || info.st_size < 0 || info.st_size > 256 * 1024 * 1024)) {
    ::close(result); return -1;
  }
  return result;
}
// Same numeric component ordering as Constants.compareVersions for recognized
// descriptor versions. The host rejects non-version characters before create.
bool versionedDatabase(std::string_view version) {
  std::vector<int> parts;
  while (!version.empty()) {
    const auto end = version.find('.');
    const auto token = version.substr(0, end);
    if (!token.empty()) {
      int number = 0;
      const auto result = std::from_chars(token.data(), token.data() + token.size(), number);
      if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) { return false; }
      parts.push_back(number);
    }
    if (end == std::string_view::npos) { break; }
    version.remove_prefix(end + 1);
  }
  const std::array<int, 3> minimum{0, 1, 1};
  for (std::size_t i = 0; i < std::max(parts.size(), minimum.size()); ++i) {
    const auto left = i < parts.size() ? parts[i] : 0;
    const auto right = i < minimum.size() ? minimum[i] : 0;
    if (left != right) { return left > right; }
  }
  return false;
}
struct Database {
  Fd fd;
  MMDB_s data{};
  struct stat identity{};
  bool attempted = false;

  ~Database() {
    if (fd.value >= 0) { MMDB_close(&data); }
  }

  bool open(int directory, const std::string& name) {
    Fd candidate(openFile(directory, name));
    if (candidate.value < 0 || ::fstat(candidate.value, &identity) != 0) { return false; }
    const auto path = "/proc/self/fd/" + std::to_string(candidate.value);
    if (MMDB_open(path.c_str(), MMDB_MODE_MMAP, &data) != MMDB_SUCCESS) { return false; }
    fd.value = candidate.value;
    candidate.value = -1;
    return true;
  }

  bool ready() const {
    struct stat current{};
    if (fd.value < 0 || ::fstat(fd.value, &current) != 0 || !S_ISREG(current.st_mode)
        || current.st_uid != ::geteuid() || current.st_nlink != 1 || (current.st_mode & 0022) != 0
        || current.st_size <= 0 || current.st_size > 256 * 1024 * 1024) { return false; }
    return current.st_dev == identity.st_dev && current.st_ino == identity.st_ino
        && current.st_uid == identity.st_uid && current.st_mode == identity.st_mode
        && current.st_nlink == identity.st_nlink && current.st_size == identity.st_size
        && current.st_mtim.tv_sec == identity.st_mtim.tv_sec && current.st_mtim.tv_nsec == identity.st_mtim.tv_nsec
        && current.st_ctim.tv_sec == identity.st_ctim.tv_sec && current.st_ctim.tv_nsec == identity.st_ctim.tv_nsec;
  }

  MMDB_lookup_result_s lookup(const bt_location_request_v1& request, int& error) const {
    if (request.family == 4) {
      sockaddr_in address{};
      address.sin_family = AF_INET;
      std::memcpy(&address.sin_addr, request.address, 4);
      return MMDB_lookup_sockaddr(&data, reinterpret_cast<const sockaddr*>(&address), &error);
    }
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    std::memcpy(&address.sin6_addr, request.address, 16);
    return MMDB_lookup_sockaddr(&data, reinterpret_cast<const sockaddr*>(&address), &error);
  }
};

struct Provider {
  Fd directory;
  std::string javaVersion;
  bool started = false;
  Database country, asn;
  void* hostContext = nullptr;
  bt_country_name_fn countryName = nullptr;
  explicit Provider(int fd, std::string version) : directory(fd), javaVersion(std::move(version)) {}

  bool ensureCountry() {
    if (!country.attempted) {
      country.attempted = true;
      if (versionedDatabase(javaVersion)) { country.open(directory.value, "GeoLite2-Country_" + javaVersion + ".mmdb"); }
      if (country.fd.value < 0 && !country.open(directory.value, "GeoLite2-Country.mmdb")) {
        country.open(directory.value, "DBIP-Country.mmdb");
      }
    }
    return country.fd.value >= 0;
  }

  bool directoryReady() const {
    struct stat info{};
    return ::fstat(directory.value, &info) == 0 && S_ISDIR(info.st_mode)
        && info.st_uid == ::geteuid() && (info.st_mode & 0077) == 0 && info.st_nlink != 0;
  }

  // Strict availability is opt-in. Legacy lookup keeps its original rules.
  bool databaseReady() {
    return directoryReady() && ensureCountry() && country.ready();
  }

  bt_status lookupAsn(const bt_location_request_v1& request, bt_location_asn_result_v1& result) {
    result = {};
    if (request.family != 4 && request.family != 6) { return BT_INVALID; }
    if (!directoryReady()) { return BT_IO; }
    if (!asn.attempted) {
      asn.attempted = true;
      if (!asn.open(directory.value, "GeoLite2-ASN.mmdb")) {
        asn.open(directory.value, "DBIP-ASN.mmdb");
      }
    }
    if (asn.fd.value < 0) { return BT_NOT_FOUND; }
    if (!asn.ready()) { return BT_IO; }
    int error = MMDB_SUCCESS;
    auto found = asn.lookup(request, error);
    if (error != MMDB_SUCCESS) { return BT_IO; }
    if (!found.found_entry) { return BT_NOT_FOUND; }
    MMDB_entry_data_s value{};
    const auto status = MMDB_get_value(&found.entry, &value, "autonomous_system_number", nullptr);
    if (status != MMDB_SUCCESS || !value.has_data || value.type != MMDB_DATA_TYPE_UINT32
        || value.uint32 == 0) { return BT_NOT_FOUND; }
    result.number = value.uint32;
    if (MMDB_get_value(&found.entry, &value, "autonomous_system_organization", nullptr) == MMDB_SUCCESS
        && value.has_data && value.type == MMDB_DATA_TYPE_UTF8_STRING
        && value.data_size < sizeof(result.organization)) {
      std::memcpy(result.organization, value.utf8_string, value.data_size);
    }
    return asn.ready() ? BT_OK : BT_IO;
  }

  bt_status lookup(const bt_location_request_v1& request, bt_location_result_v1& result, bool checked = false) {
    result = {};
    if ((request.family != 4 && request.family != 6)
        || !std::memchr(request.locale, 0, sizeof(request.locale))) { return BT_INVALID; }
    if (!ensureCountry()) { return checked ? BT_IO : BT_NOT_FOUND; }
    int error = MMDB_SUCCESS;
    auto found = country.lookup(request, error);
    if (error != MMDB_SUCCESS) { return checked ? BT_IO : BT_NOT_FOUND; }
    if (!found.found_entry) { return BT_NOT_FOUND; }
    MMDB_entry_data_s value{};
    const auto isoStatus = MMDB_get_value(&found.entry, &value, "country", "iso_code", nullptr);
    if (isoStatus != MMDB_SUCCESS) {
      return checked && isoStatus != MMDB_LOOKUP_PATH_DOES_NOT_MATCH_DATA_ERROR ? BT_IO : BT_NOT_FOUND;
    }
    if (!value.has_data) { return BT_NOT_FOUND; }
    if (value.type != MMDB_DATA_TYPE_UTF8_STRING || value.data_size != 2) { return checked ? BT_IO : BT_NOT_FOUND; }
    for (int i = 0; i < 2; ++i) {
      const auto c = value.utf8_string[i];
      if (c < 'A' || c > 'Z') { return checked ? BT_IO : BT_NOT_FOUND; }
      result.iso3166[i] = c;
    }
    if (countryName) {
      bt_buffer_v1 output{reinterpret_cast<std::uint8_t*>(result.country), sizeof(result.country), 0};
      if (countryName(hostContext, result.iso3166, request.locale[0] ? request.locale : "en", &output) == BT_OK
          && output.size > 0 && output.size <= sizeof(result.country) && result.country[output.size - 1] == 0) {
        return BT_OK;
      }
      result.country[0] = 0;
    }
    if (MMDB_get_value(&found.entry, &value, "country", "names", "en", nullptr) == MMDB_SUCCESS
        && value.has_data && value.type == MMDB_DATA_TYPE_UTF8_STRING
        && value.data_size < sizeof(result.country)) {
      std::memcpy(result.country, value.utf8_string, value.data_size);
      result.country[value.data_size] = 0;
    }
    return BT_OK;
  }
  std::vector<std::uint8_t> flagFromJar(const std::string& name, const std::string& entryName) {
    Fd file(openFile(directory.value, name));
    if (file.value < 0) { return {}; }
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    if (!reader) { return {}; }
    archive_read_support_format_zip(reader.get());
    if (archive_read_open_fd(reader.get(), file.value, 16384) != ARCHIVE_OK) { return {}; }
    archive_entry* entry = nullptr;
    unsigned count = 0;
    while (archive_read_next_header(reader.get(), &entry) == ARCHIVE_OK && ++count <= 10000) {
      const auto* path = archive_entry_pathname(entry);
      if (path && path == entryName && archive_entry_filetype(entry) == AE_IFREG
          && !archive_entry_is_encrypted(entry) && archive_entry_size(entry) > 0
          && archive_entry_size(entry) <= static_cast<la_int64_t>(MaximumFlag)) {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(archive_entry_size(entry)));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
          const auto size = archive_read_data(reader.get(), bytes.data() + offset, bytes.size() - offset);
          if (size <= 0) { return {}; }
          offset += static_cast<std::size_t>(size);
        }
        std::uint8_t extra{};
        if (archive_read_data(reader.get(), &extra, 1) != 0) { return {}; }
        return bytes;
      }
      if (archive_read_data_skip(reader.get()) != ARCHIVE_OK) { return {}; }
    }
    return {};
  }
  bt_status flag(const bt_flag_request_v1& request, bt_buffer_v1& result) {
    result.size = 0;
    if (request.reserved || request.iso3166[2] || (result.capacity && !result.data)) { return BT_INVALID; }
    std::string code;
    for (int i = 0; i < 2; ++i) {
      char c = request.iso3166[i];
      if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
      if (c < 'a' || c > 'z') { return BT_INVALID; }
      code += c;
    }
    const std::string size = request.size_index == 0 ? "18x12" : "25x15";
    const auto basename = code + ".png";
    std::vector<std::uint8_t> bytes;
    Fd images(::openat(directory.value, "images", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (images.value >= 0) {
      Fd sizes(::openat(images.value, size.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
      Fd file(sizes.value < 0 ? -1 : openFile(sizes.value, basename));
      struct stat info{};
      if (file.value >= 0 && ::fstat(file.value, &info) == 0 && info.st_size > 0
          && info.st_size <= static_cast<off_t>(MaximumFlag)) {
        bytes.resize(static_cast<std::size_t>(info.st_size));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
          const auto read = ::read(file.value, bytes.data() + offset, bytes.size() - offset);
          if (read <= 0) { bytes.clear(); break; }
          offset += static_cast<std::size_t>(read);
        }
      }
    }
    if (bytes.empty()) {
      const auto entry = "com/vuze/plugins/azlocprov/images/" + size + "/" + basename;
      if (!javaVersion.empty()) { bytes = flagFromJar("azlocprov_" + javaVersion + ".jar", entry); }
      if (bytes.empty()) { bytes = flagFromJar("azlocprov.jar", entry); }
    }
    if (bytes.empty()) { return BT_NOT_FOUND; }
    result.size = static_cast<std::uint32_t>(bytes.size());
    if (result.capacity < bytes.size()) { return BT_BUFFER_SMALL; }
    std::memcpy(result.data, bytes.data(), bytes.size()); return BT_OK;
  }
};
bt_status create(const bt_plugin_options_v1* options, void** instance) noexcept {
  if (!instance) { return BT_INVALID; }
  *instance = nullptr;
  if (!options || options->size != sizeof(*options) || options->abi != BT_PLUGIN_ABI_V1
      || options->reserved || !options->java_plugin_version
      || ::strnlen(options->java_plugin_version, 65) > 64) { return BT_INVALID; }
  try {
    const std::string version(options->java_plugin_version);
    if (!std::ranges::all_of(version, [](char c) { return (c >= '0' && c <= '9') || c == '.'; })) {
      return BT_INVALID;
    }
    struct stat info{};
    if (::fstat(options->plugin_directory_fd, &info) != 0 || !S_ISDIR(info.st_mode)) { return BT_INVALID; }
    const int fd = ::fcntl(options->plugin_directory_fd, F_DUPFD_CLOEXEC, 3);
    if (fd < 0) { return BT_IO; }
    Fd guard(fd);
    auto provider = std::make_unique<Provider>(-1, version);
    provider->hostContext = options->host_context; provider->countryName = options->country_name;
    provider->directory.value = fd; guard.value = -1;
    *instance = provider.release(); return BT_OK;
  } catch (...) { return BT_FAILED; }
}
bt_status start(void* instance) noexcept {
  if (!instance) { return BT_INVALID; }
  static_cast<Provider*>(instance)->started = true; return BT_OK;
}
bt_status call(void* instance, std::uint32_t operation, const void* request, std::uint32_t requestSize,
    void* response, std::uint32_t responseSize) noexcept {
  if (!instance || !static_cast<Provider*>(instance)->started || !request || !response) { return BT_INVALID; }
  try {
    auto& provider = *static_cast<Provider*>(instance);
    if (operation == BT_LOCATION_DATABASE_STATUS && requestSize == sizeof(bt_location_database_request_v1)
        && responseSize == sizeof(bt_location_database_result_v1)) {
      bt_location_database_request_v1 input{}; bt_location_database_result_v1 output{};
      std::memcpy(&input, request, sizeof(input));
      if (input.reserved) { return BT_INVALID; }
      const bool ready = provider.databaseReady(); output.available = ready ? 1U : 0U;
      std::memcpy(response, &output, sizeof(output)); return ready ? BT_OK : BT_NOT_FOUND;
    }
    if ((operation == BT_LOCATION_LOOKUP || operation == BT_LOCATION_LOOKUP_CHECKED) && requestSize == sizeof(bt_location_request_v1)
        && responseSize == sizeof(bt_location_result_v1)) {
      bt_location_request_v1 input{}; bt_location_result_v1 output{};
      std::memcpy(&input, request, sizeof(input));
      if (operation == BT_LOCATION_LOOKUP_CHECKED && !provider.databaseReady()) { return BT_IO; }
      const auto status = provider.lookup(input, output, operation == BT_LOCATION_LOOKUP_CHECKED);
      std::memcpy(response, &output, sizeof(output)); return status;
    }
    if (operation == BT_LOCATION_ASN_LOOKUP && requestSize == sizeof(bt_location_request_v1)
        && responseSize == sizeof(bt_location_asn_result_v1)) {
      bt_location_request_v1 input{}; bt_location_asn_result_v1 output{};
      std::memcpy(&input, request, sizeof(input));
      const auto status = provider.lookupAsn(input, output);
      std::memcpy(response, &output, sizeof(output));
      return status;
    }
    if (operation == BT_LOCATION_FLAG && requestSize == sizeof(bt_flag_request_v1)
        && responseSize == sizeof(bt_buffer_v1)) {
      bt_flag_request_v1 input{}; bt_buffer_v1 output{};
      std::memcpy(&input, request, sizeof(input)); std::memcpy(&output, response, sizeof(output));
      const auto status = provider.flag(input, output);
      std::memcpy(response, &output, sizeof(output)); return status;
    }
    return BT_UNSUPPORTED;
  } catch (...) { return BT_FAILED; }
}
void stop(void* instance) noexcept { if (instance) { static_cast<Provider*>(instance)->started = false; } }
void destroy(void* instance) noexcept { delete static_cast<Provider*>(instance); }
const bt_plugin_v1 Api{sizeof(bt_plugin_v1), BT_PLUGIN_ABI_V1, "azlocprov", BT_NATIVE_VERSION,
    BT_CAP_LOCATION, create, start, call, stop, destroy};
}
extern "C" BT_PLUGIN_EXPORT const bt_plugin_v1* biglybtc_plugin_entry(std::uint32_t requested) {
  return requested == BT_PLUGIN_ABI_V1 ? &Api : nullptr;
}

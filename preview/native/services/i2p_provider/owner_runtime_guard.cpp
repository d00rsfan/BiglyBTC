// SPDX-License-Identifier: GPL-2.0-or-later
// Mapping/FD/hash proof retained from qualified persistent_runtime_guard.cpp;
// private compiled artifact constants and all31 same-image exports required.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "owner_runtime_guard.hpp"
#include "private_i2p_runtime_config.hpp"
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#include <openssl/evp.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits>
#include <link.h>
#include <optional>
#include <string_view>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <utility>
namespace biglybtc::services::i2p::provider {
namespace runtime_binding=private_i2p_runtime_binding;
namespace {
struct Descriptor {
  int value = -1;
  ~Descriptor() { if (value >= 0) { ::close(value); } }
};
struct Library {
  void* value = nullptr;
  ~Library() { if (value) { ::dlclose(value); } }
};
struct Digest {
  EVP_MD_CTX* value = EVP_MD_CTX_new();
  ~Digest() { EVP_MD_CTX_free(value); }
};
struct File {
  FILE* value = nullptr;
  ~File() { if (value) { std::fclose(value); } }
};
struct Mapping {
  std::uintptr_t begin = 0, end = 0;
  std::uint64_t offset = 0, deviceMajor = 0, deviceMinor = 0, inode = 0;
  bool operator==(const Mapping&) const = default;
};

template<class Number>
bool number(std::string_view token, Number& result, int base) {
  const auto converted = std::from_chars(token.data(), token.data() + token.size(), result, base);
  return converted.ec == std::errc{} && converted.ptr == token.data() + token.size();
}
std::optional<std::string_view> field(std::string_view& line) {
  const auto begin = line.find_first_not_of(' ');
  if (begin == std::string_view::npos) { return {}; }
  line.remove_prefix(begin);
  const auto end = line.find(' ');
  const auto result = line.substr(0, end);
  line.remove_prefix(end == std::string_view::npos ? line.size() : end);
  return result;
}
std::optional<Mapping> mappingFor(std::uintptr_t address) {
  // Inspect only this process. No /proc/PID, peer or profile discovery.
  File file{std::fopen("/proc/self/maps", "re")};
  if (!file.value) { return {}; }
  std::optional<Mapping> found;
  std::array<char, 8192> buffer{};
  std::size_t bytes = 0, lines = 0;
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), file.value)) {
    const auto length = std::strlen(buffer.data());
    bytes += length;
    if (++lines > 16384 || bytes > 2 * 1024 * 1024 || !length || buffer[length - 1] != '\n') { return {}; }
    std::string_view line(buffer.data(), length - 1);
    const auto range = field(line), permissions = field(line), offset = field(line),
        device = field(line), inode = field(line);
    if (!range || !permissions || !offset || !device || !inode) { return {}; }
    const auto dash = range->find('-'), colon = device->find(':');
    if (dash == std::string_view::npos || colon == std::string_view::npos) { return {}; }
    Mapping candidate;
    if (!number(range->substr(0, dash), candidate.begin, 16)
        || !number(range->substr(dash + 1), candidate.end, 16)
        || !number(*offset, candidate.offset, 16)
        || !number(device->substr(0, colon), candidate.deviceMajor, 16)
        || !number(device->substr(colon + 1), candidate.deviceMinor, 16)
        || !number(*inode, candidate.inode, 10) || candidate.begin >= candidate.end) { return {}; }
    if (address >= candidate.begin && address < candidate.end) {
      if (found || permissions->size() != 4 || (*permissions)[0] != 'r'
          || (*permissions)[2] != 'x' || !candidate.inode) { return {}; }
      found = candidate;
    }
  }
  if (std::ferror(file.value)) { return {}; }
  return found;
}
struct ObjectQuery {
  std::uintptr_t base = 0, symbol = 0;
  int matches = 0;
};
int objectContaining(dl_phdr_info* info, std::size_t, void* opaque) {
  auto& query = *static_cast<ObjectQuery*>(opaque);
  if (info->dlpi_addr != query.base) { return 0; }
  for (std::size_t i = 0; i < info->dlpi_phnum; ++i) {
    const auto& header = info->dlpi_phdr[i];
    if (header.p_type != PT_LOAD || !(header.p_flags & PF_X)
        || header.p_vaddr > std::numeric_limits<std::uintptr_t>::max() - query.base) { continue; }
    const auto begin = query.base + header.p_vaddr;
    if (header.p_memsz > std::numeric_limits<std::uintptr_t>::max() - begin) { continue; }
    if (query.symbol >= begin && query.symbol < begin + header.p_memsz) { ++query.matches; }
  }
  return 0;
}
bool sameMetadata(const struct stat& left, const struct stat& right) {
  return left.st_dev == right.st_dev && left.st_ino == right.st_ino
      && left.st_mode == right.st_mode && left.st_uid == right.st_uid
      && left.st_gid == right.st_gid && left.st_nlink == right.st_nlink
      && left.st_size == right.st_size
      && left.st_mtim.tv_sec == right.st_mtim.tv_sec && left.st_mtim.tv_nsec == right.st_mtim.tv_nsec
      && left.st_ctim.tv_sec == right.st_ctim.tv_sec && left.st_ctim.tv_nsec == right.st_ctim.tv_nsec;
}
using CanonicalPath = std::array<char, 4096>;
std::optional<CanonicalPath> canonicalPath(const char* path) {
  if (!path || !*path) { return {}; }
  CanonicalPath result{};
  if (!::realpath(path, result.data()) || result[0] != '/') { return {}; }
  return result;
}
bool canonicalMatches(const char* path, const CanonicalPath& actual) {
  const auto resolved = canonicalPath(path);
  return resolved && std::string_view(resolved->data()) == actual.data();
}
bool hashed(int descriptor) {
  Digest context;
  if (!context.value || EVP_DigestInit_ex(context.value, EVP_sha256(), nullptr) != 1) { return false; }
  std::array<unsigned char, 65536> buffer{};
  unsigned long long bytes = 0;
  for (;;) {
    const auto count = ::read(descriptor, buffer.data(), buffer.size());
    if (count < 0) { if (errno == EINTR) { continue; } return false; }
    if (!count) { break; }
    const auto size = static_cast<std::size_t>(count);
    bytes += size;
    if (bytes > runtime_binding::bytes || EVP_DigestUpdate(context.value, buffer.data(), size) != 1) { return false; }
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int size = 0;
  if (bytes != runtime_binding::bytes || EVP_DigestFinal_ex(context.value, digest.data(), &size) != 1 || size != 32) { return false; }
  constexpr char hex[] = "0123456789abcdef";
  std::array<char, 64> encoded{};
  for (std::size_t i = 0; i < 32; ++i) {
    encoded[2 * i] = hex[digest[i] >> 4]; encoded[2 * i + 1] = hex[digest[i] & 15];
  }
  return std::string_view(encoded.data(), encoded.size()) == runtime_binding::sha256;
}
bool check(std::shared_ptr<void>* retainedPin) {
  const char* preload = std::getenv("LD_PRELOAD");
  const char* audit = std::getenv("LD_AUDIT");
  if ((preload && *preload) || (audit && *audit)) { return false; }
  // RTLD_NOLOAD pins the already mapped image while checking it. Never load a
  // second, correct provider just to hash it while another services LT calls.
  Library library{::dlopen("libtorrent-rasterbar.so.2.0", RTLD_NOW | RTLD_NOLOAD)};
  if (!library.value) { return false; }
  link_map* object = nullptr;
  if (::dlinfo(library.value, RTLD_DI_LINKMAP, &object) || !object) { return false; }
  const auto actual = canonicalPath(object->l_name);
  if (!actual) { return false; }
  // These are ORIGINAL dynamic exports, not &method / __wrap_* references.
  // GNU --wrap test seams therefore do not misidentify the test executable as LT.
  constexpr const char* symbols[] = {
      "_ZN10libtorrent7versionEv",
      "_ZN10libtorrent21session_stats_metricsEv",
      "_ZN10libtorrent14session_handle18post_session_statsEv",
      "_ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE",
      "_ZNK10libtorrent19session_stats_alert8countersEv",
      "_ZN10libtorrent14session_handle14remove_torrentERKNS_14torrent_handleENS_5flags13bitfield_flagIhNS_16remove_flags_tagEvEE",
      "biglybtc_payload_counter_abi_v1", "biglybtc_read_payload_counters_v1",
      "biglybtc_swarm_mask_abi_v1",
      "biglybtc_traffic_counter_abi_v2", "biglybtc_read_traffic_counters_v2",
      "biglybtc_queue_observer_abi_v1", "biglybtc_attach_queue_observer_v1",
      "biglybtc_traffic_counter_abi_v3", "biglybtc_read_traffic_counters_v3",
      "biglybtc_socks4a_abi_v1", "biglybtc_socks4a_webseed_abi_v1",
      "biglybtc_mainline_port_abi_v1", "biglybtc_attach_mainline_port_observer_v1",
      "biglybtc_refresh_mainline_port_v1",
      "biglybtc_mainline_identity_abi_v1", "biglybtc_configure_mainline_identity_v1", "biglybtc_read_mainline_identity_v1",
      "biglybtc_tracker_response_abi_v1", "biglybtc_attach_tracker_response_observer_v1",
      "biglybtc_mainline_routing_abi_v1", "biglybtc_query_mainline_routing_v1",
      "biglybtc_private_i2p_owner_abi_v1", "biglybtc_configure_private_i2p_owner_v1",
      "biglybtc_read_private_i2p_owner_v1", "biglybtc_withdraw_private_i2p_owner_v1"};
  constexpr std::size_t required=31;
  std::array<std::uintptr_t,31> addresses{};
  std::array<Mapping,31> beforeMappings{};
  std::size_t at = 0;
  for (; at < required; ++at) {
    const auto* name = symbols[at];
    void* exported = ::dlsym(library.value, name);
    Dl_info info{};
    if (!exported || exported != ::dlsym(RTLD_DEFAULT, name) || !::dladdr(exported, &info)
        || reinterpret_cast<std::uintptr_t>(info.dli_fbase) != object->l_addr
        || !canonicalMatches(info.dli_fname, *actual)) { return false; }
    const auto address = reinterpret_cast<std::uintptr_t>(exported);
    ObjectQuery query{object->l_addr, address, 0};
    ::dl_iterate_phdr(objectContaining, &query);
    const auto mapping = mappingFor(address);
    if (query.matches != 1 || !mapping || at == addresses.size()) { return false; }
    addresses[at] = address; beforeMappings[at] = *mapping;
  }
  if (at != required) { return false; }
  // The path is selected by the already loaded object, never by a runtime caller
  // or by loading the configured provider to mask a different active image.
  // A replaced FIFO must fail metadata admission rather than block opening.
  Descriptor file{::open(actual->data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
  if (file.value < 0) { return false; }
  struct stat before{}, after{}, named{};
  if (::fstat(file.value, &before) || !S_ISREG(before.st_mode) || before.st_size < 0
      || static_cast<unsigned long long>(before.st_size) != runtime_binding::bytes) { return false; }
  for (std::size_t i = 0; i < required; ++i) {
    const auto& mapping = beforeMappings[i];
    if (static_cast<std::uint64_t>(before.st_ino) != mapping.inode
        || static_cast<std::uint64_t>(::major(before.st_dev)) != mapping.deviceMajor
        || static_cast<std::uint64_t>(::minor(before.st_dev)) != mapping.deviceMinor) { return false; }
  }
  if (!hashed(file.value) || ::fstat(file.value, &after) || !sameMetadata(before, after)
      || ::lstat(actual->data(), &named) || !sameMetadata(after, named)) { return false; }
  for (std::size_t i = 0; i < required; ++i) {
    const auto afterMapping = mappingFor(addresses[i]);
    if (!afterMapping || beforeMappings[i] != *afterMapping) { return false; }
  }
  if (!canonicalMatches(object->l_name, *actual)) { return false; }
  if (retainedPin) {
    // Transfer, rather than reopen, the object which passed every mapping,
    // descriptor, source-bound digest and before/after identity check above.
    // exchange also prevents a double dlclose if shared_ptr allocation fails.
    *retainedPin = std::shared_ptr<void>(std::exchange(library.value, nullptr),
        [](void* value) { if (value) { ::dlclose(value); } });
  }
  return true;
}

}
std::shared_ptr<void> privateOwnerRuntimePin()noexcept{
  try{std::shared_ptr<void> pin;if(!check(&pin)||libtorrent::biglybtc_private_i2p_owner_abi_v1()!=libtorrent::BiglybtcPrivateI2pOwnerAbiV1){return {};}return pin;}catch(...){return {};}
}
}

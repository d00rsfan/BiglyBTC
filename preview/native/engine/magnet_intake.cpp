#include "magnet_intake.hpp"
#include "../adapters/file_io.hpp"
#include "../adapters/torrent_intake.hpp"

#include "biglybtc/torrent_layout.hpp"

#include <libtorrent/address.hpp>
#include <libtorrent/torrent_info.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <new>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace biglybtc::engine {
namespace {
namespace fs = std::filesystem;
using Error = storage::Error;
using Code = storage::ErrorCode;
using Node = core::Node;
using storage::detail::Descriptor;
constexpr std::string_view Active = "MagnetPlugin.active.magnets";
constexpr std::string_view Recovery = "Plugin.Magnet URI Handler.MagnetPlugin.recover.magnets";
constexpr std::string_view Destination = "BiglyBTC.pending.destination";
constexpr std::string_view Version = "BiglyBTC.pending.version";
constexpr std::size_t MaximumUri = 64 * 1024;
constexpr std::size_t MaximumPending = 128;
constexpr std::size_t MaximumMetadata = 32 * 1024 * 1024;
struct Failure { Error error; };
[[noreturn]] void fail(Code code, std::string message) { throw Failure{{code, std::move(message)}}; }
void require(bool value, Code code, const char* message) { if (!value) { fail(code, message); } }
template<class T> T take(std::expected<T, Error> value) {
  if (!value) { throw Failure{std::move(value.error())}; }
  return std::move(*value);
}
void take(std::expected<void, Error> value) { if (!value) { throw Failure{std::move(value.error())}; } }
std::string b(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string integer(std::int64_t value) { return 'i' + std::to_string(value) + 'e'; }
std::string lower(std::string_view value) {
  std::string result(value);
  for (auto& c : result) { if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c + ('a' - 'A')); } }
  return result;
}
int hexDigit(char c) {
  if (c >= '0' && c <= '9') { return c - '0'; }
  if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
  if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
  return -1;
}
std::string decode(std::string_view text) {
  std::string result;
  for (std::size_t n = 0; n < text.size(); ++n) {
    unsigned char c = static_cast<unsigned char>(text[n]);
    if (c == '%') {
      require(n + 2 < text.size() && hexDigit(text[n + 1]) >= 0 && hexDigit(text[n + 2]) >= 0,
          Code::InvalidFormat, "Malformed percent escape in magnet");
      c = static_cast<unsigned char>(hexDigit(text[n + 1]) * 16 + hexDigit(text[n + 2])); n += 2;
    } else if (c == '+') { c = ' '; }
    require(c >= 32 && c != 127, Code::InvalidFormat, "Control byte in magnet parameter");
    result.push_back(static_cast<char>(c));
  }
  return result;
}
std::string unhex(std::string_view value, std::size_t bytes) {
  require(value.size() == bytes * 2, Code::InvalidFormat, "Incorrect magnet hash length");
  std::string result(bytes, '\0');
  for (std::size_t n = 0; n < bytes; ++n) {
    require(hexDigit(value[n * 2]) >= 0 && hexDigit(value[n * 2 + 1]) >= 0, Code::InvalidFormat, "Non-hexadecimal magnet hash");
    result[n] = static_cast<char>(hexDigit(value[n * 2]) * 16 + hexDigit(value[n * 2 + 1]));
  }
  return result;
}
std::string unbase32(std::string_view value) {
  require(value.size() == 32, Code::InvalidFormat, "Incorrect base32 magnet hash length");
  std::string result;
  unsigned int bits = 0, count = 0;
  for (char c : value) {
    if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - ('a' - 'A')); }
    const int digit = c >= 'A' && c <= 'Z' ? c - 'A' : c >= '2' && c <= '7' ? c - '2' + 26 : -1;
    require(digit >= 0, Code::InvalidFormat, "Invalid base32 magnet hash");
    bits = (bits << 5) | static_cast<unsigned>(digit); count += 5;
    if (count >= 8) { count -= 8; result.push_back(static_cast<char>((bits >> count) & 255)); }
  }
  return result;
}
std::string base32(const core::TorrentHash& hash) {
  constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  std::string result;
  unsigned int bits = 0, count = 0;
  for (const auto byte : hash.bytes()) {
    bits = (bits << 8) | std::to_integer<unsigned>(byte); count += 8;
    while (count >= 5) { count -= 5; result += alphabet[(bits >> count) & 31]; }
  }
  if (count) { result += alphabet[(bits << (5 - count)) & 31]; }
  return result;
}
void pathShape(const fs::path& path) {
  require(path.is_absolute() && path.lexically_normal() == path && path.native().size() <= 4096
      && path.native().find('\0') == std::string::npos, Code::UnsafePath, "Magnet path must be absolute and normalized");
  for (const auto& part : path.relative_path()) {
    require(part != "." && part != ".." && !part.empty(), Code::UnsafePath, "Unsafe magnet path component");
  }
}
Descriptor directory(const fs::path& path) {
  pathShape(path);
  Descriptor fd(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  require(fd.get() >= 0, Code::Io, "Cannot open filesystem root");
  for (const auto& part : path.relative_path()) {
    Descriptor next(::openat(fd.get(), part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    require(next.get() >= 0, Code::UnsafePath, "Magnet path contains an inaccessible or symbolic directory");
    fd = std::move(next);
  }
  return fd;
}
std::string profilePath(const storage::ProfileLease& lease) {
  require(lease.valid(), Code::UnsafePath, "Magnet operation requires a held profile lease");
  std::array<char, 4097> buffer{};
  const auto path = "/proc/self/fd/" + std::to_string(lease.directoryHandle());
  const auto count = ::readlink(path.c_str(), buffer.data(), buffer.size());
  require(count > 0 && static_cast<std::size_t>(count) < buffer.size(), Code::UnsafePath, "Cannot identify held profile");
  std::string result(buffer.data(), static_cast<std::size_t>(count));
  auto check = directory(result);
  struct stat opened{}, held{};
  require(::fstat(check.get(), &opened) == 0 && ::fstat(lease.directoryHandle(), &held) == 0
      && opened.st_dev == held.st_dev && opened.st_ino == held.st_ino,
      Code::Conflict, "Held profile path changed");
  return result;
}
int port(std::string_view text) {
  unsigned value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  require(!text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
      && value >= 1 && value <= 65535, Code::InvalidFormat, "Invalid magnet endpoint port");
  return static_cast<int>(value);
}
libtorrent::tcp::endpoint numericPeer(std::string_view value) {
  const auto colon = value.rfind(':');
  require(colon != std::string_view::npos, Code::InvalidFormat, "Magnet peer hint has no port");
  auto host = value.substr(0, colon);
  if (host.starts_with('[') && host.ends_with(']')) { host = host.substr(1, host.size() - 2); }
  libtorrent::error_code error;
  const auto address = libtorrent::make_address(std::string(host), error);
  require(!error, Code::Unsupported, "Only numeric magnet peer hints are supported");
  return {address, static_cast<unsigned short>(port(value.substr(colon + 1)))};
}
void policy(NetworkPolicyError error) {
  fail(error.code == NetworkPolicyErrorCode::Memory ? Code::Io
      : error.code == NetworkPolicyErrorCode::Unsupported ? Code::Unsupported : Code::InvalidFormat,
      "Magnet network policy rejected field: " + error.field);
}
libtorrent::add_torrent_params parameters(const PendingMagnet& intent, const NetworkPolicy& network) {
  require(intent.trackers.size() <= 128 && intent.webSeeds.size() <= 128 && intent.peers.size() <= 256
      && intent.dhtNodes.size() <= 128, Code::InvalidFormat, "Over-limit typed magnet sources");
  libtorrent::add_torrent_params params;
  params.info_hashes = intent.requested; params.name = intent.displayName;
  params.trackers.assign(intent.trackers.begin(), intent.trackers.end());
  params.url_seeds.assign(intent.webSeeds.begin(), intent.webSeeds.end());
  params.peers.assign(intent.peers.begin(), intent.peers.end());
  params.dht_nodes.assign(intent.dhtNodes.begin(), intent.dhtNodes.end());
  const auto accepted = applyTorrentNetworkPolicy(network, {}, params);
  if (!accepted) { policy(accepted.error()); }
  return params;
}
PendingMagnet parse(std::string_view uri, std::optional<fs::path> destination, bool stopped, const NetworkPolicy& network) {
  require(uri.size() <= MaximumUri && uri.starts_with("magnet:?") && uri.find('#') == std::string_view::npos,
      Code::InvalidFormat, "Invalid or over-limit magnet URI");
  if (destination) { pathShape(*destination); }
  std::optional<std::string> v1, v2;
  PendingMagnet result;
  result.uri = uri; result.destination = std::move(destination); result.stopped = stopped;
  auto query = uri.substr(8);
  std::size_t fields = 0;
  while (!query.empty()) {
    require(++fields <= 512, Code::InvalidFormat, "Too many magnet parameters");
    const auto ampersand = query.find('&');
    const auto field = query.substr(0, ampersand);
    const auto equals = field.find('=');
    require(equals != std::string_view::npos && equals != 0, Code::InvalidFormat, "Malformed magnet query field");
    auto key = lower(decode(field.substr(0, equals)));
    const auto dot = key.rfind('.');
    if (dot != std::string::npos && dot + 1 < key.size()
        && std::all_of(key.begin() + static_cast<std::ptrdiff_t>(dot + 1), key.end(), [](char c) { return c >= '0' && c <= '9'; })) {
      key.resize(dot);
    }
    const auto value = decode(field.substr(equals + 1));
    if (key == "xt") {
      const auto lc = lower(value);
      if (lc.starts_with("urn:btih:")) {
        auto hash = value.size() - 9 == 40 ? unhex(std::string_view(value).substr(9), 20) : unbase32(std::string_view(value).substr(9));
        require(!v1 || *v1 == hash, Code::Conflict, "Conflicting magnet v1 hashes"); v1 = std::move(hash);
      } else if (lc.starts_with("urn:btmh:1220")) {
        auto hash = unhex(std::string_view(value).substr(13), 32);
        require(!v2 || *v2 == hash, Code::Conflict, "Conflicting magnet v2 hashes"); v2 = std::move(hash);
      } else { fail(Code::Unsupported, "Unsupported magnet exact-topic hash"); }
    } else if (key == "tr" || key == "ws") {
      require(value.size() <= 4096 && !value.empty(), Code::InvalidFormat, "Invalid magnet source URL");
      auto& output = key == "tr" ? result.trackers : result.webSeeds;
      require(output.size() < 128, Code::InvalidFormat, "Too many magnet sources");
      output.push_back(value);
    } else if (key == "dn") {
      require(value.size() <= 512, Code::InvalidFormat, "Magnet display name exceeds limit"); result.displayName = value;
    } else if (key == "net") {
      require(value == "Public", Code::Unsupported, "Tor/I2P magnet networks require their own route adapter");
    } else if (key == "x.pe" || key == "xsource") {
      require(result.peers.size() < 128, Code::InvalidFormat, "Too many magnet peer hints"); result.peers.push_back(numericPeer(value));
    } else if (key == "dht") {
      require(result.dhtNodes.size() < 128, Code::InvalidFormat, "Too many magnet DHT hints");
      const auto colon = value.rfind(':');
      require(colon != std::string::npos, Code::InvalidFormat, "Magnet DHT hint has no port");
      auto host = value.substr(0, colon);
      if (host.starts_with('[') && host.ends_with(']')) { host = host.substr(1, host.size() - 2); }
      result.dhtNodes.emplace_back(std::move(host), port(std::string_view(value).substr(colon + 1)));
    } else if (key == "xs" || key == "as" || key == "fl" || key == "so") {
      fail(Code::Unsupported, "Explicit metadata URL/file selection needs its own bounded adapter");
    }
    if (ampersand == std::string_view::npos) { break; }
    query.remove_prefix(ampersand + 1);
  }
  require(v1 || v2, Code::InvalidFormat, "Magnet has no actual requested hash");
  if (v1) { result.requested.v1.assign(v1->data()); }
  if (v2) { result.requested.v2.assign(v2->data()); }
  require((!v1 || result.requested.has_v1()) && (!v2 || result.requested.has_v2()), Code::InvalidFormat, "Zero magnet hash is not admissible");
  const auto& original = v1 ? *v1 : *v2;
  auto shortHash = core::TorrentHash::parse(std::string_view(original).substr(0, 20));
  require(shortHash.has_value(), Code::InvalidFormat, "Invalid original magnet recovery identity"); result.hash = *shortHash;
  const auto admitted = parameters(result, network);
  result.trackers.assign(admitted.trackers.begin(), admitted.trackers.end());
  result.webSeeds.assign(admitted.url_seeds.begin(), admitted.url_seeds.end());
  result.peers.assign(admitted.peers.begin(), admitted.peers.end());
  result.dhtNodes.assign(admitted.dht_nodes.begin(), admitted.dht_nodes.end());
  return result;
}
core::Document document(std::string bytes, std::size_t maximum = 4 * 1024 * 1024) {
  core::Limits limits; limits.inputBytes = maximum; limits.stringBytes = maximum;
  auto parsed = core::Document::parse(std::move(bytes), limits);
  require(parsed && parsed->root().type == Node::Type::Dictionary, Code::InvalidFormat, "Invalid pending magnet document");
  return std::move(*parsed);
}
void put(core::Document& doc, std::string_view key, std::string value) {
  require(doc.replaceValue(key, std::move(value)).has_value(), Code::InvalidFormat, "Pending magnet config edit exceeds limits");
}
std::string raw(const core::Document& doc, const Node& node) { return doc.originalBytes().substr(node.begin, node.end - node.begin); }
std::string text(const Node* value, std::string fallback = {}) {
  if (!value) { return fallback; }
  require(value->type == Node::Type::Bytes, Code::InvalidFormat, "Invalid pending magnet text"); return std::string(value->bytes);
}
std::int64_t number(const Node* value, std::int64_t fallback) {
  if (!value) { return fallback; }
  require(value->type == Node::Type::Integer, Code::InvalidFormat, "Invalid pending magnet integer"); return value->integer;
}
struct Configuration { std::optional<storage::ConfigFile> file; core::Document doc; };
Configuration load(const storage::ProfileLease& lease) {
  require(lease.valid(), Code::UnsafePath, "Pending magnets require a held profile lease");
  auto file = storage::ConfigFile::load(lease, "biglybt.config", {});
  if (file) {
    require(file->recoverySource() == storage::RecoverySource::Primary, Code::Conflict, "Resolve configuration recovery before pending magnet changes");
    auto doc = document(file->document().originalBytes());
    return {std::move(*file), std::move(doc)};
  }
  if (file.error().code != Code::Missing) { throw Failure{file.error()}; }
  for (const auto* name : {"biglybt.config", "biglybt.config.bak", "biglybt.config.saving"}) {
    struct stat info{};
    require(::fstatat(lease.directoryHandle(), name, &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT,
        Code::Conflict, "Pending magnet config has unresolved candidates");
  }
  return {std::nullopt, document("de")};
}
bool recoveryEnabled(const Configuration& c) {
  const auto value = number(c.doc.find(Recovery), 1);
  require(value == 0 || value == 1, Code::InvalidFormat, "Invalid original magnet recovery setting"); return value == 1;
}
const Node* active(const Configuration& c) {
  const auto* map = c.doc.find(Active);
  require(!map || (map->type == Node::Type::Dictionary && map->children.size() / 2 <= MaximumPending),
      Code::InvalidFormat, "Invalid or over-limit original magnet recovery map"); return map;
}
void save(Configuration& c, storage::ProfileLease& lease) {
  // Never overwrite a different interrupted save in the same config namespace.
  struct stat info{};
  require(::fstatat(lease.directoryHandle(), "biglybt.config.saving", &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT,
      Code::Conflict, "Resolve existing config saving file before pending magnet update");
  if (c.file) { c.file->document() = std::move(c.doc); take(c.file->save(lease)); }
  else { (void)take(storage::ConfigFile::createMissing(lease, "biglybt.config", c.doc.serialize(), {})); }
}
std::string mapChange(const Configuration& c, std::string_view key, std::optional<std::string_view> replacement) {
  std::string result = "d";
  const auto* map = active(c);
  bool found = false;
  if (map) {
    for (std::size_t n = 0; n < map->children.size(); n += 2) {
      if (map->children[n].bytes == key) {
        found = true;
        if (replacement) { result += raw(c.doc, map->children[n]) + std::string(*replacement); }
      } else { result += raw(c.doc, map->children[n]) + raw(c.doc, map->children[n + 1]); }
    }
  }
  if (!found && replacement) { result += b(key) + std::string(*replacement); }
  return result + 'e';
}
PendingMagnet row(const Configuration& c, const Node& node, std::string_view key, const NetworkPolicy& network) {
  require(node.type == Node::Type::Dictionary, Code::InvalidFormat, "Pending magnet row is not a map");
  const auto hash = text(node.find("hash"));
  require(hash.size() == 20, Code::Unsupported, "Unsupported original pending hash width");
  auto parsedHash = core::TorrentHash::parse(hash);
  require(parsedHash && base32(*parsedHash) == key, Code::InvalidFormat, "Original pending hash/key mismatch");
  auto args = text(node.find("args"));
  require(args.size() <= MaximumUri, Code::InvalidFormat, "Pending query exceeds limit");
  if (args.starts_with('?')) { args.erase(args.begin()); }
  const auto* other = node.find("other_metadata");
  require(!other || other->type == Node::Type::Dictionary, Code::InvalidFormat, "Invalid pending other_metadata");
  std::optional<fs::path> destination;
  if (other && other->find(Destination)) {
    require(number(other->find(Version), -1) == 1, Code::Unsupported, "Unsupported native pending version");
    destination = text(other->find(Destination));
  }
  const auto state = number(node.find("ss"), 2);
  require(state >= 1 && state <= 3, Code::InvalidFormat, "Invalid original magnet start state");
  auto result = parse(args.starts_with("magnet:?") ? args : "magnet:?" + args, std::move(destination), state == 3, network);
  require(result.hash == *parsedHash, Code::Conflict, "Pending full URI and original short hash disagree");
  result.forceStart = state == 1;
  result.timeoutMillis = number(node.find("timeout"), -1);
  require(result.timeoutMillis >= 1 && result.timeoutMillis <= 3600000, Code::Unsupported, "Pending timeout outside bounded owner range");
  if (node.find("added")) { result.addedMillis = number(node.find("added"), 0); }
  const auto* sources = node.find("sources");
  require(!sources || (sources->type == Node::Type::List && sources->children.size() <= 128), Code::InvalidFormat, "Invalid pending sources");
  if (sources) {
    for (const auto& source : sources->children) {
      require(source.type == Node::Type::Dictionary, Code::InvalidFormat, "Invalid original pending source");
      const auto p = number(source.find("port"), 0);
      require(p >= 1 && p <= 65535, Code::InvalidFormat, "Invalid original pending source port");
      require(!source.find("host"), Code::Unsupported, "Unresolved Java pending peer sources need hostname adapter");
      const auto bytes = text(source.find("address"));
      require(bytes.size() == 4 || bytes.size() == 16, Code::InvalidFormat, "Invalid original numeric source address");
      libtorrent::address address;
      if (bytes.size() == 4) {
        libtorrent::address_v4::bytes_type data{};
        std::copy(bytes.begin(), bytes.end(), data.begin()); address = libtorrent::address_v4(data);
      } else {
        libtorrent::address_v6::bytes_type data{};
        std::copy(bytes.begin(), bytes.end(), data.begin()); address = libtorrent::address_v6(data);
      }
      const libtorrent::tcp::endpoint endpoint(address, static_cast<unsigned short>(p));
      if (std::ranges::find(result.peers, endpoint) == result.peers.end()) { result.peers.push_back(endpoint); }
    }
  }
  auto checked = parameters(result, network);
  result.peers.assign(checked.peers.begin(), checked.peers.end());
  result.sourceEntry = raw(c.doc, node);
  return result;
}
PendingMagnet current(const Configuration& c, const PendingMagnet& expected, const NetworkPolicy& network) {
  const auto* map = active(c);
  const auto key = base32(expected.hash);
  const auto* value = map ? map->find(key) : nullptr;
  require(value && !expected.sourceEntry.empty() && raw(c.doc, *value) == expected.sourceEntry,
      Code::Conflict, "Pending magnet changed or was cancelled");
  return row(c, *value, key, network);
}
std::string entry(const PendingMagnet& intent) {
  require(intent.destination.has_value(), Code::Missing, "New pending magnet needs an explicit destination");
  std::string sources = "l";
  for (const auto& peer : intent.peers) {
    std::string address;
    if (peer.address().is_v4()) {
      const auto value = peer.address().to_v4().to_bytes(); address.assign(reinterpret_cast<const char*>(value.data()), value.size());
    } else {
      const auto value = peer.address().to_v6().to_bytes(); address.assign(reinterpret_cast<const char*>(value.data()), value.size());
    }
    sources += "d" + b("address") + b(address) + b("port") + integer(peer.port()) + 'e';
  }
  sources += 'e';
  const auto bytes = intent.hash.bytes();
  std::string result = "d" + b("hash") + b(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
      + b("args") + b(std::string_view(intent.uri).substr(8)) + b("sources") + sources
      + b("timeout") + integer(intent.timeoutMillis) + b("ss") + integer(intent.forceStart ? 1 : intent.stopped ? 3 : 2)
      + b("other_metadata") + "d" + b(Version) + "i1e" + b(Destination) + b(intent.destination->native()) + 'e';
  if (intent.addedMillis) { result += b("added") + integer(*intent.addedMillis); }
  return result + 'e';
}
std::string readOwned(int root, const std::string& name, std::size_t maximum) {
  struct stat before{};
  require(::fstatat(root, name.c_str(), &before, AT_SYMLINK_NOFOLLOW) == 0
      && S_ISREG(before.st_mode) && before.st_uid == ::geteuid() && before.st_nlink == 1,
      Code::UnsafePath, "Magnet metadata must be an owned nonsymlink regular file");
  auto bytes = take(storage::detail::readFile(root, name, maximum));
  require(bytes.has_value(), Code::Missing, "Actual magnet metadata is missing");
  struct stat after{};
  require(::fstatat(root, name.c_str(), &after, AT_SYMLINK_NOFOLLOW) == 0 && before.st_dev == after.st_dev
      && before.st_ino == after.st_ino && before.st_size == after.st_size
      && before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec
      && before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec,
      Code::Conflict, "Magnet metadata changed during read");
  return std::move(*bytes);
}
std::shared_ptr<libtorrent::torrent_info> metadata(const std::string& bytes) {
  libtorrent::error_code error;
  auto result = std::make_shared<libtorrent::torrent_info>(libtorrent::span<const char>(bytes.data(), bytes.size()), error, libtorrent::from_span);
  require(!error && result->is_valid() && result->num_files() <= 100000 && result->num_pieces() <= 1000000,
      Code::InvalidFormat, "Actual magnet metadata failed validation"); return result;
}
void matching(const PendingMagnet& pending, const libtorrent::torrent_info& info) {
  const auto actual = info.info_hashes();
  require((!pending.requested.has_v1() || (actual.has_v1() && actual.v1 == pending.requested.v1))
      && (!pending.requested.has_v2() || (actual.has_v2() && actual.v2 == pending.requested.v2)),
      Code::Conflict, "Actual metadata does not match every requested magnet hash");
}
} // namespace

std::expected<PendingMagnet, Error> parseMagnetIntent(std::string_view uri, const fs::path& destination,
    bool stopped, const NetworkPolicy& network) {
  try { return parse(uri, destination, stopped, network); }
  catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{Code::Io, "Cannot prepare bounded magnet intent"}); }
}
std::expected<PendingMagnet, Error> queuePendingMagnet(storage::ProfileLease& lease,
    const PendingMagnet& supplied, const NetworkPolicy& network) {
  try {
    auto intent = parse(supplied.uri, supplied.destination, supplied.stopped, network);
    require(supplied.timeoutMillis >= 1 && supplied.timeoutMillis <= 3600000,
        Code::Unsupported, "Unsupported pending timeout request");
    intent.forceStart = supplied.forceStart;
    if (intent.forceStart) { intent.stopped = false; }
    intent.timeoutMillis = supplied.timeoutMillis; intent.addedMillis = supplied.addedMillis;
    auto c = load(lease);
    require(recoveryEnabled(c), Code::Unsupported, "Original magnet recovery is disabled; use explicit ephemeral owner intent");
    const auto* map = active(c);
    const auto key = base32(intent.hash);
    require(!map || (!map->find(key) && map->children.size() / 2 < MaximumPending), Code::Conflict, "Magnet is already pending or pending limit reached");
    put(c.doc, Active, mapChange(c, key, entry(intent)));
    save(c, lease);
    auto saved = load(lease); const auto* savedMap = active(saved);
    const auto* stored = savedMap ? savedMap->find(key) : nullptr;
    require(stored != nullptr, Code::Conflict, "Queued magnet disappeared before acknowledgement");
    return row(saved, *stored, key, network);
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{Code::Io, "Pending magnet save may be indeterminate; reload before retry"}); }
}
std::expected<PendingMagnet, Error> attachPendingMagnetDestination(storage::ProfileLease& lease,
    const PendingMagnet& expected, const fs::path& destination, const NetworkPolicy& network) {
  try {
    pathShape(destination);
    auto c = load(lease);
    require(recoveryEnabled(c), Code::Unsupported, "Original magnet recovery is disabled");
    auto intent = current(c, expected, network);
    if (intent.destination) {
      require(*intent.destination == destination, Code::Conflict, "Pending magnet already has a different destination");
      return intent; // No backup or config write for explicit same-destination retry.
    }
    auto ownedRow = document(intent.sourceEntry);
    const auto* other = ownedRow.find("other_metadata");
    auto ownedOther = document(other ? raw(ownedRow, *other) : "de");
    require(!ownedOther.find(Version) || number(ownedOther.find(Version), -1) == 1,
        Code::Unsupported, "Unsupported native pending version");
    put(ownedOther, Version, "i1e");
    put(ownedOther, Destination, b(destination.native()));
    put(ownedRow, "other_metadata", ownedOther.serialize());
    const auto key = base32(intent.hash);
    put(c.doc, Active, mapChange(c, key, ownedRow.serialize()));
    save(c, lease);
    auto saved = load(lease); const auto* map = active(saved);
    const auto* stored = map ? map->find(key) : nullptr;
    require(stored != nullptr, Code::Conflict, "Updated pending magnet disappeared before acknowledgement");
    auto result = row(saved, *stored, key, network);
    require(result.destination && *result.destination == destination, Code::Conflict, "Pending magnet destination changed before acknowledgement");
    return result;
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{Code::Io, "Pending destination save may be indeterminate; reload before retry"}); }
}
std::expected<std::vector<PendingMagnet>, Error> readPendingMagnets(const storage::ProfileLease& lease,
    const NetworkPolicy& network) {
  try {
    auto c = load(lease);
    std::vector<PendingMagnet> result;
    if (!recoveryEnabled(c)) { return result; }
    const auto* map = active(c); if (!map) { return result; }
    for (std::size_t n = 0; n < map->children.size(); n += 2) { result.push_back(row(c, map->children[n + 1], map->children[n].bytes, network)); }
    return result;
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{Code::Io, "Cannot load bounded original pending magnets"}); }
}
std::expected<void, Error> cancelPendingMagnet(storage::ProfileLease& lease, const PendingMagnet& expected) {
  try {
    auto c = load(lease); const auto* map = active(c);
    const auto key = base32(expected.hash); const auto* value = map ? map->find(key) : nullptr;
    require(value && !expected.sourceEntry.empty() && raw(c.doc, *value) == expected.sourceEntry,
        Code::Conflict, "Pending magnet changed before cancellation");
    put(c.doc, Active, mapChange(c, key, std::nullopt)); save(c, lease); return {};
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{Code::Io, "Pending cancellation may be indeterminate; reload before retry"}); }
}
std::expected<libtorrent::add_torrent_params, Error> metadataOnlyParameters(const PendingMagnet& supplied,
    const NetworkPolicy& network, const fs::path& ownedMetadataDirectory) {
  try {
    pathShape(ownedMetadataDirectory);
    auto intent = parse(supplied.uri, supplied.destination, supplied.stopped, network);
    require(intent.hash == supplied.hash && intent.requested == supplied.requested, Code::Conflict, "Magnet URI/hash identity was changed");
    // Preserve separately supplied actual Java numeric recovery hints, then
    // reapply the policy. No hostname lookup or new source is manufactured.
    intent.peers = supplied.peers;
    auto params = parameters(intent, network);
    params.save_path = ownedMetadataDirectory.native();
    // LT counts downloading_metadata as ready: stop_when_ready would pause
    // BEFORE BEP9 metadata requests. Payload remains disabled independently.
    params.flags &= ~(libtorrent::torrent_flags::paused | libtorrent::torrent_flags::auto_managed
        | libtorrent::torrent_flags::stop_when_ready);
    params.flags |= libtorrent::torrent_flags::upload_mode | libtorrent::torrent_flags::default_dont_download;
    return params;
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{Code::Io, "Cannot prepare metadata-only magnet parameters"}); }
}
std::expected<core::TorrentHash, Error> completePendingMagnet(storage::ProfileLease& lease,
    const PendingMagnet& expected, const fs::path& actualMetadata, const NetworkPolicy& network) {
  try {
    auto c = load(lease); auto intent = current(c, expected, network);
    require(intent.destination.has_value(), Code::Unsupported, "Recovered magnet needs an explicit destination");
    pathShape(actualMetadata);
    auto parent = directory(actualMetadata.parent_path());
    auto bytes = readOwned(parent.get(), actualMetadata.filename().native(), MaximumMetadata);
    auto info = metadata(bytes); matching(intent, *info);
    require(intent.requested.has_v1() || !info->info_hashes().has_v1(), Code::Unsupported,
        "A v2-only magnet resolving hybrid metadata needs alternate-hybrid identity intake");
    auto doc = document(bytes, MaximumMetadata);
    const auto layout = compat::originalLayout(doc);
    require(layout.has_value() && !doc.find("info")->find("hash-override"), Code::InvalidFormat, "Unqualified actual magnet file layout");
    auto params = parameters(intent, network); params.ti = info;
    params.flags &= ~(libtorrent::torrent_flags::override_trackers | libtorrent::torrent_flags::override_web_seeds);
    const auto accepted = applyTorrentNetworkPolicy(network, {}, params);
    if (!accepted) { policy(accepted.error()); }
    std::string trackers = "l";
    for (const auto& tracker : params.trackers) { trackers += 'l' + b(tracker) + 'e'; }
    put(doc, "announce-list", trackers + 'e');
    if (!params.trackers.empty()) { put(doc, "announce", b(params.trackers.front())); }
    std::string seeds = "l";
    for (const auto& seed : params.url_seeds) { seeds += b(seed); }
    put(doc, "url-list", seeds + 'e');
    seeds = "l"; for (const auto& seed : params.http_seeds) { seeds += b(seed); }
    put(doc, "httpseeds", seeds + 'e');
    bytes = doc.serialize();
    const auto profile = profilePath(lease);
    const auto sourceName = "BiglyBTC.magnet-" + base32(intent.hash) + ".torrent";
    const int root = lease.directoryHandle();
    auto previous = take(storage::detail::readFile(root, sourceName, MaximumMetadata));
    if (previous) { require(*previous == bytes, Code::Conflict, "Prior pending metadata conflicts; retained unchanged"); }
    else {
      auto fd = take(storage::detail::writeExclusiveOwned(root, sourceName, bytes, 0600));
      require(::fchmod(fd.get(), 0600) == 0 && ::fsync(fd.get()) == 0 && ::fsync(root) == 0,
          Code::Io, "Cannot synchronize actual pending metadata");
    }
    const auto published = take(storage::detail::readFile(root, "downloads.config", 64 * 1024 * 1024));
    bool already = false;
    if (published) {
      auto inventory = core::Document::parse(*published, storage::compatibleConfigLimits());
      const auto saved = inventory ? core::readLibrary(*inventory) : std::expected<core::LibrarySnapshot, core::LibraryError>{std::unexpected(core::LibraryError::Root)};
      require(saved && saved->issues.empty(), Code::InvalidFormat, "Invalid inventory during pending completion");
      for (const auto& item : saved->downloads) {
        if (!item.hash || *item.hash != intent.hash) { continue; }
        require(!already && item.saveDirectory == intent.destination->native() && item.saveFile == layout->name
            && item.torrentFile == profile + "/torrents/" + intent.hash.hex() + ".torrent",
            Code::Conflict, "Existing download differs from pending destination");
        auto archive = directory(fs::path(profile) / "torrents");
        auto existing = metadata(readOwned(archive.get(), intent.hash.hex() + ".torrent", MaximumMetadata)); matching(intent, *existing);
        auto activeDir = directory(fs::path(profile) / "active");
        auto state = metadata(readOwned(activeDir.get(), intent.hash.hex() + ".dat", 64 * 1024 * 1024)); matching(intent, *state);
        already = true;
      }
    }
    if (!already) {
      const auto imported = take(storage::importTorrent(lease, fs::path(profile) / sourceName,
          *intent.destination, intent.stopped, nullptr, intent.forceStart));
      require(imported == intent.hash, Code::Conflict, "Imported identity differs from pending request");
    }
    take(cancelPendingMagnet(lease, intent));
    // Source is an owned recovery artifact; only remove exact matching bytes.
    if (readOwned(root, sourceName, MaximumMetadata) == bytes) {
      require(::unlinkat(root, sourceName.c_str(), 0) == 0 && ::fsync(root) == 0,
          Code::Io, "Magnet imported but metadata-source cleanup failed");
    }
    return intent.hash;
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::exception&) { return std::unexpected(Error{Code::Io, "Magnet completion may be indeterminate; recover intake and reload pending state"}); }
}
} // namespace biglybtc::engine

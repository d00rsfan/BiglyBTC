// SPDX-License-Identifier: GPL-2.0-or-later
#include "country_provider_binding.hpp"
#include "state_file_io.hpp"
#include "../plugins/BiglyBTC-plugin-azlocprov/location_windows.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>

namespace biglybtc::storage {
namespace {
namespace win = location::windows;
namespace state = storage::detail;
namespace files = platform::detail;
using LoadResult = std::expected<std::unique_ptr<CountryProvider>, Error>;

std::unexpected<Error> fail(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}

struct DirectoryFd {
  int value;
  explicit DirectoryFd(state::StateDescriptor directory) : value(
      ::_open_osfhandle(reinterpret_cast<std::intptr_t>(directory.get()), _O_RDONLY | _O_BINARY | _O_NOINHERIT)) {
    if (value >= 0) { static_cast<void>(directory.release()); }
  }
  ~DirectoryFd() { if (value >= 0) { ::_close(value); } }
  DirectoryFd(const DirectoryFd&) = delete;
  DirectoryFd& operator=(const DirectoryFd&) = delete;
};

std::expected<state::StateDescriptor, Error> assetDirectory(const std::filesystem::path& path, bool packaged) {
  auto opened = state::openStatePathDirectory(path);
  if (!opened) { return opened; }
  BY_HANDLE_FILE_INFORMATION info{};
  if (!win::shape(opened->get(), true, info) || !win::trustedAsset(opened->get(), packaged)) {
    return fail(ErrorCode::UnsafePath, "Country database directory owner or permissions are unsafe");
  }
  return opened;
}

bool validApproval(const CountryModuleApproval& approval) {
  const auto hex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); };
  const auto version = [](char c) { return (c >= '0' && c <= '9') || c == '.'; };
  return approval.bytes != 0 && approval.bytes <= 64U * 1024U * 1024U && approval.sha256.size() == 64
      && std::ranges::all_of(approval.sha256, hex) && !approval.version.empty() && approval.version.size() <= 64
      && std::ranges::all_of(approval.version, version);
}

std::expected<std::string, Error> digest(state::StateHandle file, std::uint64_t length) {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!md || EVP_DigestInit_ex(md.get(), EVP_sha256(), nullptr) != 1) {
    return fail(ErrorCode::Io, "Cannot initialize country module authentication");
  }
  std::array<char, 64 * 1024> bytes;
  for (std::uint64_t offset = 0; offset < length;) {
    const auto read = state::readStateSequential(file, bytes.data(), std::min<std::uint64_t>(bytes.size(), length - offset));
    if (read <= 0 || EVP_DigestUpdate(md.get(), bytes.data(), static_cast<std::size_t>(read)) != 1) {
      return fail(ErrorCode::Io, "Cannot hash country module bytes");
    }
    offset += static_cast<std::uint64_t>(read);
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
  unsigned count = 0;
  if (EVP_DigestFinal_ex(md.get(), hash.data(), &count) != 1 || count != 32) {
    return fail(ErrorCode::Io, "Cannot finish country module authentication");
  }
  constexpr std::string_view hex = "0123456789abcdef";
  std::string result;
  result.reserve(count * 2);
  for (unsigned index = 0; index < count; ++index) {
    result += hex[hash[index] >> 4];
    result += hex[hash[index] & 15];
  }
  return result;
}

LoadResult bind(std::expected<plugins::Module, plugins::LoadError> loaded) {
  if (!loaded) { return std::unexpected(Error{ErrorCode::Unsupported, loaded.error().message}); }
  auto provider = CountryProvider::bind(std::move(*loaded));
  if (!provider) { return fail(ErrorCode::Unsupported, "Cannot bind approved country location provider"); }
  return std::move(*provider);
}

template<class Load> LoadResult loadWithAssets(const std::filesystem::path& path, bool packaged, Load load) {
  try {
    auto directory = assetDirectory(path, packaged);
    if (!directory) { return std::unexpected(directory.error()); }
    const DirectoryFd fd(std::move(*directory));
    if (fd.value < 0) { return fail(ErrorCode::Io, "Cannot supply country database directory to the plugin"); }
    const bt_plugin_options_v1 options{sizeof(bt_plugin_options_v1), BT_PLUGIN_ABI_V1, fd.value, 0, "0.0", nullptr, nullptr};
    return load(options);
  } catch (const std::bad_alloc&) {
    return fail(ErrorCode::Io, "Not enough memory to bind country provider");
  } catch (const std::filesystem::filesystem_error&) {
    return fail(ErrorCode::UnsafePath, "Invalid country asset path");
  }
}
} // namespace

std::expected<std::unique_ptr<CountryProvider>, Error> loadApprovedCountryProvider(
    const ProfileLease& lease, const std::filesystem::path& artifact, const CountryModuleApproval& approval,
    const std::filesystem::path& database, bool packaged) {
  if (!lease.valid()) { return fail(ErrorCode::Busy, "Country provider requires a held profile lease"); }
  if (!validApproval(approval)) { return fail(ErrorCode::InvalidFormat, "Invalid app-owned country module approval"); }
  return loadWithAssets(database, packaged, [&](const bt_plugin_options_v1& options) -> LoadResult {
    const auto parent = state::openStatePathDirectory(artifact.parent_path());
    if (!parent) { return std::unexpected(parent.error()); }
    const auto opened = files::openRelativeFile(parent->get(), artifact.filename().native(), FILE_GENERIC_READ,
        FILE_SHARE_READ, files::OpenExisting, files::NonDirectoryFile);
    if (!opened) { return fail(ErrorCode::Missing, "Cannot open approved country module"); }
    const state::StateDescriptor module(*opened);
    const auto before = state::stateStamp(module.get());
    if (!before || !before->regular || before->links != 1 || before->size <= 0
        || static_cast<std::uint64_t>(before->size) != approval.bytes || !win::trustedAsset(module.get(), true)) {
      return fail(ErrorCode::UnsafePath, "Country module differs from safe approved artifact shape");
    }
    const auto sha = digest(module.get(), approval.bytes);
    if (!sha) { return std::unexpected(sha.error()); }
    const auto after = state::stateStamp(module.get());
    if (*sha != approval.sha256 || !after || !state::sameStateSnapshot(*before, *after)) {
      return fail(ErrorCode::Conflict, "Country module bytes differ from app-owned approval");
    }
    return bind(plugins::Module::loadDescriptor(module.get(), "azlocprov", BT_CAP_LOCATION, options, approval.version, true));
  });
}

std::expected<std::unique_ptr<CountryProvider>, Error> loadBuiltinCountryProvider(
    const ProfileLease& lease, bt_plugin_entry_fn entry, const std::filesystem::path& database, bool packaged) {
  if (!lease.valid()) { return fail(ErrorCode::Busy, "Country provider requires a held profile lease"); }
  return loadWithAssets(database, packaged, [entry](const bt_plugin_options_v1& options) {
    return bind(plugins::Module::loadBuiltin(entry, "azlocprov", BT_CAP_LOCATION, options, "0.1.0"));
  });
}

std::expected<std::unique_ptr<CountryProvider>, Error> loadVerifiedCountryProvider(
    plugins::ModuleFileHandle module, const std::string& version, const std::filesystem::path& database) {
  return loadWithAssets(database, false, [&](const bt_plugin_options_v1& options) {
    return bind(plugins::Module::loadDescriptor(module, "azlocprov", BT_CAP_LOCATION, options, version));
  });
}
} // namespace biglybtc::storage

// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/plugin_host.hpp"
#include "file_io.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <memory>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool asciiToken(std::string_view text) {
  return !text.empty() && text.size() <= 64 && std::ranges::all_of(text, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '_' || c == '.' || c == '-';
  });
}

bool matchesModuleDigest(int fd, off_t size, std::string_view expected) {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!digest || EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1) { return false; }

  std::array<unsigned char, 32768> buffer{};
  off_t total = 0;
  for (;;) {
    const auto read = ::read(fd, buffer.data(), buffer.size());
    if (read < 0 && errno == EINTR) { continue; }
    if (read < 0) { return false; }
    if (read == 0) { break; }
    total += read;
    if (total > size || EVP_DigestUpdate(digest.get(), buffer.data(), static_cast<std::size_t>(read)) != 1) {
      return false;
    }
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> result{};
  unsigned count = 0;
  if (total != size || EVP_DigestFinal_ex(digest.get(), result.data(), &count) != 1 ||
      count * 2 != expected.size()) {
    return false;
  }
  constexpr char digits[] = "0123456789abcdef";
  for (unsigned index = 0; index < count; ++index) {
    if (expected[index * 2] != digits[result[index] >> 4] ||
        expected[index * 2 + 1] != digits[result[index] & 0x0f]) {
      return false;
    }
  }
  return true;
}
} // namespace

int main(int argc, char** argv) {
  if (argc != 5) { return 2; }
  const std::string_view expectedHash(argv[1]);
  const std::string_view id(argv[2]), version(argv[3]), requested(argv[4]);
  std::uint64_t capabilities = 0;
  const auto parsed = std::from_chars(requested.data(), requested.end(), capabilities);
  if (expectedHash.size() != 64 || !asciiToken(id) || !asciiToken(version) || parsed.ec != std::errc{} ||
      parsed.ptr != requested.end()) {
    return 2;
  }
  using biglybtc::storage::detail::Descriptor;
  const Descriptor root(::open(".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  struct stat directory{};
  if (root.get() < 0 || ::fstat(root.get(), &directory) != 0 || directory.st_uid != ::geteuid() ||
      (directory.st_mode & 0077) != 0) {
    return 2;
  }
  const Descriptor module(::openat(root.get(), "plugin.so", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  constexpr int maximumModule = 64 * 1024 * 1024;
  struct stat info{};
  if (module.get() < 0 || ::fstat(module.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_uid != ::geteuid() || info.st_nlink != 1 || (info.st_mode & 0777) != 0400 ||
      info.st_size <= 0 || info.st_size > maximumModule || !matchesModuleDigest(module.get(), info.st_size, expectedHash)) {
    return 2;
  }
  const bt_plugin_options_v1 options{sizeof(bt_plugin_options_v1), BT_PLUGIN_ABI_V1, root.get(), 0, "0.0", nullptr, nullptr};
  // On success the module is stopped and destroyed before the process exits.
  const auto started = biglybtc::plugins::Module::loadDescriptor(module.get(), id, capabilities, options, version);
  return started ? 0 : 1;
}

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "persistent_runtime_guard.hpp"
#include "persistent_runtime_config.hpp"
#include <libtorrent/version.hpp>

#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#if !BIGLYBTC_PERSISTENT_RUNTIME_BOUND
#error This fixture requires explicit configure-time source binding; default fallback tests remain separate.
#endif

namespace {
namespace fs = std::filesystem;
std::string actual;
std::string_view fault;
bool armed = false;
int descriptor = -1, opens = 0, originalOpens = 0, injected = 0;
std::size_t readBytes = 0;
mode_t actualMode = 0;
struct Restore {
  fs::path image, saved;
  bool changed = false;
  void restore() {
    if (!changed) { return; }
    std::error_code error;
    fs::remove(image, error);
    if (error) { throw std::runtime_error("Cannot remove owned replacement"); }
    fs::rename(saved, image);
    changed = false;
  }
  ~Restore() {
    if (changed) {
      std::error_code error;
      fs::remove(image, error);
      error.clear(); fs::rename(saved, image, error);
    }
  }
};
}

extern "C" int __real_open(const char*, int, ...);
extern "C" int __real_fstat(int, struct stat*);
extern "C" ssize_t __real_read(int, void*, size_t);
extern "C" ssize_t __real___read_chk(int, void*, size_t, size_t);
extern "C" int __wrap_open(const char* path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list arguments; va_start(arguments, flags);
    mode = static_cast<mode_t>(va_arg(arguments, int)); va_end(arguments);
  }
  if (armed && path && flags == (O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)) {
    if (std::strcmp(path, biglybtc::engine::detail::runtime_binding::artifact) == 0) {
      ++originalOpens; errno = EACCES; return -1;
    }
    if (actual == path) {
      ++opens;
      if (fault == "open") { ++injected; errno = EACCES; return -1; }
      descriptor = __real_open(path, flags);
      struct stat info{};
      if (descriptor >= 0 && !__real_fstat(descriptor, &info)) { actualMode = info.st_mode; }
      return descriptor;
    }
  }
  return (flags & O_CREAT) ? __real_open(path, flags, mode) : __real_open(path, flags);
}
extern "C" int __wrap_fstat(int fd, struct stat* info) {
  const auto result = __real_fstat(fd, info);
  if (armed && !result && fd == descriptor && fault == "inode") { info->st_ino ^= 1; ++injected; }
  return result;
}
extern "C" ssize_t __wrap_read(int fd, void* bytes, size_t count) {
  const auto result = __real_read(fd, bytes, count);
  if (armed && result > 0 && fd == descriptor) {
    readBytes += static_cast<std::size_t>(result);
    if (fault == "content" && !injected) { static_cast<unsigned char*>(bytes)[0] ^= 1; ++injected; }
  }
  return result;
}

// Preserve libc fortify bounds before applying the existing owned test seam.
extern "C" ssize_t __wrap___read_chk(int fd, void* bytes, size_t count, size_t bound) {
  const auto result = __real___read_chk(fd, bytes, count, bound);
  if (armed && result > 0 && fd == descriptor) {
    readBytes += static_cast<std::size_t>(result);
    if (fault == "content" && !injected) { static_cast<unsigned char*>(bytes)[0] ^= 1; ++injected; }
  }
  return result;
}

int main(int argc, char** argv) {
  try {
    if (argc != 4) { return 64; }
    const fs::path root(argv[1]); fault = argv[2];
    const std::string_view expectation(argv[3]);
    if (expectation != "bound" && expectation != "unbound") { return 64; }
    if (fault != "copy" && fault != "moved" && fault != "open" && fault != "inode"
        && fault != "content" && fault != "fifo" && fault != "replace" && fault != "missing"
        && fault != "strict" && fault != "alternate" && fault != "preload") { return 64; }
    struct stat area{};
    if (!root.is_absolute() || fs::canonical(root) != root || root.parent_path() != "/tmp"
        || !root.filename().string().starts_with("biglybtc-runtime-deploy-")
        || ::lstat(root.c_str(), &area) || !S_ISDIR(area.st_mode) || area.st_uid != ::getuid()
        || (area.st_mode & 0777) != 0700) { return 65; }
    const fs::path image = root / "lib/libtorrent-rasterbar.so.2.0.12";
    struct stat before{};
    if (::lstat(image.c_str(), &before) || !S_ISREG(before.st_mode) || before.st_nlink != 1
        || before.st_uid != ::getuid() || fs::canonical(image) != image) { return 66; }
    // Keep the linked DSO in DT_NEEDED despite --as-needed. Version is not admission.
    if (!libtorrent::version()) { return 67; }
    void* provider = ::dlopen("libtorrent-rasterbar.so.2.0", RTLD_NOW | RTLD_NOLOAD);
    if (!provider) { return 68; }
    void* exported = ::dlsym(provider, "_ZN10libtorrent7versionEv");
    Dl_info info{};
    const bool copiedOrigin = exported && ::dladdr(exported, &info) && info.dli_fname
        && fs::canonical(info.dli_fname) == image;
    ::dlclose(provider); // The executable's DT_NEEDED still owns the image.
    if (!copiedOrigin) { return 68; }
    actual = image.string();
    Restore cleanup{image, root / "mapped-original"};
    if (fault == "fifo" || fault == "replace" || fault == "missing") {
      fs::rename(image, cleanup.saved); cleanup.changed = true;
      if (fault == "fifo" && ::mkfifo(image.c_str(), 0600)) { return 69; }
      if (fault == "replace") { fs::copy_file(cleanup.saved, image); }
    }
    armed = true;
#if defined(BIGLYBTC_MAINLINE_ROUTING_DEPLOYMENT) && BIGLYBTC_MAINLINE_ROUTING_DEPLOYMENT
    const auto retainedPin = biglybtc::engine::detail::mainlineRoutingRuntimePin();
    const bool admitted = static_cast<bool>(retainedPin);
#elif defined(BIGLYBTC_ORIGINAL_IDENTITY_DEPLOYMENT) && BIGLYBTC_ORIGINAL_IDENTITY_DEPLOYMENT
    const auto retainedPin = biglybtc::engine::detail::managedTrackerResponseRuntimePin();
    const bool admitted = static_cast<bool>(retainedPin);
#elif defined(BIGLYBTC_MAINLINE_PORT_DEPLOYMENT) && BIGLYBTC_MAINLINE_PORT_DEPLOYMENT
    const auto retainedPin = biglybtc::engine::detail::mainlinePortRuntimePin();
    const bool admitted = static_cast<bool>(retainedPin);
#else
    const bool admitted = biglybtc::engine::detail::persistentRuntimeQualified();
#endif
    armed = false;
    if (admitted != (expectation == "bound") || originalOpens != 0) { return 1; }
    if ((fault == "copy" || fault == "moved") && (opens != 1 || !readBytes || !S_ISREG(actualMode))) { return 2; }
    if ((fault == "open" || fault == "inode" || fault == "content") && (opens != 1 || injected < 1)) { return 3; }
    if (fault == "content" && !readBytes) { return 4; }
    if (fault == "fifo" && (opens != 1 || !S_ISFIFO(actualMode) || readBytes)) { return 5; }
    if (fault == "replace" && (opens != 1 || !S_ISREG(actualMode) || readBytes)) { return 6; }
    if ((fault == "strict" || fault == "missing" || fault == "preload") && opens != 0) { return 7; }
    cleanup.restore();
    struct stat after{};
    if (::lstat(image.c_str(), &after) || after.st_dev != before.st_dev || after.st_ino != before.st_ino
        || after.st_size != before.st_size || after.st_mode != before.st_mode) { return 8; }
    std::cout << "case=" << fault << " admitted=" << admitted << " copied_opens=" << opens
        << " original_opens=" << originalOpens << " injected=" << injected << " real_bytes=" << readBytes
        << " mapped_copy=" << actual << '\n';
#if defined(BIGLYBTC_MAINLINE_ROUTING_DEPLOYMENT) && BIGLYBTC_MAINLINE_ROUTING_DEPLOYMENT
    std::cout << "proof=mainline_routing27 retained_pin=" << static_cast<bool>(retainedPin) << '\n';
#elif defined(BIGLYBTC_ORIGINAL_IDENTITY_DEPLOYMENT) && BIGLYBTC_ORIGINAL_IDENTITY_DEPLOYMENT
    std::cout << "proof=original_identity_tracker25 retained_pin=" << static_cast<bool>(retainedPin) << '\n';
#elif defined(BIGLYBTC_MAINLINE_PORT_DEPLOYMENT) && BIGLYBTC_MAINLINE_PORT_DEPLOYMENT
    std::cout << "proof=mainline20 retained_pin=" << static_cast<bool>(retainedPin) << '\n';
#endif
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 70;
  }
}

#include "persistent_session.hpp"
#include "persistent_runtime_config.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#if !BIGLYBTC_PERSISTENT_RUNTIME_BOUND
#error This negative driver belongs ONLY to the explicitly source-bound lane.
#endif

namespace {
enum class Fault { None, OpenFailure, Inode, Content, Fifo };
Fault fault = Fault::None;
int matchedDescriptor = -1;
int matchedOpens = 0, injected = 0;
char fifoDirectory[] = "/tmp/biglybtc-runtime-fifo-XXXXXX";
std::string fifoPath;
struct Cleanup {
  ~Cleanup() {
    if (!fifoPath.empty()) { ::unlink(fifoPath.c_str()); ::rmdir(fifoDirectory); }
  }
};
} // namespace

extern "C" int __real_open(const char*, int, ...);
extern "C" int __real_fstat(int, struct stat*);
extern "C" ssize_t __real_read(int, void*, size_t);
extern "C" ssize_t __real___read_chk(int, void*, size_t, size_t);
extern "C" int __wrap_open(const char* path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list arguments; va_start(arguments, flags); mode = static_cast<mode_t>(va_arg(arguments, int)); va_end(arguments);
  }
  if (path && std::strcmp(path, biglybtc::engine::detail::runtime_binding::artifact) == 0
      && flags == (O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)) {
    ++matchedOpens;
    if (fault == Fault::OpenFailure) { ++injected; errno = EACCES; return -1; }
    if (fault == Fault::Fifo) {
      matchedDescriptor = __real_open(fifoPath.c_str(), flags);
      if (matchedDescriptor >= 0) { ++injected; }
      return matchedDescriptor;
    }
    matchedDescriptor = __real_open(path, flags);
    return matchedDescriptor;
  }
  return (flags & O_CREAT) ? __real_open(path, flags, mode) : __real_open(path, flags);
}
extern "C" int __wrap_fstat(int descriptor, struct stat* info) {
  const auto result = __real_fstat(descriptor, info);
  if (!result && descriptor == matchedDescriptor && fault == Fault::Inode) {
    info->st_ino ^= 1; ++injected;
  }
  return result;
}
extern "C" ssize_t __wrap_read(int descriptor, void* bytes, size_t count) {
  const auto result = __real_read(descriptor, bytes, count);
  if (result > 0 && descriptor == matchedDescriptor && fault == Fault::Content && !injected) {
    static_cast<unsigned char*>(bytes)[0] ^= 1; ++injected;
  }
  return result;
}

// Preserve libc fortify bounds before applying the existing owned test seam.
extern "C" ssize_t __wrap___read_chk(int descriptor, void* bytes, size_t count, size_t bound) {
  const auto result = __real___read_chk(descriptor, bytes, count, bound);
  if (result > 0 && descriptor == matchedDescriptor && fault == Fault::Content && !injected) {
    static_cast<unsigned char*>(bytes)[0] ^= 1; ++injected;
  }
  return result;
}

int main(int argc, char** argv) {
  using namespace biglybtc::engine;
  if (argc != 2) { return 64; }
  const std::string_view mode(argv[1]);
  if (mode == "open") { fault = Fault::OpenFailure; }
  else if (mode == "inode") { fault = Fault::Inode; }
  else if (mode == "content") { fault = Fault::Content; }
  else if (mode == "fifo") { fault = Fault::Fifo; }
  else { return 64; }
  Cleanup cleanup;
  if (fault == Fault::Fifo) {
    if (!::mkdtemp(fifoDirectory)) { return 65; }
    fifoPath = std::string(fifoDirectory) + "/owned-fifo";
    if (::mkfifo(fifoPath.c_str(), 0600)) { return 66; }
  }
  const auto owner = PersistentSession::create({1, 2, 3});
  if (owner || owner.error() != AccountingError::DependencyUnqualified || matchedOpens != 1 || injected < 1) { return 1; }
  std::cout << "Synthetic owned I/O fault rejected; actual mapped LT image unchanged\n";
  return 0;
}

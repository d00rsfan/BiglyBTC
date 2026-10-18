#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

// Test-only Linux interposer. The exact generated pathname is provided only to
// the disposable native driver; all other writes pass directly to the kernel.
extern "C" ssize_t write(int descriptor, const void* data, size_t size) {
  static bool injected = false;
  const auto* target = std::getenv("BIGLYBTC_FAULT_SAVING");
  char link[64]{};
  char actual[4096]{};
  if (!injected && target && std::strncmp(target, "/tmp/biglybtc-", 14) == 0 && size > 16) {
    std::snprintf(link, sizeof(link), "/proc/self/fd/%d", descriptor);
    const auto length = ::readlink(link, actual, sizeof(actual) - 1);
    if (length > 0 && std::strcmp(actual, target) == 0) {
      injected = true;
      char retained[4096]{};
      const auto count = std::snprintf(retained, sizeof(retained), "%s.owned-partial", target);
      if (count < 0 || static_cast<size_t>(count) >= sizeof(retained)
          || ::syscall(SYS_write, descriptor, data, size_t{16}) != 16
          || ::rename(target, retained) != 0) {
        errno = EIO;
        return -1;
      }
      const auto replacement = static_cast<int>(::syscall(SYS_openat, AT_FDCWD, target,
          O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600));
      constexpr char marker[] = "FOREIGN RECOVERY BYTES\n";
      if (replacement < 0) { errno = EIO; return -1; }
      const auto written = ::syscall(SYS_write, replacement, marker, sizeof(marker) - 1);
      ::close(replacement);
      errno = written == static_cast<long>(sizeof(marker) - 1) ? ENOSPC : EIO;
      return -1;
    }
  }
  return static_cast<ssize_t>(::syscall(SYS_write, descriptor, data, size));
}

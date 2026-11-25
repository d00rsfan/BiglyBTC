// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "loaded_mapping_snapshot.hpp"

#include <sys/stat.h>

#if defined(__APPLE__)
#include <libproc.h>
#include <mach/vm_prot.h>
#include <limits>
#include <unistd.h>
#else
#include <sys/sysmacros.h>
#endif

namespace biglybtc::engine::detail {

inline bool matchesMappedFile(const LoadedMapping& mapping, const struct stat& file) noexcept {
  return mapping.inode == static_cast<std::uint64_t>(file.st_ino)
      && mapping.deviceMajor == static_cast<std::uint64_t>(major(file.st_dev))
      && mapping.deviceMinor == static_cast<std::uint64_t>(minor(file.st_dev));
}

inline std::optional<LoadedMappings> nativeLoadedMappings(
    std::span<const std::uintptr_t> addresses) noexcept {
#ifdef __APPLE__
  if (addresses.empty() || addresses.size() > MaximumMappedSymbols) { return {}; }
  LoadedMappings result{};
  const auto pid = ::getpid();
  for (std::size_t i = 0; i < addresses.size(); ++i) {
    // The kernel supplies the vnode itself, so a renamed or replaced image
    // cannot gain authority just by having the expected current pathname.
    struct proc_regionwithpathinfo info{};
    if (::proc_pidinfo(pid, PROC_PIDREGIONPATHINFO, addresses[i], &info, static_cast<int>(sizeof(info)))
        != static_cast<int>(sizeof(info))) { return {}; }
    const auto& region = info.prp_prinfo;
    const auto& file = info.prp_vip.vip_vi.vi_stat;
    constexpr auto protection = VM_PROT_READ | VM_PROT_EXECUTE;
    if ((region.pri_protection & protection) != protection
        || !S_ISREG(file.vst_mode) || !file.vst_ino
        || region.pri_address > addresses[i]
        || addresses[i] - region.pri_address >= region.pri_size
        || region.pri_size > std::numeric_limits<std::uintptr_t>::max() - region.pri_address) {
      return {};
    }
    const auto device = static_cast<dev_t>(file.vst_dev);
    result[i] = {region.pri_address, region.pri_address + region.pri_size, region.pri_offset,
        static_cast<std::uint64_t>(major(device)), static_cast<std::uint64_t>(minor(device)),
        file.vst_ino};
  }
  return result;
#else
  FILE* file = std::fopen("/proc/self/maps", "re");
  auto result = readLoadedMappings(file, addresses);
  if (file) { std::fclose(file); }
  return result;
#endif
}

} // namespace biglybtc::engine::detail

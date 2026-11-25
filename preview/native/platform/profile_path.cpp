// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_path.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

#include <memory>
#include <string>
#endif

namespace biglybtc::platform {

std::filesystem::path defaultBiglyBtProfile(const std::filesystem::path& homeDirectory) {
#ifdef _WIN32
  PWSTR rawFolder = nullptr;
  const auto status = ::SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DONT_VERIFY, nullptr, &rawFolder);
  const auto freeFolder = [](PWSTR folder) { ::CoTaskMemFree(folder); };
  const std::unique_ptr<wchar_t, decltype(freeFolder)> knownFolder(rawFolder, freeFolder);
  if (SUCCEEDED(status) && knownFolder && *knownFolder.get()) {
    return std::filesystem::path(knownFolder.get()) / L"BiglyBT";
  }
  const auto size = ::GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
  if (size) {
    std::wstring folder(size, L'\0');
    const auto actual = ::GetEnvironmentVariableW(L"APPDATA", folder.data(), size);
    if (actual && actual < size) {
      folder.resize(actual);
      const std::filesystem::path path(folder);
      if (path.is_absolute()) { return path / L"BiglyBT"; }
    }
  }
  return homeDirectory / L"Application Data" / L"BiglyBT";
#elif defined(__APPLE__)
  return homeDirectory / "Library" / "Application Support" / "BiglyBT";
#else
  return homeDirectory / ".biglybt";
#endif
}

} // namespace biglybtc::platform

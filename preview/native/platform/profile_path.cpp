// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_path.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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

bool isGeneratedTransferProfile(const std::filesystem::path& profile) {
  std::error_code error;
#ifdef _WIN32
  const auto temporary = std::filesystem::temp_directory_path(error);
  if (error) { return false; }
#else
  const std::filesystem::path temporary("/tmp");
#endif
  const auto root = std::filesystem::canonical(temporary, error).make_preferred();
  if (error || !profile.is_absolute() || profile.lexically_normal() != profile) { return false; }
  const auto actual = std::filesystem::canonical(profile, error).make_preferred();
  if (error || actual != profile) { return false; }
  const auto relative = actual.lexically_relative(root);
  const auto first = relative.begin();
  const auto prefix = std::filesystem::path("biglybtc-").native();
  return !relative.has_root_path() && first != relative.end() && first->native().compare(0, prefix.size(), prefix) == 0;
}

} // namespace biglybtc::platform

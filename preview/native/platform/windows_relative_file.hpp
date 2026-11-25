// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "windows_file_owner.hpp"
#include <winternl.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string_view>
#include <vector>

namespace biglybtc::platform::detail {

inline constexpr ULONG OpenExisting = 1;
inline constexpr ULONG CreateExclusive = 2;
inline constexpr ULONG DirectoryFile = 0x1;
inline constexpr ULONG WriteThrough = 0x2;
inline constexpr ULONG SynchronousIo = 0x20;
inline constexpr ULONG NonDirectoryFile = 0x40;
inline constexpr ULONG OpenReparsePoint = 0x00200000;

inline bool validRelativeLeaf(std::wstring_view leaf) noexcept {
  return !leaf.empty() && leaf.size() <= 255 && leaf != L"." && leaf != L".."
      && leaf.find_first_of(L"\\/:*?\"<>|") == leaf.npos && leaf.find(L'\0') == leaf.npos
      && leaf.back() != L'.' && leaf.back() != L' ';
}

struct RelativeFileFunctions {
  using CreateFileAt = decltype(&::NtCreateFile);
  using ErrorFromStatus = ULONG (NTAPI*)(NTSTATUS);
  using SetFileInformation = NTSTATUS (NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
  CreateFileAt open{};
  ErrorFromStatus translate{};
  SetFileInformation set{};
};

inline const RelativeFileFunctions& relativeFileFunctions() noexcept {
  static const auto functions = [] {
    const auto ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) { return RelativeFileFunctions{}; }
    return RelativeFileFunctions{
        std::bit_cast<RelativeFileFunctions::CreateFileAt>(::GetProcAddress(ntdll, "NtCreateFile")),
        std::bit_cast<RelativeFileFunctions::ErrorFromStatus>(::GetProcAddress(ntdll, "RtlNtStatusToDosError")),
        std::bit_cast<RelativeFileFunctions::SetFileInformation>(::GetProcAddress(ntdll, "NtSetInformationFile"))};
  }();
  return functions;
}

inline std::expected<HANDLE, DWORD> openRelativeFile(HANDLE directory, std::wstring_view leaf,
    ACCESS_MASK rights, ULONG sharing, ULONG disposition, ULONG options) noexcept {
  if (!validRelativeLeaf(leaf)) { return std::unexpected(ERROR_INVALID_NAME); }
  const auto& functions = relativeFileFunctions();
  if (!functions.open || !functions.translate) { return std::unexpected(ERROR_NOT_SUPPORTED); }

  const auto bytes = static_cast<USHORT>(leaf.size() * sizeof(wchar_t));
  UNICODE_STRING name{bytes, bytes, const_cast<wchar_t*>(leaf.data())};
  OBJECT_ATTRIBUTES attributes{};
  attributes.Length = sizeof(attributes);
  attributes.RootDirectory = directory;
  attributes.ObjectName = &name;
  attributes.Attributes = OBJ_CASE_INSENSITIVE;
  IO_STATUS_BLOCK io{};
  HANDLE handle = nullptr;
  const auto status = functions.open(&handle, rights | SYNCHRONIZE, &attributes, &io, nullptr,
      FILE_ATTRIBUTE_NORMAL, sharing, disposition, options | SynchronousIo | OpenReparsePoint, nullptr, 0);
  if (status < 0) { return std::unexpected(functions.translate(status)); }
  return handle;
}

inline std::expected<void, DWORD> renameRelativeFile(HANDLE source, HANDLE destinationDirectory,
    std::wstring_view leaf, bool replace) {
  if (!validRelativeLeaf(leaf)) { return std::unexpected(ERROR_INVALID_NAME); }
  const auto& functions = relativeFileFunctions();
  if (!functions.set || !functions.translate) { return std::unexpected(ERROR_NOT_SUPPORTED); }
  const auto bytes = leaf.size() * sizeof(wchar_t);
  const auto needed = sizeof(FILE_RENAME_INFO) + bytes;
  std::vector<std::uint64_t> storage((needed + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
  auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
  rename->ReplaceIfExists = replace;
  rename->RootDirectory = destinationDirectory;
  rename->FileNameLength = static_cast<DWORD>(bytes);
  std::memcpy(reinterpret_cast<std::byte*>(rename) + offsetof(FILE_RENAME_INFO, FileName), leaf.data(), bytes);
  // FileRenameInformation resolves this component against RootDirectory
  // directly, without converting it to a process-relative DOS path.
  constexpr auto FileRenameInformation = static_cast<FILE_INFORMATION_CLASS>(10);
  IO_STATUS_BLOCK io{};
  const auto status = functions.set(source, &io, rename, static_cast<ULONG>(needed), FileRenameInformation);
  if (status < 0) { return std::unexpected(functions.translate(status)); }
  return {};
}

} // namespace biglybtc::platform::detail

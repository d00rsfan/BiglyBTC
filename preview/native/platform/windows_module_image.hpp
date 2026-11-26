// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "module_symbols.hpp"
#include <psapi.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <limits>
#include <span>
#include <string_view>

namespace biglybtc::platform {
namespace windows_image {

inline std::wstring moduleName(HMODULE module) {
  std::wstring name(32768, L'\0');
  const auto size = ::GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size()));
  if (!size || size >= name.size()) { return {}; }
  name.resize(size);
  return name;
}

inline bool sameMappedPath(HANDLE file, HMODULE module) {
  std::wstring actual(32768, L'\0'), mapped(32768, L'\0');
  const auto a = ::GetFinalPathNameByHandleW(file, actual.data(), static_cast<DWORD>(actual.size()),
      VOLUME_NAME_NT | FILE_NAME_NORMALIZED);
  const auto b = ::GetMappedFileNameW(::GetCurrentProcess(), module, mapped.data(), static_cast<DWORD>(mapped.size()));
  if (!a || !b || a >= actual.size() || b >= mapped.size()) { return false; }
  if (::CompareStringOrdinal(actual.data(), static_cast<int>(a), mapped.data(), static_cast<int>(b), TRUE) == CSTR_EQUAL) {
    return true;
  }
  mapped.resize(b);
  // Some loaders return an NT DOS-device alias. Accept only a local drive
  // path and prove it names the same open file before using the image.
  if (!mapped.starts_with(L"\\??\\") || mapped.size() < 8 || mapped[5] != L':' || mapped[6] != L'\\'
      || !((mapped[4] >= L'A' && mapped[4] <= L'Z') || (mapped[4] >= L'a' && mapped[4] <= L'z'))) { return false; }
  mapped.replace(0, 4, L"\\\\?\\");
  const auto alias = ::CreateFileW(mapped.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (alias == INVALID_HANDLE_VALUE) { return false; }
  const std::unique_ptr<void, decltype(&::CloseHandle)> held(alias, ::CloseHandle);
  FILE_ID_INFO approved{}, candidate{};
  return ::GetFileInformationByHandleEx(file, FileIdInfo, &approved, sizeof(approved))
      && ::GetFileInformationByHandleEx(alias, FileIdInfo, &candidate, sizeof(candidate))
      && approved.VolumeSerialNumber == candidate.VolumeSerialNumber
      && std::memcmp(approved.FileId.Identifier, candidate.FileId.Identifier, sizeof(approved.FileId.Identifier)) == 0;
}

inline bool executableRange(HMODULE module, const void* address, std::size_t length) {
  auto start = reinterpret_cast<std::uintptr_t>(address);
  while (length) {
    MEMORY_BASIC_INFORMATION region{};
    if (::VirtualQuery(reinterpret_cast<const void*>(start), &region, sizeof(region)) != sizeof(region)
        || region.Type != MEM_IMAGE || region.State != MEM_COMMIT || region.AllocationBase != module
        || region.Protect != PAGE_EXECUTE_READ) { return false; }
    const auto begin = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    if (start < begin || start - begin >= region.RegionSize) { return false; }
    const auto available = std::min(length, region.RegionSize - (start - begin));
    if (available > std::numeric_limits<std::uintptr_t>::max() - start) { return false; }
    start += available;
    length -= available;
  }
  return true;
}

inline bool knownCode(HMODULE module, std::span<const std::byte> file) {
  const auto get = [&]<class T>(std::size_t where, T& value) {
    if (where > file.size() || sizeof(T) > file.size() - where) { return false; }
    std::memcpy(&value, file.data() + where, sizeof(T));
    return true;
  };
  IMAGE_DOS_HEADER dos{};
  if (!get(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) { return false; }
  const auto pe = static_cast<std::size_t>(dos.e_lfanew);
  IMAGE_NT_HEADERS64 image{};
  if (!get(pe, image) || image.Signature != IMAGE_NT_SIGNATURE
      || image.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
      || !(image.FileHeader.Characteristics & IMAGE_FILE_DLL)
      || image.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64)
      || !image.FileHeader.NumberOfSections || image.FileHeader.NumberOfSections > 96) { return false; }
#if defined(_M_ARM64) || defined(__aarch64__)
  constexpr WORD machine = IMAGE_FILE_MACHINE_ARM64;
#else
  constexpr WORD machine = IMAGE_FILE_MACHINE_AMD64;
#endif
  if (image.FileHeader.Machine != machine) { return false; }
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  if (image.OptionalHeader.SizeOfImage > std::numeric_limits<std::uintptr_t>::max() - base) { return false; }
  bool hasCode = false;
  for (std::size_t index = 0; index < image.FileHeader.NumberOfSections; ++index) {
    IMAGE_SECTION_HEADER section{};
    if (!get(pe + sizeof(image) + index * sizeof(section), section)) { return false; }
    if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) { continue; }
    // Only compare file-backed contents. Page padding and non-executable
    // relocations and import fixups belong to the Windows loader.
    const auto bytes = std::min(section.SizeOfRawData, section.Misc.VirtualSize);
    const auto raw = section.PointerToRawData;
    const auto address = section.VirtualAddress;
    if (!bytes || (section.Characteristics & IMAGE_SCN_MEM_WRITE) || raw > file.size() || bytes > file.size() - raw
        || address > image.OptionalHeader.SizeOfImage || bytes > image.OptionalHeader.SizeOfImage - address) {
      return false;
    }
    const auto memory = reinterpret_cast<const void*>(base + address);
    if (!executableRange(module, memory, bytes) || std::memcmp(memory, file.data() + raw, bytes) != 0) { return false; }
    hasCode = true;
  }
  return hasCode;
}

inline bool knownDigest(std::span<const std::byte> file, std::string_view approval) {
  if (approval.size() != 64) { return false; }
  std::array<unsigned char, 32> hash{};
  unsigned bytes = 0;
  if (EVP_Digest(file.data(), file.size(), hash.data(), &bytes, EVP_sha256(), nullptr) != 1 || bytes != hash.size()) {
    return false;
  }
  constexpr auto hex = "0123456789abcdef";
  for (std::size_t index = 0; index < hash.size(); ++index) {
    if (approval[2 * index] != hex[hash[index] >> 4] || approval[2 * index + 1] != hex[hash[index] & 15]) { return false; }
  }
  return true;
}
} // namespace windows_image

inline std::shared_ptr<void> qualifyWindowsModule(const void* linked, std::wstring_view basename,
    std::uint64_t bytes, std::string_view sha256, std::span<const char* const> exports) {
  if (!linked || !bytes || bytes > 64 * 1024 * 1024 || exports.empty()) { return {}; }
  HMODULE loaded = nullptr;
  if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(linked), &loaded)) { return {}; }
  const auto freeLibrary = [](void* value) { ::FreeLibrary(static_cast<HMODULE>(value)); };
  auto module = std::unique_ptr<void, decltype(freeLibrary)>(loaded, freeLibrary);
  const auto name = windows_image::moduleName(loaded);
  const auto actual = std::filesystem::path(name).filename().native();
  if (name.empty() || ::CompareStringOrdinal(actual.data(), static_cast<int>(actual.size()),
      basename.data(), static_cast<int>(basename.size()), TRUE) != CSTR_EQUAL) { return {}; }
  for (const auto* symbol : exports) {
    const auto candidate = loadedSymbol(loaded, symbol);
    if (!candidate || moduleContaining(candidate) != loaded || !windows_image::executableRange(loaded, candidate, 1)) { return {}; }
  }
  const auto raw = ::CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (raw == INVALID_HANDLE_VALUE) { return {}; }
  const auto close = [](void* value) { ::CloseHandle(value); };
  auto file = std::shared_ptr<void>(raw, close);
  BY_HANDLE_FILE_INFORMATION info{};
  if (::GetFileType(raw) != FILE_TYPE_DISK || !::GetFileInformationByHandle(raw, &info)
      || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || info.nNumberOfLinks != 1
      || ((std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow) != bytes
      || !windows_image::sameMappedPath(raw, loaded)) { return {}; }
  const std::unique_ptr<void, decltype(close)> mapping(::CreateFileMappingW(raw, nullptr, PAGE_READONLY, 0, 0, nullptr), close);
  if (!mapping) { return {}; }
  const auto unmap = [](const void* value) { ::UnmapViewOfFile(value); };
  const std::unique_ptr<const void, decltype(unmap)> view(::MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0), unmap);
  if (!view) { return {}; }
  const auto sealed = std::span(static_cast<const std::byte*>(view.get()), static_cast<std::size_t>(bytes));
  if (!windows_image::knownDigest(sealed, sha256) || !windows_image::knownCode(loaded, sealed)
      || windows_image::moduleName(loaded) != name || !windows_image::sameMappedPath(raw, loaded)) { return {}; }
  return std::shared_ptr<void>(module.release(), [file = std::move(file)](void* value) {
    static_cast<void>(file);
    ::FreeLibrary(static_cast<HMODULE>(value));
  });
}
} // namespace biglybtc::platform

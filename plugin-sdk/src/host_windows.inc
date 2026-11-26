// SPDX-License-Identifier: GPL-2.0-or-later
// Keep the selected file and its directory chain open while loading.
using File = ModuleFileHandle;
using HeldFile = std::unique_ptr<void, decltype(&::CloseHandle)>;

HeldFile openFile(const std::filesystem::path& path, bool directory = false) {
  const DWORD sharing = directory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ;
  const DWORD rights = directory ? FILE_READ_ATTRIBUTES : GENERIC_READ | READ_CONTROL;
  const DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0);
  const auto result = ::CreateFileW(path.c_str(), rights, sharing, nullptr, OPEN_EXISTING, flags, nullptr);
  return HeldFile(result == INVALID_HANDLE_VALUE ? nullptr : result, ::CloseHandle);
}

std::filesystem::path resolvedPath(File file) {
  constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
  const auto size = ::GetFinalPathNameByHandleW(file, nullptr, 0, flags);
  if (!size || size > 32768) { return {}; }
  std::wstring path(size, L'\0');
  const auto actual = ::GetFinalPathNameByHandleW(file, path.data(), size, flags);
  if (!actual || actual >= size) { return {}; }
  path.resize(actual);
  if (!path.starts_with(L"\\\\?\\") || path.size() < 7 || path[5] != L':' || path[6] != L'\\') { return {}; }
  // Hold and resolve DOS volume components one at a time.
  return std::filesystem::path(path.substr(4));
}

bool sameFile(File a, File b) {
  BY_HANDLE_FILE_INFORMATION left{}, right{};
  return a && b && ::GetFileInformationByHandle(a, &left) && ::GetFileInformationByHandle(b, &right)
      && left.dwVolumeSerialNumber == right.dwVolumeSerialNumber
      && left.nFileIndexHigh == right.nFileIndexHigh && left.nFileIndexLow == right.nFileIndexLow;
}

bool safeModuleFile(File file, bool allowSystemOwner) {
  BY_HANDLE_FILE_INFORMATION identity{};
  if (!file || ::GetFileType(file) != FILE_TYPE_DISK || !::GetFileInformationByHandle(file, &identity)
      || (identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
      || identity.nNumberOfLinks != 1) { return false; }

  HANDLE rawToken = nullptr;
  if (!::OpenThreadToken(::GetCurrentThread(), TOKEN_QUERY, TRUE, &rawToken)
      && (::GetLastError() != ERROR_NO_TOKEN || !::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken))) {
    return false;
  }
  const HeldFile token(rawToken, ::CloseHandle);
  DWORD capacity = 0;
  ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &capacity);
  if (!capacity) { return false; }
  std::vector<unsigned char> user(capacity);
  if (!::GetTokenInformation(token.get(), TokenUser, user.data(), capacity, &capacity)) { return false; }
  const auto userSid = reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid;

  PSID owner = nullptr;
  PACL permissions = nullptr;
  PSECURITY_DESCRIPTOR raw = nullptr;
  const auto read = ::GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
      &owner, nullptr, &permissions, nullptr, &raw);
  const std::unique_ptr<void, decltype(&::LocalFree)> security(raw, ::LocalFree);
  const auto system = [](PSID sid) {
    return ::IsWellKnownSid(sid, WinLocalSystemSid) || ::IsWellKnownSid(sid, WinBuiltinAdministratorsSid);
  };
  if (read != ERROR_SUCCESS || !owner || !permissions
      || (!::EqualSid(owner, userSid) && !(allowSystemOwner && system(owner)))) { return false; }
  constexpr DWORD write = GENERIC_ALL | GENERIC_WRITE | DELETE | WRITE_DAC | WRITE_OWNER
      | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_DELETE_CHILD;
  for (DWORD i = 0; i < permissions->AceCount; ++i) {
    void* entry = nullptr;
    if (!::GetAce(permissions, i, &entry)) { return false; }
    const auto type = static_cast<const ACE_HEADER*>(entry)->AceType;
    if (type == ACCESS_DENIED_ACE_TYPE) { continue; }
    if (type != ACCESS_ALLOWED_ACE_TYPE) { return false; }
    const auto* allowed = static_cast<const ACCESS_ALLOWED_ACE*>(entry);
    if ((allowed->Mask & write) == 0) { continue; }
    const auto sid = const_cast<DWORD*>(&allowed->SidStart);
    if (!::EqualSid(sid, userSid) && !system(sid)) { return false; }
  }
  return true;
}

std::optional<std::vector<HeldFile>> holdModuleParents(const std::filesystem::path& path) {
  std::vector<HeldFile> retained;
  auto current = path.root_path();
  for (const auto& component : path.parent_path().relative_path()) {
    if (retained.size() >= 128) { return {}; }
    current /= component;
    auto folder = openFile(current, true);
    BY_HANDLE_FILE_INFORMATION identity{};
    if (!folder || !::GetFileInformationByHandle(folder.get(), &identity)
        || (identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        || !(identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { return {}; }
    retained.push_back(std::move(folder));
  }
  return retained;
}

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>

#include <memory>
#include <vector>

namespace biglybtc::platform::detail {

inline bool ownedByCurrentUser(HANDLE file) {
  HANDLE rawToken = nullptr;
  if (!::OpenThreadToken(::GetCurrentThread(), TOKEN_QUERY, TRUE, &rawToken)
      && (::GetLastError() != ERROR_NO_TOKEN
          || !::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken))) {
    return false;
  }
  const auto closeToken = [](void* handle) { ::CloseHandle(handle); };
  const std::unique_ptr<void, decltype(closeToken)> token(rawToken, closeToken);
  PSID owner = nullptr;
  PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
  const auto status = ::GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
      &owner, nullptr, nullptr, nullptr, &rawDescriptor);
  const std::unique_ptr<void, decltype(&::LocalFree)> descriptor(rawDescriptor, ::LocalFree);
  if (status != ERROR_SUCCESS || !owner) { return false; }

  const auto matches = [&](TOKEN_INFORMATION_CLASS type, auto sid) {
    DWORD bytes = 0;
    ::GetTokenInformation(token.get(), type, nullptr, 0, &bytes);
    if (!bytes) { return false; }
    std::vector<unsigned char> result(bytes);
    return ::GetTokenInformation(token.get(), type, result.data(), bytes, &bytes)
        && ::EqualSid(owner, sid(result.data()));
  };
  return matches(TokenUser, [](const void* info) { return static_cast<const TOKEN_USER*>(info)->User.Sid; })
      || matches(TokenOwner, [](const void* info) { return static_cast<const TOKEN_OWNER*>(info)->Owner; });
}

} // namespace biglybtc::platform::detail

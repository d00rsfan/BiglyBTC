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

inline bool tokenMatchesFileSid(PSID sid, bool allowTokenOwner) {
  HANDLE rawToken = nullptr;
  if (!::OpenThreadToken(::GetCurrentThread(), TOKEN_QUERY, TRUE, &rawToken)
      && (::GetLastError() != ERROR_NO_TOKEN
          || !::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken))) {
    return false;
  }
  const auto closeToken = [](void* handle) { ::CloseHandle(handle); };
  const std::unique_ptr<void, decltype(closeToken)> token(rawToken, closeToken);
  const auto matches = [&](TOKEN_INFORMATION_CLASS type, auto tokenSid) {
    DWORD bytes = 0;
    ::GetTokenInformation(token.get(), type, nullptr, 0, &bytes);
    if (!bytes) { return false; }
    std::vector<unsigned char> result(bytes);
    return ::GetTokenInformation(token.get(), type, result.data(), bytes, &bytes)
        && ::EqualSid(sid, tokenSid(result.data()));
  };
  return matches(TokenUser, [](const void* info) { return static_cast<const TOKEN_USER*>(info)->User.Sid; })
      || (allowTokenOwner && matches(TokenOwner, [](const void* info) { return static_cast<const TOKEN_OWNER*>(info)->Owner; }));
}

inline bool ownedByCurrentUser(HANDLE file) {
  PSID owner = nullptr;
  PSECURITY_DESCRIPTOR raw = nullptr;
  const auto status = ::GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
      &owner, nullptr, nullptr, nullptr, &raw);
  const std::unique_ptr<void, decltype(&::LocalFree)> descriptor(raw, ::LocalFree);
  return status == ERROR_SUCCESS && owner && tokenMatchesFileSid(owner, true);
}

} // namespace biglybtc::platform::detail

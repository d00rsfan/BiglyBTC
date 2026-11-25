// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_lock.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {
using biglybtc::platform::ProfileLock;
using biglybtc::platform::ProfileLockError;

int exercise(const std::filesystem::path& path, bool hold) {
  auto lock = ProfileLock::acquire(path);
  if (!lock) {
    switch (lock.error()) {
      case ProfileLockError::Busy: return 2;
      case ProfileLockError::UnsafeLock: return 3;
      case ProfileLockError::MissingDirectory: return 4;
      default:
        std::cerr << "Profile lock error " << static_cast<int>(lock.error()) << '\n';
        return 5;
    }
  }
  if (!hold) { return 0; }

  auto retained = lock->retain();
  if (!retained) { return 6; }
  lock = std::unexpected(ProfileLockError::Changed);
  auto duplicate = ProfileLock::acquire(path);
  if (duplicate || duplicate.error() != ProfileLockError::Busy) { return 7; }

  std::cout << "held" << std::endl;
  std::string command;
  if (!(std::cin >> command) || command != "release") { return 8; }
  return 0;
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  return argc == 3 ? exercise(std::filesystem::path(argv[2]), std::wstring_view(argv[1]) == L"hold") : 9;
}
#else
int main(int argc, char** argv) {
  return argc == 3 ? exercise(std::filesystem::path(argv[2]), std::string_view(argv[1]) == "hold") : 9;
}
#endif

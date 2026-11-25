// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {
using biglybtc::storage::Error;
using biglybtc::storage::ErrorCode;
using biglybtc::storage::ProfileLease;

int exercise(const std::filesystem::path& path, bool hold) {
  auto lock = ProfileLease::acquire(path);
  if (!lock) {
    switch (lock.error().code) {
      case ErrorCode::Busy: return 2;
      case ErrorCode::UnsafePath: return 3;
      case ErrorCode::Missing: return 4;
      default:
        std::cerr << "Profile lock error " << lock.error().message << '\n';
        return 5;
    }
  }
  if (!hold) { return 0; }

  auto retained = lock->retain();
  if (!retained || !lock->sharesOwnership(**retained)) { return 6; }
  lock = std::unexpected(Error{ErrorCode::Conflict, "Probe released original lease"});
  auto duplicate = ProfileLease::acquire(path);
  if (duplicate || duplicate.error().code != ErrorCode::Busy) { return 7; }

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

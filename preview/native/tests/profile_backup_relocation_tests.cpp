// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_backup_relocation.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace biglybtc;
using storage::detail::relocateBackupConfiguration;

void check(bool valid, const char* message) {
  if (!valid) { throw std::runtime_error(message); }
}

std::string encode(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }

void verifyPaths() {
  const std::string previous = "/source/profile";
  const std::string target = "/restored/\u03a9";
  const std::string unrelated = "/source/profile-extra/content";
  const std::string binary("\0\xff", 2);
  const auto source = "d22:azureus.user.directory" + encode(previous + '/')
      + "6:nestedd" + encode(previous + "/metadata") + "l" + encode(previous)
      + encode(unrelated) + encode(binary) + "i48eeee";
  const auto result = relocateBackupConfiguration(source, previous + '/', target);
  check(result.has_value(), result ? "" : result.error().message.c_str());
  const auto document = core::Document::parse(*result);
  check(document && document->find("azureus.user.directory")
      && document->find("azureus.user.directory")->bytes == target + '/', "Relocate the original profile path");
  const auto* nested = document->find("nested");
  const auto* values = nested ? nested->find(target + "/metadata") : nullptr;
  check(values && values->children.size() == 4 && values->children[0].bytes == target
      && values->children[1].bytes == unrelated && values->children[2].bytes == binary
      && values->children[3].integer == 48, "Relocate nested paths and keep sibling paths and binary bytes");

  const std::string oldWindows = "C:\\Users\\Old\\Profile\\";
  const std::string windowsFile = oldWindows + "torrents\\local.torrent";
  const std::string newWindows = "D:\\Restored\\Profile";
  const auto windows = relocateBackupConfiguration("d4:path" + encode(windowsFile) + 'e', oldWindows, newWindows);
  check(windows && *windows == "d4:path" + encode(newWindows + "\\torrents\\local.torrent") + 'e',
      "Preserve Windows native directory separators");
  const auto crossPlatform = relocateBackupConfiguration("d4:path" + encode(windowsFile) + 'e', oldWindows, target);
  check(crossPlatform && *crossPlatform == "d4:path" + encode(target + "/torrents/local.torrent") + 'e',
      "Use the new filesystem's separators in relocated paths");
}

void verifyRefusals() {
  const auto duplicate = "d" + encode("/source/profile/entry") + "i1e"
      + encode("/target/profile/entry") + "i2ee";
  const auto collision = relocateBackupConfiguration(duplicate, "/source/profile", "/target/profile");
  check(!collision && collision.error().code == storage::ErrorCode::Conflict, "Keep overlapping configuration keys from overwriting each other");
  const auto malformed = relocateBackupConfiguration("unreadable", "/source/profile", "/target/profile");
  check(!malformed && malformed.error().code == storage::ErrorCode::InvalidFormat, "Refuse malformed configuration");
  const auto untouched = relocateBackupConfiguration("d4:node4:kept4:marki5ee", "/source/profile", "/target/profile");
  check(untouched && *untouched == "d4:node4:kept4:marki5ee", "Preserve exact bytes outside relocated values");
}
} // namespace

int main() try {
  verifyPaths();
  verifyRefusals();
  std::cout << "Backup path relocation and configuration preservation passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}

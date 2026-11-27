// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "biglybtc/library.hpp"

namespace biglybtc::core {

struct ArchivedDownloadMetadata {
  std::optional<std::string> name, savePath, category, tracker;
  std::int64_t size = 0;
  std::int64_t archivedMillis = 0, completedMillis = 0;
  std::int32_t shareRatio = -1;
  std::size_t files = 0;
  std::optional<std::vector<std::int64_t>> fileLengths{std::in_place};
  std::vector<std::string> tags;
};

struct ArchivedDownload {
  std::optional<TorrentHash> hash;
  ArchivedDownloadMetadata metadata;
};

} // namespace biglybtc::core

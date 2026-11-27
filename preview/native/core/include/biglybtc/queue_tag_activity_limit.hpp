#pragma once
#include <cstdint>

namespace biglybtc::core {
struct QueueTagActivityLimit {
  std::int32_t id, maximumDownloads, maximumSeeds;
  bool strict;
  bool operator==(const QueueTagActivityLimit&) const = default;
};
}

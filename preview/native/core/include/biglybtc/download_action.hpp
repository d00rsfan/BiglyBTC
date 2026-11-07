#pragma once
#include <cstddef>

namespace biglybtc::core {

// Saved library intent. Executing engine/payload actions is a separate Host duty.
enum class DownloadAction {
  Start, Stop, Pause, Resume, Queue, ForceStart, MoveTop, MoveUp, MoveDown, MoveBottom, CancelForceStart
};

inline constexpr std::size_t DownloadActionCount = 11;

} // namespace biglybtc::core

#pragma once

namespace biglybtc::core {

// Saved library intent. Executing engine/payload actions is a separate Host duty.
enum class DownloadAction {
  Start, Stop, Pause, Resume, Queue, ForceStart, MoveTop, MoveUp, MoveDown, MoveBottom
};

} // namespace biglybtc::core

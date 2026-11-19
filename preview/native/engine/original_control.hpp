#pragma once
#include "../adapters/original_control.hpp"

namespace biglybtc::engine::detail {
// Compatibility name for current engine owners;implementation belongs to the
// shared storage adapter and has no engine/SDK dependency.
using OriginalControl=storage::OriginalControl;
}

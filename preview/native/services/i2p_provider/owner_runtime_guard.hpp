// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
namespace biglybtc::services::i2p::provider {
// Pins the already-loaded approved31-export image; never dlopens a replacement.
std::shared_ptr<void> privateOwnerRuntimePin()noexcept;
}

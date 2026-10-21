#pragma once

namespace biglybtc::engine::detail {
// Private build/runtime admission, never a caller-supplied trust token. This is
// source/artifact binding only; causal qualification remains a separate gate.
bool persistentRuntimeQualified() noexcept;
}

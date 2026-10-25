#pragma once

#include <memory>

namespace biglybtc::engine::detail {
// Private build/runtime admission, never a caller-supplied trust token. This is
// source/artifact binding only; causal qualification remains a separate gate.
bool persistentRuntimeQualified() noexcept;
// Same full proof, plus the raw payload-counter and selected-swarm exports. This does not confer reset, storage-retirement or finality authority.
bool managedPayloadCounterRuntimeQualified() noexcept;
// Returns the SAME handle held throughout the complete mapped-image proof.
// It cannot unload between that proof and the admission/plugin lifetime pin.
// combined=true DEMANDS two additional original exports from this SAME fully
// source/artifact/hash/mapping-bound object. It never bypasses any admission.
std::shared_ptr<void> managedPayloadCounterRuntimePin(bool combined = false) noexcept;
}

#pragma once

#include <memory>

namespace biglybtc::engine::detail {
// Private build/runtime admission, never a caller-supplied trust token. This is
// source/artifact binding only; causal qualification remains a separate gate.
bool persistentRuntimeQualified() noexcept;
// Optional PORT ABI requires the complete new exact mapped-image proof. Old
// providers return absent; the caller retains this same pin through callbacks.
std::shared_ptr<void> mainlinePortRuntimePin() noexcept;
// Same full proof, plus the raw payload-counter and selected-swarm exports. This does not confer reset, storage-retirement or finality authority.
bool managedPayloadCounterRuntimeQualified() noexcept;
// Returns the SAME handle held throughout the complete mapped-image proof.
// It cannot unload between that proof and the admission/plugin lifetime pin.
// combined=true DEMANDS two additional original exports from this SAME fully
// source/artifact/hash/mapping-bound object. It never bypasses any admission.
std::shared_ptr<void> managedPayloadCounterRuntimePin(bool combined = false) noexcept;
// Existing complete proof plus both versioned queue observer exports from the
// SAME pinned image. No new source/artifact admission follows this declaration.
std::shared_ptr<void> managedQueueObserverRuntimePin() noexcept;
// Optional V3 requires the complete FIFTEEN-export same-image proof.
std::shared_ptr<void> managedExtendedTrafficRuntimePin() noexcept;
}

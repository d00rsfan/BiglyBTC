#pragma once

#include "biglybtc/bencode.hpp"

#include <array>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace biglybtc::core {

inline constexpr std::array<std::string_view, 13> ClassicPreferenceIds{
    "name", "state", "size", "progress", "download-rate", "upload-rate",
    "received", "sent", "hash", "saved-queue-position", "saved-selected-size",
    "saved-selected-remaining", "saved-selected-done"};

struct ClassicColumnPreference {
  bool visible = false;
  // Name is responsive Stretch and must omit width; all other columns must
  // carry a requested width16..8192, still subject to the UI's minimum.
  std::optional<int> width;
  bool operator==(const ClassicColumnPreference&) const = default;
};
struct ClassicPanePreferences {
  std::array<ClassicColumnPreference, 13> columns{}; // Logical catalog order.
  std::array<std::string, 13> order{}; // Exactly the known IDs, once each.
  std::string sortId;
  bool ascending = true;
  bool operator==(const ClassicPanePreferences&) const = default;
};
struct ClassicPreferences {
  ClassicPanePreferences incomplete;
  ClassicPanePreferences complete;
  bool operator==(const ClassicPreferences&) const = default;
};
enum class ClassicPreferenceError { Invalid, Unsupported, Limit, Memory, EditedDocument };

std::expected<void, ClassicPreferenceError> validateClassicPreferences(const ClassicPreferences&);

// Native-only schema under BiglyBTC.ClassicColumns.v1; original Java layout
// keys are never interpreted or edited. Missing namespace is a successful
// nullopt, not default preferences. Any other root key beginning with
// BiglyBTC.ClassicColumns, unknown private field or version is Unsupported.
// Malformed required known fields are Invalid. All-hidden is valid.
//
// Both operations require pristine, raw/tree-consistent input. Bounded iterative
// preflight precedes inspecting caller byte views, fresh parsing or recursion.
// Whole-document limits are4MiB/100000nodes/depth64; the private value is bounded
// by32KiB/1024nodes/depth8. Duplicate dictionaries retain the parser's rejection.
std::expected<std::optional<ClassicPreferences>, ClassicPreferenceError>
readClassicPreferences(const Document&);

// Validate any existing private namespace before replacement. Stage the edit
// and enforce whole/private bounds before a nonthrowing commit; failure leaves
// the supplied document intact. Lower inherited Document edit quotas also apply.
// Untouched key/value encodings and relative order remain byte-exact. Success may
// create an edited Document for serialize()/ConfigFile::save (an identical no-op
// can remain pristine). Freshly parse serialized output before reusing the codec.
// This pure codec performs no files, clocks, UI, lease or service operations.
std::expected<void, ClassicPreferenceError> writeClassicPreferences(Document&, const ClassicPreferences&);

} // namespace biglybtc::core

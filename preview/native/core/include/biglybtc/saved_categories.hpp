// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include <optional>
#include <variant>

namespace biglybtc::core {
enum class SavedCategoryError {Format,Root,List,Record,Name,ReservedName,DuplicateName,MissingCategory,Attributes,Assignment,Limit,Memory};
enum class SavedCategoryKind {User,All,Uncategorized};
inline constexpr std::string_view SavedAllCategory="__all__",SavedUncategorized="__uncategorised__";
struct SavedCategory {
  std::size_t sourceIndex=0;
  std::string name;
  SavedCategoryKind kind=SavedCategoryKind::User;
  // Original reads Long.intValue(), with absent limits defaulting to zero.
  // These are raw declarations, not current/applied limiter authority.
  std::optional<std::int64_t> rawMaxUpload,rawMaxDownload;
  bool attributesPresent=false;
};
struct SavedCategories {
  std::shared_ptr<const Document> source;
  bool listPresent=false;
  std::vector<SavedCategory> categories;
};
struct SavedCategoryLimits {
  Limits document{};
  std::size_t categories=4096,nameBytes=4096,textBytes=1024*1024;
};
// Strict fresh owning projection. Missing list means original built-in defaults,
// not two invented persisted records. Duplicated names, malformed known fields,
// invalid UTF8 and runtime-special-name collisions refuse without rewriting.
std::expected<SavedCategories,SavedCategoryError> readSavedCategories(
    std::string_view,SavedCategoryLimits={});
struct SavedCategoryCreate {std::string name;};
struct SavedCategoryDelete {std::string name;};
using SavedCategoryEdit=std::variant<SavedCategoryCreate,SavedCategoryDelete>;
// Value-only deletion work description. categoriesBefore is an owning raw
// preimage, NOT a file/lease capability or proof of inventory completeness.
struct SavedCategoryRemovalIntent {
  std::string name;
  std::shared_ptr<const Document> categoriesBefore;
};
struct SavedCategoryEditResult {
  SavedCategories value;
  bool changed=false;
  std::optional<std::string> createdName;
  // Definition deletion does NOT apply/claim any per-download cleanup.
  std::optional<SavedCategoryRemovalIntent> clearResolvedAssignments;
};
std::expected<SavedCategoryEditResult,SavedCategoryError> editSavedCategories(
    std::string_view,const SavedCategoryEdit&,SavedCategoryLimits={});

struct SavedCategoryAssignment {
  std::shared_ptr<const Document> source;
  std::optional<std::string> declaredName; // Exact attributes.category bytes.
  bool resolved=false; // Matching USER definition only; never a live DM join.
};
std::expected<SavedCategoryAssignment,SavedCategoryError> readSavedCategoryAssignment(
    std::string_view categoriesBytes,std::string_view stateBytes,SavedCategoryLimits={});
struct SavedCategoryAssignmentEditResult {
  SavedCategoryAssignment value;
  bool changed=false;
  // Present only for a changed attributes dictionary. Future held state writer
  // must bind its actual full hash/preimage and use its attributes-only CAS.
  std::optional<std::string> encodedAttributes;
};
// Destination absent means original setCategory(null). An unresolved existing
// name stays untouched: original category==null already, so setter is a no-op.
// Explicit assignment to an existing USER category can replace that old hint.
// No auto-create-on-assignment, built-in membership, file IO, clocks, payload,
// tag.config writes, listeners, rates/rules or network effects are performed.
std::expected<SavedCategoryAssignmentEditResult,SavedCategoryError> assignSavedCategory(
    std::string_view categoriesBytes,std::string_view stateBytes,
    const std::optional<std::string>& destination,SavedCategoryLimits={});
// Apply a deletion intent only to a supplied currently matching saved state;
// unrelated/unresolved names remain byte-exact. It cannot enumerate profiles,
// authorize a state file, or certify deletion across not-supplied inventory.
std::expected<SavedCategoryAssignmentEditResult,SavedCategoryError> clearDeletedCategoryAssignment(
    const SavedCategoryRemovalIntent&,std::string_view stateBytes,SavedCategoryLimits={});
} // namespace biglybtc::core

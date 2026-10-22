#pragma once

#include "biglybtc/overall_statistics.hpp"

#include <array>
#include <expected>
#include <memory>
#include <string_view>

namespace biglybtc::core {

enum class OverallDocumentError { InvalidDocument, InvalidAll, Limit, Memory };
enum class OverallFieldPresence { Missing, Integer, WrongType };
struct OverallDocument {
  std::shared_ptr<const Document> source;
  bool hasAll = false;
  OverallValues values{};
  std::array<OverallFieldPresence, SavedStatisticKeys.size()> presence{};
};

// Ordinary maxima 4 MiB/100000 nodes/depth64; limits can only be lowered.
// Input is raw bytes, so edited/higher-quota public Node trees are never trusted.
// Bencode integers correspond to original Long; absent/noninteger fields load
// Java zero with explicit provenance. Wrong-shaped all is rejected. Duplicated
// dictionary keys retain the existing parser's rejection policy.
std::expected<OverallDocument, OverallDocumentError> readOverallDocument(
    std::string_view bytes, Limits limits = {});

// Replace the 13 known integer values while retaining original root/all key
// order and every unknown subtree byte exactly. Missing fields are appended in
// SavedStatisticKeys order. This deliberate preservation differs from Java's
// fresh13-key save map, which drops unknown fields. Whole result is freshly
// bounded/parsed/owned before return; input is never mutated.
std::expected<OverallDocument, OverallDocumentError> prepareOverallDocument(
    std::string_view pristineBytes, const OverallValues& values, Limits limits = {});

} // namespace biglybtc::core

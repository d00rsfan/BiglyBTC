#include "biglybtc/saved_statistics.hpp"

#include <new>
#include <string>
#include <utility>

namespace biglybtc::core {

const SavedStatisticValue& SavedStatistics::get(SavedStatistic field) const {
  return values.at(static_cast<std::size_t>(field));
}

std::expected<SavedStatistics, SavedStatisticsError> readSavedStatistics(
    std::string_view bytes) {
  constexpr Limits limits;
  if (bytes.size() > limits.inputBytes) {
    return std::unexpected(SavedStatisticsError::Limit);
  }
  try {
    auto parsed = Document::parse(std::string(bytes), limits);
    if (!parsed) {
      const auto& code = parsed.error().code;
      const bool limit = code == "input_limit" || code == "string_limit"
          || code == "node_limit" || code == "depth_limit";
      return std::unexpected(limit ? SavedStatisticsError::Limit : SavedStatisticsError::InvalidDocument);
    }
    if (parsed->root().type != Node::Type::Dictionary) {
      return std::unexpected(SavedStatisticsError::InvalidDocument);
    }
    const auto* all = parsed->find("all");
    if (all && all->type != Node::Type::Dictionary) {
      return std::unexpected(SavedStatisticsError::InvalidAll);
    }
    SavedStatistics result;
    result.hasAll = all != nullptr;
    if (all) {
      for (std::size_t i = 0; i < SavedStatisticKeys.size(); ++i) {
        if (const auto* node = all->find(SavedStatisticKeys[i])) {
          if (node->type == Node::Type::Integer) { result.values[i].value = node->integer; }
          else { result.values[i].invalidType = true; }
        }
      }
    }
    result.source = std::make_shared<const Document>(std::move(*parsed));
    return result;
  } catch (const std::bad_alloc&) {
    return std::unexpected(SavedStatisticsError::Memory);
  }
}

std::optional<SavedSinceMark> deriveSavedSinceMark(
    std::optional<std::int64_t> total, std::optional<std::int64_t> baseline) {
  if (!total || !baseline || *total < 0 || *baseline < 0) { return std::nullopt; }
  if (*baseline > *total) { return SavedSinceMark{0, true}; }
  return SavedSinceMark{*total - *baseline, false};
}

} // namespace biglybtc::core

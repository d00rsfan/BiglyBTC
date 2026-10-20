#pragma once

namespace biglybtc::ui {

enum class SavedHistoryTraffic { Payload, Protocol, Dht };
constexpr bool validSavedHistoryTraffic(SavedHistoryTraffic value) noexcept {
  return value == SavedHistoryTraffic::Payload || value == SavedHistoryTraffic::Protocol
      || value == SavedHistoryTraffic::Dht;
}

} // namespace biglybtc::ui

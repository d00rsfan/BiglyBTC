#pragma once

#include <biglybtc/application_settings.hpp>
#include <QMetaType>
#include <QString>
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace biglybtc::ui {

struct OptionsRequest { quint64 session = 0, request = 0; };
enum class OptionsFieldState { Defaulted, Stored, InvalidType, OutOfRange, UnavailableDefault };
struct OptionsField {
  core::SettingId id;
  OptionsFieldState state;
  std::optional<core::SettingValue> value;
};
// Worker translates typed adapter facts; no Document, profile, or storage owner
// enters the GUI. Every catalog field occurs exactly once in a valid snapshot.
struct OptionsValues { std::vector<OptionsField> fields; bool originalClassicUi=false; };
struct OptionsPacket {
  quint64 session = 0, request = 0, revision = 0;
  std::shared_ptr<const OptionsValues> snapshot;
  bool restartRequired = false;
  QString failure;
};
using OptionsPresentation = std::shared_ptr<const OptionsPacket>;
struct OptionsSubmission {
  quint64 session = 0, request = 0, revision = 0;
  std::vector<core::SettingChange> changes; // Sparse, value-owned, catalog order.
};
struct OptionsSavePacket {
  quint64 session = 0, request = 0, revision = 0; // Original submitted revision.
  bool saved = false;
  bool restartRequired = false; // Actual cumulative worker policy, never inferred.
  // Success requires a fresh complete packet with this session/request and a
  // strictly newer revision. Failure retains the original GUI draft.
  OptionsPresentation snapshot;
  QString failure;
};
using OptionsSavePresentation = std::shared_ptr<const OptionsSavePacket>;

inline bool validOptionsValues(const OptionsValues& values) noexcept {
  const auto catalog = core::applicationSettingsCatalog();
  if (values.fields.size() != catalog.size()) { return false; }
  std::array<bool, static_cast<std::size_t>(core::SettingId::Count)> seen{};
  std::size_t textBytes = 0;
  for (const auto& item : values.fields) {
    const auto index = static_cast<std::size_t>(item.id);
    if (index >= catalog.size() || seen[index]) { return false; }
    seen[index] = true;
    const auto& field = catalog[index];
    if (item.value && std::holds_alternative<std::string>(*item.value)) {
      const auto size = std::get<std::string>(*item.value).size();
      if (size > 4 * 1024 * 1024 - textBytes) { return false; }
      textBytes += size;
    }
    const auto typed = [&] {
      if (!item.value) { return false; }
      switch (field.kind) {
        case core::SettingKind::Boolean: return std::holds_alternative<bool>(*item.value);
        case core::SettingKind::Integer: return std::holds_alternative<std::int64_t>(*item.value);
        case core::SettingKind::String: return std::holds_alternative<std::string>(*item.value);
      }
      return false;
    }();
    switch (item.state) {
      case OptionsFieldState::Stored:
        if (!typed || !core::validApplicationSettingValue(field, *item.value)) { return false; }
        break;
      case OptionsFieldState::Defaulted:
        if (!typed || !field.defaultValue || !core::validApplicationSettingValue(field, *item.value)) { return false; }
        if(field.id==core::SettingId::FirstPriorityIgnoreZeroPeers && values.originalClassicUi){
          if(std::get<bool>(*item.value)){return false;}
          break;
        }
        if (!std::visit([&](const auto& fallback) {
          using T = std::decay_t<decltype(fallback)>;
          if constexpr (std::is_same_v<T, std::string_view>) {
            const auto* actual = std::get_if<std::string>(&*item.value); return actual && *actual == fallback;
          } else {
            const auto* actual = std::get_if<T>(&*item.value); return actual && *actual == fallback;
          }
        }, *field.defaultValue)) { return false; }
        break;
      case OptionsFieldState::OutOfRange:
        if ((!typed && !(field.kind == core::SettingKind::Boolean && item.value
                && std::holds_alternative<std::int64_t>(*item.value)))
            || core::validApplicationSettingValue(field, *item.value)) { return false; }
        break;
      case OptionsFieldState::InvalidType:
        if (item.value) { return false; }
        break;
      case OptionsFieldState::UnavailableDefault:
        if (item.value || field.defaultValue) { return false; }
        break;
      default: return false;
    }
  }
  return true;
}
} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::OptionsRequest)
Q_DECLARE_METATYPE(biglybtc::ui::OptionsSubmission)
Q_DECLARE_METATYPE(biglybtc::ui::OptionsPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::OptionsSavePresentation)

#include "application_settings.hpp"

#include <array>
#include <cerrno>
#include <new>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>

namespace biglybtc::storage {
namespace {
constexpr const char* Filename = "biglybt.config";
Error conflict() { return {ErrorCode::Conflict, "Settings require the unchanged pristine primary or all-absent baseline"}; }
Error cancelled() { return {ErrorCode::Cancelled, "Settings operation cancelled"}; }
Error invalid(std::string_view key = {}) {
  return {ErrorCode::InvalidFormat, key.empty() ? "Invalid settings changes" : "Invalid settings field: " + std::string(key)};
}
std::expected<bool, Error> allAbsent(const ProfileLease& lease) {
  for (const auto* name : {Filename, "biglybt.config.bak", "biglybt.config.saving"}) {
    struct stat info{};
    if (::fstatat(lease.directoryHandle(), name, &info, AT_SYMLINK_NOFOLLOW) == 0) { return false; }
    if (errno != ENOENT) { return std::unexpected(Error{ErrorCode::Io, "Cannot inspect settings candidates"}); }
  }
  return true;
}
// Validate supplied public trees against a fresh bounded parser, without ever
// dereferencing a supplied bytes view. The raw original is the CAS authority.
bool pristine(const core::Document& supplied, const core::Document& bounded) {
  struct Frame { const core::Node* a; const core::Node* b; std::size_t next; };
  std::array<Frame, ApplicationSettingsLimits.depth + 1> stack{};
  std::size_t depth = 0, count = 0;
  const auto same = [&](const core::Node& a, const core::Node& b) {
    if (count == ApplicationSettingsLimits.nodes) { return false; } ++count;
    if (a.changed || a.origin || a.type != b.type || a.begin != b.begin || a.end != b.end
        || a.integer != b.integer || a.children.size() != b.children.size()
        || a.children.size() > ApplicationSettingsLimits.nodes - count) { return false; }
    if (b.type == core::Node::Type::Bytes) {
      const auto offset = static_cast<std::size_t>(b.bytes.data() - bounded.originalBytes().data());
      return a.bytes.size() == b.bytes.size() && a.bytes.data() == supplied.originalBytes().data() + offset;
    }
    return a.bytes.empty();
  };
  if (!same(supplied.root(), bounded.root())) { return false; }
  stack[0] = {&supplied.root(), &bounded.root(), 0};
  for (;;) {
    auto& frame = stack[depth];
    if (frame.next == frame.b->children.size()) { if (depth == 0) { return true; } --depth; continue; }
    if (depth == ApplicationSettingsLimits.depth) { return false; }
    const auto index = frame.next++;
    if (!same(frame.a->children[index], frame.b->children[index])) { return false; }
    ++depth; stack[depth] = {&frame.a->children[index], &frame.b->children[index], 0};
  }
}
ApplicationSettingsSnapshot project(std::shared_ptr<const core::Document> source, std::optional<RecoverySource> recovery) {
  ApplicationSettingsSnapshot result{std::move(source), recovery, {}};
  result.fields.reserve(core::applicationSettingsCatalog().size());
  for (const auto& field : core::applicationSettingsCatalog()) {
    ApplicationSetting value{field.id, SettingState::Defaulted, {}};
    const auto* node = result.source ? result.source->find(field.key) : nullptr;
    if (!node) {
      value.value = core::applicationSettingDefault(field);
      if (!value.value) { value.state = SettingState::UnavailableDefault; }
    } else {
      value.state = SettingState::InvalidType;
      if (field.kind == core::SettingKind::Boolean && node->type == core::Node::Type::Integer) {
        if (node->integer == 0 || node->integer == 1) { value.value = node->integer != 0; value.state = SettingState::Stored; }
        else { value.value = node->integer; value.state = SettingState::OutOfRange; }
      } else if (field.kind == core::SettingKind::Integer && node->type == core::Node::Type::Integer) {
        value.value = node->integer;
        value.state = core::validApplicationSettingValue(field, *value.value) ? SettingState::Stored : SettingState::OutOfRange;
      } else if (field.kind == core::SettingKind::String && node->type == core::Node::Type::Bytes) {
        value.value = std::string(node->bytes);
        value.state = core::validApplicationSettingValue(field, *value.value) ? SettingState::Stored : SettingState::OutOfRange;
      }
    }
    result.fields.push_back(std::move(value));
  }
  return result;
}
std::expected<core::Document, Error> baselineDocument(const ApplicationSettingsSnapshot& baseline) {
  if ((!baseline.source && baseline.recoverySource) || (baseline.source && !baseline.recoverySource)) {
    return std::unexpected(conflict());
  }
  const std::string_view bytes = baseline.source ? std::string_view(baseline.source->originalBytes()) : std::string_view("de");
  if (bytes.size() > ApplicationSettingsLimits.inputBytes) { return std::unexpected(invalid()); }
  auto fresh = core::Document::parse(std::string(bytes), ApplicationSettingsLimits);
  if (!fresh || fresh->root().type != core::Node::Type::Dictionary
      || (baseline.source && !pristine(*baseline.source, *fresh))) { return std::unexpected(conflict()); }
  return std::move(*fresh);
}
std::string encode(const core::SettingValue& value) {
  if (const auto* boolean = std::get_if<bool>(&value)) { return *boolean ? "i1e" : "i0e"; }
  if (const auto* integer = std::get_if<std::int64_t>(&value)) { return 'i' + std::to_string(*integer) + 'e'; }
  const auto& bytes = std::get<std::string>(value);
  return std::to_string(bytes.size()) + ':' + bytes;
}
} // namespace

std::expected<ApplicationSettingsSnapshot, Error> readApplicationSettings(const ProfileLease& lease, std::stop_token stop) {
  try {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto file = ConfigFile::load(lease, Filename, ApplicationSettingsLimits);
    if (!file) {
      if (file.error().code != ErrorCode::Missing) { return std::unexpected(file.error()); }
      auto absent = allAbsent(lease);
      if (!absent) { return std::unexpected(absent.error()); }
      if (!*absent) { return std::unexpected(Error{ErrorCode::InvalidFormat, "No complete settings candidate is readable"}); }
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      return project({}, {});
    }
    auto source = std::make_shared<const core::Document>(std::move(file->document()));
    auto result = project(std::move(source), file->recoverySource());
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return result;
  } catch (const std::bad_alloc&) { return std::unexpected(Error{ErrorCode::Io, "Insufficient memory for settings"}); }
}

std::expected<PreparedApplicationSettings, Error> prepareApplicationSettings(
    const ApplicationSettingsSnapshot& baseline, std::span<const core::SettingChange> changes) {
  try {
    if (changes.size() > core::applicationSettingsCatalog().size()) { return std::unexpected(invalid()); }
    auto fresh = baselineDocument(baseline);
    if (!fresh) { return std::unexpected(fresh.error()); }
    std::array<bool, static_cast<std::size_t>(core::SettingId::Count)> seen{};
    // First validate every requested change. Never prepare a partial submission.
    for (const auto& change : changes) {
      const auto* field = core::applicationSetting(change.id);
      if (!field || !core::validApplicationSettingValue(*field, change.value)) { return std::unexpected(invalid(field ? field->key : std::string_view{})); }
      const auto index = static_cast<std::size_t>(change.id);
      if (seen[index]) { return std::unexpected(Error{ErrorCode::InvalidFormat, "Duplicate settings field"}); }
      seen[index] = true;
    }
    bool changed = false;
    const auto effective = project(baseline.source, baseline.recoverySource);
    for (const auto& change : changes) {
      const auto& field = *core::applicationSetting(change.id);
      const auto& previous = effective.fields[static_cast<std::size_t>(change.id)];
      if ((previous.state == SettingState::Stored || previous.state == SettingState::Defaulted)
          && previous.value && *previous.value == change.value) { continue; }
      if (!fresh->replaceValue(field.key, encode(change.value))) { return std::unexpected(invalid(field.key)); }
      changed = true;
    }
    if (changed) {
      const auto bytes = fresh->serialize();
      if (bytes.size() > ApplicationSettingsLimits.inputBytes) { return std::unexpected(invalid()); }
      const auto checked = core::Document::parse(bytes, ApplicationSettingsLimits);
      if (!checked || checked->root().type != core::Node::Type::Dictionary) { return std::unexpected(invalid()); }
    }
    auto desired = std::make_shared<const core::Document>(std::move(*fresh));
    return PreparedApplicationSettings{baseline.source, baseline.recoverySource, std::move(desired), changed};
  } catch (const std::bad_alloc&) { return std::unexpected(Error{ErrorCode::Io, "Insufficient memory while preparing settings"}); }
}

std::expected<ApplicationSettingsSnapshot, Error> saveApplicationSettings(const ProfileLease& lease,
    const ApplicationSettingsSnapshot& baseline, const PreparedApplicationSettings& prepared,
    std::stop_token stop, void (*observer)(SaveStage)) {
  try {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!lease.valid()) { return std::unexpected(Error{ErrorCode::UnsafePath, "Held profile lease required for settings"}); }
    if (!prepared.document_ || baseline.source != prepared.baseline_ || baseline.recoverySource != prepared.source_
        || (baseline.source && baseline.recoverySource != RecoverySource::Primary)) { return std::unexpected(conflict()); }
    const auto verified = baselineDocument(baseline);
    if (!verified) { return std::unexpected(verified.error()); }
    const auto encoded = prepared.document().serialize();
    if (encoded.size() > ApplicationSettingsLimits.inputBytes) { return std::unexpected(invalid()); }
    auto parsed = core::Document::parse(encoded, ApplicationSettingsLimits);
    if (!parsed || parsed->root().type != core::Node::Type::Dictionary) { return std::unexpected(invalid()); }
    auto success = project(std::make_shared<const core::Document>(std::move(*parsed)), RecoverySource::Primary);
    if (baseline.missing()) {
      auto current = readApplicationSettings(lease, stop);
      if (!current) { return std::unexpected(current.error()); }
      if (!current->missing()) { return std::unexpected(conflict()); }
      if (!prepared.changed()) { return current; }
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      const auto created = ConfigFile::createMissing(lease, Filename, encoded, ApplicationSettingsLimits, observer);
      if (!created) { return std::unexpected(created.error()); }
      return success;
    }
    auto current = ConfigFile::load(lease, Filename, ApplicationSettingsLimits);
    if (!current) { return std::unexpected(current.error()); }
    if (current->recoverySource() != RecoverySource::Primary
        || current->document().originalBytes() != baseline.source->originalBytes()) { return std::unexpected(conflict()); }
    if (!prepared.changed()) {
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      return success;
    }
    current->document() = prepared.document(); // Original baseline retained for ConfigFile CAS.
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    const auto saved = current->save(lease, observer);
    if (!saved) { return std::unexpected(saved.error()); }
    return success; // Fully allocated before commit; no postpublication cancellation.
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error{ErrorCode::Io, "Settings allocation failed; inspect primary and recovery files if publication began"});
  }
}
} // namespace biglybtc::storage

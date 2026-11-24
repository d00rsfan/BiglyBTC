// SPDX-License-Identifier: GPL-2.0-or-later
#include "local_media_identity.hpp"

#include "application_settings.hpp"

namespace biglybtc::app {
namespace {
constexpr auto filename = "biglybt.config";
constexpr auto identityKey = "Plugin.azupnpav.uuid";

std::expected<QUuid, storage::Error> savedId(const core::Document& document) {
  using Code = storage::ErrorCode;
  const auto& root = document.root();
  if (root.type != core::Node::Type::Dictionary) {
    return std::unexpected(storage::Error{Code::InvalidFormat, "Media configuration requires a dictionary"});
  }
  const core::Node* field = nullptr;
  for (std::size_t i = 0; i + 1 < root.children.size(); i += 2) {
    if (root.children[i].bytes != identityKey) { continue; }
    if (field) {
      return std::unexpected(storage::Error{Code::Conflict, "Multiple saved media device identities"});
    }
    field = &root.children[i + 1];
  }
  if (!field || (field->type == core::Node::Type::Bytes && field->bytes.empty())) { return QUuid{}; }
  if (field->type == core::Node::Type::Bytes && field->bytes.size() == 41 && field->bytes.starts_with("uuid:")) {
    const auto id = QUuid::fromString(QString::fromLatin1(field->bytes.substr(5)));
    if (!id.isNull()) { return id; }
  }
  return std::unexpected(storage::Error{Code::InvalidFormat, "Invalid saved media device UUID"});
}

std::string encodedId(const QUuid& id) {
  return "41:uuid:" + id.toString(QUuid::WithoutBraces).toStdString();
}
} // namespace

std::expected<QUuid, storage::Error> loadOrCreateLocalMediaId(const storage::ProfileLease& lease) {
  using Code = storage::ErrorCode;
  auto configuration = storage::ConfigFile::load(lease, filename, storage::ApplicationSettingsLimits);
  if (!configuration) {
    if (configuration.error().code != Code::Missing) { return std::unexpected(configuration.error()); }
    auto document = core::Document::parse("de", storage::ApplicationSettingsLimits);
    const auto id = QUuid::createUuid();
    if (!document || !document->replaceValue(identityKey, encodedId(id))) {
      return std::unexpected(storage::Error{Code::InvalidFormat, "Cannot prepare the media device UUID"});
    }
    const auto created = storage::ConfigFile::createMissing(lease, filename, document->serialize(), storage::ApplicationSettingsLimits);
    if (!created) { return std::unexpected(created.error()); }
    return id;
  }
  const auto existing = savedId(configuration->document());
  if (!existing || !existing->isNull()) { return existing; }
  if (configuration->recoverySource() != storage::RecoverySource::Primary) {
    return std::unexpected(storage::Error{Code::Conflict, "Recover the settings before creating a media device UUID"});
  }
  const auto id = QUuid::createUuid();
  if (!configuration->document().replaceValue(identityKey, encodedId(id))) {
    return std::unexpected(storage::Error{Code::InvalidFormat, "Cannot prepare the media device UUID"});
  }
  const auto saved = configuration->save(lease);
  if (!saved) { return std::unexpected(saved.error()); }
  return id;
}

} // namespace biglybtc::app

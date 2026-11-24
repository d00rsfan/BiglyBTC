// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_activation_state.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <array>

namespace biglybtc::update {
namespace {
constexpr qsizetype maximumPointer = 192 * 1024;
using Error = PluginActivationError;

std::expected<std::optional<PluginActivationEntry>, Error> readEntry(
    const QJsonValue& value, QByteArrayView key, const PluginReleaseTarget& target, qint64 now) {
  if (value.isNull()) { return std::nullopt; }
  const auto object = value.toObject();
  const auto acceptedAt = object.value(QStringLiteral("acceptedAt")).toInteger();
  if (object.size() != 3 || acceptedAt <= 0 || acceptedAt > now) { return std::unexpected(Error::Format); }
  const auto payload = QByteArray::fromBase64Encoding(object.value(QStringLiteral("catalogue")).toString().toLatin1(),
      QByteArray::AbortOnBase64DecodingErrors);
  const auto signature = QByteArray::fromBase64Encoding(object.value(QStringLiteral("signature")).toString().toLatin1(),
      QByteArray::AbortOnBase64DecodingErrors);
  if (!payload || !signature) { return std::unexpected(Error::Format); }
  auto release = verifyPluginReleaseCatalogue(*payload, *signature, key, target, 0, acceptedAt);
  if (!release) { return std::unexpected(Error::Proof); }
  return PluginActivationEntry{std::move(*release), acceptedAt};
}

QJsonValue encodeEntry(const std::optional<PluginActivationEntry>& entry) {
  if (!entry) { return QJsonValue::Null; }
  return QJsonObject{{QStringLiteral("acceptedAt"), entry->acceptedAt},
      {QStringLiteral("catalogue"), QString::fromLatin1(entry->release.catalogue().toBase64())},
      {QStringLiteral("signature"), QString::fromLatin1(entry->release.signature().toBase64())}};
}

bool sameSlotArtifact(const PluginReleaseAsset& present, const PluginReleaseAsset& incoming) {
  return present.sha256 == incoming.sha256 && present.bytes == incoming.bytes &&
      present.nativeCommit == incoming.nativeCommit && present.upstreamCommit == incoming.upstreamCommit;
}

bool validSequence(const PluginActivationState& state) {
  if (state.highestSequence < 0 || (!state.active && state.previous)) { return false; }
  const auto present = [&](const auto& entry) { return !entry || entry->release.sequence() <= state.highestSequence; };
  return present(state.active) && present(state.previous) &&
      (!state.previous || state.previous->release.asset().version != state.active->release.asset().version);
}
} // namespace

std::expected<PluginActivationState, Error> readPluginActivationState(
    QByteArrayView encoded, QByteArrayView approvedPublicKey, const PluginReleaseTarget& target, qint64 now) {
  if (encoded.empty() || encoded.size() > maximumPointer || approvedPublicKey.size() != 32 || now <= 0) {
    return std::unexpected(Error::Format);
  }
  const auto object = QJsonDocument::fromJson(encoded.toByteArray()).object();
  const auto sequence = object.value(QStringLiteral("highestSequence")).toInteger(-1);
  if (object.size() != 4 || object.value(QStringLiteral("schema")).toInteger() != 2 || sequence < 0) {
    return std::unexpected(Error::Format);
  }
  auto active = readEntry(object.value(QStringLiteral("active")), approvedPublicKey, target, now);
  if (!active) { return std::unexpected(active.error()); }
  auto previous = readEntry(object.value(QStringLiteral("previous")), approvedPublicKey, target, now);
  if (!previous) { return std::unexpected(previous.error()); }
  PluginActivationState state{sequence, std::move(*active), std::move(*previous)};
  if (!validSequence(state)) { return std::unexpected(Error::Format); }
  return state;
}

QByteArray encodePluginActivationState(const PluginActivationState& state) {
  return QJsonDocument(QJsonObject{{QStringLiteral("schema"), 2},
      {QStringLiteral("highestSequence"), state.highestSequence}, {QStringLiteral("active"), encodeEntry(state.active)},
      {QStringLiteral("previous"), encodeEntry(state.previous)}}).toJson(QJsonDocument::Compact);
}

std::expected<PluginActivationState, Error> selectPluginActivation(
    const PluginActivationState& state, const VerifiedPluginRelease& release, qint64 now) {
  if (!validSequence(state) || now <= 0 || now >= release.expiresAt()) { return std::unexpected(Error::Format); }
  if (release.sequence() <= state.highestSequence) { return std::unexpected(Error::Replay); }
  for (const auto* entry : std::array{&state.active, &state.previous}) {
    if (!*entry) { continue; }
    const auto& recorded = (*entry)->release;
    if (recorded.target() != release.target()) { return std::unexpected(Error::Incompatible); }
    if (recorded.signingKey() != release.signingKey()) { return std::unexpected(Error::Proof); }
    if (recorded.asset().version == release.asset().version && !sameSlotArtifact(recorded.asset(), release.asset())) {
      return std::unexpected(Error::Conflict);
    }
  }
  const bool refresh = state.active && state.active->release.asset().version == release.asset().version;
  return PluginActivationState{release.sequence(), PluginActivationEntry{release, now},
      refresh ? state.previous : state.active};
}

std::expected<PluginActivationState, Error> selectPluginRollback(const PluginActivationState& state) {
  if (!validSequence(state)) { return std::unexpected(Error::Format); }
  if (!state.previous) { return std::unexpected(Error::NoPrevious); }
  return PluginActivationState{state.highestSequence, state.previous, state.active};
}

} // namespace biglybtc::update

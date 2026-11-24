// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_activation_state.hpp"
#include "plugin_release_test_signing.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

using namespace biglybtc::update;
using namespace biglybtc::update::test;

namespace {
constexpr qint64 now = 1795440000;

VerifiedPluginRelease admitted(const SigningFixture& signer, const PluginReleaseTarget& target,
    QString version, qint64 sequence, char digestDigit = '5') {
  const QJsonObject entry{{"id", target.runtimeId}, {"version", version}, {"platform", target.platform},
      {"qt", target.qtVersion}, {"minHostApi", 1}, {"maxHostApi", 1}, {"minHostAbi", 1}, {"maxHostAbi", 1},
      {"bytes", 100}, {"sha256", QString(64, QChar::fromLatin1(digestDigit))}, {"nativeCommit", QString(40, u'4')},
      {"upstreamCommit", QString(40, u'5')}, {"url", QStringLiteral("https://github.com/") + target.repository +
          QStringLiteral("/releases/download/v") + version + QStringLiteral("/fixture.tar.zst")}};
  const auto [data, signature] = signedCatalogue(signer, sequence, now + 120, {entry});
  auto release = verifyPluginReleaseCatalogue(data, signature, signer.publicKey(), target, 0, now);
  require(bool(release), "Create the original signed release fixture");
  return std::move(*release);
}
} // namespace

int main() try {
  using Error = PluginActivationError;
  const SigningFixture signer, differentSigner;
  const PluginReleaseTarget target{QStringLiteral("azlocprov"), QStringLiteral("d00rsfan/BiglyBTC-plugin-azlocprov"),
      QStringLiteral("linux-amd64"), QStringLiteral("6.12.0"), 1, 1};
  const auto first = admitted(signer, target, QStringLiteral("0.1.0"), 8);
  const auto second = admitted(signer, target, QStringLiteral("0.2.0"), 10);
  const auto updated = admitted(signer, target, QStringLiteral("0.2.0"), 11);
  const auto differentBytes = admitted(signer, target, QStringLiteral("0.2.0"), 12, '6');
  const auto differentProof = admitted(differentSigner, target, QStringLiteral("0.3.0"), 15);

  const PluginActivationState empty;
  const auto admittedFirst = selectPluginActivation(empty, first, now);
  require(admittedFirst && admittedFirst->highestSequence == 8 && admittedFirst->active &&
      admittedFirst->active->release.asset().version == u"0.1.0" && !admittedFirst->previous,
      "First activation saves the full release proof and sequence");
  const auto admittedSecond = selectPluginActivation(*admittedFirst, second, now + 12);
  require(admittedSecond && admittedSecond->highestSequence == 10 && admittedSecond->active &&
      admittedSecond->previous && admittedSecond->previous->release.asset().version == u"0.1.0",
      "Remember the previous validated version");
  const auto refresh = selectPluginActivation(*admittedSecond, updated, now + 13);
  require(refresh && refresh->active->release.sequence() == 11 && refresh->previous->release.sequence() == 8,
      "Refresh the same immutable version without erasing rollback");
  const auto conflict = selectPluginActivation(*refresh, differentBytes, now + 14);
  const auto foreign = selectPluginActivation(*refresh, differentProof, now + 14);
  require(!conflict && conflict.error() == Error::Conflict && !foreign && foreign.error() == Error::Proof,
      "Reject conflicting same-version bytes or foreign signing identity");
  const auto expired = selectPluginActivation(*admittedSecond, updated, now + 120);
  require(!expired && expired.error() == Error::Format, "Expiry still applies to new activations");

  const auto encoded = encodePluginActivationState(*refresh);
  const auto reopened = readPluginActivationState(encoded, signer.publicKey(), target, now + 3600);
  require(reopened && reopened->highestSequence == 11 &&
      reopened->active->acceptedAt == now + 13 && reopened->previous->release.asset().version == u"0.1.0",
      "Reopen the confirmed history after the original network catalogue expires");
  const auto rolledBack = selectPluginRollback(*reopened);
  require(rolledBack && rolledBack->highestSequence == 11 && rolledBack->active->release.asset().version == u"0.1.0" &&
      rolledBack->previous->release.asset().version == u"0.2.0", "Local rollback swaps only authenticated version identities");
  const auto replay = selectPluginActivation(*rolledBack, second, now + 20);
  require(!replay && replay.error() == Error::Replay, "Rollback preserves the highest admitted sequence");
  const auto missingPrevious = selectPluginRollback(*admittedFirst);
  require(!missingPrevious && missingPrevious.error() == Error::NoPrevious, "Refuse a missing prior installation");

  const auto wrongKey = readPluginActivationState(encoded, differentSigner.publicKey(), target, now + 3600);
  require(!wrongKey && wrongKey.error() == Error::Proof, "Reopen with only the approved signing key");
  auto document = QJsonDocument::fromJson(encoded).object();
  document.insert("highestSequence", 7);
  const auto regressedState = readPluginActivationState(QJsonDocument(document).toJson(QJsonDocument::Compact),
      signer.publicKey(), target, now + 3600);
  require(!regressedState && regressedState.error() == Error::Format, "Do not accept a lowered sequence below retained proofs");
  document.insert("highestSequence", 11);
  auto active = document.value("active").toObject();
  auto signature = active.value("signature").toString();
  signature[0] = signature[0] == u'A' ? u'B' : u'A';
  active.insert("signature", signature);
  document.insert("active", active);
  const auto changedProof = readPluginActivationState(QJsonDocument(document).toJson(QJsonDocument::Compact),
      signer.publicKey(), target, now + 3600);
  require(!changedProof && changedProof.error() == Error::Proof, "Reject altered retained signature bytes");
  std::cout << "PASS original signed activation proof, replay floor and rollback state\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}

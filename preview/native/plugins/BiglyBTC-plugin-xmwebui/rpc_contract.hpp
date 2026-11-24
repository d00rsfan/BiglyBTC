// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <expected>
#include <functional>
#include <optional>
#include <vector>
namespace biglybtc::plugins::rpc {
enum class OuterError { MissingMethod, WrongType, Permission, Limit, MissingDispatcher };
struct ExceptionInfo { QString className; std::optional<QString> message; };
struct Failure {
  enum class Kind { Permission, Textual, General } kind;
  std::optional<QString> message;
  std::vector<ExceptionInfo> causes;
};
struct Call {
  QString method; // JSON-RPC2 underscore names are normalized to original hyphens.
  QJsonObject arguments;
  bool jsonrpc2;
};
using DispatchResult = std::expected<std::optional<QJsonObject>, Failure>;
using Dispatcher = std::function<DispatchResult(const Call&)>;
// Owner-thread wrapper only. The caller supplies implemented host methods; this
// does not provide a listener, authentication, torrent access or fake RPC data.
std::expected<QJsonObject, OuterError> process(const std::optional<QJsonObject>& request,
    const Dispatcher& dispatcher);
using Headers = QMap<QString, QString>;
// Raw incoming tunnel claims are ignored. Only an already authenticated host
// tunnel adapter may supply trustedTunnel=true. Expected session is host-owned.
std::expected<bool, OuterError> sessionValid(const QString& path, const Headers& headers,
    bool checkCookie, const QString& expectedSession, bool trustedTunnel = false);
}

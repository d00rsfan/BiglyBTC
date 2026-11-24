// SPDX-License-Identifier: GPL-2.0-or-later
// Original XMWebUIPlugin contract at c9ab8f7cd62b2874f7385772992c0288a224e85d.
// Copyright2009 Vuze, Inc.; native host owns dispatch, errors and trust metadata.
#include "rpc_contract.hpp"
#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
namespace biglybtc::plugins::rpc {
namespace {
QString trim(QString text) {
  qsizetype first = 0, last = text.size();
  while (first < last && text[first].unicode() <= 32) { ++first; }
  while (last > first && text[last - 1].unicode() <= 32) { --last; }
  return text.mid(first, last - first);
}
QStringList split(const QString& text, QChar separator) {
  QStringList result;
  for (qsizetype position = 0; position < text.size();) {
    auto end = text.indexOf(separator, position);
    if (end < 0) { end = text.size(); }
    result.append(text.mid(position, end - position));
    position = end + 1;
  }
  return result;
}
QString cookie(const QString& text) {
  for (const auto& part : split(text, ';')) {
    const auto bits = split(part, '=');
    if (bits.size() == 2 && trim(bits[0]) == QStringLiteral("X-Transmission-Session-Id")) { return trim(bits[1]); }
  }
  return {};
}
bool bounded(const QJsonObject& request) {
  std::vector<std::pair<QJsonValue, unsigned>> work{{request, 0}};
  unsigned nodes = 0;
  while (!work.empty()) {
    auto [value, depth] = std::move(work.back()); work.pop_back();
    if (++nodes > 10000 || depth > 32) { return false; }
    if (value.isObject()) {
      const auto object = value.toObject();
      if (object.size() + work.size() > 10000) { return false; }
      for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key().size() > 1024 * 1024) { return false; }
        work.emplace_back(it.value(), depth + 1);
      }
    } else if (value.isArray()) {
      const auto array = value.toArray();
      if (array.size() + work.size() > 10000) { return false; }
      for (const auto& child : array) { work.emplace_back(child, depth + 1); }
    } else if (value.isString() && value.toString().size() > 1024 * 1024) { return false; }
  }
  return true;
}
QString message(const std::optional<QString>& text) { return text ? *text : QStringLiteral("null"); }
QString causeString(const ExceptionInfo& cause) {
  return cause.className + (cause.message ? QStringLiteral(": ") + *cause.message : QString{});
}
QString causeMessages(const std::vector<ExceptionInfo>& causes) {
  QStringList result;
  for (const auto& cause : causes) { result.append(cause.className.section('.', -1) + QStringLiteral(": ") + message(cause.message)); }
  return result.join(QStringLiteral(", "));
}
}
std::expected<QJsonObject, OuterError> process(const std::optional<QJsonObject>& request,
    const Dispatcher& dispatcher) {
  QJsonObject response;
  if (!request) { response.insert(QStringLiteral("result"), QStringLiteral("error: Bad or missing JSON string")); return response; }
  if (!bounded(*request)) { return std::unexpected(OuterError::Limit); }
  const auto jsonrpc = request->value(QStringLiteral("jsonrpc"));
  if (!jsonrpc.isNull() && !jsonrpc.isUndefined() && !jsonrpc.isString()) { return std::unexpected(OuterError::WrongType); }
  const bool rpc2 = jsonrpc.toString() == QStringLiteral("2.0");
  const auto method = request->value(QStringLiteral("method"));
  if (method.isNull() || method.isUndefined()) { return std::unexpected(OuterError::MissingMethod); }
  if (!method.isString()) { return std::unexpected(OuterError::WrongType); }
  const auto args = request->value(rpc2 ? QStringLiteral("params") : QStringLiteral("arguments"));
  if (!args.isNull() && !args.isUndefined() && !args.isObject()) { return std::unexpected(OuterError::WrongType); }
  if (rpc2) {
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    const auto id = request->value(QStringLiteral("id"));
    if (!id.isNull() && !id.isUndefined()) { response.insert(QStringLiteral("id"), id); }
  }
  if (!dispatcher) { return std::unexpected(OuterError::MissingDispatcher); }
  auto name = method.toString();
  if (rpc2) { name.replace('_', '-'); }
  const auto result = dispatcher(Call{std::move(name), args.toObject(), rpc2});
  if (result) {
    const auto object = *result ? **result : QJsonObject{};
    if (rpc2) { response.insert(QStringLiteral("result"), object); }
    else { response.insert(QStringLiteral("arguments"), object); response.insert(QStringLiteral("result"), QStringLiteral("success")); }
  } else {
    const auto& failure = result.error();
    if (failure.kind == Failure::Kind::Permission) { return std::unexpected(OuterError::Permission); }
    if (failure.kind == Failure::Kind::Textual) {
      if (rpc2) {
        QJsonValue text = failure.message ? QJsonValue(*failure.message) : QJsonValue(QJsonValue::Null);
        if (!failure.causes.empty()) { text = message(failure.message) + '/' + causeString(failure.causes.front()); }
        response.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), 1}, {QStringLiteral("message"), text}});
      } else {
        response.insert(QStringLiteral("result"), failure.message ? QJsonValue(*failure.message) : QJsonValue(QJsonValue::Null));
        if (!failure.causes.empty()) { response.insert(QStringLiteral("cause"), causeString(failure.causes.front())); }
      }
    } else {
      const auto text = causeMessages(failure.causes);
      if (rpc2) { response.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), text}}); }
      else { response.insert(QStringLiteral("result"), QStringLiteral("error: ") + text); }
    }
  }
  if (!rpc2) {
    const auto tag = request->value(QStringLiteral("tag"));
    if (!tag.isNull() && !tag.isUndefined()) { response.insert(QStringLiteral("tag"), tag); }
  }
  return response;
}
std::expected<bool, OuterError> sessionValid(const QString& path, const Headers& headers,
    bool checkCookie, const QString& expectedSession, bool trustedTunnel) {
  if (path.size() > 16384 || headers.size() > 128 || expectedSession.isEmpty() || expectedSession.size() > 128) {
    return std::unexpected(OuterError::Limit);
  }
  for (auto it = headers.begin(); it != headers.end(); ++it) {
    if (it.key().size() > 1024 || it.value().size() > 65536) { return std::unexpected(OuterError::Limit); }
  }
  if (!path.startsWith(QStringLiteral("/transmission/")) || trustedTunnel) { return true; }
  auto provided = headers.value(QStringLiteral("X-Transmission-Session-Id"));
  if (provided.isNull()) { provided = headers.value(QStringLiteral("x-transmission-session-id")); }
  if (provided.isNull() && checkCookie) { provided = cookie(headers.value(QStringLiteral("cookie"))); }
  return !provided.isNull() && provided == expectedSession;
}
}

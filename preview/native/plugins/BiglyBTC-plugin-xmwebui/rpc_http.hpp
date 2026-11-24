// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "rpc_contract.hpp"

#include <QByteArrayView>
#include <QTcpServer>
#include <set>

namespace biglybtc::plugins::rpc {

struct HttpCredentials {
  static std::optional<HttpCredentials> fromBasic(QByteArrayView user, QByteArrayView password);
  static std::optional<HttpCredentials> fromOriginal(QByteArrayView user, QByteArrayView sha1);
  bool valid() const;
  bool matches(QByteArrayView decodedBasic) const;

private:
  QByteArray digest_;
  QByteArray originalUser_;
};

// Optional one-request loopback transport. The host owns RPC dispatch and the
// supplied credentials. Incoming tunnel and session headers grant no trust.
class LocalRpcHttpServer final : public QObject {
public:
  explicit LocalRpcHttpServer(QObject* parent = nullptr);
  ~LocalRpcHttpServer() override;
  bool start(quint16 port, HttpCredentials, Dispatcher);
  void stop();
  quint16 port() const { return listener_.serverPort(); }

private:
  class Connection;
  void accept();
  QTcpServer listener_;
  HttpCredentials credentials_;
  QByteArray session_;
  Dispatcher dispatch_;
  std::set<Connection*> open_;
};

} // namespace biglybtc::plugins::rpc

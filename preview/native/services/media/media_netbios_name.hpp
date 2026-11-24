// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>
#include <QTimer>
#include <QUdpSocket>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>

namespace biglybtc::media {

QByteArray makeNetbiosNameQuery(std::uint16_t transaction);
std::optional<QString> decodeNetbiosName(QByteArrayView response, std::uint16_t transaction);

// A single explicit numeric endpoint on the owner worker. An empty cached
// string records a valid negative result or a timed-out request.
class MediaNetbiosName final : public QObject {
public:
  explicit MediaNetbiosName(QObject* parent = nullptr);
  bool request(const QHostAddress& peer, std::uint16_t port, std::chrono::milliseconds timeout);
  std::optional<QString> name(std::uint16_t port) const;
  void clear();

private:
  void accept();
  void settle(QString);

  QUdpSocket socket_;
  QTimer timer_;
  std::uint16_t activePort_ = 0;
  std::uint16_t transaction_ = 0;
  std::map<std::uint16_t, QString> results_;
};

} // namespace biglybtc::media

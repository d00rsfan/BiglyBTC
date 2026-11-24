// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_netbios_name.hpp"

#include <QAbstractEventDispatcher>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QThread>
#include <QtEndian>
#include <algorithm>

namespace biglybtc::media {
namespace {
using namespace std::chrono_literals;
constexpr qsizetype maximumPacket = 1024;
constexpr std::size_t maximumResults = 128;
constexpr qsizetype headerSize = 12;
constexpr qsizetype labelSize = 34;
constexpr qsizetype recordHeaderSize = 10;
constexpr qsizetype tableEntrySize = 18;
constexpr std::uint16_t nodeStatusType = 0x21;
constexpr std::uint16_t internetClass = 1;

std::uint16_t word(QByteArrayView data, qsizetype offset) {
  return qFromBigEndian<std::uint16_t>(data.data() + offset);
}

bool label(QByteArrayView raw) {
  return raw.size() == labelSize && raw.front() == 32 && raw.back() == 0 &&
      std::ranges::all_of(raw.sliced(1, 32), [](char ch) { return ch >= 'A' && ch <= 'P'; });
}
} // namespace

QByteArray makeNetbiosNameQuery(std::uint16_t transaction) {
  QByteArray query(headerSize, '\0');
  qToBigEndian(transaction, query.data());
  qToBigEndian<std::uint16_t>(1, query.data() + 4);
  query += char(32);
  query += "CK"; // '*' in the first-level NetBIOS name encoding.
  query += QByteArray(30, 'A');
  query.append(QByteArrayView("\0\0\x21\0\1", 5));
  return query;
}

std::optional<QString> decodeNetbiosName(QByteArrayView response, std::uint16_t transaction) {
  if (response.size() < headerSize || response.size() > maximumPacket || word(response, 0) != transaction) {
    return std::nullopt;
  }
  const auto flags = word(response, 2);
  const auto questions = word(response, 4);
  if ((flags & 0xfa0f) != 0x8000 || questions > 1 || word(response, 6) != 1 ||
      word(response, 8) != 0 || word(response, 10) != 0) {
    return std::nullopt;
  }
  auto record = response.sliced(headerSize);
  if (questions == 1) {
    constexpr auto questionSize = labelSize + 4;
    const auto expected = makeNetbiosNameQuery(transaction);
    if (record.size() < questionSize || record.first(questionSize) != QByteArrayView(expected).sliced(headerSize)) {
      return std::nullopt;
    }
    record = record.sliced(questionSize);
  }
  if (questions == 1 && record.startsWith(QByteArrayView("\xc0\x0c", 2))) {
    record = record.sliced(2);
  } else {
    if (record.size() < labelSize || !label(record.first(labelSize))) {
      return std::nullopt;
    }
    record = record.sliced(labelSize);
  }
  if (record.size() <= recordHeaderSize || word(record, 0) != nodeStatusType || word(record, 2) != internetClass ||
      record.size() != recordHeaderSize + word(record, 8)) {
    return std::nullopt;
  }
  const auto table = record.sliced(recordHeaderSize);
  const auto count = static_cast<unsigned char>(table.front());
  if (count > 32 || table.size() < 1 + count * tableEntrySize + 6) {
    return std::nullopt;
  }
  if (count == 0) {
    return QString{};
  }
  auto first = table.sliced(1, 15);
  while (!first.empty() && first.back() == ' ') {
    first = first.first(first.size() - 1);
  }
  if (!std::ranges::all_of(first, [](unsigned char ch) { return ch >= 32 && ch <= 126; })) {
    return std::nullopt;
  }
  return QString::fromLatin1(first);
}

MediaNetbiosName::MediaNetbiosName(QObject* parent) : QObject(parent), socket_(this), timer_(this) {
  timer_.setSingleShot(true);
  connect(&socket_, &QUdpSocket::readyRead, this, [this] { accept(); });
  connect(&timer_, &QTimer::timeout, this, [this] { settle({}); });
}

bool MediaNetbiosName::request(const QHostAddress& peer, std::uint16_t port, std::chrono::milliseconds timeout) {
  if (QThread::currentThread() != thread() || !QAbstractEventDispatcher::instance() ||
      peer != QHostAddress::LocalHost || port == 0 || timeout < 100ms || timeout > 20s) {
    return false;
  }
  if (results_.contains(port) || activePort_ == port) {
    return true;
  }
  if (activePort_ != 0 || results_.size() >= maximumResults ||
      !socket_.bind(QHostAddress::LocalHost, 0, QUdpSocket::DontShareAddress)) {
    return false;
  }
  transaction_ = static_cast<std::uint16_t>(QRandomGenerator::global()->bounded(1u, 65536u));
  activePort_ = port;
  const auto packet = makeNetbiosNameQuery(transaction_);
  if (socket_.writeDatagram(packet, peer, port) != packet.size()) {
    settle({});
    return false;
  }
  timer_.start(timeout);
  return true;
}

std::optional<QString> MediaNetbiosName::name(std::uint16_t port) const {
  const auto found = results_.find(port);
  return found == results_.end() ? std::nullopt : std::optional{found->second};
}

void MediaNetbiosName::clear() {
  timer_.stop();
  activePort_ = 0;
  socket_.close();
  results_.clear();
}

void MediaNetbiosName::accept() {
  for (unsigned turn = 0; turn < 16 && socket_.hasPendingDatagrams(); ++turn) {
    const auto packet = socket_.receiveDatagram(maximumPacket + 1);
    if (packet.senderAddress() == QHostAddress::LocalHost && packet.senderPort() == activePort_) {
      if (auto decoded = decodeNetbiosName(packet.data(), transaction_)) {
        settle(std::move(*decoded));
        return;
      }
    }
  }
}

void MediaNetbiosName::settle(QString name) {
  if (activePort_ != 0) {
    results_.insert_or_assign(activePort_, std::move(name));
    activePort_ = 0;
  }
  timer_.stop();
  socket_.close();
}

} // namespace biglybtc::media

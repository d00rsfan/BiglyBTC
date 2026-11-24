// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QHostAddress>
#include <optional>

namespace biglybtc::media {

class MediaLanEndpoint final {
public:
  static std::optional<MediaLanEndpoint> fromInterface(int interfaceIndex, const QHostAddress& localAddress);

  int interfaceIndex() const { return interfaceIndex_; }
  const QHostAddress& address() const { return address_; }
  bool accepts(const QHostAddress&) const;
  bool available() const;

private:
  MediaLanEndpoint(int index, QHostAddress address, int prefix)
      : interfaceIndex_(index), address_(std::move(address)), prefix_(prefix) {}

  int interfaceIndex_;
  QHostAddress address_;
  int prefix_;
};

} // namespace biglybtc::media

// SPDX-License-Identifier: GPL-2.0-or-later
#include "media_lan_endpoint.hpp"

#include <QNetworkInterface>

namespace biglybtc::media {
namespace {

bool localIpv4(const QHostAddress& address) {
  return address.protocol() == QAbstractSocket::IPv4Protocol &&
      (address.isPrivateUse() || address.isLinkLocal() || address == QHostAddress::LocalHost);
}

} // namespace

std::optional<MediaLanEndpoint> MediaLanEndpoint::fromInterface(int index, const QHostAddress& address) {
  if (index <= 0 || !localIpv4(address)) {
    return std::nullopt;
  }
  const auto interface = QNetworkInterface::interfaceFromIndex(index);
  const auto flags = interface.flags();
  if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning) ||
      (address != QHostAddress::LocalHost && !flags.testFlag(QNetworkInterface::CanMulticast))) {
    return std::nullopt;
  }
  for (const auto& entry : interface.addressEntries()) {
    if (entry.ip() == address && entry.prefixLength() >= 8 && entry.prefixLength() < 32) {
      return MediaLanEndpoint(index, address, entry.prefixLength());
    }
  }
  return std::nullopt;
}

bool MediaLanEndpoint::accepts(const QHostAddress& peer) const {
  if (address_ == QHostAddress::LocalHost) {
    return peer == address_;
  }
  if (!localIpv4(peer) || peer.isLoopback() || !peer.isInSubnet(address_, prefix_)) {
    return false;
  }
  if (prefix_ == 31) {
    return true;
  }
  const auto hostMask = 0xffffffffu >> prefix_;
  const auto suffix = peer.toIPv4Address() & hostMask;
  return suffix != 0 && suffix != hostMask;
}

bool MediaLanEndpoint::available() const {
  const auto current = fromInterface(interfaceIndex_, address_);
  return current && current->prefix_ == prefix_;
}

} // namespace biglybtc::media

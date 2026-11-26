// SPDX-License-Identifier: GPL-2.0-or-later
#include <libtorrent/biglybtc_mainline_identity.hpp>
#include <libtorrent/biglybtc_mainline_routing.hpp>
#include <libtorrent/biglybtc_mixed_i2p_transport.hpp>
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#include <libtorrent/biglybtc_public_udp.hpp>
#include <libtorrent/biglybtc_tag_gate.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>


namespace {
namespace lt = libtorrent;
namespace asio = boost::asio;
using Udp = asio::ip::udp;
using Status = lt::BiglybtcPublicUdpStatusV1;

constexpr std::array<char, 32> azureusFrame() {
  std::array<char, 32> frame{};
  frame[0] = static_cast<char>(0x80);
  frame[10] = 4;
  frame[16] = 55;
  return frame;
}

class Observer final : public lt::BiglybtcPublicUdpObserverV1 {
public:
  Observer(std::uint16_t peer, std::uint16_t local) : peer_(peer), local_(local) {}
  bool source_current() const noexcept override { return true; }
  void receive(const lt::BiglybtcPublicUdpPacketV1& packet) noexcept override {
    constexpr auto frame = azureusFrame();
    if (packet.family == 4 && packet.peer_port == peer_ && packet.local_port == local_
        && packet.size == frame.size() && std::memcmp(packet.bytes, frame.data(), frame.size()) == 0) {
      received.store(true, std::memory_order_release);
    }
  }
  std::atomic<bool> received{false};
private:
  std::uint16_t peer_, local_;
};

bool sharedSocketRoutesBothProtocols(lt::session& session) {
  asio::io_context io;
  const auto address = asio::ip::make_address("127.0.0.1");
  Udp::socket other(io, Udp::endpoint(address, 0));
  const Udp::endpoint local(address, session.listen_port());
  const auto observer = std::make_shared<Observer>(other.local_endpoint().port(), local.port());
  const std::shared_ptr<lt::BiglybtcPublicUdpObserverV1> callback = observer;
  std::array<lt::BiglybtcPublicUdpBindingV1, 16> bindings{};
  std::uint64_t token = 0;
  std::uint32_t count = 0;
  if (lt::biglybtc_attach_public_udp_v1(&session, &callback, local.port(), 500,
      &token, bindings.data(), bindings.size(), &count) != static_cast<std::uint32_t>(Status::Accepted)
      || !count || !token || bindings[0].port != local.port()) {
    return false;
  }

  constexpr auto frame = azureusFrame();
  constexpr std::string_view ping = "d1:ad2:id20:aaaaaaaaaaaaaaaaaaaae1:q4:ping1:t2:zz1:y1:qe";
  other.non_blocking(true);
  other.send_to(asio::buffer(frame), local);
  other.send_to(asio::buffer(ping), local);
  bool mainline = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    std::array<char, 2048> message{};
    boost::system::error_code error;
    Udp::endpoint sender;
    const auto size = other.receive_from(asio::buffer(message), sender, 0, error);
    if (!error && sender == local) {
      const auto response = std::string_view(message.data(), size);
      mainline = mainline || (response.starts_with('d') && response.find("1:t2:zz") != response.npos);
    }
    if (mainline && observer->received.load(std::memory_order_acquire)) { break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
  }
  const auto detached = lt::biglybtc_detach_public_udp_v1(&session, token, 500);
  return mainline && observer->received.load(std::memory_order_acquire)
      && detached == static_cast<std::uint32_t>(Status::Accepted);
}

template<class Metadata>
bool signedSubscriptionIdentity() {
  if constexpr (requires(Metadata& info, const lt::sha1_hash& hash) { info.use_biglybt_subscription_hash(hash); }) {
    const std::string rawHash(20, static_cast<char>(0x93));
    const std::string descriptor = "d4:infod13:hash-override20:" + rawHash
        + "6:lengthi1e4:name14:test.biglybt.112:piece lengthi262144e6:pieces20:"
        + std::string(20, 'p') + "ee";
    const lt::sha1_hash expected(rawHash.data());
    auto different = expected;
    different[0] ^= 1;
    lt::error_code ec;
    Metadata info(lt::span<const char>(descriptor.data(), descriptor.size()), ec, lt::from_span);
    if (ec || !info.info_hashes().has_v1() || info.info_hashes().v1 == expected) { return false; }
    const auto regular = info.info_hashes();
    return !info.use_biglybt_subscription_hash(different) && info.info_hashes() == regular
        && info.use_biglybt_subscription_hash(expected) && info.info_hashes().v1 == expected;
  }
  return true;
}
} // namespace

int main() {
  namespace lt = libtorrent;
  if (lt::biglybtc_public_udp_abi_v1() != TORRENT_BIGLYBTC_PUBLIC_UDP_ABI
      || lt::biglybtc_mainline_identity_abi_v1() != 1 || lt::biglybtc_mainline_routing_abi_v1() != 1
      || lt::biglybtc_private_i2p_owner_abi_v1() != lt::BiglybtcPrivateI2pOwnerAbiV1
      || lt::biglybtc_mixed_i2p_transport_abi_v1() != lt::BiglybtcMixedI2pTransportAbiV1
      || ::biglybtc_tag_class_abi_v1() != TORRENT_BIGLYBTC_TAG_CLASS_ABI) {
    std::cerr << "The native torrent library is missing a daily ABI\n";
    return 1;
  }
  if (!signedSubscriptionIdentity<lt::torrent_info>()) {
    std::cerr << "The native torrent SDK rejected the explicit subscription identity\n";
    return 5;
  }
  lt::settings_pack settings;
  settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
  settings.set_bool(lt::settings_pack::enable_dht, true);
  settings.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
  settings.set_bool(lt::settings_pack::dht_ignore_dark_internet, false);
  settings.set_bool(lt::settings_pack::enable_lsd, false);
  settings.set_bool(lt::settings_pack::enable_natpmp, false);
  settings.set_bool(lt::settings_pack::enable_upnp, false);
  lt::session session(std::move(settings));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!session.is_listening() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!session.is_listening() || session.listen_port() == 0) {
    std::cerr << "The native torrent SDK did not bind a loopback port\n";
    return 2;
  }
  const auto original = session.biglybtc_mainline_identity_status();
  const auto privateOwner = session.biglybtc_private_i2p_owner_status();
  if (original.generation || privateOwner.phase || privateOwner.version != 1) {
    std::cerr << "Unexpected state from the empty native SDK session\n";
    return 3;
  }
  if (!sharedSocketRoutesBothProtocols(session)) {
    std::cerr << "The shared socket failed Azureus callback or Mainline DHT ping\n";
    return 4;
  }
  std::cout << "The shared native SDK exports the daily ABIs and routes both protocols on one socket\n";
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "dormant_seed_demand.hpp"

#include <boost/asio/write.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
namespace lt = libtorrent;
using Demand = biglybtc::engine::detail::DormantSeedDemand;

void check(bool condition, const char* message) {
  if (!condition) { throw std::runtime_error(message); }
}

void handshake(lt::io_context& io, unsigned short port, char hashByte) {
  lt::tcp::socket socket(io);
  socket.connect({lt::address_v4::loopback(), port});
  std::array<char, 68> request{};
  request[0] = 19;
  std::memcpy(request.data() + 1, "BitTorrent protocol", 19);
  std::memset(request.data() + 28, hashByte, 20);
  std::memset(request.data() + 48, 'p', 20);
  boost::asio::write(socket, boost::asio::buffer(request));
  // A rejection closes this exact test socket once the handshake is consumed.
  socket.non_blocking(true);
  std::array<char, 16> response{};
  for (int retry = 0; retry < 200; ++retry) {
    lt::error_code error;
    socket.read_some(boost::asio::buffer(response), error);
    if (error == boost::asio::error::eof || error == boost::asio::error::connection_reset) { return; }
    if (error && error != boost::asio::error::would_block && error != boost::asio::error::try_again) {
      throw std::runtime_error(error.message());
    }
    std::this_thread::sleep_for(10ms);
  }
  throw std::runtime_error("SDK did not consume the loopback handshake");
}
}

int main() {
  try {
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    for (const auto disabled : {lt::settings_pack::enable_dht, lt::settings_pack::enable_lsd,
             lt::settings_pack::enable_upnp, lt::settings_pack::enable_natpmp}) {
      settings.set_bool(disabled, false);
    }
    const auto demand = std::make_shared<Demand>();
    lt::sha1_hash hash;
    std::memset(hash.data(), 'k', 20);
    const std::array candidates{hash};
    demand->replace(candidates);
    lt::session_params parameters(settings);
    parameters.extensions.push_back(demand);
    lt::session session(std::move(parameters));
    lt::io_context io;
    for (int retry = 0; retry < 100 && !session.listen_port(); ++retry) { std::this_thread::sleep_for(10ms); }
    const auto port = session.listen_port();
    check(port != 0, "expected an owned TCP listener");

    handshake(io, port, 'x');
    check(demand->take().empty(), "unregistered hash generated a demand");
    handshake(io, port, 'k');
    demand->replace(candidates);
    check(demand->take() == std::vector{hash}, "registered dormant demand was lost");
    handshake(io, port, 'k');
    check(demand->take().empty(), "duplicate source bypassed cooldown");
    demand->replace({});
    handshake(io, port, 'k');
    check(demand->take().empty(), "withdrawn dormant hash generated a demand");
    check(session.get_torrents().empty(), "a demand attached torrent storage on the network thread");
    std::cout << "Owned dormant seed demand checks passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}

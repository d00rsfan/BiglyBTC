// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "provider.hpp"
#include <QTest>
#include <QUdpSocket>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <stdexcept>
using namespace biglybtc;using namespace std::chrono_literals;
namespace udp=services::azureus_udp;namespace shared=services::shared_udp;namespace lt=libtorrent;
namespace {
void need(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
struct Actor {
  lt::session session;std::unique_ptr<shared::Provider> provider;
  udp::OwnedPingTransport transport;QObject receiver;bool permit=true;std::uint64_t revision=1;
  unsigned delivered=0;udp::Peer actualEndpoint;bool sourceHeld=false;
  std::function<bool(udp::Peer,std::string)> intercept;
  Actor(const QHostAddress& address,bool v6,quint16 requestedPort=0,bool differentAdvertisedPort=false,std::int32_t network=-1):session([&]{
    QUdpSocket reservation;need(reservation.bind(address,requestedPort),"Owned shared UDP port");const auto port=reservation.localPort();reservation.close();
    lt::session_params p;p.settings.set_str(lt::settings_pack::listen_interfaces,
      (v6?"["+address.toString().toStdString()+"]":address.toString().toStdString())+":"+std::to_string(port));
    p.settings.set_bool(lt::settings_pack::enable_dht,false);p.settings.set_bool(lt::settings_pack::enable_lsd,false);p.settings.set_bool(lt::settings_pack::enable_upnp,false);p.settings.set_bool(lt::settings_pack::enable_natpmp,false);return p;
  }()),transport([&]{udp::Config c;c.network=network>=0?network:(v6?3:0);c.servePings=true;c.routingScope=udp::RoutingScope::OwnedLoopback;return c;}()){
    const auto end=std::chrono::steady_clock::now()+3s;
    // Session settings give the actual selected listener without inventing a
    // second port. The fixture reserves that port only until SDK construction.
    while(!provider&&std::chrono::steady_clock::now()<end){
      const auto port=session.listen_port();if(port>0){auto attached=shared::Provider::attach(session,static_cast<quint16>(port));if(attached){provider=std::move(*attached);break;}}
      QTest::qWait(5);
    }
    need(bool(provider),"Qualified shared provider attachment");
    const auto local=provider->bindings();auto found=std::ranges::find_if(local,[&](const auto& v){return v.address==address;});need(found!=local.end(),"Exact actual SDK listener");
    actualEndpoint=*found;auto channel=provider->channel(*found,*found,[this]{return permit;});need(bool(channel),"Shared channel");
    // Generated header-only identity seam; the real SDK channel/send/source
    // and its original current() witness remain unchanged. No external claim.
    if(differentAdvertisedPort){channel->advertised.port=channel->advertised.port==65535?65534:channel->advertised.port+1;}
    need(bool(transport.startShared(std::move(*channel))),"Shared actor start");
    provider->receive([this](auto local,auto peer,std::string data){if(local!=actualEndpoint){return false;}return intercept?intercept(std::move(peer),std::move(data)):transport.receiveShared(std::move(peer),std::move(data));});
    services::azureus_nodes::ResolvedEndpoint endpoint;endpoint.address.length=v6?16:4;endpoint.address.port=transport.endpoint().port;
    if(v6){const auto ip=address.toIPv6Address();std::copy_n(ip.c,16,endpoint.address.bytes.begin());}else{const auto ip=address.toIPv4Address();for(unsigned i=0;i<4;++i){endpoint.address.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}
    services::azureus_nodes::NodeCache cache;const auto node=cache.lookup(endpoint,55);need(bool(node),"Actor contact");auto router=services::azureus_router::RouterOwner::create(node->id);auto database=services::azureus_db_runtime::Owner::create();need(bool(router)&&bool(database),"Actor router/database");
    need(bool((*database)->bindLocal(services::azureus_nodes::Contact{.protocol=55,.address=endpoint.address,.id=node->id})),"Actual local contact binding");
    need(bool(transport.serveDatabase(std::move(*router),std::move(*database),[]{return true;},[this](auto completion){const bool before=sourceHeld;sourceHeld=true;completion(true);sourceHeld=before;return true;})),"Actual actor service");
  }
  bool room(std::string key,std::function<std::string(std::string)> answer){return bool(transport.registerTransferHandler(std::move(key),true,&receiver,[this,answer=std::move(answer)](auto request){++delivered;need(bool(transport.answerTransfer(request->receipt,answer(request->request))),"Actor answer");},[this]()->std::optional<std::uint64_t>{return revision;}));}
};
}

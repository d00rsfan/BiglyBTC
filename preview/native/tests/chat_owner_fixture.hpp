// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "general_chat_service.hpp"
#include "azureus_contacts.hpp"
#include <QTemporaryDir>
#include <QTest>
#include <QUdpSocket>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
namespace generated_chat {
using namespace biglybtc;using namespace std::chrono_literals;
namespace udp=services::azureus_udp;namespace lt=libtorrent;
struct GeneratedClient {
 QTemporaryDir root;std::optional<storage::ProfileLease> lease;std::unique_ptr<lt::session> sdk;quint16 port=0;
 std::unique_ptr<app::GeneralChats> chats;
 explicit GeneratedClient(const QHostAddress& address,udp::Peer seed,bool delayed=false,bool unreachable=false,std::chrono::milliseconds cachePeriod={}){
  if(!root.isValid()){throw std::runtime_error("Generated profile");}auto held=storage::ProfileLease::acquire(root.path().toStdString());if(!held){throw std::runtime_error("Generated profile lease");}lease=std::move(*held);
  QUdpSocket reserve;if(!reserve.bind(address,0)){throw std::runtime_error("Owned port");}port=reserve.localPort();reserve.close();
  lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,delayed?std::string{}:address.toString().toStdString()+":"+std::to_string(port));settings.set_bool(lt::settings_pack::enable_dht,false);settings.set_bool(lt::settings_pack::enable_lsd,false);settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);sdk=std::make_unique<lt::session>(settings);
  auto cfg=storage::ConfigFile::createMissing(*lease,"biglybt.config","de");if(!cfg){throw std::runtime_error("Config");}if(!cfg->document().replaceValue("UDP.Listen.Port","i"+std::to_string(port)+"e")||!cfg->document().replaceValue("UDP.Listen.Port.Enable","i1e")||!cfg->document().replaceValue("Ip Filter Enabled","i0e")||!cfg->document().replaceValue("Plugin.DHT.dht.enabled","i1e")){throw std::runtime_error("Generated policy fields");}if(unreachable&&!cfg->document().replaceValue("Plugin.DHT.dht.reachable.4","i0e")){throw std::runtime_error("Generated reachability");}if(!cfg->save(*lease)){throw std::runtime_error("Config save");}
  auto cache=storage::AzureusContacts::open(*lease,4,4);if(!cache){throw std::runtime_error("Cache");}storage::AzureusSavedContact saved;const auto ip=seed.address.toIPv4Address();for(unsigned i=0;i<4;++i){saved.address[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}saved.port=seed.port;saved.protocol=55;if(!(*cache)->save(std::span(&saved,1))){throw std::runtime_error("Cache save");}
  const auto end=std::chrono::steady_clock::now()+3s;while(!delayed&&!sdk->is_listening()&&std::chrono::steady_clock::now()<end){QTest::qWait(5);}
  auto factory=engine::ChatTransportFactory::owned(*sdk,*lease,port,false,cachePeriod);if(!factory){throw std::runtime_error("Factory");}auto service=app::GeneralChats::create(std::move(*factory),1);if(!service){throw std::runtime_error(service.error().message);}chats=std::move(*service);
 }
 ~GeneratedClient(){if(chats){chats->requestStop();chats->join();chats.reset();}sdk.reset();}
};
}

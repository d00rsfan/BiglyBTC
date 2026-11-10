// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit developer diagnostic: fresh profile/port, NODE only, no rooms/posts.
#include "general_chat_service.hpp"
#include "persistent_session.hpp"
#include "network_policy.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QThread>
#include <iostream>
namespace {template<class T,class E>T take(std::expected<T,E> value){if(!value){throw std::runtime_error("Generated Public diagnostic setup refused");}return std::move(*value);}}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if((argc!=2&&argc!=3)||std::string_view(argv[1])!="--run-public-node-probe"){std::cerr<<"Use --run-public-node-probe to contact Public DHT nodes from a fresh temporary profile. No chat is joined or posted to.\n";return 2;}
 using namespace biglybtc;using namespace std::chrono_literals;
 try{
  const int budget=argc==3?std::stoi(argv[2]):60;if(budget<1||budget>1000){return 2;}
  QTemporaryDir root(QStringLiteral("/tmp/biglybtc-chat-node-probe-XXXXXX"));if(!root.isValid()){return 3;}auto lease=take(storage::ProfileLease::acquire(root.path().toStdString()));QUdpSocket reservation;if(!reservation.bind(QHostAddress::AnyIPv4,0)){return 4;}const auto port=reservation.localPort();reservation.close();auto config=take(storage::ConfigFile::createMissing(lease,"biglybt.config","de"));
  for(const auto* key:{"TCP.Listen.Port","UDP.Listen.Port"}){if(!config.document().replaceValue(key,"i"+std::to_string(port)+"e")){return 5;}}
  for(const auto& [key,value]:{std::pair{"Ip Filter Enabled",0},std::pair{"Plugin.DHT.dht.enabled",1},std::pair{"IPV6 Enable Support",1},std::pair{"UDP.Listen.Port.Enable",1},std::pair{"UDP.NonData.Listen.Port.Same",1}}){if(!config.document().replaceValue(key,"i"+std::to_string(value)+"e")){return 6;}}if(!config.save(lease)){return 7;}
  engine::NetworkPolicyOptions options;options.mode=engine::NetworkMode::Normal;options.portMapping=false;options.context=engine::NetworkPolicyContext::ExistingLibraryPublic;auto policy=take(engine::readNetworkPolicy(&config.document(),options));policy.dht=false;policy.pex=false;policy.lsd=false;policy.upnp=false;policy.natpmp=false;
  auto engine=take(engine::PersistentSession::create({1,1,1}));if(!engine->configureNetwork(policy)||!engine->startIdle(lease)){return 8;}auto factory=take(engine->chatTransport(lease));auto chats=take(app::GeneralChats::create(std::move(factory),1));
  bool connected=false;std::string previous;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(budget);
  while(std::chrono::steady_clock::now()<end){QCoreApplication::processEvents();auto snapshot=chats->snapshot();if(snapshot&&snapshot->availability!=previous){previous=snapshot->availability;std::cout<<previous<<std::endl;}if(snapshot&&snapshot->availability=="Public chats connected"){connected=true;break;}QThread::msleep(25);}
  chats->requestStop();chats->join();chats.reset();const auto close=engine->closeWithoutCheckpoint(std::chrono::steady_clock::now()+5s);if(!close.sessionJoined||!close.storageReleased){return 9;}engine.reset();std::cout<<(connected?"PASS_PUBLIC_NODE_PROBE_NO_ROOM_OR_POST":"NO_PUBLIC_ADDRESS_OBSERVATION_WITHIN_BUDGET")<<std::endl;return connected?0:10;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 11;}
}

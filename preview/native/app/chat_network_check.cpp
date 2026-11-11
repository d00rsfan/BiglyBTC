// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit diagnostics: disposable profile/port; never sends a chat message.
#include "general_chat_service.hpp"
#include "persistent_session.hpp"
#include "network_policy.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QThread>
#include <iostream>
#include <sys/stat.h>
namespace {
template<class T,class E>T take(std::expected<T,E> value){
 if(!value){throw std::runtime_error("Generated Public diagnostic setup refused");}return std::move(*value);
}
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 const bool room=argc>=2&&std::string_view(argv[1])=="--run-public-room-probe";
 if(argc<2||argc>4||(!room&&std::string_view(argv[1])!="--run-public-node-probe")){
  std::cerr<<"Use --run-public-node-probe or --run-public-room-probe [seconds] [0|4|6]. The room probe receives the standard Public beta room without saved history or sending messages.\n";return 2;
 }
 using namespace biglybtc;using namespace std::chrono_literals;
 try{
  const int budget=argc>=3?std::stoi(argv[2]):180,family=argc==4?std::stoi(argv[3]):0;
  if(budget<1||budget>1000||(family!=0&&family!=4&&family!=6)){return 2;}
  QTemporaryDir root(QStringLiteral("/tmp/biglybtc-chat-probe-XXXXXX"));if(!root.isValid()){return 3;}
  auto lease=take(storage::ProfileLease::acquire(root.path().toStdString()));QUdpSocket reservation;
  if(!reservation.bind(QHostAddress(family==6?QHostAddress::AnyIPv6:QHostAddress::AnyIPv4),0)){return 4;}
  const auto port=reservation.localPort();reservation.close();auto config=take(storage::ConfigFile::createMissing(lease,"biglybt.config","de"));
  for(const auto* key:{"TCP.Listen.Port","UDP.Listen.Port"}){if(!config.document().replaceValue(key,"i"+std::to_string(port)+"e")){return 5;}}
  for(const auto& [key,value]:{std::pair{"Ip Filter Enabled",0},std::pair{"Plugin.DHT.dht.enabled",1},std::pair{"IPV6 Enable Support",family==4?0:1},std::pair{"UDP.Listen.Port.Enable",1},std::pair{"UDP.NonData.Listen.Port.Same",1}}){
   if(!config.document().replaceValue(key,"i"+std::to_string(value)+"e")){return 6;}
  }
  if(!config.save(lease)||::chmod((root.path()+QStringLiteral("/biglybt.config")).toUtf8().constData(),0664)!=0){return 7;}
  engine::NetworkPolicyOptions options;options.mode=engine::NetworkMode::Normal;options.portMapping=false;options.context=engine::NetworkPolicyContext::ExistingLibraryPublic;
  auto policy=take(engine::readNetworkPolicy(&config.document(),options));policy.dht=false;policy.pex=false;policy.lsd=false;policy.upnp=false;policy.natpmp=false;
  if(family==6){policy.bindAddresses={"::"};}else if(family==4){policy.bindAddresses={"0.0.0.0"};}
  auto engine=take(engine::PersistentSession::create({1,1,1}));if(!engine->configureNetwork(policy)||!engine->startIdle(lease)){return 8;}
  auto factory=take(engine->chatTransport(lease));auto chats=take(app::GeneralChats::create(std::move(factory),1));
  if(room&&!chats->joinRoom("BiglyBT: Beta: Chat")){return 11;}
  bool connected=false,received=false;std::string previous;std::size_t lastNodes=0,lastMessages=0;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(budget);
  while(std::chrono::steady_clock::now()<end){
   QCoreApplication::processEvents();auto snapshot=chats->snapshot();
   if(snapshot&&snapshot->availability!=previous){previous=snapshot->availability;std::cout<<"probe.network="<<previous<<std::endl;}
   connected|=snapshot&&snapshot->availability=="Public chats connected";
   if(room&&snapshot){
    std::size_t nodes=0;for(const auto& value:snapshot->rooms){nodes+=value.nodes;if(value.settings.saveHistory){throw std::runtime_error("Fresh room probe unexpectedly has saved history");}}
    if(nodes!=lastNodes||snapshot->messages.size()!=lastMessages){lastNodes=nodes;lastMessages=snapshot->messages.size();std::cout<<"probe.room nodes="<<lastNodes<<" received_messages="<<lastMessages<<std::endl;}
    received|=!snapshot->messages.empty();
   }
   for(const auto& result:chats->takeResults()){if(result.failure){std::cout<<"probe.command_failed="<<result.failure->message<<std::endl;}}
   if(room?received:connected){break;}QThread::msleep(25);
  }
  chats->requestStop();chats->join();chats.reset();const auto close=engine->closeWithoutCheckpoint(std::chrono::steady_clock::now()+5s);
  if(!close.sessionJoined||!close.storageReleased){return 9;}engine.reset();
  std::cout<<"PROBE_RESULT family="<<family<<" connected="<<connected<<" received="<<received<<" nodes="<<lastNodes<<" messages="<<lastMessages<<" saved_history=0 sent_messages=0"<<std::endl;
  const bool passed=room?received:connected;std::cout<<(passed?"PASS_PUBLIC_CHAT_NETWORK_PROBE":"PUBLIC_CHAT_PROBE_INCOMPLETE_WITHIN_BUDGET")<<std::endl;return passed?0:10;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 12;}
}

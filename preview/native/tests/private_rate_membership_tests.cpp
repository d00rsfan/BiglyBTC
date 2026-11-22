// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "owned_i2p_service.hpp"
#include "profile_tag_rates.hpp"
#include "magnet_intake.hpp"
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <libtorrent/session.hpp>
#include <thread>
#include <algorithm>
#include <sys/stat.h>
#include <iostream>
namespace c=biglybtc::core;namespace e=biglybtc::engine;namespace s=biglybtc::storage;namespace a=biglybtc::app;namespace lt=libtorrent;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::string base32(const c::TorrentHash& hash){constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned value=0,bits=0;for(auto byte:hash.bytes()){value=(value<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;out+=alphabet[(value>>bits)&31];}}return out;}
void saveTags(owned_i2p_test::Fixture& f,const c::TorrentHash& pending,bool unsupported){using owned_i2p_test::dict;using owned_i2p_test::b;auto config=dict({{"rl.down","i16384e"}});if(unsupported)config=dict({{"rl.down","i16384e"},{"unknown-active-effect","i1e"}});owned_i2p_test::write(f.path/"tag.config",dict({{"3",dict({{"7",dict({{"c",config},{"n",b("Generated rate-only source")},{"o","l"+b(base32(f.hash))+b(base32(pending))+"e"}})}})}}));}
lt::settings_pack local(){lt::settings_pack p;p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");for(int key:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp})p.set_bool(key,false);return p;}
int main(int argc,char** argv){QCoreApplication app(argc,argv);const std::string mode=argc>1?argv[1]:"remove";QTcpServer server;require(server.listen(QHostAddress::LocalHost,0),"Owned I2CP holding socket");QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{while(server.hasPendingConnections()){server.nextPendingConnection()->setReadBufferSize(4096);}});const auto port=server.serverPort();std::string failure;
 std::thread worker([&]{try{
  owned_i2p_test::Fixture f;const auto pending=c::TorrentHash::parse(std::string(20,'q'));require(bool(pending),"Actual pending v1 identity");saveTags(f,*pending,mode=="unsupported");auto startup=s::TagManagerStartupMode::capture(*f.lease);require(bool(startup),"Actual startup source");auto rates=e::ProfileTagRates::open(*f.lease,*startup);require(bool(rates),"Actual shared profile controller");lt::session normal(local());require(bool((*rates)->prepare(f.hash,normal)),"Actual ordinary SDK cohort");
  a::I2pServiceOptions options;options.routerPort=port;auto service=a::OwnedI2pService::startWithTagRates(*f.lease,f.hash,**rates,options,{},&*startup);require(bool(service),service?"":service.error().message.c_str());
  const auto state=owned_i2p_test::read(f.state()),inventory=owned_i2p_test::read(f.path/"downloads.config"),initialTags=owned_i2p_test::read(f.path/"tag.config");const auto payload=std::filesystem::path(f.payloadDirectory.path().toStdString())/"owned.bin";struct stat before{};require(::stat(payload.c_str(),&before)==0,"Actual prepared payload inode");
  auto store=s::ManualTagStore::open(*f.lease);require(bool(store),"Actual original tag store");auto snapshot=(*store)->snapshot();require(bool(snapshot),"Actual source snapshot");
  if(mode=="pending"){
   auto policy=e::readNetworkPolicy(nullptr,{e::NetworkMode::Normal});require(bool(policy),"Original metadata policy");auto intent=e::parseMagnetIntent("magnet:?xt=urn:btih:"+pending->hex()+"&net=I2P",std::filesystem::path(f.payloadDirectory.path().toStdString())/"metadata-target",true,*policy,e::MagnetNetwork::I2p);require(bool(intent),"Actual pending intent");std::optional<e::PendingMagnet> saved;
   auto published=(*service)->publishPendingMagnet(*pending,[&]()->std::expected<void,s::Error>{auto queued=e::queuePendingMagnet(*f.lease,*intent,*policy,e::MagnetNetwork::I2p);if(!queued)return std::unexpected(queued.error());saved=std::move(*queued);return {};});require(bool(published)&&saved.has_value(),"Actual pending source publication");
   auto added=(*service)->addMagnetWithTagRates(*saved,**rates);require(bool(added),added?"":added.error().message.c_str());require((*service)->sourceCurrent(),"Complete pending/full private source cohort remains current");
   auto refreshed=(*service)->updateTagSource(**rates,[&]()->std::expected<void,s::Error>{auto current=(*store)->reload();if(!current)return std::unexpected(current.error());auto changed=(*store)->apply(*current,c::SavedTagSetRateLimit{7,false,8192});if(!changed)return std::unexpected(changed.error());return {};});require(bool(refreshed),refreshed?"":refreshed.error().message.c_str());require((*service)->sourceCurrent(),"Both full and pending target proofs renewed atomically");
  }else{
   const c::SavedTagRemoveMember edit{7,f.hash};require(!(*store)->apply(*snapshot,edit),"Generic active-rate/unsupported source writer must stay strict");const s::ManualTagMembershipEdit request=edit;
   const std::optional<std::pair<std::uint64_t,std::uint64_t>> owner=mode=="stale-owner"?std::optional{std::pair{UINT64_C(9999999),UINT64_C(9999999)}}:std::nullopt;
   unsigned validations=0;auto validate=[&](const s::ProfileSourceAccess&)->std::expected<void,s::Error>{++validations;if(mode=="preimage")return std::unexpected(s::Error{s::ErrorCode::Conflict,"Controlled changed preimage"});return {};};
   auto changed=(*service)->changeRateMembership(**rates,**store,*snapshot,f.hash,{&request,1},validate,{},owner);
   if(mode=="remove"){require(changed&&changed->changed,"Actual joined private rate-only CAS");require(validations==1,"Fresh source validation once immediately before CAS");auto now=(*store)->snapshot();require(bool(now)&&now->tags(),"Fresh committed tag snapshot");const auto at=std::ranges::find_if(now->tags()->tags,[](const auto& tag){return tag.id==7;});require(at!=now->tags()->tags.end()&&std::ranges::find(at->uniqueHashes,f.hash)==at->uniqueHashes.end(),"Only selected original membership removed");}
   else{require(!changed,"Unsupported/stale/preimage private membership must refuse");require(owned_i2p_test::read(f.path/"tag.config")==initialTags,"Refused private CAS preserves exact original bytes");}
   require((*service)->sourceCurrent(),"Changed or refused source transaction renews actual source");
  }
  require(owned_i2p_test::read(f.state())==state&&owned_i2p_test::read(f.path/"downloads.config")==inventory,"Rate transaction preserves exact full metadata and inventory");struct stat after{};require(::stat(payload.c_str(),&after)==0&&before.st_ino==after.st_ino&&before.st_size==after.st_size,"Rate transaction preserves payload identity/size");require(bool((*service)->closeWithoutCheckpoint()),"Actual SDK/source/Qt join");require(bool((*rates)->release(normal))&&bool((*rates)->close()),"Explicit remaining common quota retirement");
 }catch(const std::exception& ex){failure=ex.what();}QMetaObject::invokeMethod(&app,&QCoreApplication::quit,Qt::QueuedConnection);});app.exec();worker.join();if(!failure.empty()){std::cerr<<failure<<'\n';return 1;}std::cout<<"PASS_PRIVATE_RATE_MEMBERSHIP mode="<<mode<<";actual joined source/CAS,shared SDK classes,no lifecycle effects;Starting holding socket only,no router/payload credit\n";
}

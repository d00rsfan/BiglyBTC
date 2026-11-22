// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_rate_transport_network.hpp"
#include "profile_tag_rates.hpp"
#include "sdk_network_rates.hpp"
#include "owned_i2p_profile_fixture.hpp"
#include <QCoreApplication>
#include <iostream>
namespace e=biglybtc::engine;namespace s=biglybtc::storage;namespace lt=libtorrent;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
lt::settings_pack local(){lt::settings_pack p;p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");for(int key:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp})p.set_bool(key,false);return p;}
using Binding=std::pair<const void*,const void*>;
std::array<Binding,2> bindings(lt::session_handle sdk){std::array<Binding,2> result{};shared_transport_fixture::network(sdk,[&]{const auto owner=sdk.native_handle();unsigned n=0;for(const auto id:{lt::session_handle::global_peer_class_id,lt::session_handle::local_peer_class_id}){const auto* group=owner->peer_classes().at(id);require(group,"Actual SDK class");const auto* ops=group->channel[0].shared_operations();require(ops&&ops->scope&&ops->budget,"Actual SDK common budget binding");result[n++]={ops->scope,ops->budget};}});return result;}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 if(!e::SdkNetworkRates::supportsOriginalProtocol()){std::cout<<"UNSUPPORTED exact Original policy SDK\n";return 77;}
 owned_i2p_test::Fixture f;owned_i2p_test::write(f.path/"tag.config","damaged-generated-tags");const auto original=owned_i2p_test::read(f.path/"tag.config");
 auto mode=s::TagManagerStartupMode::capture(*f.lease);require(bool(mode),"Actual startup");auto rates=e::ProfileTagRates::openDeferred(*f.lease,*mode);require(bool(rates),"Deferred common profile controller");
 require(bool((*rates)->configureNetworkRates({16384,32768},{8192,4096})),"Before-admission common network policy");
 {
  lt::session first(local()),second(local());require(bool((*rates)->admitIdleSession(first))&&bool((*rates)->admitIdleSession(second)),"Two actual empty SDK admissions");
  const auto one=bindings(first),two=bindings(second);require(one==two&&one[0]!=one[1],"One common global cell and one distinct common LAN cell across both SDKs");
  require(!(*rates)->configureOriginalLanBypass(true),"Live LAN classification needs restart");
  {auto gate=f.lease->sourceAccess();require(bool(gate),"Actual held source gate");require(!(*rates)->configureNetworkRates({4096,8192},{2048,1024}),"No SDK waits under source gate");require(!(*rates)->configureOriginalProtocol(false,true),"No protocol waits under source gate");}
  require(bool((*rates)->configureNetworkRates({4096,8192},{2048,1024})),"Live common rate update acknowledgement");require(bool((*rates)->configureOriginalProtocol(false,true)),"Both actual SDK protocol policy acknowledgements");
  require(bindings(first)==one&&bindings(second)==two,"Live policy updates retain the common cells");
  require(!(*rates)->prepare(f.hash,first),"Idle network admission cannot grant malformed tag authority");require(first.get_torrents().empty()&&second.get_torrents().empty(),"No payload or metadata SDK effects");
 } // Actual SDK shutdown;controller must prune its remaining weak owners.
 require(bool((*rates)->configureOriginalLanBypass(true)),"Expired SDK owners cannot block fresh LAN policy");
 require(bool((*rates)->configureOriginalProtocol(true,false)),"Fresh policy after expired owners");
 {lt::session next(local());require(bool((*rates)->admitIdleSession(next)),"Fresh SDK admission after previous joins");require(bool((*rates)->release(next)),"Explicit current SDK release");}
 require(bool((*rates)->close()),"Explicit profile scope retirement");require(owned_i2p_test::read(f.path/"tag.config")==original,"Exact damaged tag bytes retained");
 std::cout<<"PASS_PROFILE_NETWORK_POLICY common SDK cells,live acknowledged changes,released FS gate,expired-owner retirement,no malformed tag authority\n";
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

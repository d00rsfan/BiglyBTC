// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "profile_owner.hpp"
#include "profile_tag_rates.hpp"
#include "private_tag_rate_source.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <QCoreApplication>
#include <future>
#include <iostream>
#include <thread>
namespace lt=libtorrent;namespace s=biglybtc::storage;namespace e=biglybtc::engine;namespace p=biglybtc::services::i2p::provider;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Probe{std::shared_ptr<const s::ProfileLease> lease;std::filesystem::path payload;unsigned calls=0;bool untouched=true,unlocked=true;void observe(){++calls;untouched=untouched&&std::filesystem::is_empty(payload);auto done=std::make_shared<std::promise<bool>>();auto ready=done->get_future();std::thread reader([lease=lease,done]{auto gate=lease->sourceAccess();done->set_value(bool(gate));});const bool fast=ready.wait_for(500ms)==std::future_status::ready;reader.join();unlocked=unlocked&&fast&&ready.get();}};
thread_local Probe* active=nullptr;
lt::settings_pack local(){lt::settings_pack q;q.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");q.set_str(lt::settings_pack::dht_bootstrap_nodes,"");for(int flag:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp})q.set_bool(flag,false);return q;}
void tag(owned_i2p_test::Fixture& f){using owned_i2p_test::dict;using owned_i2p_test::b;std::string id;unsigned value=0,bits=0;constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";for(auto byte:f.hash.bytes()){value=(value<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;id+=alphabet[(value>>bits)&31];}}owned_i2p_test::write(f.path/"tag.config",dict({{"3",dict({{"7",dict({{"c",dict({{"rl.down","i16384e"}})},{"n",b("Generated shared private tag")},{"o","l"+b(id)+"e"}})}})}}));}
}
extern "C" void realSession(lt::session*,lt::session_params&&) asm("__real__ZN10libtorrent7sessionC1EONS_14session_paramsE");
extern "C" void wrappedSession(lt::session*,lt::session_params&&) asm("__wrap__ZN10libtorrent7sessionC1EONS_14session_paramsE");
extern "C" void wrappedSession(lt::session* self,lt::session_params&& params){if(active)active->observe();realSession(self,std::move(params));}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 const std::string mode=argc>1?argv[1]:"tagged";owned_i2p_test::Fixture fixture,other;tag(fixture);tag(other);
 auto startup=s::TagManagerStartupMode::capture(*fixture.lease);check(bool(startup),"Actual private startup mode");auto otherMode=s::TagManagerStartupMode::capture(*other.lease);check(bool(otherMode),"Foreign startup mode");
 const bool wrong=mode=="foreign-controller";auto rates=e::ProfileTagRates::open(wrong?*other.lease:*fixture.lease,wrong?*otherMode:*startup);check(bool(rates),"Actual common tag controller");
 lt::session normal(local());check(bool((*rates)->prepare(fixture.hash,normal)),"Actual normal SDK sharing same tag budget");
 const auto originalState=owned_i2p_test::read(fixture.state()),inventory=owned_i2p_test::read(fixture.path/"downloads.config"),source=owned_i2p_test::read(fixture.path/"tag.config");
 Probe probe;probe.lease=*fixture.lease->retain();probe.payload=fixture.payloadDirectory.path().toStdString();
 std::expected<std::unique_ptr<p::OwnedProfile>,s::Error> owner=std::unexpected(s::Error{s::ErrorCode::Unsupported,"not prepared"});
 const bool held=mode=="held-gate",stale=mode=="stale-config",plain=mode=="default-fenced";
 auto run=[&]{active=&probe;if(plain){owner=p::OwnedProfile::prepare(*fixture.lease,fixture.hash,{},&*startup);}else{owner=p::OwnedProfile::prepareWithTagRates(*fixture.lease,fixture.hash,**rates,{},&*startup,stale?std::optional<std::string_view>{"de"}:std::nullopt);}active=nullptr;};
 if(held){auto gate=fixture.lease->sourceAccess();check(bool(gate),"Outer source gate");run();}else{run();}
 const bool success=mode=="tagged";
 check(bool(owner)==success,owner?"Unexpected private storage acceptance":owner.error().message.c_str());
 if(success){check(!std::filesystem::is_empty(probe.payload),"Actual private ManagedStorage was not prepared");check((*owner)->sourceCurrent(),"Prepared actual private owner source");owner->reset();}
 else{check(std::filesystem::is_empty(probe.payload),"Refused private preparation modified payload");check(owned_i2p_test::read(fixture.state())==originalState&&owned_i2p_test::read(fixture.path/"downloads.config")==inventory,"Refused preparation changed original resume/inventory");}
 check(probe.calls==unsigned(!(held||stale))&&probe.untouched&&probe.unlocked,"Actual private SDK must precede payload and run outside source gates");
 check(owned_i2p_test::read(fixture.path/"tag.config")==source,"Source preparation wrote manual tags");
 std::cout<<"PASS_PRIVATE_TAG_STORAGE mode="<<mode<<";actual private SDK/common source,storage ordering,ownership/full identity,default active fence,outer gate and no source writes;no transport credit\n";
}catch(std::exception const& error){active=nullptr;std::cerr<<"PRIVATE_TAG_STORAGE "<<error.what()<<'\n';return 1;}}

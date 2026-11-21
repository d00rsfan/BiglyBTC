// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "profile_tag_rates.hpp"
#include "persistent_session.hpp"
#include "sdk_shared_tag_budget.hpp"
#include <libtorrent/session.hpp>
#include <QCoreApplication>
#include <future>
#include <iostream>
namespace e=biglybtc::engine;namespace s=biglybtc::storage;namespace lt=libtorrent;
using namespace std::chrono_literals;
namespace {
unsigned checks=0;void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void tag(owned_i2p_test::Fixture& f){using owned_i2p_test::dict;using owned_i2p_test::b;std::string id;unsigned value=0,bits=0;constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";for(auto byte:f.hash.bytes()){value=(value<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;id+=alphabet[(value>>bits)&31];}}owned_i2p_test::write(f.path/"tag.config",dict({{"3",dict({{"7",dict({{"c",dict({{"rl.down","i16384e"}})},{"n",b("Generated common owner")},{"o","l"+b(id)+"e"}})}})}}));}
lt::settings_pack local(){lt::settings_pack q;q.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");q.set_str(lt::settings_pack::dht_bootstrap_nodes,"");for(int flag:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp})q.set_bool(flag,false);return q;}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 const std::string finish=argc>1?argv[1]:"close";owned_i2p_test::Fixture f,foreign;tag(f);tag(foreign);auto mode=s::TagManagerStartupMode::capture(*f.lease),otherMode=s::TagManagerStartupMode::capture(*foreign.lease);check(bool(mode)&&bool(otherMode),"Actual startup modes");
 auto opened=e::ProfileTagRates::open(*f.lease,*mode),other=e::ProfileTagRates::open(*foreign.lease,*otherMode);check(bool(opened)&&bool(other),"Actual controllers");std::shared_ptr<e::ProfileTagRates> root(std::move(*opened)),wrong(std::move(*other));
 check(root->matchesProfile(*f.lease,*mode)&&!root->matchesProfile(*foreign.lease,*otherMode),"Common owner binds actual profile/startup");
 auto thread=std::async(std::launch::async,[&]{return root->matchesProfile(*f.lease,*mode);});check(!thread.get(),"Controller ownership check retains creating-worker affinity");
 lt::session sibling(local());const bool shared=e::SdkSharedQuotaScope::supported();if(shared){check(bool(root->prepare(f.hash,sibling)),"Actual sibling SDK joined common scope");}
 auto engine=e::PersistentSession::create({301,302,303});check(bool(engine),"Qualified actual normal engine");check(!(*engine)->configureCommonTagRateOwner(*f.lease,root),"Startup mode must precede common owner injection");check(bool((*engine)->configureTagManagerStartup(*mode)),"Actual normal startup mode");
 check(!(*engine)->configureCommonTagRateOwner(*f.lease,wrong)&&root->matchesProfile(*f.lease,*mode),"Foreign controller rejected before binding");
 auto rejected=std::async(std::launch::async,[&]{return (*engine)->configureCommonTagRateOwner(*f.lease,root);});check(!rejected.get(),"Foreign worker cannot inject common owner");
 check(bool((*engine)->configureCommonTagRateOwner(*f.lease,root)),"Actual shared controller injection");check(!(*engine)->configureCommonTagRateOwner(*f.lease,root),"Duplicate injection refuses");
 check(!(*engine)->startIdle(*foreign.lease),"Injected common owner must reject a different startup profile before binding");
 check(bool((*engine)->startIdle(*f.lease)),"Actual normal SDK startup with injected controller");
 auto store=s::ManualTagStore::open(*f.lease);check(bool(store),"Actual actor store");auto snapshot=(*store)->snapshot();check(bool(snapshot),"Actual source capture");const std::array ids{7};
 auto before=(*engine)->prepareTagRateSource(*f.lease,**store,*snapshot,ids);check(bool(before)&&before->matchesAssignment(*f.lease,**store,*snapshot,7,302),"Normal writer proof uses actual common SDK cohort");
 if(finish=="destructor"){engine->reset();}
 else if(finish=="no-checkpoint"){const auto closed=(*engine)->closeWithoutCheckpoint(std::chrono::steady_clock::now()+8s);check(closed.sessionJoined,"Normal SDK joined without checkpoint");}
 else{const auto closed=(*engine)->close(std::chrono::steady_clock::now()+8s);check(closed.sessionJoined,"Normal SDK joined");}
 check(root->matchesProfile(*f.lease,*mode),"Normal close must not destroy Host-profile controller");check(!before->matchesAssignment(*f.lease,**store,*snapshot,7,302),"Normal owner release revokes complete old cohort");
 auto remaining=root->prepareSource(*f.lease,**store,*snapshot,ids,sibling);check(bool(remaining)&&remaining->matches(*f.lease,**store,*snapshot,7),"Surviving/new SDK retains common root after normal close");
 auto expired=e::PersistentSession::create({401,402,403});check(bool(expired)&&bool((*expired)->configureTagManagerStartup(*mode))&&bool((*expired)->configureCommonTagRateOwner(*f.lease,root)),"Actual waiting engine bound to common root");
 check(bool(root->close())&&!root->matchesProfile(*f.lease,*mode),"Explicit Host-profile root close");check(!remaining->matches(*f.lease,**store,*snapshot,7),"Root close revokes remaining source proof");
 check(!(*expired)->startIdle(*f.lease),"Root close refuses SDK startup on the waiting engine");
 std::cout<<"PASS_COMMON_TAG_OWNER "<<checks<<" mode="<<finish<<";actual normal engine injection,profile/thread/duplicate guards,normal close releases only cohort,root and sibling survive,explicit final close\n";
}catch(std::exception const& error){std::cerr<<"COMMON_TAG_OWNER "<<error.what()<<'\n';return 1;}}

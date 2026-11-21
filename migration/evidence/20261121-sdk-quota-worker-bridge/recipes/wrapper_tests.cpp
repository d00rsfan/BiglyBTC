// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_shared_tag_budget.hpp"
#include "sdk_tag_rate_group.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <future>
#include <iostream>
#include <limits>
namespace e=biglybtc::engine;namespace c=biglybtc::core;namespace lt=libtorrent;
namespace {
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
lt::settings_pack settings(){lt::settings_pack p;p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");for(int k:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp})p.set_bool(k,false);return p;}
}
int main(int argc,char** argv){try{
 const bool unsupported=argc>1&&std::string_view(argv[1])=="unsupported";
 if(unsupported){require(!e::SdkSharedQuotaScope::supported(),"Old/foreign SDK claimed shared ABI");const auto refused=e::SdkSharedQuotaScope::create();require(!refused&&refused.error()==e::SdkSharedQuotaError::Unsupported,"Old SDK did not reach clean normal refusal");std::cout<<"PASS optional shared API refusal before SDK/session access\n";return 0;}
 require(e::SdkSharedQuotaScope::supported(),"New SDK shared API absent");auto a=e::SdkSharedQuotaScope::create(),b=e::SdkSharedQuotaScope::create();require(bool(a)&&bool(b),"Scope creation");
 const c::SavedTagRateLimits initial{1,-1},changed{2,-1};
 auto one=(*a)->acquire(7,initial),two=(*a)->acquire(7,initial),other=(*b)->acquire(7,initial);require(bool(one)&&bool(two)&&bool(other),"Budget references");
 for(const auto limits:{c::SavedTagRateLimits{-2,0},c::SavedTagRateLimits{0,-2},c::SavedTagRateLimits{std::numeric_limits<std::int32_t>::max(),0}}){const auto invalid=(*a)->acquire(8,limits);require(!invalid&&invalid.error()==e::SdkSharedQuotaError::Limits,"Invalid exact limit accepted");}
 const auto conflict=(*a)->acquire(7,changed);require(!conflict&&conflict.error()==e::SdkSharedQuotaError::Limits,"One Source identity split into conflicting budget cells");
 lt::session first(settings()),second(settings());
 auto g1=e::SdkTagRateGroup::create(first,7,initial,*one),g2=e::SdkTagRateGroup::create(second,7,initial,*two);require(bool(g1)&&bool(g2),"Cross-session group creation");
 const auto w1=(*g1)->readiness(),w2=(*g2)->readiness();require(w1.currentFor(7,initial)&&w2.currentFor(7,initial),"Initial exact passive witnesses");
 require(bool((*one)->setLimits(initial)),"Budget no-op");require(w1.currentFor(7,initial)&&w2.currentFor(7,initial),"No-op revoked shared epoch");
 require(bool((*two)->setLimits(changed)),"Other owner mutation");require(!w1.currentFor(7,initial)&&!w2.currentFor(7,initial),"Cross-owner mutation left saved writer witnesses live");
 require(!(*g1)->readiness().currentFor(7,initial),"Stale local class minted a new witness for changed common budget");
 require(bool((*g1)->setLimits(changed)),"First local owner synchronized");auto new1=(*g1)->readiness();require(new1.currentFor(7,changed),"New current first owner witness");
 require(!(*g2)->readiness().currentFor(7,initial),"Unsynchronized sibling minted old-limit witness");require(bool((*g2)->setLimits(changed)),"Second owner synchronized");auto new2=(*g2)->readiness();require(new2.currentFor(7,changed),"Second current witness");
 require(bool((*g1)->setLimits(changed))&&new1.currentFor(7,changed)&&new2.currentFor(7,changed),"Exact group no-op changed epochs");
 auto foreign=e::SdkTagRateGroup::create(first,7,initial,*other);require(!foreign,"One actual manager accepted a foreign scope");require(new1.currentFor(7,changed)&&new2.currentFor(7,changed),"Foreign rejection poisoned actual scope");
 const auto wrong=std::async(std::launch::async,[&]{const auto update=(*one)->setLimits({3,-1});const auto acquire=(*a)->acquire(9,{0,0});const auto close=(*a)->close();return !update&&!acquire&&!close&&update.error()==e::SdkSharedQuotaError::Thread&&acquire.error()==e::SdkSharedQuotaError::Thread&&close.error()==e::SdkSharedQuotaError::Thread;}).get();require(wrong&&new1.currentFor(7,changed),"Foreign worker altered common owner");
 require(bool((*g1)->close()),"One SDK class close");require(!new1.currentFor(7,changed)&&new2.currentFor(7,changed),"Closing one SDK class closed common sibling");
 require(bool((*a)->close()),"Common scope close");require(!new2.currentFor(7,changed)&&!(*g2)->current()&&!(*two)->current(),"Common close left owner/readiness live");require(!(*two)->setLimits({0,0}),"Closed budget revived as unlimited");
 require(bool((*g2)->close()),"Closed-scope class retirement");
 auto temporary=e::SdkSharedQuotaScope::create();auto retained=(*temporary)->acquire(19,{0,0});require(bool(retained)&&(*retained)->current(),"Retained budget creation");temporary->reset();require(!(*retained)->current(),"Budget retained root owner permission after scope destruction");
 std::cout<<"PASS shared SDK native wrapper "<<checks<<" assertions;two actual sessions,exact/passive epochs,other-owner mutation,no-op,foreign/thread/close/destruction guards\n";
 }catch(const std::exception& ex){std::cerr<<"SHARED_WRAPPER "<<ex.what()<<'\n';return 1;}}

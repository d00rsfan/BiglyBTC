// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_tag_rate_group.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
using namespace biglybtc;using namespace std::chrono_literals;
void require(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
libtorrent::settings_pack isolated(){libtorrent::settings_pack p;p.set_str(libtorrent::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(libtorrent::settings_pack::dht_bootstrap_nodes,"");for(int key:{libtorrent::settings_pack::enable_dht,libtorrent::settings_pack::enable_lsd,libtorrent::settings_pack::enable_upnp,libtorrent::settings_pack::enable_natpmp}){p.set_bool(key,false);}return p;}
int main(){try{
 constexpr std::uint64_t tag=77;const core::SavedTagRateLimits first{16384,32768},second{32768,65536};
 core::TagRateReadiness absent;require(!absent.currentFor(tag,first),"Default readiness granted permission");
#ifdef BIGLYBTC_EXPECT_STOCK_TAG_RATE_REFUSAL
 require(!engine::SdkTagRateGroup::supports(first), "Stock library unexpectedly advertises private tag rates");
 const auto refused = engine::SdkTagRateGroup::create(libtorrent::session_handle{}, tag, first);
 require(!refused && refused.error() == engine::SdkTagRateError::Engine,
     "Stock library did not reject the private tag-rate group");
 return 0;
#endif
 std::unique_ptr<engine::SdkTagRateGroup> group;core::TagRateReadiness final;
 {
  libtorrent::session sdk(isolated());auto created=engine::SdkTagRateGroup::create(sdk,tag,first);require(bool(created),"Actual group create");group=std::move(*created);
  auto proof=group->readiness();require(proof.currentFor(tag,first),"Actual configured class not ready");
  require(!proof.currentFor(tag+1,first)&&!proof.currentFor(tag,second),"Witness accepted foreign tag or limits");
  auto wrong=std::async(std::launch::async,[&]{auto hint=group->readiness();return !hint.currentFor(tag,first)&&proof.currentFor(tag,first);});require(wrong.get(),"Worker authority confused with passive observation");
  require(bool(group->setLimits(first))&&proof.currentFor(tag,first),"Exact no-op invalidated current class");
  require(bool(group->setLimits(second)),"Actual SDK class update");require(!proof.currentFor(tag,first),"Old epoch survived a limit change");final=group->readiness();require(final.currentFor(tag,second),"New epoch not ready");
  require(bool(group->close())&&!final.currentFor(tag,second),"Closed group retained readiness");
  auto replacement=engine::SdkTagRateGroup::create(sdk,tag,second);require(bool(replacement),"Replacement group");group=std::move(*replacement);final=group->readiness();require(final.currentFor(tag,second),"Replacement readiness");
 }
 // Actual SDK abort invalidates a proof even while its group object survives.
 const auto end=std::chrono::steady_clock::now()+2s;while(final.currentFor(tag,second)&&std::chrono::steady_clock::now()<end){std::this_thread::sleep_for(5ms);}
 require(!final.currentFor(tag,second)&&!group->current(),"SDK abort left a source-usable proof");
 require(bool(group->close()),"Expired SDK owner close");
 std::cout<<"PASS actual SDK exact-limit readiness;default,foreign,thread,no-op,change,close,replacement and abort\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

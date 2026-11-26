// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/auto_speed_v2.hpp"
#include "biglybtc/bencode.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace biglybtc::core;
void check(bool test,const char* description) { if(!test) { throw std::runtime_error(description); } }
}

int main() try {
  check(!adaptivePing({-1,-1,-1}),"timeouts cannot fabricate a latency signal");
  const auto ping=adaptivePing({20,48,999});
  check(ping&&ping->millis==27&&ping->successful==3,"drop slowest reply and bias surviving average to the fastest");
  const auto quick=adaptivePing({-1,0,17});
  check(quick&&quick->millis==0&&quick->successful==1,"zero RTT is valid for the mapper and excluded from the source provider count");
  auto document=Document::parse("de");
  check(document&&document->replaceValue("Auto Upload Speed Seeding Enabled","i1e"),"saved auto enablement");
  auto preferences=readAutoSpeedV2Preferences(&*document);
  check(preferences&&*preferences&&!(**preferences).active(false)&&(**preferences).active(true),"default adaptive algorithm and seeding-only enablement");
  check(bool(document->replaceValue("Auto Upload Speed Version","i3e")),"change algorithm");
  check(readAutoSpeedV2Preferences(&*document)->has_value(),"version 3 uses the same Java adaptive provider");
  check(bool(document->replaceValue("Auto Upload Speed Version","i1e")),"select classic");
  check(!readAutoSpeedV2Preferences(&*document)->has_value(),"classic profile is handled by its own provider");

  AutoSpeedV2 controller({},0);
  check(!controller.adjust(1.0f,3,0),"controller must have observed real rates");
  controller.observeRates({1000,5000},{30000,0},1000);
  check(!controller.adjust(1.f,1,1000),"wait for configured sample count");
  check(controller.adjust(1.f,1,2000)==AdaptiveRates{11776,0},"rounded source slider and unlimited download");
  check(!controller.adjust(-1.f,3,3000),"skip the configured interval after adjustment");
  controller.observeRates({1000,0},{11776,0},62000);
  check(controller.mode()==AdaptiveMode::Seeding,"idle download switches to seeding after sixty seconds");
  check(!controller.adjust(0.f,3,61999),"reject old timestamps");

  AutoSpeedV2Settings config;
  config.ratingInterval=1;config.skipAfterAdjustment=false;
  AutoSpeedV2 search(config,0);
  search.observeCapacity({},AdaptiveCapacity{61440,1,0},0);
  search.observeRates({1,61440},{12000,61440},30000);
  check(search.adjust(1.f,1,30000).has_value()&&!search.testing(),"search does not start at the exact threshold");
  search.observeRates({1,61440},{12000,61440},30001);
  check(search.adjust(1.f,1,30001)==AdaptiveRates{5120,73728}&&search.testing(),"low-confidence capacity starts a download test");
  search.observeRates({1,73728},{5120,73728},32000);
  check(search.adjust(1.f,1,32000)==AdaptiveRates{5120,81100},"a saturated trial raises the selected limit");
  search.observeRates({1,0},{5120,81100},62001);
  AdaptiveTrialEstimate trial;trial.download=AdaptiveCapacity{64000,-1,0.5f};trial.choked=true;
  check(search.adjust(0.f,1,62001,trial)==AdaptiveRates{12000,64000}&&!search.testing(),"trial accepts stop evidence and restores the other axis");

  AutoSpeedV2 fixed(config,0);
  fixed.observeCapacity(AdaptiveCapacity{30720,0,1},AdaptiveCapacity{61440,0,1},0);
  fixed.observeRates({30720,61440},{30720,61440},31000);
  check(fixed.adjust(1.f,1,31000).has_value()&&!fixed.testing(),"manual line capacity prevents an automatic capacity trial");
  AutoSpeedV2 restored(config,0,AdaptiveCapacity{120000,1,0},AdaptiveCapacity{240000,1,0});
  restored.observeRates({1000,40000},{200000,0},1000);
  check(restored.adjust(0.f,1,1000)==AdaptiveRates{120000,24000},"startup restores a verified saved capacity even when the saved metric is positive");
  std::cout<<"Adaptive Auto Speed settings, intervals, caps, and confidence trial passed\n";
  return 0;
} catch(const std::exception& failure) {
  std::cerr<<failure.what()<<'\n';return 1;
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "persistent_runtime_guard.hpp"
#include "owner_runtime_guard.hpp"
#include <libtorrent/version.hpp>
#include <iostream>
#include <string_view>
int main(int argc,char** argv){
 const bool expected=!(argc>1&&std::string_view(argv[1])=="refuse");
 if(!libtorrent::version()){return 2;}
 using namespace biglybtc::engine::detail;
 for(bool actual:{persistentRuntimeQualified(),bool(managedExtendedTrafficRuntimePin()),bool(mainlineRoutingRuntimePin()),bool(mixedI2pTransportRuntimePin()),bool(biglybtc::services::i2p::provider::privateOwnerRuntimePin())}){
  if(actual!=expected){std::cerr<<"ROLE_RUNTIME_MISMATCH expected="<<expected<<'\n';return 1;}
 }
 std::cout<<(expected?"PASS_ACTUAL_ALL_ROLE_59_RUNTIME":"PASS_ACTUAL_ALL_ROLE_RUNTIME_REFUSAL")<<'\n';return 0;
}

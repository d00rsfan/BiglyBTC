// SPDX-License-Identifier: GPL-2.0-or-later
#include "persistent_runtime_guard.hpp"
#include <libtorrent/version.hpp>
#include <iostream>
#include <string_view>
int main(int argc,char**argv){
 const bool expected=argc==1||std::string_view(argv[1])!="reject";
 const auto* version=libtorrent::version();if(!version||!*version){return 1;}
 using namespace biglybtc::engine::detail;
 const bool qualified=persistentRuntimeQualified();
 const bool payload=bool(managedExtendedTrafficRuntimePin());
 const bool routing=bool(mainlineRoutingRuntimePin());
 if(qualified!=expected||payload!=expected||routing!=expected||mixedI2pTransportRuntimePin()){
  std::cerr<<"FAIL derived Public tag provider admission expected="<<expected<<" actual="<<qualified<<" payload="<<payload<<" routing="<<routing<<'\n';return 1;
 }
 std::cout<<"PASS derived Public tag provider expected="<<expected<<" full51=1 private_mixed=0\n";return 0;
}

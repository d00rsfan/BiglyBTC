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
 const bool mixed=bool(mixedI2pTransportRuntimePin());
#if defined(BIGLYBTC_EXPECT_SHARED_ROLE_BINDING) && BIGLYBTC_EXPECT_SHARED_ROLE_BINDING
 const bool expectedMixed=expected;
#else
 const bool expectedMixed=false;
#endif
 if(qualified!=expected||payload!=expected||routing!=expected||mixed!=expectedMixed){
  std::cerr<<"FAIL derived Public tag provider admission expected="<<expected<<" actual="<<qualified<<" payload="<<payload<<" routing="<<routing<<'\n';return 1;
 }
 std::cout<<"PASS derived Public tag provider expected="<<expected<<" exact-source-image=1\n";return 0;
}

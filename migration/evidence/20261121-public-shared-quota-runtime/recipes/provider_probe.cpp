// SPDX-License-Identifier: GPL-2.0-or-later
#include "persistent_runtime_guard.hpp"
#include <libtorrent/version.hpp>
#include <iostream>
#include <string_view>
int main(int argc,char** argv){
 const bool expected=!(argc>1&&std::string_view(argv[1])=="refuse");
 const auto* version=libtorrent::version();if(!version){return 2;}
 using namespace biglybtc::engine::detail;
 const bool actual=persistentRuntimeQualified();
 const bool payload=bool(managedExtendedTrafficRuntimePin());
 const bool routing=bool(mainlineRoutingRuntimePin());
 if(payload!=expected||routing!=expected||mixedI2pTransportRuntimePin()){
  std::cerr<<"RUNTIME_ROLE_MISMATCH payload="<<payload<<" routing="<<routing<<'\n';return 1;
 }
 if(actual!=expected){std::cerr<<"RUNTIME_MISMATCH qualified="<<actual<<" expected="<<expected<<'\n';return 1;}
 std::cout<<(actual?"PASS_59_EXPORT_PUBLIC_SHARED_QUOTA_RUNTIME":"PASS_SHARED_QUOTA_RUNTIME_REFUSAL")<<'\n';return 0;
}

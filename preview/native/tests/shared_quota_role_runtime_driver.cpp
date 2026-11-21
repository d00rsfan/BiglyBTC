// SPDX-License-Identifier: GPL-2.0-or-later
#include "persistent_session.hpp"
#include "persistent_runtime_guard.hpp"
#include "owner_runtime_guard.hpp"
#include <iostream>
#include <string_view>
int main(int argc,char** argv){
 using namespace biglybtc::engine;
 const bool expected=!(argc==2&&std::string_view(argv[1])=="reject");
 const bool qualified=detail::persistentRuntimeQualified();
 const bool publicPin=bool(detail::managedExtendedTrafficRuntimePin());
 const bool mixedPin=bool(detail::mixedI2pTransportRuntimePin());
 const bool privatePin=bool(biglybtc::services::i2p::provider::privateOwnerRuntimePin());
 if(qualified!=expected||publicPin!=expected||mixedPin!=expected||privatePin!=expected){return 1;}
 const auto owner=PersistentSession::create({1,1,1});
 if(expected&&!owner){return 2;}
 if(!expected&&(owner||owner.error()!=AccountingError::DependencyUnqualified)){return 3;}
 std::cout<<(expected?"PASS_SHARED_QUOTA_ALL_ROLE_RUNTIME_59":"PASS_SHARED_QUOTA_ALL_ROLE_REFUSAL")<<'\n';return 0;
}

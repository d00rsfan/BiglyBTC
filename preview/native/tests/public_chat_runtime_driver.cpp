// SPDX-License-Identifier: GPL-2.0-or-later
#include "persistent_session.hpp"
#include "persistent_runtime_guard.hpp"
#include <string_view>
int main(int argc,char** argv){
 using namespace biglybtc::engine;const bool reject=argc==2&&std::string_view(argv[1])=="reject";const auto owner=PersistentSession::create({1,1,1});
 if(reject){return !owner&&owner.error()==AccountingError::DependencyUnqualified?0:2;}
 if(!owner){return 3;}
 // An image containing private symbols is still only a Public role here.
 if(detail::mixedI2pTransportRuntimePin()){return 4;}
 if(!detail::managedPayloadCounterRuntimePin(true)||!detail::mainlineRoutingRuntimePin()||!detail::managedTrackerResponseRuntimePin()){return 5;}
 return 0;
}

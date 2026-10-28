#include "persistent_runtime_guard.hpp"
#include <libtorrent/version.hpp>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <string_view>
int main(int argc,char** argv){
  if(argc!=2||!std::getenv("LD_PRELOAD")||!libtorrent::version()){return 1;}
  const bool identity=std::string_view(argv[1])=="identity";
  const bool routing=std::string_view(argv[1])=="routing";
  if(!identity&&!routing&&std::string_view(argv[1])!="tracker"){return 2;}
  const char* name=routing?"biglybtc_mainline_routing_abi_v1":identity?"biglybtc_mainline_identity_abi_v1":"biglybtc_tracker_response_abi_v1";
  void* module=::dlopen("libtorrent-rasterbar.so.2.0",RTLD_NOW|RTLD_NOLOAD);if(!module){return 3;}
  void* actual=::dlsym(module,name);void* foreign=::dlsym(RTLD_DEFAULT,name);Dl_info a{},b{};
  if(!actual||!foreign||actual==foreign||!::dladdr(actual,&a)||!::dladdr(foreign,&b)||a.dli_fbase==b.dli_fbase){::dlclose(module);return 4;}
  using Marker=unsigned(*)()noexcept;
  if(reinterpret_cast<Marker>(actual)()!=1||reinterpret_cast<Marker>(foreign)()!=1){::dlclose(module);return 5;}
  if(::unsetenv("LD_PRELOAD")||std::getenv("LD_PRELOAD")){::dlclose(module);return 6;}
  using namespace biglybtc::engine::detail;
  const auto base=mainlinePortRuntimePin();const auto original=mainlineIdentityRuntimePin();const auto rich=managedTrackerResponseRuntimePin();const auto qualified=mainlineRoutingRuntimePin();
  ::dlclose(module);
  if(!base||qualified||static_cast<bool>(original)==identity||static_cast<bool>(rich)!=routing){return 7;}
  std::cout<<"genuine20=1 original23="<<static_cast<bool>(original)<<" foreign27_rejected=1 after_env_removed=1\n";
  return 0;
}

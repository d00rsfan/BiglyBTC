// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE 1
#include <libtorrent/biglybtc_shared_quota.hpp>
#include <libtorrent/version.hpp>
#include <dlfcn.h>
#include <link.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Driver {
 void* library=nullptr;void* owner=nullptr;
 void* (*create)(const biglybtc_shared_quota_v1*)noexcept=nullptr;
 void* (*plain)()noexcept=nullptr;
 int (*foreign_probe)(const biglybtc_shared_quota_v1*)noexcept=nullptr;
 int (*bind)(void*,const biglybtc_shared_quota_v1*)noexcept=nullptr;
 int (*pump)(void*)noexcept=nullptr;
 int (*bytes)(void*)noexcept=nullptr;
 void (*destroy)(void*)noexcept=nullptr;
 void* (*sdk)()noexcept=nullptr;
 Driver(const char* path,bool separate){library=separate ? ::dlmopen(LM_ID_NEWLM,path,RTLD_NOW|RTLD_LOCAL) : ::dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!library){const auto* error=::dlerror();throw std::runtime_error(error?error:"SDK image load");}
  create=reinterpret_cast<decltype(create)>(::dlsym(library,"shared_image_create"));plain=reinterpret_cast<decltype(plain)>(::dlsym(library,"shared_image_plain_create"));bind=reinterpret_cast<decltype(bind)>(::dlsym(library,"shared_image_bind_again"));pump=reinterpret_cast<decltype(pump)>(::dlsym(library,"shared_image_pump"));bytes=reinterpret_cast<decltype(bytes)>(::dlsym(library,"shared_image_bytes"));destroy=reinterpret_cast<decltype(destroy)>(::dlsym(library,"shared_image_destroy"));sdk=reinterpret_cast<decltype(sdk)>(::dlsym(library,"shared_image_sdk"));require(create&&plain&&bind&&pump&&bytes&&destroy&&sdk,"C driver exports");
  foreign_probe=reinterpret_cast<decltype(foreign_probe)>(::dlsym(library,"shared_image_foreign_probe"));require(foreign_probe,"Foreign allocator probe export");
 }
 ~Driver(){std::cerr<<"IMAGE_DESTROY begin sdk="<<sdk()<<'\n';if(owner)destroy(owner);std::cerr<<"IMAGE_DESTROY owner joined\n";if(library)::dlclose(library);std::cerr<<"IMAGE_DESTROY unloaded\n";}
};
struct Scope {
 void* root=biglybtc_shared_quota_scope_create_v1();biglybtc_shared_quota_v1 budget;
 Scope(){require(root&&biglybtc_shared_quota_acquire_v1(root,7,0,0,&budget)==0,"Primary quota scope");}
 ~Scope(){biglybtc_shared_quota_scope_close_v1(root);budget.release_budget(budget.budget);biglybtc_shared_quota_scope_release_v1(root);}
};
}
int main(int argc,char** argv){try{require(argc==2||argc==3,"Driver library argument");
 if(argc==3&&std::string_view(argv[2])=="plain"){Driver unloaded_scope_control(argv[1],true);unloaded_scope_control.owner=unloaded_scope_control.plain();require(unloaded_scope_control.owner,"Plain second-namespace session");const auto* flag=static_cast<const char*>(::dlsym(unloaded_scope_control.library,"__libc_single_threaded"));std::cout<<"PLAIN_IMAGE_NO_SHARED_SCOPE libc_single_threaded="<<(flag?int(*flag):-1)<<std::endl;for(int n=0;n<300;++n){require(unloaded_scope_control.pump(unloaded_scope_control.owner)>=0,"Plain unbound SDK socket");std::this_thread::sleep_for(10ms);}std::cout<<"PLAIN_IMAGE_NO_SHARED_SCOPE closing\n";return 0;}
 if(argc==3&&std::string_view(argv[2])=="foreign"){
  {Scope primary;Driver admitted(argv[1],false),foreign_image(argv[1],true);require(admitted.sdk()!=foreign_image.sdk(),"Foreign allocator image control");admitted.owner=admitted.create(&primary.budget);require(admitted.owner,"Admitted SDK binding");const auto epoch=primary.budget.epoch(primary.budget.budget);require(foreign_image.foreign_probe(&primary.budget)==-3,"Foreign SDK image borrowed allocator or inspected raw session");require(primary.budget.epoch(primary.budget.budget)==epoch&&admitted.pump(admitted.owner)>0,"Foreign-image refusal altered admitted owner");}
  std::cout<<"PASS foreign SDK allocator refusal before callback/class binding;admitted owner unchanged;all sessions/images/scopes destroyed\n";return 0;
 }
 const bool single=argc==2||(argc==3&&std::string_view(argv[2])=="single");
 {Scope primary,foreign;
 Driver one(argv[1],false),two(argv[1],!single);require(one.sdk()==reinterpret_cast<void*>(&libtorrent::version),"Primary driver selected another SDK");require(single?two.sdk()==one.sdk():two.sdk()!=one.sdk(),"SDK image control did not load expected image");
 one.owner=one.create(&primary.budget);two.owner=two.create(&primary.budget);require(one.owner&&two.owner,"Opaque common budget binding across actual images");
 require(one.bind(one.owner,&primary.budget)==0&&two.bind(two.owner,&primary.budget)==0,"Same binding no-op");require(one.bind(one.owner,&foreign.budget)==-3&&two.bind(two.owner,&foreign.budget)==-3,"Foreign root accepted across images");
 const auto* flag1=static_cast<const char*>(::dlsym(one.library,"__libc_single_threaded"));const auto* flag2=static_cast<const char*>(::dlsym(two.library,"__libc_single_threaded"));std::cout<<"IMAGE_LIBC_SINGLE_THREADED "<<(flag1?int(*flag1):-1)<<'+'<<(flag2?int(*flag2):-1)<<std::endl;
 require(biglybtc_shared_quota_set_v1(primary.budget.budget,16,0)==0,"Begin finite clock after image/session construction");
 const auto start=std::chrono::steady_clock::now();while(std::chrono::steady_clock::now()-start<3s){require(one.pump(one.owner)>=0&&two.pump(two.owner)>=0,"Actual SDK manager callback");std::this_thread::sleep_for(10ms);}
 const int first=one.bytes(one.owner),second=two.bytes(two.owner);const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 std::cout<<"IMAGE_COUNTS "<<first<<'+'<<second<<" seconds="<<elapsed<<" available="<<primary.budget.available(primary.budget.budget,0)<<std::endl;
 require(first>0&&second>0,"One SDK image starved");require(first+second<=16*elapsed+1&&first+second>=24,"Separate images duplicated or stalled common clock/budget");
 two.destroy(two.owner);two.owner=nullptr;const auto surviving=one.bytes(one.owner);const auto drain=std::chrono::steady_clock::now();while(std::chrono::steady_clock::now()-drain<500ms){one.pump(one.owner);std::this_thread::sleep_for(10ms);}require(one.bytes(one.owner)>surviving,"Closing one live SDK owner stranded the sibling budget");
 biglybtc_shared_quota_scope_close_v1(primary.root);require(!primary.budget.epoch(primary.budget.budget),"Cross-image common close epoch");const int before=one.bytes(one.owner)+two.bytes(two.owner);
 const auto closed=std::chrono::steady_clock::now();while(std::chrono::steady_clock::now()-closed<500ms){one.pump(one.owner);two.pump(two.owner);std::this_thread::sleep_for(10ms);}require(one.bytes(one.owner)+two.bytes(two.owner)==before,"Closed root leaked quota into another image");
 }
 std::cout<<"PASS one admitted SDK image,two actual session owners;common steady clock,finite quota,foreign/no-op/close/lifetime controls;all owners/images/scopes destroyed;Public sessions only,no private-role qualification\n";
 }catch(const std::exception& e){std::cerr<<"SHARED_IMAGE "<<e.what()<<'\n';return 1;}}

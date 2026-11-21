// SPDX-License-Identifier: GPL-2.0-or-later
#include <libtorrent/biglybtc_shared_quota.hpp>
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Scope {
 void* value=biglybtc_shared_quota_scope_create_v1();biglybtc_shared_quota_v1 budget;
 Scope(){require(value&&biglybtc_shared_quota_acquire_v1(value,7,16,0,&budget)==0,"Admitted scope");}
 ~Scope(){biglybtc_shared_quota_scope_close_v1(value);budget.release_budget(budget.budget);biglybtc_shared_quota_scope_release_v1(value);}
};
void run(const char* path){
 Scope admitted;void* module=::dlopen(path,RTLD_NOW|RTLD_LOCAL);require(module,"Foreign allocator module load");
 const auto owns=reinterpret_cast<bool(*)(const biglybtc_shared_quota_v1*)noexcept>(::dlsym(module,"foreign_allocator_owns"));
 const auto probe=reinterpret_cast<int(*)(const biglybtc_shared_quota_v1*)noexcept>(::dlsym(module,"foreign_allocator_probe"));
 const auto create=reinterpret_cast<decltype(&biglybtc_shared_quota_scope_create_v1)>(::dlsym(module,"biglybtc_shared_quota_scope_create_v1"));
 const auto acquire=reinterpret_cast<decltype(&biglybtc_shared_quota_acquire_v1)>(::dlsym(module,"biglybtc_shared_quota_acquire_v1"));
 const auto close=reinterpret_cast<decltype(&biglybtc_shared_quota_scope_close_v1)>(::dlsym(module,"biglybtc_shared_quota_scope_close_v1"));
 const auto release=reinterpret_cast<decltype(&biglybtc_shared_quota_scope_release_v1)>(::dlsym(module,"biglybtc_shared_quota_scope_release_v1"));
 require(owns&&probe&&create&&acquire&&close&&release,"Actual foreign allocator exports");
 Dl_info first{},second{};require(::dladdr(reinterpret_cast<void*>(&biglybtc_shared_quota_scope_create_v1),&first)&&::dladdr(reinterpret_cast<void*>(create),&second)&&first.dli_fbase!=second.dli_fbase,"Allocator image control");
 void* local=create();biglybtc_shared_quota_v1 budget;require(local&&acquire(local,7,16,0,&budget)==0,"Foreign allocator own scope control");
 require(owns(&budget)&&!owns(&admitted.budget),"Actual factory identity predicate");const auto epoch=admitted.budget.epoch(admitted.budget.budget);
 require(probe(&admitted.budget)==-3,"Actual SDK binder inspected sentinel session or accepted foreign allocator");
 require(admitted.budget.epoch(admitted.budget.budget)==epoch&&admitted.budget.limit(admitted.budget.budget,0)==16,"Foreign refusal changed admitted allocator");
 close(local);budget.release_budget(budget.budget);release(local);require(::dlclose(module)==0,"Foreign allocator module teardown");
}
}
int main(int argc,char** argv){try{require(argc==2,"Foreign module argument");run(argv[1]);std::cout<<"PASS actual allocator/binder TUs in separate image;own factory positive control,foreign descriptor refused before raw session/callback access;all budgets/scopes/module destroyed;one C++ runtime\n";}catch(const std::exception& e){std::cerr<<"ALLOCATOR_IMAGE "<<e.what()<<'\n';return 1;}}

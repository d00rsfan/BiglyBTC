// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_tag_rate_group.hpp"
#include "sdk_prepared_tag_attachment.hpp"
#include "sdk_shared_tag_budget.hpp"
#include "tag_preadd_fixture.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <iostream>
#include <thread>
namespace e=biglybtc::engine;namespace lt=libtorrent;
namespace {unsigned count=0;void check(bool value,const char* why){++count;if(!value){throw std::runtime_error(why);}}}
int main(){try{
 shared_transport_fixture::Root root;lt::session sdk(shared_transport_fixture::settings()),foreign(shared_transport_fixture::settings());
 const biglybtc::core::SavedTagRateLimits rate{16384,8192};
 std::unique_ptr<e::SdkSharedQuotaScope> scope;std::shared_ptr<e::SdkSharedTagBudget> budget;
 if(e::SdkSharedQuotaScope::supported()){
  auto created=e::SdkSharedQuotaScope::create();check(bool(created),"Actual SDK origin");scope=std::move(*created);
  auto acquired=scope->acquire(7,rate);check(bool(acquired),"Actual common budget");budget=*acquired;
 }
 auto group=e::SdkTagRateGroup::create(sdk,7,rate,budget);check(bool(group),"Actual SDK class");
 check(!(*group)->prepareAttachment({}),"Empty hash refused");
 auto params=shared_transport_fixture::torrent(root.path/"one",'a');params.flags|=lt::torrent_flags::paused;params.flags&=~lt::torrent_flags::auto_managed;
 auto prepared=(*group)->prepareAttachment(params.ti->info_hashes());check(bool(prepared)&&(*prepared)->current(),"Actual class pre-add preparation");
 auto unsafe=params;unsafe.flags&=~lt::torrent_flags::paused;check(!(*prepared)->install(unsafe)&&unsafe.extensions.empty(),"Running add refused without mutation");
 unsafe=params;unsafe.flags|=lt::torrent_flags::auto_managed;check(!(*prepared)->install(unsafe)&&unsafe.extensions.empty(),"Automatic add refused without mutation");
 auto other=shared_transport_fixture::torrent(root.path/"other",'b');other.flags=params.flags;
 check(!(*prepared)->install(other)&&other.extensions.empty(),"Different full hash refused");
 bool installed=false;std::thread worker([&]{installed=bool((*prepared)->install(params));});worker.join();check(installed,"Token installs on another Native worker without SDK wait");
 check(!(*prepared)->install(params),"One-shot installation");
 lt::error_code ec;auto handle=sdk.add_torrent(params,ec);check(!ec&&handle.is_valid(),"Actual paused add");
 check((*prepared)->boundTo(handle),"Constructor callback binds exact actual handle");
 check(!(*prepared)->boundTo({}),"Absent target refused");
 check(bool((*group)->detach(handle))&&!(*prepared)->boundTo(handle),"Ordinary detach disables pre-added class");
 check(bool((*group)->attach(handle))&&(*prepared)->boundTo(handle),"Ordinary attach reactivates actual pre-add plugin");
 check(bool((*group)->setLimits(rate))&&(*prepared)->boundTo(handle),"Exact no-op preserves binding witness");
 auto wrong=(*group)->prepareAttachment(other.ti->info_hashes());check(bool(wrong)&&bool((*wrong)->install(other)),"Second exact target token");
 auto outsider=foreign.add_torrent(other,ec);check(!ec&&outsider.is_valid(),"Foreign paused fixture");
 check(!(*wrong)->boundTo(outsider)&&!(*wrong)->current(),"Foreign SDK callback cannot publish a binding");
 auto lateParams=shared_transport_fixture::torrent(root.path/"late",'c');lateParams.flags=params.flags;
 auto late=(*group)->prepareAttachment(lateParams.ti->info_hashes());check(bool(late)&&bool((*late)->install(lateParams)),"Prepared callback before close");
 check(bool((*group)->setLimits({-1,-1}))&&!(*prepared)->current()&&!(*prepared)->boundTo(handle),"Actual policy epoch revokes prior binding proof");
 auto current=(*group)->prepareAttachment(lateParams.ti->info_hashes());check(bool(current)&&(*current)->current(),"New exact current-policy token");
 check(bool((*group)->close())&&!(*current)->current(),"Close passively revokes pre-add authority");
 auto lateHandle=sdk.add_torrent(lateParams,ec);check(!ec&&lateHandle.is_valid()&&!(*late)->boundTo(lateHandle),"Late constructor callback cannot bind a retired class");
 if(scope){check(bool(scope->close()),"Origin close");}
 std::cout<<"PASS_SDK_PREADD_CONTROLS "<<count<<" assertions;actual SDK constructor,exact hash/session,worker handoff,one shot,detach/reattach,epochs,foreign and late-close refusal\n";
 }catch(std::exception const& error){std::cerr<<"PREADD_CONTROL "<<error.what()<<'\n';return 1;}}

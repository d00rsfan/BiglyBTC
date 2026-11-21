// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_prepared_tag_attachment.hpp"
#include "transport_fixture.hpp"
#include <libtorrent/session.hpp>
#include <iostream>
#include <thread>
namespace e=biglybtc::engine;namespace lt=libtorrent;
namespace {unsigned checks=0;void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}}
int main(){try{
 shared_transport_fixture::Root root;
 auto sdk=std::make_unique<lt::session>(shared_transport_fixture::settings());
 auto group=e::SdkTagRateGroup::create(*sdk,19,{16384,8192});check(bool(group),"Actual ordinary SDK group");
 lt::sha1_hash h1;h1[0]=17;lt::sha256_hash h2;h2[0]=23;
 const lt::info_hash_t hybrid(h1,h2);
 auto params=[&](lt::info_hash_t hashes){lt::add_torrent_params p;p.info_hashes=hashes;p.save_path=root.path.string();p.flags|=lt::torrent_flags::paused;p.flags&=~lt::torrent_flags::auto_managed;return p;};
 auto prepared=(*group)->prepareAttachment(hybrid);check(bool(prepared),"Hybrid preparation");
 auto truncated=params(lt::info_hash_t(h1));check(!(*prepared)->install(truncated)&&truncated.extensions.empty(),"V1 match cannot substitute full hybrid identity");
 auto onlyV2=params(lt::info_hash_t(h2));check(!(*prepared)->install(onlyV2)&&onlyV2.extensions.empty(),"V2 match cannot substitute full hybrid identity");
 auto full=params(hybrid);check(bool((*prepared)->install(full)),"Full hybrid magnet installation");
 check(bool(full.flags&lt::torrent_flags::duplicate_is_error),"Duplicate add cannot silently return an unmodified torrent");
 lt::error_code ec;auto target=sdk->add_torrent(full,ec);check(!ec&&(*prepared)->boundTo(target),"Actual metadata-less hybrid construction");
 auto duplicate=sdk->add_torrent(full,ec);(void)duplicate;check(bool(ec)&&(*prepared)->boundTo(target),"Copied parameters cannot replace or revoke existing exact binding");
 lt::sha256_hash v2OnlyHash;v2OnlyHash[0]=41;const lt::info_hash_t pureV2(v2OnlyHash);
 auto pure=(*group)->prepareAttachment(pureV2);auto pureParams=params(pureV2);
 check(bool(pure)&&bool((*pure)->install(pureParams)),"Exact v2-only magnet token");
 auto pureTarget=sdk->add_torrent(pureParams,ec);check(!ec&&(*pure)->boundTo(pureTarget),"Actual v2-only SDK constructor binding");
 const lt::info_hash_t v2(h2);auto v2Token=(*group)->prepareAttachment(v2);auto v2Params=params(v2);
 check(bool(v2Token)&&bool((*v2Token)->install(v2Params)),"V2-only metadata-less preparation");
 // Use a different hash from the hybrid's v2 side to avoid SDK duplicate resolution.
 lt::sha256_hash freshV2;freshV2[0]=29;v2Params.info_hashes=lt::info_hash_t(freshV2);
 auto changed=sdk->add_torrent(v2Params,ec);check(!ec&&changed.is_valid()&&!(*v2Token)->boundTo(changed)&&!(*v2Token)->current(),"Changed parameters fail inside actual SDK constructor");
 auto stale=(*group)->prepareAttachment(lt::info_hash_t(freshV2));auto staleParams=params(lt::info_hash_t(freshV2));
 check(bool(stale)&&bool((*stale)->install(staleParams)),"Armed current policy callback");
 check(bool((*group)->setLimits({8192,4096})),"Actual policy change before construction");
 // A new hash avoids duplicate resolution suppressing the stale callback.
 lt::sha1_hash staleHash;staleHash[0]=31;auto stale2=(*group)->prepareAttachment(lt::info_hash_t(staleHash));auto staleAdd=params(lt::info_hash_t(staleHash));
 check(bool(stale2)&&bool((*stale2)->install(staleAdd)),"Second armed callback");
 check(bool((*group)->setLimits({4096,2048})),"Second policy publication");
 auto staleTarget=sdk->add_torrent(staleAdd,ec);check(!ec&&staleTarget.is_valid()&&!(*stale2)->boundTo(staleTarget)&&!(*stale2)->current(),"Stale epoch refuses constructor attachment while group stays alive");
 bool threadRefused=false;std::thread foreign([&]{auto value=(*group)->prepareAttachment(hybrid);threadRefused=!value&&value.error()==e::SdkTagRateError::Thread;});foreign.join();check(threadRefused,"Preparation retains creating-worker affinity");
 lt::sha1_hash lastHash;lastHash[0]=37;auto retained=(*group)->prepareAttachment(lt::info_hash_t(lastHash));check(bool(retained)&&(*retained)->current(),"Lifetime token");
 sdk.reset();check(!(*retained)->current()&&!(*prepared)->boundTo(target),"Actual SDK abort passively revokes retained tokens");
 check(bool((*group)->close()),"Retirement after SDK join");
 std::cout<<"PASS_SDK_PREADD_EDGES "<<checks<<";hybrid/v2 magnet identities,post-install mutation,duplicate copies,stale callbacks,worker affinity,actual SDK abort\n";
}catch(std::exception const& error){std::cerr<<"PREADD_EDGE "<<error.what()<<'\n';return 1;}}

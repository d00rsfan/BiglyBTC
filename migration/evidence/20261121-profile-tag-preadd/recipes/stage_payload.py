from pathlib import Path
D=Path(__file__).resolve().parent
s=(D.parent/'profile-shared-tag-rates-20261121/profile_payload_tests.cpp').read_text()
s=s.replace('#include "profile_tag_rates.hpp"','#include "profile_tag_rates.hpp"\n#include "profile_prepared_tag_attachment.hpp"')
s=s.replace('const bool partial=argc>2&&std::string_view(argv[2])=="partial";', 'const bool empty=argc>2&&std::string_view(argv[2])=="empty";')
s=s.replace('for(int n=first?0:1;n<2;++n)', 'for(int n=0;n<(first?2:1);++n)')
s=s.replace(' write(cap);auto lease=', ' write(empty?0:cap);auto lease=')
start=s.index(' std::array<lt::torrent_handle,2> source,target;');end=s.index(' const auto total=',start)
s=s[:start]+''' std::array<lt::torrent_handle,2> source,target;std::array<std::shared_ptr<std::atomic_bool>,2> drained,requestDrained;
 // Admit/prepare the complete SDK cohort before issuing any pending bundle.
 for(int n=0;n<2;++n){require(bool((*rates)->prepare(hash(n),down?leeches[n]:seeds[n])),"Actual complete SDK cohort before pending preparation");}
 std::optional<s::PreparedTagRateSource> proof;
 if(!empty){auto ready=(*rates)->prepareSource(*lease,**store,snapshot,ids,down?leeches[0]:seeds[0]);require(bool(ready),"Actual initial complete source proof");proof=*ready;}
 for(int n=0;n<2;++n){auto& session=down?leeches[n]:seeds[n];auto p=params[n];p.flags|=lt::torrent_flags::seed_mode|lt::torrent_flags::paused;p.flags&=~lt::torrent_flags::auto_managed;
  auto q=p;q.flags&=~lt::torrent_flags::seed_mode;q.save_path=(root.path/("leech"+std::to_string(n))).string();auto limited=down?q:p;
  auto prepared=(*rates)->prepareAttachment(hash(n),limited.ti->info_hashes(),session);require(bool(prepared),"Actual profile pending reservation");
  require(bool((*prepared)->install(limited)),"Actual paused bundle installation");auto handle=session.add_torrent(limited);
  require((*prepared)->boundTo(handle),"Complete actual source/cohort bundle attaches inside SDK construction");
  source[n]=down?seeds[n].add_torrent(p):handle;target[n]=down?handle:leeches[n].add_torrent(q);
  drained[n]=shared_transport_fixture::installDrain(target[n]);requestDrained[n]=shared_transport_fixture::installDrain(source[n]);
 }
 auto change=[&](int rate,bool first=true){write(rate,first);snapshot=(*store)->reload().value();require(bool((*rates)->synchronize(down?leeches[0]:seeds[0])),"Actual source effects harvest/cancel every pending target");};
 // No explicit attach and no controller call after the second constructor.
 // In the empty case,the first source change must attach the first real class
 // to both previously class-less pending handles before their first resume.
 if(empty){change(-1);}
'''+s[end:]
needle=' auto deadline=std::chrono::steady_clock::now()+14s;'
s=s.replace(needle,''' if(empty){std::this_thread::sleep_for(1500ms);require(total()==0,"First active source tag missed a constructor target with no prior classes");change(cap);auto ready=(*rates)->prepareSource(*lease,**store,snapshot,ids,down?leeches[0]:seeds[0]);require(bool(ready),"First active source proof");proof=*ready;}
'''+needle,1)
start=s.index(' auto change=[&](int rate,bool first=true)',s.index(' const auto before='));end=s.index(' change(0);change(cap);',start)
s=s[:start]+''' // Combine the first post-constructor source update with last-target removal:
 // failing to harvest that pending handle would leave its class Disabled.
 const auto detachedBefore=target[1].status().total_payload_download;
 change(-1,false);require(proof&&!proof->matches(*lease,**store,snapshot,7),"Old source proof survived changed common budget");
 if(down){shared_transport_fixture::drainPriorPayload(leeches[0],target[0],requestDrained[0]);}
 shared_transport_fixture::drainPriorPayload(seeds[0],source[0],drained[0]);
 const auto stopped=target[0].status().total_payload_download;
 for(int tick=0;tick<3;++tick){shared_transport_fixture::keepAlive(seeds[0],shared_transport_fixture::capturePeer(seeds[0],source[0]));std::this_thread::sleep_for(1s);require(target[0].status().total_payload_download==stopped,"Removed sibling lifted the retained target prohibition");}
 deadline=std::chrono::steady_clock::now()+8s;while(target[1].status().total_payload_download<=detachedBefore+16384&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(25ms);}
 require(target[1].status().total_payload_download>detachedBefore+16384,"Source removal missed the last constructor target before census harvest");
'''+s[end:]
s=s.replace('";partial-recovery="<<partial', '";initially-empty="<<empty')
s=s.replace('PASS actual profile saved tag budgets;', 'PASS_PROFILE_PREADD_PAYLOAD constructor source/census;')
(D/'profile_payload_tests.cpp').write_text(s)
print('Staged pending-census payload control;last constructor harvested by first source change')

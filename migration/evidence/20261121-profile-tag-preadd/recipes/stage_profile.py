from pathlib import Path
import hashlib,json,subprocess,shutil
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC');P=json.loads((D/'pre-implementation.json').read_text());O=D/'drafts';O.mkdir(exist_ok=True)
def write(path,data):
 if not path.exists() or path.read_text()!=data:path.write_text(data)
def source(name):
 path='preview/native/engine/'+name;data=subprocess.check_output(['git','-C',str(N),'show',P['profile_source_baseline']+':'+path]);assert hashlib.sha256(data).hexdigest()==P['profile_source_before_sha256'][path];return data.decode()
s=source('profile_tag_rates.hpp').replace('namespace biglybtc::engine {','namespace biglybtc::engine {\nclass ProfilePreparedTagAttachment;')
needle=' std::expected<void,storage::Error> prepare(const core::TorrentHash&,libtorrent::session_handle);'
assert s.count(needle)==1;s=s.replace(needle,needle+'''
 // Reserve a pending row before a paused add on another Native worker.
 std::expected<std::unique_ptr<ProfilePreparedTagAttachment>,storage::Error> prepareAttachment(
     const core::TorrentHash&,const libtorrent::info_hash_t&,libtorrent::session_handle);''');write(O/'profile_tag_rates.hpp',s)
shutil.copy2(D/'profile_prepared_tag_attachment.hpp',O/'profile_prepared_tag_attachment.hpp')
s=source('profile_tag_rates.cpp').replace('#include "profile_tag_rates.hpp"','#include "profile_tag_rates.hpp"\n#include "profile_prepared_tag_attachment.hpp"\n#include "sdk_prepared_tag_attachment.hpp"\n#include <libtorrent/add_torrent_params.hpp>\n#include <libtorrent/torrent_info.hpp>\n#include <libtorrent/torrent.hpp>\n#include <libtorrent/aux_/session_impl.hpp>\n#include <libtorrent/extensions.hpp>\n#include <cstring>\n#include <mutex>')
needle='struct ProfileTagRates::Impl {';assert s.count(needle)==1;s=s.replace(needle,(D/'pending_state.cpp.inc').read_text()+'\n'+needle)
needle='struct Binding{core::TorrentHash hash;libtorrent::torrent_handle handle;std::vector<std::uint64_t> tags;};';assert s.count(needle)==1;s=s.replace(needle,'struct Binding{core::TorrentHash hash;libtorrent::torrent_handle handle;std::vector<std::uint64_t> tags;std::shared_ptr<ProfilePreparedTagAttachment::Impl> pending{};};')
needle=' std::expected<void,Error> sync(libtorrent::session_handle session){';assert s.count(needle)==1;s=s.replace(needle,''' // The constructor and this controller serialize publication/cancellation.
 // No SDK wait occurs while the pending-state mutex is held.
 void settlePending(){
  for(auto it=bindings.begin();it!=bindings.end();){
   const auto pending=it->second.pending;if(!pending){++it;continue;}
   std::lock_guard held(pending->mutex);
   if(pending->phase==ProfilePreparedTagAttachment::Impl::Phase::Bound){
    it->second.handle=pending->handle;it->second.tags=pending->tags;
    it->second.pending.reset();++it;
   }else if(dirty||!pending->current()){
    pending->cancel();it=bindings.erase(it);
   }else{++it;}
  }
 }
'''+needle)
needle='   if(!dirty){return {};}';assert s.count(needle)==1;s=s.replace(needle,'   settlePending();\n'+needle)
# Ordinary cleanup must not discard a reservation whose constructor has not run.
s=s.replace('if(!it->second.handle.is_valid()){it=bindings.erase(it);continue;}','if(!it->second.handle.is_valid()){if(it->second.pending){++it;continue;}it=bindings.erase(it);continue;}')
s=s.replace('old!=impl_->bindings.end()&&!old->second.handle.is_valid()', 'old!=impl_->bindings.end()&&!old->second.pending&&!old->second.handle.is_valid()')
needle='std::expected<void,storage::Error> ProfileTagRates::prepare(const core::TorrentHash& hash';assert s.count(needle)==1;s=s.replace(needle,(D/'prepare_method.cpp.inc').read_text()+'\n'+needle)
write(O/'profile_tag_rates.cpp',s)
print('Staged ProfileTagRates pending bundle and serialized harvest/cancellation;Native untouched')

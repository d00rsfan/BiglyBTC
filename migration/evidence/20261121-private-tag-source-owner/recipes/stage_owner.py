from pathlib import Path
import hashlib,json,subprocess
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC');P=json.loads((D/'pre-implementation.json').read_text());O=D/'drafts'
def source(name):
 path='preview/native/services/i2p_provider/'+name;data=subprocess.check_output(['git','-C',str(N),'show',P['native_parent']+':'+path]);assert hashlib.sha256(data).hexdigest()==P['source_before_sha256'][path];return data.decode()
def write(p,s):
 if not p.exists()or p.read_text()!=s:p.write_text(s)
s=source('profile_owner.hpp').replace('namespace biglybtc::services::i2p::provider {','namespace biglybtc::engine {class ProfileTagRates;}\nnamespace biglybtc::services::i2p::provider {')
needle='  // Pending-first pure bootstrap uses the SAME key1 session and disk sandbox.';assert s.count(needle)==1;s=s.replace(needle,'''  // Explicit common tag owner,used synchronously on the I/O worker only.
  // The controller reference is never retained or invoked by the socket turn.
  static std::expected<std::unique_ptr<OwnedProfile>,storage::Error> prepareWithTagRates(
      const storage::ProfileLease&,const core::TorrentHash&,engine::ProfileTagRates&,
      const Retained& external={},const storage::TagManagerStartupMode* startup=nullptr,
      std::optional<std::string_view> expectedConfiguration=std::nullopt);
'''+needle)
needle='  // Original construction IO worker only; FS-only, no Qt/SDK dispatch.';assert s.count(needle)==1;s=s.replace(needle,'''  std::expected<std::unique_ptr<Prepared>,storage::Error> prepareTorrentWithTagRates(
      const core::TorrentHash&,const Retained&,engine::ProfileTagRates&,
      std::optional<std::string_view> expectedConfiguration=std::nullopt);
'''+needle)
# Keep the existing class tail and put the shared factory in its private section.
needle='  struct Impl;explicit OwnedProfile';assert s.count(needle)==1;s=s.replace(needle,'''  static std::expected<std::unique_ptr<OwnedProfile>,storage::Error> prepareImpl(
      const storage::ProfileLease&,const core::TorrentHash&,const Retained&,
      const storage::TagManagerStartupMode*,std::optional<std::string_view>,engine::ProfileTagRates*);
'''+needle);write(O/'profile_owner.hpp',s)
s=source('profile_owner.cpp').replace('#include "profile_owner.hpp"','#include "profile_owner.hpp"\n#include "profile_tag_rates.hpp"\n#include "profile_prepared_tag_attachment.hpp"\n#include "private_tag_rate_source.hpp"')
needle='  std::uint64_t admission=0;std::size_t inventoryIndex=0;';assert s.count(needle)==1;s=s.replace(needle,needle+'\n  std::shared_ptr<engine::ProfilePreparedTagAttachment> tagTarget;')
needle='std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepare(const storage::ProfileLease& lease,const core::TorrentHash& hash,const Retained& external,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration){';assert s.count(needle)==1;s=s.replace(needle,'''std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepare(const storage::ProfileLease& lease,const core::TorrentHash& hash,const Retained& external,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration){
 return prepareImpl(lease,hash,external,startup,expectedConfiguration,nullptr);
}
std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepareWithTagRates(const storage::ProfileLease& lease,const core::TorrentHash& hash,engine::ProfileTagRates& rates,const Retained& external,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration){
 return prepareImpl(lease,hash,external,startup,expectedConfiguration,&rates);
}
std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepareImpl(const storage::ProfileLease& lease,const core::TorrentHash& hash,const Retained& external,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration,engine::ProfileTagRates* tagRates){''')
needle='''    auto first=[&]()->std::expected<std::unique_ptr<Prepared>,Error>{''';assert s.count(needle)==1;s=s.replace(needle,'''    auto first=tagRates?owner->prepareTorrentWithTagRates(hash,external,*tagRates,expectedConfiguration):[&]()->std::expected<std::unique_ptr<Prepared>,Error>{''')
needle='std::expected<std::unique_ptr<OwnedProfile::Prepared>,Error> OwnedProfile::prepareTorrent(const core::TorrentHash& hash,const Retained& retained){';assert s.count(needle)==1;s=s.replace(needle,(D/'prepare_tagged_torrent.cpp.inc').read_text()+'\n'+needle)
needle='''    lt::error_code ec;m.handle=session->add_torrent(params,ec);if(ec||!m.handle.is_valid()){return false;}
    m.installed=true;''';assert s.count(needle)==1;s=s.replace(needle,'''    if(m.data().tagTarget&&!m.data().tagTarget->installForChecking(params)){return false;}
    lt::error_code ec;m.handle=session->add_torrent(params,ec);if(ec||!m.handle.is_valid()){return false;}
    if(m.data().tagTarget&&!m.data().tagTarget->boundTo(m.handle)){m.handle.pause();return false;}
    m.installed=true;''')
needle='''      }else if(m->queued){
        // A queued intent''';assert s.count(needle)==1;s=s.replace(needle,'''      }else if(m->queued){
        if(m->data().tagTarget&&!m->data().source->prepared()){fail(*m,{Code::Conflict,"Private tag source is no longer current before Run"});continue;}
        // A queued intent''')
needle='''    m.activity=d.activity;m.justCompleted=false;''';assert s.count(needle)==1;s=s.replace(needle,'''    if(action==core::QueueAction::Run&&m.data().tagTarget&&!m.data().source->prepared()){
      s.fail(m,{Code::Conflict,"Private tag source is no longer current before external Run"});return std::unexpected(Error{Code::Conflict,"Private tag Run source changed"});
    }
'''+needle)
write(O/'profile_owner.cpp',s);print('Staged explicit transient-controller private owner preparation;default legacy path unchanged')

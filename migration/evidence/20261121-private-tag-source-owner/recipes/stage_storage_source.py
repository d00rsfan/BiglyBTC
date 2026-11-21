from pathlib import Path
import hashlib,json,subprocess
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC');P=json.loads((D/'pre-implementation.json').read_text());O=D/'drafts';O.mkdir(exist_ok=True)
def source(name):
 path='preview/native/services/i2p_provider/'+name;data=subprocess.check_output(['git','-C',str(N),'show',P['native_parent']+':'+path]);assert hashlib.sha256(data).hexdigest()==P['source_before_sha256'][path];return data.decode()
def write(p,s):
 if not p.exists()or p.read_text()!=s:p.write_text(s)
s=source('profile_source.hpp').replace('#include <functional>','#include <functional>\nnamespace libtorrent {class session_handle;struct info_hash_t;}').replace('namespace biglybtc::engine {class MixedManagedSource;', 'namespace biglybtc::engine {class PrivateTagRateSource;class MixedManagedSource;');needle='  std::expected<engine::detail::ManagedStorage,storage::Error> prepareStorageDirect();';assert s.count(needle)==1;s=s.replace(needle,'''  // Trusted actual owner only. Capture raw full identity under its source gate,
  // release before SDK preparation,then revalidate immediately before storage.
  std::expected<libtorrent::info_hash_t,storage::Error> previewTagTarget()const;
  std::expected<engine::detail::ManagedStorage,storage::Error> prepareStorageWithTagSource(
      const engine::PrivateTagRateSource&,const libtorrent::info_hash_t&,libtorrent::session_handle);
'''+needle);write(O/'profile_source.hpp',s)
s=source('profile_source.cpp').replace('#include "profile_source.hpp"','#include "profile_source.hpp"\n#include "private_tag_rate_source.hpp"\n#include <libtorrent/session_handle.hpp>');needle='std::optional<RateStamps> noPrivateTagRates(';assert s.count(needle)==1;s=s.replace(needle,'''struct PrivateRatePreparation {
 engine::PrivateTagRateSource source;libtorrent::info_hash_t expected;libtorrent::session_handle session;
};
'''+needle)
s=s.replace('const core::TorrentHash& hash,const storage::TagManagerStartupMode& mode){','const core::TorrentHash& hash,const storage::TagManagerStartupMode& mode,\n    const std::shared_ptr<const PrivateRatePreparation>& prepared={}){',1)
needle='  const auto& saved=*snapshot.tags();const auto* manual=saved.source->find("3");';assert s.count(needle)==1;s=s.replace(needle,'''  const bool admitted=prepared&&prepared->source.matches(lease,hash,snapshot,prepared->expected,prepared->session);
  require(!prepared||admitted,ErrorCode::Conflict,"Private tag source/SDK cohort changed before storage");
'''+needle)
s=s.replace('require(*member.hash!=hash,ErrorCode::Unsupported,"Shared manual-tag rates are unavailable for the dedicated private I2P owner");','require(*member.hash!=hash||admitted,ErrorCode::Unsupported,"Shared manual-tag rates are unavailable for the dedicated private I2P owner");')
needle='  std::optional<storage::TagManagerStartupMode> tagMode;std::optional<RateStamps> rateSource;';assert s.count(needle)==1;s=s.replace(needle,needle+'''
  std::atomic<std::shared_ptr<const PrivateRatePreparation>> privateRates;
  bool privateRatesCurrent()const noexcept{const auto prepared=privateRates.load();return !prepared||prepared->source.current();}''')
s=s.replace('return impl_->stamp.has_value()&&!impl_->revoked&&impl_->managedCurrent()', 'return impl_->stamp.has_value()&&!impl_->revoked&&impl_->privateRatesCurrent()&&impl_->managedCurrent()')
s=s.replace('return !impl_->revoked&&!impl_->dirty&&impl_->managedCurrent()', 'return !impl_->revoked&&!impl_->dirty&&impl_->privateRatesCurrent()&&impl_->managedCurrent()')
s=s.replace('// This separately qualified private owner has no shared manual-tag limiter.', '// The default private path refuses active rates. A trusted actual owner may\n// provide a sealed current shared-SDK/source preparation before storage.')
s=s.replace('  if(!mode.enabled()){return {};}', '  if(!mode.enabled()){require(!prepared,ErrorCode::Unsupported,\"Private shared tag preparation requires the enabled tag manager\");return {};}')
s=s.replace('noPrivateTagRates(*impl_->lease,impl_->hash,*impl_->tagMode);','noPrivateTagRates(*impl_->lease,impl_->hash,*impl_->tagMode,impl_->privateRates.load());')
needle='std::expected<engine::detail::ManagedStorage,storage::Error> ProfileSource::prepareStorage(){';assert s.count(needle)==1;s=s.replace(needle,(D/'source_storage_methods.cpp.inc').read_text()+'\n'+needle)
needle='    require(!impl_->revoked,ErrorCode::Conflict,"Owned I2P source is revoked");';assert s.count(needle)==1;s=s.replace(needle,needle+'\n    require(impl_->privateRatesCurrent(),ErrorCode::Conflict,"Private tag SDK/source preparation is no longer current");')
write(O/'profile_source.cpp',s);print('Staged friend-only private source consumption and full raw identity preview;Native unchanged')

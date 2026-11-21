from pathlib import Path
import subprocess,json
D=Path(__file__).resolve().parent;N=D.parents[1].parent/'BiglyBTC';parent=json.loads((D/'pre-implementation.json').read_text())['native_parent'];O=D/'drafts'
def source(name):return subprocess.check_output(['git','-C',str(N),'show',parent+':preview/native/app/'+name],text=True)
h=source('owned_i2p_service.hpp').replace('struct PendingMagnet;struct PendingMagnetStatus;','struct PendingMagnet;struct PendingMagnetStatus;class ProfileTagRates;')
needle='  static std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> startPending('
h=h.replace(needle,'''  // Explicit common-controller preparation on the calling Host worker. The
  // controller reference is consumed synchronously and is never retained.
  static std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> startWithTagRates(
      const storage::ProfileLease&,const core::TorrentHash&,engine::ProfileTagRates&,
      I2pServiceOptions={},const engine::RetainedPayloads& external={},
      const storage::TagManagerStartupMode* startup=nullptr);
'''+needle)
needle='  std::expected<void,storage::Error> stop(const core::TorrentHash&);'
h=h.replace(needle,'''  std::expected<void,storage::Error> addWithTagRates(const core::TorrentHash&,
      engine::ProfileTagRates&,const engine::RetainedPayloads& external={});
'''+needle)
needle='  struct Impl;explicit OwnedI2pService(std::unique_ptr<Impl>);'
h=h.replace(needle,'''  static std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> startImpl(
      const storage::ProfileLease&,const core::TorrentHash&,I2pServiceOptions,
      const engine::RetainedPayloads&,const storage::TagManagerStartupMode*,engine::ProfileTagRates*);
  std::expected<void,storage::Error> addImpl(const core::TorrentHash&,
      const engine::RetainedPayloads&,engine::ProfileTagRates*);
'''+needle)
(O/'owned_i2p_service.hpp').write_text(h)
s=source('owned_i2p_service.cpp')
needle='std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::start(const storage::ProfileLease& lease,const core::TorrentHash& hash,I2pServiceOptions options,const engine::RetainedPayloads& external,const storage::TagManagerStartupMode* startup){'
assert s.count(needle)==1
s=s.replace(needle,needle+'''
  return startImpl(lease,hash,std::move(options),external,startup,nullptr);
}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startWithTagRates(const storage::ProfileLease& lease,const core::TorrentHash& hash,engine::ProfileTagRates& rates,I2pServiceOptions options,const engine::RetainedPayloads& external,const storage::TagManagerStartupMode* startup){
  return startImpl(lease,hash,std::move(options),external,startup,&rates);
}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startImpl(const storage::ProfileLease& lease,const core::TorrentHash& hash,I2pServiceOptions options,const engine::RetainedPayloads& external,const storage::TagManagerStartupMode* startup,engine::ProfileTagRates* rates){''')
needle='auto prepared=provider::OwnedProfile::prepare(lease,hash,external,startup,bootstrap->configBytes);'
assert s.count(needle)==1
s=s.replace(needle,'auto prepared=rates?provider::OwnedProfile::prepareWithTagRates(lease,hash,*rates,external,startup,bootstrap->configBytes):provider::OwnedProfile::prepare(lease,hash,external,startup,bootstrap->configBytes);')
needle='std::expected<void,storage::Error> OwnedI2pService::add(const core::TorrentHash& h,const engine::RetainedPayloads& external){'
assert s.count(needle)==1
s=s.replace(needle,needle+'''
  return addImpl(h,external,nullptr);
}
std::expected<void,storage::Error> OwnedI2pService::addWithTagRates(const core::TorrentHash& h,engine::ProfileTagRates& rates,const engine::RetainedPayloads& external){
  return addImpl(h,external,&rates);
}
std::expected<void,storage::Error> OwnedI2pService::addImpl(const core::TorrentHash& h,const engine::RetainedPayloads& external,engine::ProfileTagRates* rates){''')
needle='auto prepared=s.owner->prepareTorrent(h,retained);'
assert s.count(needle)==1
s=s.replace(needle,'auto prepared=rates?s.owner->prepareTorrentWithTagRates(h,retained,*rates):s.owner->prepareTorrent(h,retained);')
# Disabled-owner API variants stay inert and do not touch their controller.
needle='std::optional<storage::Error> OwnedI2pService::contactCacheDiagnostic()const{return std::nullopt;}'
assert s.count(needle)==1
s=s.replace(needle,'''std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startWithTagRates(const storage::ProfileLease&,const core::TorrentHash&,engine::ProfileTagRates&,I2pServiceOptions,const engine::RetainedPayloads&,const storage::TagManagerStartupMode*){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::addWithTagRates(const core::TorrentHash&,engine::ProfileTagRates&,const engine::RetainedPayloads&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
'''+needle)
(O/'owned_i2p_service.cpp').write_text(s)
print('Staged synchronous explicit service preparation;no retained controller or Host activation')

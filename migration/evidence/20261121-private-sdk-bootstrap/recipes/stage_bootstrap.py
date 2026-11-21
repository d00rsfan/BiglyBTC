"""Stage SDK-before-storage bootstrap with a fresh config check inside its gate."""
from pathlib import Path
import json,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;O=D/'native-staged'
parent=json.loads((D/'pre-implementation.json').read_text())['native_parent']
def old(name):return subprocess.check_output(['git','-C',str(N),'show',parent+':preview/native/'+name],text=True)
def put(name,text):
 p=O/'preview/native'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
name='services/i2p_provider/profile_owner.hpp';s=old(name).replace('#include <span>','#include <span>\n#include <string_view>')
s=s.replace('const storage::TagManagerStartupMode* startup=nullptr);','const storage::TagManagerStartupMode* startup=nullptr,\n      std::optional<std::string_view> expectedConfiguration=std::nullopt);')
s=s.replace('  static std::expected<std::unique_ptr<OwnedProfile>,storage::Error> prepare(', '  // SDK construction precedes payload preparation and requires no outer\n  // source gate. Optional expected configuration only narrows the accepted\n  // source;it is rechecked under the gate immediately before storage work.\n  static std::expected<std::unique_ptr<OwnedProfile>,storage::Error> prepare(')
put(name,s)
name='services/i2p_provider/profile_owner.cpp';s=old(name)
helper='''std::expected<storage::ProfileSourceAccess,Error> bootstrapSourceAccess(
    const storage::ProfileLease& lease,std::optional<std::string_view> expected){
  auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
  if(!access->observationsAllowed()){return std::unexpected(Error{Code::Conflict,"Release the outer source gate before private SDK preparation"});}
  if(expected){
    auto config=storage::ConfigFile::load(lease,"biglybt.config");if(!config){return std::unexpected(config.error());}
    if(config->document().serialize()!=*expected){return std::unexpected(Error{Code::Conflict,"Original I2P configuration changed before payload preparation"});}
  }
  return std::move(*access);
}
'''
needle='struct QueueSink final:';assert s.count(needle)==1;s=s.replace(needle,helper+needle)
s=s.replace('const storage::TagManagerStartupMode* startup){','const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration){')
start=s.index('std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepare(')
end=s.index('std::expected<std::unique_ptr<OwnedProfile::PreparedMagnet>,Error> OwnedProfile::prepareMagnet(',start)
s=s[:start]+'''std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepare(const storage::ProfileLease& lease,const core::TorrentHash& hash,const Retained& external,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration){
  try{auto pin=privateOwnerRuntimePin();if(!pin||lt::biglybtc_private_i2p_torrents_abi_v1()!=lt::BiglybtcPrivateI2pTorrentsAbiV1){return std::unexpected(Error{Code::Unsupported,"Approved multi-torrent private I2P provider required"});}
    {auto access=bootstrapSourceAccess(lease,expectedConfiguration);if(!access){return std::unexpected(access.error());}}
    auto source=ProfileSource::createSession(lease,"azneti2phelper",startup);if(!source){return std::unexpected(source.error());}auto s=std::make_unique<Impl>();s->source=*source;s->pin=std::move(pin);
    auto owner=std::unique_ptr<OwnedProfile>(new OwnedProfile(std::move(s)));
    auto identity=(*source)->takeIdentity();if(!identity){return std::unexpected(identity.error());}owner->impl_->identity.emplace(std::move(*identity));
    auto settings=(*source)->policy().settings();settings.set_int(lt::settings_pack::alert_mask,static_cast<int>(lt::alert_category::error|lt::alert_category::status|lt::alert_category::storage|lt::alert_category::tracker));settings.set_int(lt::settings_pack::alert_queue_size,4096);
    lt::session_params params(std::move(settings));params.disk_io_constructor=engine::detail::metadataDiskConstructor(owner->impl_->disks);owner->impl_->session=std::make_unique<lt::session>(std::move(params));
    auto first=[&]()->std::expected<std::unique_ptr<Prepared>,Error>{
      auto access=bootstrapSourceAccess(lease,expectedConfiguration);if(!access){return std::unexpected(access.error());}
      return owner->prepareTorrent(hash,external);
    }();if(!first){return std::unexpected(first.error());}owner->impl_->admission=(*first)->impl_->admission;
    auto added=owner->add(std::move(*first));if(!added){return std::unexpected(added.error());}return owner;
  }catch(...){return std::unexpected(Error{Code::Io,"Cannot prepare shared original I2P profile"});}
}
std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::preparePending(const storage::ProfileLease& lease,const engine::PendingMagnet& intent,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration){
#if defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
  try{
    auto pin=privateOwnerRuntimePin();if(!pin){return std::unexpected(Error{Code::Unsupported,"Qualified discovery provider required"});}
    const auto abi=reinterpret_cast<decltype(&lt::biglybtc_i2p_discovery_abi_v1)>(::dlsym(pin.get(),"biglybtc_i2p_discovery_abi_v1"));
    if(!abi||abi()!=lt::BiglybtcI2pDiscoveryAbiV1){return std::unexpected(Error{Code::Unsupported,"Qualified discovery ABI required"});}
    {auto access=bootstrapSourceAccess(lease,expectedConfiguration);if(!access){return std::unexpected(access.error());}}
    auto source=ProfileSource::createSession(lease,"azneti2phelper",startup);if(!source){return std::unexpected(source.error());}auto state=std::make_unique<Impl>();state->source=*source;state->pin=std::move(pin);
    auto owner=std::unique_ptr<OwnedProfile>(new OwnedProfile(std::move(state)));
    auto identity=(*source)->takeIdentity();if(!identity){return std::unexpected(identity.error());}owner->impl_->identity.emplace(std::move(*identity));
    auto settings=(*source)->policy().settings();settings.set_int(lt::settings_pack::alert_mask,static_cast<int>(lt::alert_category::error|lt::alert_category::status|lt::alert_category::storage|lt::alert_category::tracker));settings.set_int(lt::settings_pack::alert_queue_size,4096);
    lt::session_params parameters(std::move(settings));parameters.disk_io_constructor=engine::detail::metadataDiskConstructor(owner->impl_->disks);owner->impl_->session=std::make_unique<lt::session>(std::move(parameters));
    auto first=[&]()->std::expected<std::unique_ptr<PreparedMagnet>,Error>{
      auto access=bootstrapSourceAccess(lease,expectedConfiguration);if(!access){return std::unexpected(access.error());}
      return owner->prepareMagnet(intent);
    }();if(!first){return std::unexpected(first.error());}owner->impl_->admission=(*first)->impl_->admission;
    auto added=owner->startMagnet(std::move(*first));if(!added){return std::unexpected(added.error());}return owner;
  }catch(...){return std::unexpected(Error{Code::Io,"Cannot prepare pending-first I2P session"});}
#else
  (void)lease;(void)intent;(void)startup;(void)expectedConfiguration;return std::unexpected(Error{Code::Unsupported,"Qualified discovery SDK unavailable"});
#endif
}
'''+s[end:]
put(name,s)
name='app/owned_i2p_service.cpp';s=old(name)
needle='std::unique_ptr<provider::ContactOwner> contacts;};';assert s.count(needle)==2;s=s.replace(needle,'std::unique_ptr<provider::ContactOwner> contacts;std::string configBytes;};')
for call in ['provider::OwnedProfile::prepare(lease,hash,external,startup)','provider::OwnedProfile::preparePending(lease,intent,startup)']:
 needle='      auto prepared='+call+';if(!prepared){return std::unexpected(prepared.error());}\n';assert s.count(needle)==1;s=s.replace(needle,'')
old_finish='''      auto contacts=provider::ContactOwner::open(lease,1,*discovery);if(!contacts){return std::unexpected(contacts.error());}
      auto current=storage::ConfigFile::load(lease,"biglybt.config");if(!current||current->document().serialize()!=configBytes){return std::unexpected(Error{Code::Conflict,"Original I2P queue/rate configuration changed during preparation"});}
      return Bootstrap{std::move(*prepared),std::move(*retained),std::move(*queue),*rates,std::move(*discovery),std::move(*contacts)};
'''
assert s.count(old_finish)==2;s=s.replace(old_finish,'      return Bootstrap{nullptr,std::move(*retained),std::move(*queue),*rates,std::move(*discovery),nullptr,configBytes};\n')
needle='    if(!bootstrap){return std::unexpected(bootstrap.error());}\n';assert s.count(needle)==2
for method,arguments in [('start','lease,hash,external,startup'),('startPending','lease,intent,startup')]:
 start=s.index('OwnedI2pService::'+method+'(');at=s.index(needle,start)+len(needle)
 prepare='provider::OwnedProfile::prepare'+('Pending' if method=='startPending' else '')
 addition='''    // SDK waits belong outside the caller's filesystem/source gate.
    auto prepared='''+prepare+'('+arguments+''',bootstrap->configBytes);if(!prepared){return std::unexpected(prepared.error());}bootstrap->owner=std::move(*prepared);
    auto completed=[&]()->std::expected<void,Error>{
      auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
      auto current=storage::ConfigFile::load(lease,"biglybt.config");if(!current||current->document().serialize()!=bootstrap->configBytes){return std::unexpected(Error{Code::Conflict,"Original I2P queue/rate configuration changed during preparation"});}
      auto contacts=provider::ContactOwner::open(lease,1,bootstrap->discovery);if(!contacts){return std::unexpected(contacts.error());}bootstrap->contacts=std::move(*contacts);
      current=storage::ConfigFile::load(lease,"biglybt.config");if(!current||current->document().serialize()!=bootstrap->configBytes){return std::unexpected(Error{Code::Conflict,"Original I2P configuration changed during contact preparation"});}return {};
    }();if(!completed){return std::unexpected(completed.error());}
'''
 s=s[:at]+addition+s[at:]
put(name,s)
paths=['preview/native/'+name for name in ['services/i2p_provider/profile_owner.hpp','services/i2p_provider/profile_owner.cpp','app/owned_i2p_service.cpp','tests/private_sdk_bootstrap_tests.cpp','tests/CMakeLists.txt']]
(D/'native-source-paths.json').write_text(json.dumps(paths,indent=2)+'\n')
print('Staged SDK-first bootstrap;source/config guards precede storage;private active rates stay fenced')

from pathlib import Path
import hashlib,json,subprocess
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC');P=json.loads((D/'pre-implementation.json').read_text());O=D/'drafts'
def source(name):
 path='preview/native/engine/'+name;data=subprocess.check_output(['git','-C',str(N),'show',P['native_parent']+':'+path]);assert hashlib.sha256(data).hexdigest()==P['source_before_sha256'][path];return data.decode()
def write(p,s):
 if not p.exists()or p.read_text()!=s:p.write_text(s)
s=source('profile_tag_rates.hpp').replace(' ~ProfileTagRates();',' ~ProfileTagRates();\n // Passive creating-worker/profile/startup ownership check;no SDK/source work.\n bool matchesProfile(const storage::ProfileLease&,const storage::TagManagerStartupMode&)const noexcept;');write(O/'profile_tag_rates.hpp',s)
s=source('profile_tag_rates.cpp');needle='ProfileTagRates::~ProfileTagRates(){';at=s.index(needle);s=s[:at]+'''bool ProfileTagRates::matchesProfile(const storage::ProfileLease& lease,const storage::TagManagerStartupMode& mode)const noexcept{
 return impl_&&impl_->worker==std::this_thread::get_id()&&impl_->lease&&mode.matches(lease)&&impl_->lease->sharesOwnership(lease)&&impl_->enabled==mode.enabled();
}
'''+s[at:];write(O/'profile_tag_rates.cpp',s)
s=source('persistent_session.hpp').replace('namespace biglybtc::engine {','namespace biglybtc::engine {\nclass ProfileTagRates;',1);needle='  std::optional<storage::Error> tagRateFailure()const;';assert s.count(needle)==1;s=s.replace(needle,'''  // Before startup,bind the same Host-profile controller used by private SDKs.
  // Closing this session releases its cohort entry rather than the common root.
  std::expected<void,storage::Error> configureCommonTagRateOwner(
      const storage::ProfileLease&,std::shared_ptr<ProfileTagRates>);
'''+needle);write(O/'persistent_session.hpp',s)
s=source('persistent_session.cpp');needle='  std::unique_ptr<ProfileTagRates> tagRates;';assert s.count(needle)==1;s=s.replace(needle,'''  std::shared_ptr<ProfileTagRates> tagRates;bool commonTagRateOwner=false;
  void releaseTagRateOwner()noexcept{
    if(commonTagRateOwner&&tagRates&&session){try{(void)tagRates->release(*session);}catch(...){}}
    tagRates.reset();commonTagRateOwner=false;tagManagerMode.reset();
  }''')
s=s.replace('tagRates.reset();tagManagerMode.reset();','releaseTagRateOwner();')
s=s.replace('impl_->tagRates.reset();impl_->tagManagerMode.reset();','impl_->releaseTagRateOwner();')
needle='std::optional<storage::Error> PersistentSession::tagRateFailure()const{';assert s.count(needle)==1;s=s.replace(needle,'''std::expected<void,storage::Error> PersistentSession::configureCommonTagRateOwner(
    const storage::ProfileLease& lease,std::shared_ptr<ProfileTagRates> owner){
 if(!impl_->onWorker()||impl_->call||impl_->closed||impl_->poisoned||impl_->session||impl_->profileBound
     ||impl_->tagRates||!impl_->tagManagerMode||!owner
     ||!owner->matchesProfile(lease,*impl_->tagManagerMode)){
  return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Common tag owner must match this profile/startup before engine startup"});
 }
 impl_->tagRates=std::move(owner);impl_->commonTagRateOwner=true;return {};
}
'''+needle);write(O/'persistent_session.cpp',s)
print('Staged Host-owned shared controller injection and per-session release;Native unchanged')

from pathlib import Path
import hashlib,json,shutil,subprocess
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC');P=json.loads((D/'pre-implementation.json').read_text());O=D/'drafts';O.mkdir(exist_ok=True)
def source(name):
 path='preview/native/engine/'+name;data=subprocess.check_output(['git','-C',str(N),'show',P['native_parent']+':'+path]);assert hashlib.sha256(data).hexdigest()==P['source_before_sha256'][path];return data.decode()
def write(path,data):
 if not path.exists()or path.read_text()!=data:path.write_text(data)
s=source('profile_tag_rates.hpp').replace('class ProfilePreparedTagAttachment;','class ProfilePreparedTagAttachment;\nclass PrivateTagRateSource;');needle=' std::expected<void,storage::Error> attach(const core::TorrentHash&,libtorrent::torrent_handle);';assert s.count(needle)==1;s=s.replace(needle,''' // Separate sealed current-source preparation for the actual future private SDK.
 std::expected<PrivateTagRateSource,storage::Error> preparePrivateSource(
     const core::TorrentHash&,libtorrent::session_handle,const ProfilePreparedTagAttachment&);
'''+needle);write(O/'profile_tag_rates.hpp',s)
shutil.copy2(D/'private_tag_rate_source.hpp',O/'private_tag_rate_source.hpp')
shutil.copy2(N/'preview/native/engine/profile_prepared_tag_attachment.hpp',O/'profile_prepared_tag_attachment.hpp')
s=source('profile_tag_rates.cpp').replace('#include "profile_tag_rates.hpp"','#include "profile_tag_rates.hpp"\n#include "private_tag_rate_source.hpp"');needle='struct ProfileTagRates::Impl {';assert s.count(needle)==1;s=s.replace(needle,(D/'private_source_state.cpp.inc').read_text()+'\n'+needle)
needle='std::expected<std::unique_ptr<ProfilePreparedTagAttachment>,storage::Error> ProfileTagRates::prepareAttachment(';assert s.count(needle)==1;s=s.replace(needle,(D/'prepare_private_source.cpp.inc').read_text()+'\n'+needle);write(O/'profile_tag_rates.cpp',s)
print('Staged separate sealed source witness;Native and private active-rate fence untouched')

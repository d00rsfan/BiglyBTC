from pathlib import Path
import hashlib,json,subprocess
D=Path(__file__).resolve().parent;N=Path('/home/yurii/Java/BiglyBTC');P=json.loads((D/'pre-implementation.json').read_text());O=D/'drafts'
def source(name):
 path='preview/native/engine/'+name;data=subprocess.check_output(['git','-C',str(N),'show',P['native_parent']+':'+path]);assert data==(N/path).read_bytes();P['source_before_sha256'].setdefault(path,hashlib.sha256(data).hexdigest());return data.decode()
def write(p,s):
 if not p.exists()or p.read_text()!=s:p.write_text(s)
s=source('sdk_prepared_tag_attachment.hpp');needle=' std::expected<void,SdkTagRateError> install(libtorrent::add_torrent_params&);';assert s.count(needle)==1;s=s.replace(needle,needle+'''
 // Full metainfo only. The SDK's stop_when_ready must prevent data transfer;
 // caller verifies boundTo before any later queue-authorized resume.
 std::expected<void,SdkTagRateError> installForChecking(libtorrent::add_torrent_params&);''');needle=' struct Impl;';assert s.count(needle)==1;s=s.replace(needle,' std::expected<void,SdkTagRateError> installImpl(libtorrent::add_torrent_params&,bool checking);\n'+needle);write(O/'sdk_prepared_tag_attachment.hpp',s)
s=source('sdk_tag_rate_group.cpp');needle='std::expected<void,Error> SdkPreparedTagAttachment::install(lt::add_torrent_params& params){';assert s.count(needle)==1;s=s.replace(needle,'''std::expected<void,Error> SdkPreparedTagAttachment::install(lt::add_torrent_params& params){return installImpl(params,false);}
std::expected<void,Error> SdkPreparedTagAttachment::installForChecking(lt::add_torrent_params& params){return installImpl(params,true);}
std::expected<void,Error> SdkPreparedTagAttachment::installImpl(lt::add_torrent_params& params,bool checking){''');needle=' if(!(params.flags&lt::torrent_flags::paused)||(params.flags&lt::torrent_flags::auto_managed)){';assert s.count(needle)==1;s=s.replace(needle,''' const bool paused=bool(params.flags&lt::torrent_flags::paused);
 if((params.flags&lt::torrent_flags::auto_managed)
     ||(checking?(!params.ti||paused||!(params.flags&lt::torrent_flags::stop_when_ready)):!paused)){''');write(O/'sdk_tag_rate_group.cpp',s)
s=source('profile_prepared_tag_attachment.hpp');needle=' std::expected<void,storage::Error> install(libtorrent::add_torrent_params&);';assert s.count(needle)==1;s=s.replace(needle,needle+'''
 // Full metadata check under native stop_when_ready;no later resume grant.
 std::expected<void,storage::Error> installForChecking(libtorrent::add_torrent_params&);''');s=s.replace(' struct Impl;', ' std::expected<void,storage::Error> installImpl(libtorrent::add_torrent_params&,bool checking);\n struct Impl;');write(O/'profile_prepared_tag_attachment.hpp',s)
s=(O/'profile_tag_rates.cpp').read_text();needle='std::expected<void,storage::Error> ProfilePreparedTagAttachment::install(libtorrent::add_torrent_params& params){';assert s.count(needle)==1;s=s.replace(needle,'''std::expected<void,storage::Error> ProfilePreparedTagAttachment::install(libtorrent::add_torrent_params& params){return installImpl(params,false);}
std::expected<void,storage::Error> ProfilePreparedTagAttachment::installForChecking(libtorrent::add_torrent_params& params){return installImpl(params,true);}
std::expected<void,storage::Error> ProfilePreparedTagAttachment::installImpl(libtorrent::add_torrent_params& params,bool checking){''');needle=' if(impl_->phase!=Impl::Phase::Fresh||!(params.flags&libtorrent::torrent_flags::paused)';assert s.count(needle)==1;s=s.replace(needle,''' const bool paused=bool(params.flags&libtorrent::torrent_flags::paused);
 if(impl_->phase!=Impl::Phase::Fresh
     ||(checking?(!params.ti||paused||!(params.flags&libtorrent::torrent_flags::stop_when_ready)):!paused)''');s=s.replace('if(!token->install(staged))','if(!(checking?token->installForChecking(staged):token->install(staged)))');write(O/'profile_tag_rates.cpp',s)
(D/'pre-implementation.json').write_text(json.dumps(P,indent=2)+'\n');print('Staged explicit full-metadata check-only add;ordinary paused API stays strict,SDK unchanged')

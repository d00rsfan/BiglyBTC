from pathlib import Path
import hashlib,json,argparse
n=Path('/home/yurii/Java/BiglyBTC');w=Path(__file__).resolve().parent;p=argparse.ArgumentParser();p.add_argument('--apply',action='store_true');a=p.parse_args();edits={}
def change(rel,old,new):
 f=n/rel;s=edits.get(rel,f.read_text());assert s.count(old)==1,old;edits[rel]=s.replace(old,new)
rel='preview/native/engine/persistent_session.cpp'
change(rel,'''  if (options.manuallyPaused && !impl_->queuePolicy) {
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported, "Paused verification requires the original-rule queue controller"});
  }
''','')
change(rel,'''    if (impl_->queuePolicy) {
      parameters.flags &= ~lt::torrent_flags::auto_managed;
      parameters.flags |= lt::torrent_flags::stop_when_ready;
    }''','''    if (impl_->queuePolicy || options.manuallyPaused) {
      parameters.flags &= ~lt::torrent_flags::auto_managed;
      parameters.flags |= lt::torrent_flags::stop_when_ready;
    }''')
change(rel,'if (impl_->queuePolicy || !options.queued || options.forceStart) { slot->second->handle.resume(); }','if (impl_->queuePolicy || options.manuallyPaused || !options.queued || options.forceStart) { slot->second->handle.resume(); }')
change(rel,'    if (!value.forceStart && !impl_->queuePolicy) { value.handle.set_flags(lt::torrent_flags::auto_managed); }','    if (!impl_->queuePolicy) { value.handle.unset_flags(lt::torrent_flags::stop_when_ready); }\n    if (!value.forceStart && !impl_->queuePolicy) { value.handle.set_flags(lt::torrent_flags::auto_managed); }')
change(rel,'  value.manualPaused = false; value.forceStart = forceStart;','  value.manualPaused = false; value.forceStart = forceStart;\n  if (!impl_->queuePolicy) { value.handle.unset_flags(lt::torrent_flags::stop_when_ready); }')
change('preview/native/ui/torrent_creation_dialog.cpp','Add to My Torrents after successful creation','Add to My Torrents (stopped) after successful creation')
receipt={}
for rel,s in edits.items():
 before=hashlib.sha256((n/rel).read_bytes()).hexdigest();out=w/'corrections/staging'/rel;out.parent.mkdir(parents=True,exist_ok=True);out.write_text(s);receipt[rel]={'before_sha256':before,'after_sha256':hashlib.sha256(out.read_bytes()).hexdigest()}
 if a.apply:(n/rel).write_text(s)
(w/'corrections').mkdir(exist_ok=True);(w/'corrections/receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(('Applied ' if a.apply else 'Prepared ')+str(len(edits))+' file corrections')

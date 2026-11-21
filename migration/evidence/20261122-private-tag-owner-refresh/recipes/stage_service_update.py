from pathlib import Path
import hashlib,json
D=Path(__file__).resolve().parent;N=D.parents[1].parent/'BiglyBTC/preview/native';O=D/'drafts';B=D/'baseline';pins={}
for name in ['app/owned_i2p_service.hpp','app/owned_i2p_service.cpp']:
 target=B/name;target.parent.mkdir(parents=True,exist_ok=True)
 if not target.exists():target.write_bytes((N/name).read_bytes())
 pins[name]=hashlib.sha256(target.read_bytes()).hexdigest();s=target.read_text()
 if name.endswith('.hpp'):
  needle='  std::expected<void,storage::Error> stop(const core::TorrentHash&);';assert s.count(needle)==1
  s=s.replace(needle,'''  // Original Host I/O worker only. Quiesce both private source readers,run
  // one real source/CAS callback,apply SDK effects and publish the full batch.
  // Controller reference is used synchronously;ordinary writers remain strict.
  std::expected<void,storage::Error> updateTagSource(engine::ProfileTagRates&,
      const std::function<std::expected<void,storage::Error>()>&);
'''+needle)
 else:
  needle='std::expected<void,storage::Error> OwnedI2pService::stop(const core::TorrentHash& h){';assert s.count(needle)==1
  s=s.replace(needle,'''std::expected<void,storage::Error> OwnedI2pService::updateTagSource(engine::ProfileTagRates& rates,const std::function<std::expected<void,storage::Error>()>& operation){
  auto& s=*impl_;std::lock_guard lock(s.mutex);
  if(s.closed||s.checkpointOmitted||QThread::currentThread()!=s.caller||!operation){return std::unexpected(Error{Code::Conflict,"Current private Host worker/source callback required"});}
  auto preflight=s.owner->preflightTagSourceUpdate(rates);if(!preflight){return preflight;}
  std::expected<std::unique_ptr<provider::OwnedProfile::TagSourceUpdate>,Error> update=std::unexpected(Error{Code::Io,"Private event loop unavailable"});
  if(!s.onOwner([&]{update=s.owner->beginTagSourceUpdate();})||!update){return update?std::unexpected(Error{Code::Io,"Cannot quiesce private source readers"}):std::unexpected(update.error());}
  auto joined=s.owner->joinTagSourceUpdate(**update);
  std::expected<void,Error> changed=std::unexpected(Error{Code::Io,"Private source workers did not join"});
  if(joined){try{changed=operation();}catch(...){changed=std::unexpected(Error{Code::Io,"Tag source callback failed"});}}
  // Rebase an unchanged/refused CAS too:cooperative source epochs may advance.
  auto prepared=joined?s.owner->prepareTagSourceUpdate(**update,rates):joined;
  bool resumed=false;
  if(!s.onOwner([&]{resumed=s.owner->finishTagSourceUpdate(std::move(*update));s.capture();})){
    s.owner->revokeSource();return std::unexpected(Error{Code::Io,"Private event loop unavailable after tag source update"});
  }
  if(!resumed){s.owner->join();return prepared?std::unexpected(Error{Code::Conflict,"Private source withdrew during tag refresh"}):prepared;}
  return prepared?changed:prepared;
}
'''+needle)
  needle='std::expected<void,storage::Error> OwnedI2pService::stop(const core::TorrentHash&){';assert s.count(needle)==1
  s=s.replace(needle,'''std::expected<void,storage::Error> OwnedI2pService::updateTagSource(engine::ProfileTagRates&,const std::function<std::expected<void,storage::Error>()>&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
'''+needle)
 out=O/name;out.parent.mkdir(parents=True,exist_ok=True);out.write_text(s)
(D/'service-update-baseline-sha256.json').write_text(json.dumps(pins,indent=2)+'\n');print('Staged synchronous service source transaction;no retained controller or Native mutation')

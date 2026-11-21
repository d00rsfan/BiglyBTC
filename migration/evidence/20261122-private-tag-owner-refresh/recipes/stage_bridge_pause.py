from pathlib import Path
import hashlib,json,shutil
D=Path(__file__).resolve().parent;R=D.parents[1];N=R.parent/'BiglyBTC/preview/native';O=D/'drafts';O.mkdir(exist_ok=True);B=D/'baseline';B.mkdir(exist_ok=True);pins={}
for name in ['services/i2p_provider/source_permit.hpp','services/i2p_provider/bridge.hpp','services/i2p_provider/bridge.cpp']:
 data=(N/name).read_bytes();target=B/name;target.parent.mkdir(parents=True,exist_ok=True)
 if not target.exists():target.write_bytes(data)
 pins[name]=hashlib.sha256(target.read_bytes()).hexdigest()
 def write(s):
  target=O/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(s)
 s=target.read_text()
 if name.endswith('source_permit.hpp'):
  needle='  void join()noexcept{if(worker_.joinable()){worker_.join();}}';assert s.count(needle)==1;s=s.replace(needle,needle+'\n  // Read only after the caller has synchronized with the actual I/O join.\n  bool joined()const noexcept{return !worker_.joinable();}');write(s)
 elif name.endswith('bridge.hpp'):
  needle='  friend class OwnedProfile;';assert s.count(needle)==1;s=s.replace(needle,needle+'''\n  // Trusted owned tag-source transaction only. Stop on Qt,join on Host I/O,\n  // restart on Qt after the full current source/SDK batch is published.\n  bool pauseSourceValidation();\n  bool resumeSourceValidation();''');write(s)
 else:
  needle='QTcpServer server;QTimer timer;std::unique_ptr<SourcePermit> permit;';assert s.count(needle)==1;s=s.replace(needle,needle+'bool sourcePaused=false;')
  needle='      if(!permit){arm();return;}';assert s.count(needle)==1;s=s.replace(needle,'      if(sourcePaused||!permit){arm();return;}')
  needle='void Bridge::joinSource(){if(impl_->permit){impl_->permit->join();}}';assert s.count(needle)==1
  s=s.replace(needle,'''bool Bridge::pauseSourceValidation(){
 auto& s=*impl_;
 if(!s.onThread()||s.sourcePaused||s.phase==Phase::Idle||s.phase==Phase::Withdrawn||!s.permit
     ||s.permit->phase()==SourcePermit::Phase::Executing){return false;}
 s.sourcePaused=true;s.permit->stop();return true;
}
bool Bridge::resumeSourceValidation(){
 auto& s=*impl_;
 if(!s.onThread()||!s.sourcePaused||s.phase==Phase::Idle||s.phase==Phase::Withdrawn
     ||!s.permit||!s.permit->joined()||!s.source->prepared()){return false;}
 try{s.permit=std::make_unique<SourcePermit>(s.source,this);s.sourcePaused=false;s.schedule();return true;}
 catch(...){s.close("Cannot restart private source validation");return false;}
}
'''+needle);write(s)
(D/'bridge-baseline-sha256.json').write_text(json.dumps(pins,indent=2)+'\n');print('Staged explicit two-phase Bridge source worker pause/join/restart;Native unchanged')

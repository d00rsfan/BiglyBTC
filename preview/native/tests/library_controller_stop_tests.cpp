// SPDX-License-Identifier: GPL-2.0-or-later
#include "library_controller_stop.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace s=biglybtc::storage;namespace b=biglybtc::core;
namespace {
void need(bool ok,const char* error){if(!ok){throw std::runtime_error(error);}}
template<class T>T take(std::expected<T,s::Error> result){if(!result){throw std::runtime_error(result.error().message);}return std::move(*result);}
std::string bytes(std::string_view text){return std::to_string(text.size())+':'+std::string(text);}
std::string dict(std::map<std::string,std::string> fields){std::string text="d";for(const auto& [key,value]:fields){text+=bytes(key)+value;}return text+'e';}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);need(bool(file),"Generated controller intent read");return {std::istreambuf_iterator<char>(file),{}};}
struct Area{
 std::filesystem::path path;
 Area(){std::string pattern="/tmp/biglybtc-controller-stop-XXXXXX";auto root=::mkdtemp(pattern.data());need(root,"Generated controller directory");path=root;need(::chmod(path.c_str(),0700)==0,"Generated controller mode");}
 ~Area(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
 void write(std::string_view text){std::ofstream file(path/"downloads.config",std::ios::binary|std::ios::trunc);need(bool(file),"Generated controller source open");file<<text;file.close();need(bool(file),"Generated controller source write");}
};
std::string source(const b::TorrentHash& hash,int state,bool force){
 const auto raw=std::string(reinterpret_cast<const char*>(hash.bytes().data()),hash.bytes().size());
 return dict({{"downloads","l"+dict({{"forceStart",force?"i1e":"i0e"},{"persistent","i1e"},{"position","i1e"},{"save_dir",bytes("/absent-generated-controller-payload")},{"state","i"+std::to_string(state)+"e"},{"torrent",bytes("generated.torrent")},{"torrent_hash",bytes(raw)}})+"e"},{"opaque",bytes("preserved")}});
}
void actualControllerValues(){
 auto fixture=b::Document::parse(read(CONTROLLER_STOP_ORIGINAL_FIXTURE));need(bool(fixture),"Original controller fixture");const auto hash=*b::TorrentHash::parse(std::string(20,'x'));unsigned rows=0;
 for(const auto& row:fixture->find("rows")->children){Area area;const auto state=int(row.find("state")->integer);const bool force=row.find("force")->integer;area.write(source(hash,state,force));auto lease=take(s::ProfileLease::acquire(area.path));
  auto result=take(s::applyLibraryControllerStop(lease,hash,static_cast<s::LibraryControllerStopTarget>(row.find("target")->integer),{state,false}));
  need(result.after.originalState==row.find("after_state")->integer&&result.after.forceStart==bool(row.find("after_force")->integer),"Actual original controller stop intent differs");
  auto saved=b::Document::parse(read(area.path/"downloads.config"));need(saved&&saved->find("opaque")->bytes=="preserved","Controller stop changed unknown metadata");
  if(state==65){need(!result.changed&&read(area.path/"downloads.config")==source(hash,state,force),"Already stopping source was rewritten");}
  area.write(source(hash,state,force));
  auto queued=take(s::applyLibraryControllerQueue(lease,hash));
  need(queued.after.originalState==row.find("queue_state")->integer&&queued.after.forceStart==bool(row.find("queue_force")->integer),"Actual original direct queued-state setter differs");
  ++rows;
 }need(rows==36,"Original controller rows lost");
}
void wrapperAndDirectQueueRemainDistinct(){Area area;const auto hash=*b::TorrentHash::parse(std::string(20,'q'));area.write(source(hash,0,true));auto lease=take(s::ProfileLease::acquire(area.path));auto wrapper=take(s::applyLibraryCommand(lease,hash,b::DownloadAction::Queue));need(!wrapper.changed&&wrapper.after.originalState==0&&wrapper.after.forceStart,"UI Queue wrapper semantics changed");auto direct=take(s::applyLibraryControllerStop(lease,hash,s::LibraryControllerStopTarget::Queued,{50,true}));need(direct.changed&&direct.after.originalState==75&&!direct.after.forceStart,"Active controller stop-to-queue didn't clear force");need(!s::applyLibraryControllerStop(lease,hash,static_cast<s::LibraryControllerStopTarget>(123),{50,true}),"Unknown stop target accepted");}
}
int main(){try{actualControllerValues();wrapperAndDirectQueueRemainDistinct();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}std::cout<<"PASS36 actual original controller stop rows and36 direct queued-state setter results;direct stop-to-queue,force shortcuts,stopping no-op,unknown preservation and unchanged UI wrapper\n";}

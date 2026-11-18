// SPDX-License-Identifier: GPL-2.0-or-later
#include <libtorrent/version.hpp>
#include <libtorrent/biglybtc_tag_gate.hpp>
#include <libtorrent/peer_class_set.hpp>
#include <libtorrent/aux_/bandwidth_manager.hpp>
#include <libtorrent/aux_/bandwidth_socket.hpp>
#include <libtorrent/time.hpp>
#include <algorithm>
#include <dlfcn.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>
namespace lt=libtorrent;
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
bool compatible(){
 auto abi=reinterpret_cast<std::uint64_t(*)() noexcept>(::dlsym(RTLD_DEFAULT,"biglybtc_tag_class_abi_v1"));
 Dl_info sdk{},actual{};
 return abi&&::dladdr(reinterpret_cast<void*>(&lt::version),&sdk)&&::dladdr(reinterpret_cast<void*>(abi),&actual)
     &&sdk.dli_fbase==actual.dli_fbase&&abi()==TORRENT_BIGLYBTC_TAG_CLASS_ABI;
}
struct Socket final:lt::aux::bandwidth_socket {
 int granted=0;void assign_bandwidth(int,int bytes)override{granted+=bytes;}
 bool is_disconnecting()const override{return false;}
};
void classes(int count){
 lt::peer_class_pool pool;lt::peer_class_set set;std::vector<lt::peer_class_t> ids;
 for(int i=0;i<count;++i){
  auto id=pool.new_peer_class("Generated class");ids.push_back(id);set.add_class(pool,id);set.add_class(pool,id);
  pool.decref(id); // The set must now hold the only reference.
  check(set.num_classes()==i+1&&set.has_class(id)&&pool.at(id),"duplicate/add/refcount mismatch");
 }
 for(int position:{0,count/2,count-1}){
  for(auto id:ids){pool.at(id)->set_upload_limit(4096);}
  pool.at(ids[position])->set_upload_limit(1);std::vector<lt::aux::bandwidth_channel*> channels;
  for(int i=0;i<set.num_classes();++i){channels.push_back(&pool.at(set.class_at(i))->channel[0]);}
  auto peer=std::make_shared<Socket>();lt::aux::bandwidth_manager manager(0);
  check(manager.request_bandwidth(peer,4096,1,channels.data(),int(channels.size()))==0,"class quota request not queued");manager.update_quotas(lt::seconds(1));
  check(peer->granted==0&&manager.queued_bytes()==4095,"a class limiter was omitted");manager.close();
  // Each position test gets independent fresh quota by replacing the class data.
  if(position!=count-1){
   for(auto id:ids){set.remove_class(pool,id);check(pool.at(id)==nullptr,"class ref leaked after quota close");}
   ids.clear();
   for(int i=0;i<count;++i){auto id=pool.new_peer_class("Generated class");ids.push_back(id);set.add_class(pool,id);pool.decref(id);}
  }
 }
 auto expected=ids;
 // Remove in allocation order while checking the original swap-last ordering.
 for(auto id:ids){
  auto at=std::find(expected.begin(),expected.end(),id);*at=expected.back();expected.pop_back();
  set.remove_class(pool,id);set.remove_class(pool,id);
  check(!set.has_class(id)&&!pool.at(id)&&set.num_classes()==int(expected.size()),"remove/refcount mismatch");
  for(int i=0;i<set.num_classes();++i){check(set.class_at(i)==expected[i],"class order changed");}
 }
 check(set.num_classes()==0,"classes not retired");
}
int main(int argc,char** argv){try{
 // Admission precedes every operation on the changed private C++ structures.
 const bool expected=argc<2||std::string_view(argv[1])!="reject";const bool match=compatible();
 check(match==expected,"private SDK class ABI admission mismatch");
 if(!match){std::cout<<"PASS incompatible/interposed class ABI refused before private class access\n";return 0;}
 for(int count:{1,14,15,32,64,256,1024}){classes(count);}
 std::cout<<"PASS dynamic SDK class sets:7 sizes,duplicates,pool refs,swap-last removal,21 limiter positions;sizeof="<<sizeof(lt::peer_class_set)<<'\n';
 }catch(std::exception const& e){std::cerr<<"FAIL class set: "<<e.what()<<'\n';return 1;}}

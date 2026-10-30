#pragma once
#include "libtorrent/biglybtc_private_i2p_owner.hpp"
#include "libtorrent/info_hash.hpp"
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>

namespace libtorrent { namespace aux {
enum class i2p_owner_purpose {Control,Peer,Tracker};
struct i2p_owner_context final {
  explicit i2p_owner_context(BiglybtcPrivateI2pOwnerPolicyV1 const& value):policy(value){}
  ~i2p_owner_context(){wipe();}
  i2p_owner_context(i2p_owner_context const&)=delete;
  void wipe()noexcept{volatile unsigned char* p=policy.capability.data();for(std::size_t i=0;i<policy.capability.size();++i){p[i]=0;}}
  void withdraw()noexcept{active.store(false,std::memory_order_release);}
  bool current()const noexcept{return active.load(std::memory_order_acquire);}
  static bool valid(BiglybtcPrivateI2pOwnerPolicyV1 const& p)noexcept{
    return p.version==1&&p.size==sizeof(p)&&p.generation&&p.admission&&p.bridge_port&&p.peer_port&&p.local_peer_port
      &&!p.reserved&&p.hashes&&!(p.hashes&~3U)
      &&std::any_of(p.capability.begin(),p.capability.end(),[](unsigned char v){return v!=0;});
  }
  bool matches(info_hash_t const& hashes)const noexcept{
    if(!current())return false;
    if(bool(policy.hashes&1U)!=hashes.has_v1()||bool(policy.hashes&2U)!=hashes.has_v2())return false;
    if(hashes.has_v1()&&!std::equal(policy.torrent_v1.begin(),policy.torrent_v1.end(),reinterpret_cast<unsigned char const*>(hashes.v1.data())))return false;
    return !hashes.has_v2()||std::equal(policy.torrent_v2.begin(),policy.torrent_v2.end(),reinterpret_cast<unsigned char const*>(hashes.v2.data()));
  }
  static std::string hex(unsigned char const* data,std::size_t size){
    static constexpr char alphabet[]="0123456789abcdef";std::string result(size*2,'0');
    for(std::size_t i=0;i<size;++i){result[2*i]=alphabet[data[i]>>4];result[2*i+1]=alphabet[data[i]&15];}return result;
  }
  BiglybtcPrivateI2pOwnerPolicyV1 policy;
  std::atomic<bool> active{true};
  bool started=false; // Actual provider executor only; no renewal/rebind after disable.
};
// Commands must outlive partial asynchronous writes, then erase their ephemeral
// capability bytes. This object is never passed to logging or observations.
struct i2p_owner_command final {
  explicit i2p_owner_command(std::string value):bytes(std::move(value)){}
  ~i2p_owner_command(){volatile char* p=bytes.data();for(std::size_t i=0;i<bytes.size();++i){p[i]=0;}}
  std::string bytes;
};
}} // namespace libtorrent::aux

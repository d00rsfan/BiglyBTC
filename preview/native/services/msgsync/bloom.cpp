// SPDX-License-Identifier: GPL-2.0-or-later
// Source: original core BloomFilterImpl/BloomFilterAddOnly, cb8c6cb9.
#include "bloom.hpp"
#include <openssl/rand.h>
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <utility>

namespace biglybtc::services::msgsync {
namespace {
core::Node bytes(std::string_view value){core::Node node(core::Node::Type::Bytes);node.bytes=value;return node;}
core::Node integer(std::int64_t value){core::Node node(core::Node::Type::Integer);node.integer=value;return node;}
bool unique(const core::Node& node){
  if(node.type!=core::Node::Type::Dictionary){return false;}
  for(std::size_t n=0;n<node.children.size();n+=2){for(std::size_t m=0;m<n;m+=2){if(node.children[n].bytes==node.children[m].bytes){return false;}}}return true;
}
}
AddOnlyBloom::AddOnlyBloom(std::uint32_t bits,std::uint32_t count,std::string map):bits_(bits),count_(count),map_(std::move(map)){}
Result<AddOnlyBloom> AddOnlyBloom::create(std::uint32_t requested){return createBounded(requested,MaximumBloomBits);}
Result<AddOnlyBloom> AddOnlyBloom::createForRelatedSearch(std::uint32_t requested){return createBounded(requested,50001);}
Result<AddOnlyBloom> AddOnlyBloom::createBounded(std::uint32_t requested,std::uint32_t maximum){
  if(requested==0||requested>maximum){return std::unexpected(Error::Limit);}
  const auto bits=(requested/2)*2+1;if(bits>maximum){return std::unexpected(Error::Limit);}
  try{return AddOnlyBloom(bits,0,std::string((bits+7)/8,'\0'));}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}
}
std::array<std::uint32_t,5> AddOnlyBloom::indices(std::string_view bytes)const noexcept{
  std::uint32_t value=0x51f7ac81u;for(unsigned char byte:bytes){value=value*191u+byte;}
  // Unsigned operations deliberately preserve Java int wrapping. Widening the
  // square or affine operations before wrapping changes serialized membership.
  const std::array hashes{value,value*value,value*2u+51u,value*3u+145u,value*5u+216u};
  std::array<std::uint32_t,5> result{};
  for(std::size_t n=0;n<hashes.size();++n){const auto remainder=std::bit_cast<std::int32_t>(hashes[n])%static_cast<std::int32_t>(bits_);result[n]=static_cast<std::uint32_t>(remainder<0?-remainder:remainder);}return result;
}
bool AddOnlyBloom::contains(std::string_view value)const noexcept{
  for(const auto index:indices(value)){if((static_cast<unsigned char>(map_[index/8])&(1u<<(index%8)))==0){return false;}}return true;
}
void AddOnlyBloom::add(std::string_view value)noexcept{
  bool changed=false;for(const auto index:indices(value)){const auto old=static_cast<unsigned char>(map_[index/8]);const auto next=old|(1u<<(index%8));changed|=next!=old;map_[index/8]=static_cast<char>(next);}
  if(changed&&count_!=std::numeric_limits<std::uint32_t>::max()){++count_;}
}
Result<std::string> AddOnlyBloom::encode()const{
  try{core::Node root(core::Node::Type::Dictionary);root.children={bytes("_count"),integer(count_),bytes("_impl"),bytes(".BloomFilterAddOnly"),bytes("_max"),integer(bits_),bytes("map"),bytes(map_)};
    auto encoded=core::encodeValue(root);if(!encoded){return std::unexpected(Error::Input);}return *encoded;
  }catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}
}
Result<AddOnlyBloom> AddOnlyBloom::decode(const core::Node& node){return decodeBounded(node,MaximumBloomBits,512);}
Result<AddOnlyBloom> AddOnlyBloom::decodeForRelatedSearch(const core::Node& node){return decodeBounded(node,50001,6251);}
Result<AddOnlyBloom> AddOnlyBloom::decodeBounded(const core::Node& node,std::uint32_t maximumBits,std::uint32_t maximumCount){
  if(!unique(node)){return std::unexpected(Error::Input);}
  const auto* impl=node.find("_impl");const auto* bits=node.find("_max");const auto* count=node.find("_count");const auto* map=node.find("map");
  if(!impl||impl->type!=core::Node::Type::Bytes||(impl->bytes!=".BloomFilterAddOnly"&&impl->bytes!="com.biglybt.core.util.bloom.impl.BloomFilterAddOnly")||!bits||bits->type!=core::Node::Type::Integer||!count||count->type!=core::Node::Type::Integer||!map||map->type!=core::Node::Type::Bytes){return std::unexpected(Error::Input);}
  if(bits->integer<1||bits->integer>maximumBits||count->integer<0||count->integer>maximumCount||map->bytes.size()!=static_cast<std::size_t>((bits->integer+7)/8)){return std::unexpected(Error::Limit);}
  try{return AddOnlyBloom(static_cast<std::uint32_t>(bits->integer),static_cast<std::uint32_t>(count->integer),std::string(map->bytes));}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}
}
Result<std::string> maskBloomKey(std::string_view value,std::span<const std::uint8_t,8> random){
  if(value.size()<random.size()||value.size()>1024){return std::unexpected(Error::Limit);}
  try{std::string result(value);for(std::size_t n=0;n<random.size();++n){result[n]=static_cast<char>(static_cast<unsigned char>(result[n])^random[n]);}return result;}
  catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}
}
Result<SyncBloom> buildSyncBloom(std::span<const std::string> keys){
  if(keys.size()>384){return std::unexpected(Error::Limit);}
  for(const auto& key:keys){if(key.size()<8||key.size()>1024){return std::unexpected(Error::Limit);}}
  try{
    // Remove byte-identical keys as original ByteArrayHashMap does. No supplied
    // random bytes serve as authority; these are fresh optimization masks only.
    std::vector<std::string_view> distinct;distinct.reserve(keys.size());for(const auto& key:keys){if(std::find(distinct.begin(),distinct.end(),key)==distinct.end()){distinct.emplace_back(key);}}
    for(unsigned attempt=0;attempt<64;++attempt){
      std::array<std::uint8_t,8> random{};unsigned char jitter=0;
      if(RAND_priv_bytes(random.data(),static_cast<int>(random.size()))!=1){return std::unexpected(Error::Crypto);}
      bool sampled=false;for(unsigned n=0;n<64;++n){if(RAND_priv_bytes(&jitter,1)!=1){return std::unexpected(Error::Crypto);}if(jitter<247){sampled=true;break;}}
      if(!sampled){return std::unexpected(Error::Crypto);}
      const auto bits=static_cast<std::uint32_t>(std::max<std::size_t>(64,distinct.size()*10+jitter%19));auto bloom=AddOnlyBloom::create(bits);if(!bloom){return std::unexpected(bloom.error());}
      bool collided=false;for(const auto key:distinct){auto masked=maskBloomKey(key,random);if(!masked){return std::unexpected(masked.error());}if(bloom->contains(*masked)){collided=true;break;}bloom->add(*masked);}
      if(!collided){return SyncBloom{random,std::move(*bloom)};}
    }
    return std::unexpected(Error::Limit); // Original buildBloom returns null.
  }catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}
}
}

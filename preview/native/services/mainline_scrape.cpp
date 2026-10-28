// SPDX-License-Identifier: MPL-2.0
#include "mainline_scrape.hpp"
#include <biglybtc/bencode.hpp>
#include <openssl/evp.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
namespace biglybtc::services::mainline_scrape {
namespace {
bool zero(std::span<const std::uint8_t> bytes)noexcept{return std::ranges::all_of(bytes,[](auto b){return b==0;});}
std::int32_t javaInt(double n)noexcept {
  if(std::isnan(n)){return 0;}
  if(n>=std::numeric_limits<std::int32_t>::max()){return std::numeric_limits<std::int32_t>::max();}
  if(n<=std::numeric_limits<std::int32_t>::min()){return std::numeric_limits<std::int32_t>::min();}
  return static_cast<std::int32_t>(n);
}
}
std::expected<Address,Error> address(std::span<const std::uint8_t> raw){
  if(raw.size()!=4&&raw.size()!=16){return std::unexpected(Error::Invalid);}
  // InetAddress.getByAddress canonicalizes IPv4-mapped IPv6 to Inet4Address.
  if(raw.size()==16&&zero(raw.first(10))&&raw[10]==255&&raw[11]==255){raw=raw.last(4);}
  Address result;result.length=static_cast<std::uint8_t>(raw.size());std::copy(raw.begin(),raw.end(),result.bytes.begin());return result;
}
bool permittedDirect(const Address& input,std::uint16_t port)noexcept {
  if(!port){return false;}const auto normalized=address(std::span(input.bytes).first(input.length<=16?input.length:0));if(!normalized){return false;}
  const auto& a=normalized->bytes;
  if(normalized->length==4){
    return !(a[0]==0||a[0]==10||a[0]==127||(a[0]==169&&a[1]==254)
        ||(a[0]==172&&(a[1]&240)==16)||(a[0]==192&&a[1]==168)||(a[0]&240)==224);
  }
  const bool any=zero(a),loop=zero(std::span(a).first(15))&&a[15]==1;
  return !(any||loop||a[0]==255||(a[0]==254&&((a[1]&192)==128||(a[1]&192)==192)));
}
std::expected<Bloom,Error> Bloom::parse(std::string_view raw){
  if(raw.size()!=Bytes){return std::unexpected(Error::Invalid);}Bloom result;
  std::transform(raw.begin(),raw.end(),result.bytes_.begin(),[](char b){return static_cast<std::uint8_t>(static_cast<unsigned char>(b));});return result;
}
std::expected<void,Error> Bloom::insert(const Address& input)noexcept {
  const auto canonical=address(std::span(input.bytes).first(input.length<=16?input.length:0));if(!canonical){return std::unexpected(canonical.error());}
  std::array<unsigned char,EVP_MAX_MD_SIZE> hash{};unsigned length=0;
  if(EVP_Digest(canonical->bytes.data(),canonical->length,hash.data(),&length,EVP_sha1(),nullptr)!=1||length!=20){return std::unexpected(Error::Crypto);}
  for(unsigned offset:{0U,2U}){const auto index=(static_cast<unsigned>(hash[offset])|(static_cast<unsigned>(hash[offset+1])<<8))%Bits;bytes_[index/8]|=static_cast<std::uint8_t>(1U<<(index%8));}return {};
}
void Bloom::merge(const Bloom& other)noexcept{for(std::size_t i=0;i<Bytes;++i){bytes_[i]|=other.bytes_[i];}}
std::int32_t Bloom::estimate()const noexcept {
  unsigned count=0;for(auto b:bytes_){count+=static_cast<unsigned>(std::popcount(b));}
  return javaInt(std::log1p(-static_cast<double>(count)/Bits)/(2*std::log1p(-1.0/Bits)));
}
std::expected<Response,Error> decode(std::string_view wire,Dialect dialect){
  if(wire.size()>16384){return std::unexpected(Error::Limit);}
  try{
    core::Limits limits;limits.inputBytes=16384;limits.stringBytes=16384;limits.nodes=512;limits.depth=4;
    auto doc=core::Document::parse(std::string(wire),limits);if(!doc||doc->root().type!=core::Node::Type::Dictionary){return std::unexpected(Error::Invalid);}
    // The bounded shared parser rejects duplicate keys/noncanonical tokens.
    const auto id=doc->find("id");if(!id||id->type!=core::Node::Type::Bytes||id->bytes.size()!=20){return std::unexpected(Error::Invalid);}
    const auto se=doc->find("BFse"),sd=doc->find("BFsd");
    if((se&&sd)||(dialect==Dialect::OriginalBFse?sd!=nullptr:se!=nullptr)){return std::unexpected(Error::AmbiguousDialect);}
    Response result;
    const auto filter=[&](const core::Node* n,std::optional<Bloom>& target)->bool{if(!n){return true;}if(n->type!=core::Node::Type::Bytes){return false;}auto b=Bloom::parse(n->bytes);if(!b){return false;}target=std::move(*b);return true;};
    if(!filter(dialect==Dialect::OriginalBFse?se:sd,result.seeds)||!filter(doc->find("BFpe"),result.peers)){return std::unexpected(Error::Invalid);}
    bool carrier=false;
    for(const auto& field:{std::pair{"nodes",26U},std::pair{"nodes6",38U}}){const auto n=doc->find(field.first);
      if(n&&n->type==core::Node::Type::Bytes){if(n->bytes.size()%field.second){return std::unexpected(Error::Invalid);}carrier=true;}}
    if(const auto values=doc->find("values")){
      if(values->type!=core::Node::Type::List){return std::unexpected(Error::Invalid);}if(values->children.size()>MaximumPeers){return std::unexpected(Error::Limit);}
      carrier=carrier||!values->children.empty();
      for(const auto& value:values->children){if(value.type!=core::Node::Type::Bytes){return std::unexpected(Error::Invalid);}const auto raw=value.bytes;
        if(raw.size()!=6&&raw.size()!=18){continue;}const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(raw.data()),raw.size());
        auto a=address(bytes.first(bytes.size()-2));const auto port=static_cast<std::uint16_t>((static_cast<unsigned>(bytes[bytes.size()-2])<<8)|bytes.back());
        if(a&&permittedDirect(*a,port)){result.direct.push_back(*a);}}
    }
    if(!carrier){return std::unexpected(Error::Invalid);}return result;
  }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<void,Error> Aggregate::add(const Response& response){
  if(cancelled_){return std::unexpected(Error::Cancelled);}if(closed_){return std::unexpected(Error::Closed);}
  if(responses_==MaximumResponses||response.direct.size()>MaximumPeers){return std::unexpected(Error::Limit);}
  try{
    auto next=direct_;auto peers=response.peers.value_or(Bloom{});
    for(const auto& raw:response.direct){auto a=address(std::span(raw.bytes).first(raw.length<=16?raw.length:0));if(!a){return std::unexpected(Error::Invalid);}
      if(std::ranges::find(next,*a)==next.end()){next.push_back(*a);}if(!response.peers){if(auto added=peers.insert(*a);!added){return added;}}}
    if(next.size()>MaximumResponses*MaximumPeers){return std::unexpected(Error::Limit);}
    auto seeds=seeds_;if(response.seeds){seeds.merge(*response.seeds);}auto unionPeers=peers_;unionPeers.merge(peers);
    direct_=std::move(next);seeds_=seeds;peers_=unionPeers;++responses_;return {};
  }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<Summary,Error> Aggregate::finish()noexcept {
  if(cancelled_){return std::unexpected(Error::Cancelled);}if(closed_){return std::unexpected(Error::Closed);}closed_=true;
  return Summary{seeds_.estimate(),peers_.estimate(),direct_.size(),responses_};
}
void Aggregate::cancel()noexcept{seeds_=Bloom{};peers_=Bloom{};direct_.clear();responses_=0;closed_=true;cancelled_=true;}
}

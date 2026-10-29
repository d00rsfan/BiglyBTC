// SPDX-License-Identifier: GPL-2.0-or-later
#include "leaseset.hpp"
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <algorithm>
#include <limits>
#include <memory>

namespace biglybtc::services::i2p::i2cp {
namespace {
using Code=::biglybtc::services::i2p::ErrorCode;
auto error(Code code,const char* text){return std::unexpected(::biglybtc::services::i2p::Error{code,text});}
using Bn=std::unique_ptr<BIGNUM,decltype(&BN_clear_free)>;
using Context=std::unique_ptr<BN_CTX,decltype(&BN_CTX_free)>;
Bn bn(){return Bn(BN_secure_new(),BN_clear_free);}
Bn prime(){return Bn(BN_get_rfc3526_prime_2048(nullptr),BN_clear_free);}
bool equal(View a,View b){return a.size()==b.size()&&CRYPTO_memcmp(a.data(),b.data(),a.size())==0;}
void put(Bytes& out,std::uint64_t n,unsigned width){for(unsigned i=width;i>0;--i){out.push_back(static_cast<Byte>(n>>((i-1)*8)));}}
struct Reader {View in;std::size_t at=0;bool take(std::size_t n,View& out){if(n>in.size()-at){return false;}out=in.subspan(at,n);at+=n;return true;}bool number(unsigned n,std::uint64_t& out){View b;if(!take(n,b)){return false;}out=0;for(auto c:b){out=(out<<8)|c;}return true;}};
bool javaTrue(std::string_view s){return s.size()==4&&(s[0]=='t'||s[0]=='T')&&(s[1]=='r'||s[1]=='R')&&(s[2]=='u'||s[2]=='U')&&(s[3]=='e'||s[3]=='E');}
ConfigResult<void> leasesValid(const std::vector<RequestedLease>& leases){
  if(leases.empty()||leases.size()>MaximumLeaseCount){return error(Code::Unsupported,"LeaseSet2 requires1..16 actual leases");}
  for(const auto& l:leases){if(l.tunnelId==0||l.expiresMilliseconds==0||l.expiresMilliseconds>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())||l.expiresMilliseconds/1000>0xffffffffULL){return error(Code::InvalidFormat,"Invalid LeaseSet2 tunnel/expiration");}}
  return {};
}
ConfigResult<Bytes> preimage(const SignedLeaseSet2& ls){
  auto actual=decodeDestination(ls.destination.wire);if(!actual){return std::unexpected(actual.error());}
  if(actual->encryptionType!=0){return error(Code::Unsupported,"LeaseSet2 Destination must use default Enc0/signing0 or7");}
  auto valid=leasesValid(ls.leases);if(!valid){return std::unexpected(valid.error());}
  if(ls.publishedMilliseconds==0||ls.publishedMilliseconds%1000||ls.publishedMilliseconds/1000>0xffffffffULL){return error(Code::InvalidFormat,"Invalid LeaseSet2 published time");}
  std::uint64_t latest=0;for(const auto& lease:ls.leases){latest=std::max(latest,lease.expiresMilliseconds/1000);}
  const auto pub=ls.publishedMilliseconds/1000;
  if(latest<pub||latest-pub>65535||ls.expiresMilliseconds!=latest*1000){return error(Code::InvalidFormat,"LeaseSet2 expiration/delta mismatch");}
  // Enforce a genuine group element (not a zero/one placeholder). Exact pair
  // ownership is checked separately before secret I2CP serialization.
  auto p=prime();Bn y(BN_bin2bn(ls.encryptionKey.data(),LeaseEncryptionBytes,nullptr),BN_clear_free);auto upper=bn();
  if(!p||!y||!upper||!BN_copy(upper.get(),p.get())||!BN_sub_word(upper.get(),1)){return error(Code::CryptoFailure,"Lease key validation resources unavailable");}
  if(BN_cmp(y.get(),BN_value_one())<=0||BN_cmp(y.get(),upper.get())>=0){return error(Code::KeyMismatch,"Invalid LeaseSet2 encryption public key");}
  Bytes out;out.reserve(MaximumLeaseSetBytes);out.push_back(StandardLeaseSet2Type);
  out.insert(out.end(),actual->wire.begin(),actual->wire.end());put(out,pub,4);put(out,latest-pub,2);put(out,ls.unpublished?2:0,2);
  put(out,0,2);out.push_back(1);put(out,0,2);put(out,LeaseEncryptionBytes,2);out.insert(out.end(),ls.encryptionKey.begin(),ls.encryptionKey.end());out.push_back(static_cast<Byte>(ls.leases.size()));
  for(const auto& lease:ls.leases){out.insert(out.end(),lease.gateway.begin(),lease.gateway.end());put(out,lease.tunnelId,4);put(out,lease.expiresMilliseconds/1000,4);}
  if(out.size()+64>MaximumLeaseSetBytes){return error(Code::TooLarge,"LeaseSet2 exceeds finite bound");}return out;
}
}
LeaseEncryptionKey::~LeaseEncryptionKey(){clear();}
void LeaseEncryptionKey::clear()noexcept{OPENSSL_cleanse(private_.data(),private_.size());OPENSSL_cleanse(public_.data(),public_.size());valid_=false;}
LeaseEncryptionKey::LeaseEncryptionKey(LeaseEncryptionKey&& x)noexcept:private_(x.private_),public_(x.public_),valid_(x.valid_){x.clear();}
LeaseEncryptionKey& LeaseEncryptionKey::operator=(LeaseEncryptionKey&& x)noexcept{if(this!=&x){clear();private_=x.private_;public_=x.public_;valid_=x.valid_;x.clear();}return *this;}
ConfigResult<LeaseEncryptionKey> LeaseEncryptionKey::fromPrivateKey(View bytes){
  if(bytes.size()!=LeaseEncryptionBytes){return error(Code::InvalidFormat,"ElGamal private key must be256 bytes");}
  auto p=prime(),g=bn(),x=bn(),y=bn(),upper=bn();Context ctx(BN_CTX_secure_new(),BN_CTX_free);
  if(!p||!g||!x||!y||!upper||!ctx||!BN_bin2bn(bytes.data(),static_cast<int>(bytes.size()),x.get())||!BN_copy(upper.get(),p.get())||!BN_sub_word(upper.get(),1)||!BN_set_word(g.get(),2)){return error(Code::CryptoFailure,"ElGamal resources unavailable");}
  if(BN_cmp(x.get(),BN_value_one())<=0||BN_cmp(x.get(),upper.get())>=0){return error(Code::KeyMismatch,"ElGamal private exponent outside2..p-2");}
  BN_set_flags(x.get(),BN_FLG_CONSTTIME);
  if(BN_mod_exp_mont_consttime(y.get(),g.get(),x.get(),p.get(),ctx.get(),nullptr)!=1){return error(Code::CryptoFailure,"ElGamal public derivation failed");}
  if(BN_cmp(y.get(),BN_value_one())<=0||BN_cmp(y.get(),upper.get())>=0){return error(Code::KeyMismatch,"Degenerate ElGamal public key refused");}
  LeaseEncryptionKey key;if(BN_bn2binpad(y.get(),key.public_.data(),LeaseEncryptionBytes)!=static_cast<int>(LeaseEncryptionBytes)){return error(Code::CryptoFailure,"ElGamal public width failed");}
  std::copy(bytes.begin(),bytes.end(),key.private_.begin());key.valid_=true;return key;
}
ConfigResult<LeaseEncryptionKey> LeaseEncryptionKey::generate(){
  auto p=prime(),range=bn(),x=bn();
  if(!p||!range||!x||!BN_copy(range.get(),p.get())||!BN_sub_word(range.get(),3)||!BN_priv_rand_range(x.get(),range.get())||!BN_add_word(x.get(),2)){return error(Code::CryptoFailure,"Secure independent ElGamal generation failed");}
  LeaseEncryptionKey scratch;
  if(BN_bn2binpad(x.get(),scratch.private_.data(),LeaseEncryptionBytes)!=static_cast<int>(LeaseEncryptionBytes)){return error(Code::CryptoFailure,"ElGamal private width failed");}
  return fromPrivateKey(scratch.private_);
}
SecretLeaseFrame::~SecretLeaseFrame(){clear();}
void SecretLeaseFrame::clear()noexcept{OPENSSL_cleanse(bytes_.data(),bytes_.size());size_=0;}
SecretLeaseFrame::SecretLeaseFrame(SecretLeaseFrame&& x)noexcept:bytes_(x.bytes_),size_(x.size_){x.clear();}
SecretLeaseFrame& SecretLeaseFrame::operator=(SecretLeaseFrame&& x)noexcept{if(this!=&x){clear();bytes_=x.bytes_;size_=x.size_;x.clear();}return *this;}
ConfigResult<LeaseSetPolicy> leaseSetPolicy(const SessionOptions& options){
  auto canonical=canonicalSessionOptions(options);if(!canonical){return std::unexpected(canonical.error());}LeaseSetPolicy out;
  for(const auto& [key,value]:*canonical){
    if(key=="i2cp.dontPublishLeaseSet"){out.unpublished=javaTrue(value);continue;}
    if(key=="i2cp.leaseSetType"&&(value=="3"||value=="1")){continue;} // original upgrades1 to3
    if(key=="i2cp.leaseSetEncType"&&value=="0"){continue;}
    if(key.starts_with("i2cp.leaseSet")||key=="i2cp.encryptLeaseSet"){return error(Code::Unsupported,"Unsupported LeaseSet2 type/encryption/keys/options");}
  }return out;
}
ConfigResult<LeaseRequest> decodeLeaseRequest(const Frame& frame){
  if(frame.type!=RequestVariableLeaseSetType){return error(Code::Unsupported,"Expected variable LeaseSet request37");}
  Reader r{frame.payload};std::uint64_t id=0,count=0;if(!r.number(2,id)||!r.number(1,count)||count<1||count>MaximumLeaseCount||r.in.size()!=3+count*44||id==65535){return error(Code::InvalidFormat,"Malformed/bounded variable LeaseSet request");}
  LeaseRequest out;out.sessionId=static_cast<std::uint16_t>(id);out.leases.reserve(count);
  for(std::uint64_t i=0;i<count;++i){View gateway;std::uint64_t tunnel=0,expiry=0;if(!r.take(32,gateway)||!r.number(4,tunnel)||!r.number(8,expiry)){return error(Code::InvalidFormat,"Truncated variable lease");}RequestedLease l;std::copy(gateway.begin(),gateway.end(),l.gateway.begin());l.tunnelId=static_cast<std::uint32_t>(tunnel);l.expiresMilliseconds=expiry;out.leases.push_back(l);}
  auto valid=leasesValid(out.leases);if(!valid){return std::unexpected(valid.error());}return out;
}
ConfigResult<Bytes> encodeLeaseRequest(const LeaseRequest& request){
  auto valid=leasesValid(request.leases);if(!valid){return std::unexpected(valid.error());}if(request.sessionId==65535){return error(Code::InvalidFormat,"Unknown session id");}
  Bytes out;put(out,3+request.leases.size()*44,4);out.push_back(RequestVariableLeaseSetType);put(out,request.sessionId,2);out.push_back(static_cast<Byte>(request.leases.size()));
  for(const auto& l:request.leases){out.insert(out.end(),l.gateway.begin(),l.gateway.end());put(out,l.tunnelId,4);put(out,l.expiresMilliseconds,8);}return out;
}
ConfigResult<SignedLeaseSet2> signLeaseSet2(const ClientIdentity& identity,const LeaseEncryptionKey& key,const LeaseRequest& request,LeaseSetPolicy policy,std::uint64_t now,std::uint64_t previous){
  if(!identity.valid()||!identity.destination()||!key.valid()){return error(Code::KeyMismatch,"No owned live signing/encryption capability");}
  auto valid=leasesValid(request.leases);if(!valid){return std::unexpected(valid.error());}
  if(now==0||now>0xffffffffULL*1000-500||previous>0xffffffffULL*1000-1500){return error(Code::InvalidFormat,"LeaseSet2 published clock overflow");}
  const auto raw=std::max(now,previous?previous+1000:now);const auto published=((raw+500)/1000)*1000;
  SignedLeaseSet2 out;out.destination=*identity.destination();out.unpublished=policy.unpublished;out.publishedMilliseconds=published;out.leases=request.leases;
  for(const auto& l:out.leases){if(l.expiresMilliseconds/1000<=now/1000){return error(Code::InvalidFormat,"Expired requested lease cannot establish current submission");}out.expiresMilliseconds=std::max(out.expiresMilliseconds,(l.expiresMilliseconds/1000)*1000);}
  std::copy(key.publicKey().begin(),key.publicKey().end(),out.encryptionKey.begin());auto data=preimage(out);if(!data){return std::unexpected(data.error());}
  auto signature=signData(out.destination.signingType,identity.signingPrivateKey(),*data);if(!signature){return std::unexpected(signature.error());}out.signature=std::move(*signature);return out;
}
ConfigResult<void> verifyLeaseSet2(const SignedLeaseSet2& ls){auto actual=decodeDestination(ls.destination.wire);if(!actual){return std::unexpected(actual.error());}auto data=preimage(ls);if(!data){return std::unexpected(data.error());}return verifyData(actual->signingType,actual->signingPublicKey,ls.signature,*data);}
ConfigResult<Bytes> encodeLeaseSet2(const SignedLeaseSet2& ls){auto valid=verifyLeaseSet2(ls);if(!valid){return std::unexpected(valid.error());}auto bytes=preimage(ls);if(!bytes){return std::unexpected(bytes.error());}bytes->erase(bytes->begin());bytes->insert(bytes->end(),ls.signature.begin(),ls.signature.end());return bytes;}
ConfigResult<SignedLeaseSet2> decodeLeaseSet2(View wire){
  if(wire.size()>MaximumLeaseSetBytes||wire.size()<387+8+2+1+4+256+1+40+40){return error(Code::InvalidFormat,"Bounded LeaseSet2 length refused");}
  const std::size_t destinationSize=387+(static_cast<std::size_t>(wire[385])<<8)+wire[386];if(destinationSize>wire.size()){return error(Code::InvalidFormat,"Truncated LeaseSet2 Destination");}
  auto dest=decodeDestination(wire.first(destinationSize));if(!dest){return std::unexpected(dest.error());}Reader r{wire,destinationSize};std::uint64_t pub=0,delta=0,flags=0,properties=0,keys=0,type=0,length=0,count=0;View publicKey;
  if(!r.number(4,pub)||!r.number(2,delta)||!r.number(2,flags)||flags>2||flags==1||!r.number(2,properties)||properties!=0||!r.number(1,keys)||keys!=1||!r.number(2,type)||type!=0||!r.number(2,length)||length!=256||!r.take(256,publicKey)||!r.number(1,count)||count<1||count>MaximumLeaseCount){return error(Code::Unsupported,"LeaseSet2 requires online standard/empty options/one Enc0 key");}
  SignedLeaseSet2 out;out.destination=std::move(*dest);out.publishedMilliseconds=pub*1000;out.expiresMilliseconds=(pub+delta)*1000;out.unpublished=flags==2;std::copy(publicKey.begin(),publicKey.end(),out.encryptionKey.begin());out.leases.reserve(count);
  for(std::uint64_t i=0;i<count;++i){View hash;std::uint64_t tunnel=0,expiry=0;if(!r.take(32,hash)||!r.number(4,tunnel)||!r.number(4,expiry)){return error(Code::InvalidFormat,"Truncated LeaseSet2 lease");}RequestedLease l;std::copy(hash.begin(),hash.end(),l.gateway.begin());l.tunnelId=static_cast<std::uint32_t>(tunnel);l.expiresMilliseconds=expiry*1000;out.leases.push_back(l);}
  View sig;const auto width=out.destination.signingType==SigningType::DsaSha1?40U:64U;if(!r.take(width,sig)||r.at!=wire.size()){return error(Code::InvalidFormat,"LeaseSet2 signature/trailing bytes mismatch");}out.signature.assign(sig.begin(),sig.end());auto valid=verifyLeaseSet2(out);if(!valid){return std::unexpected(valid.error());}return out;
}
ConfigResult<SecretLeaseFrame> encodeCreateLeaseSet2(std::uint16_t session,const SignedLeaseSet2& ls,const LeaseEncryptionKey& key){
  if(session==65535||!key.valid()||!equal(key.publicKey(),ls.encryptionKey)){return error(Code::KeyMismatch,"CreateLeaseSet2 requires exact owned independent pair/session");}
  auto encoded=encodeLeaseSet2(ls);if(!encoded){return std::unexpected(encoded.error());}const auto size=5+3+encoded->size()+1+4+LeaseEncryptionBytes;if(size>MaximumSecretLeaseFrameBytes){return error(Code::TooLarge,"CreateLeaseSet2 exceeds secret buffer");}
  SecretLeaseFrame out;Bytes prefix;put(prefix,size-5,4);prefix.push_back(CreateLeaseSet2Type);put(prefix,session,2);prefix.push_back(StandardLeaseSet2Type);std::size_t at=0;
  const auto append=[&](View bytes){std::copy(bytes.begin(),bytes.end(),out.bytes_.begin()+static_cast<std::ptrdiff_t>(at));at+=bytes.size();};append(prefix);append(*encoded);
  const std::array<Byte,5> trailer{1,0,0,1,0};append(trailer);append(key.privateKey());out.size_=at;return out;
}
ConfigResult<DecodedCreateLeaseSet2> decodeCreateLeaseSet2(const Frame& frame){
  if(frame.type!=CreateLeaseSet2Type){return error(Code::Unsupported,"Expected CreateLeaseSet2 message41");}const auto bytes=View(frame.payload);if(bytes.size()<3+5+LeaseEncryptionBytes){return error(Code::InvalidFormat,"Truncated CreateLeaseSet2");}
  const auto session=static_cast<std::uint16_t>((static_cast<unsigned>(bytes[0])<<8)|bytes[1]);if(session==65535||bytes[2]!=StandardLeaseSet2Type){return error(Code::Unsupported,"CreateLeaseSet2 session/type refused");}
  const auto keyAt=bytes.size()-LeaseEncryptionBytes-5;const std::array<Byte,5> expected{1,0,0,1,0};if(!std::equal(expected.begin(),expected.end(),bytes.begin()+static_cast<std::ptrdiff_t>(keyAt))){return error(Code::Unsupported,"CreateLeaseSet2 private key header mismatch");}
  auto ls=decodeLeaseSet2(bytes.subspan(3,keyAt-3));if(!ls){return std::unexpected(ls.error());}auto key=LeaseEncryptionKey::fromPrivateKey(bytes.last(LeaseEncryptionBytes));if(!key){return std::unexpected(key.error());}if(!equal(key->publicKey(),ls->encryptionKey)){return error(Code::KeyMismatch,"CreateLeaseSet2 public/private mismatch");}return DecodedCreateLeaseSet2{session,std::move(*ls),std::move(*key)};
}
} // namespace biglybtc::services::i2p::i2cp

#include "original_torrent_identity.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace st=biglybtc::storage;
namespace core=biglybtc::core;
namespace {
void check(bool yes,const char* why){if(!yes){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,st::Error>&& result){if(!result){throw std::runtime_error(result.error().message);}return std::move(*result);}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
core::Document parse(std::string raw){auto result=core::Document::parse(std::move(raw));check(result.has_value(),"Fixture dictionary");return std::move(*result);}
void put(core::Document& doc,std::string_view key,std::string value){check(doc.replaceValue(key,std::move(value)).has_value(),"Fixture key edit");}
// Deliberately synthetic full hashes: tests characterize projection/admission
// policy only, not a cryptographic provider, valid torrent or Java execution.
st::OriginalFullInfoHashes hashes(bool v1,bool v2,unsigned seed=1){st::OriginalFullInfoHashes h;if(v1){h.v1.emplace();for(std::size_t n=0;n<h.v1->size();++n){(*h.v1)[n]=static_cast<std::byte>((n+seed)%256);}}if(v2){h.v2.emplace();for(std::size_t n=0;n<h.v2->size();++n){(*h.v2)[n]=static_cast<std::byte>((n+seed+70)%256);}}return h;}
core::Document document(bool v1,bool v2,std::optional<std::string> selector={},bool overrideHash=false){auto info=parse("de");put(info,"name",b("f"));put(info,"piece length","i16384e");if(v1){put(info,"length","i1e");put(info,"pieces",b(std::string(20,'P')));}if(v2){put(info,"meta version","i2e");put(info,"file tree","d"+b("f")+"d"+b("")+"d"+b("length")+"i1e"+b("pieces root")+b(std::string(32,'R'))+"eee");}if(overrideHash){put(info,"hash-override",b(std::string(20,'H')));}const auto props=selector?b("azureus_private_properties")+"d"+b("hybrid_hash_v2")+*selector+'e':std::string{};return parse("d"+b("info")+info.serialize()+props+b("opaque")+"li7e3:rawee");}
st::OriginalTorrentIdentity select(const core::Document& d,st::OriginalFullInfoHashes h){return take(st::selectOriginalTorrentIdentity(d,{take(st::originalTorrentInfoSubject(d)),std::move(h)}));}
core::TorrentHash key(std::span<const std::byte> value){auto result=core::TorrentHash::parse(std::string_view(reinterpret_cast<const char*>(value.data()),value.size()));check(result.has_value(),"Fixture key");return std::move(*result);}
void sourceSelectorIntegers(){
  auto h=hashes(true,true);auto d=document(true,true);const auto original=d.serialize();auto identity=select(d,h);check(identity.selected==st::OriginalSelectedHash::V1&&identity.originalKey==key(*h.v1),"ordinary hybrid uses v1");for(const auto n:{std::int64_t{-1},std::int64_t{0},std::int64_t{2},std::numeric_limits<std::int64_t>::max()}){auto other=document(true,true,"i"+std::to_string(n)+'e');check(select(other,h).selected==st::OriginalSelectedHash::V1,"only exact integer1 chooses v2");}auto chosen=document(true,true,"i1e");auto v2=select(chosen,h);check(v2.selected==st::OriginalSelectedHash::V2&&v2.originalKey==key(std::span(*h.v2).first(20)),"selected hybrid retains original20 v2 prefix");check(d.serialize()==original&&d.find("opaque"),"projection never rewrites metadata/unknowns");
}
void pureAndExplicitFullKeys(){
  auto only1=select(document(true,false,"i1e"),hashes(true,false));check(only1.selected==st::OriginalSelectedHash::V1,"v1 ignores typed hybrid selection");auto h=hashes(false,true);auto only2=select(document(false,true,"i0e"),h);check(only2.selected==st::OriginalSelectedHash::V2&&only2.originalKey.bytes().size()==20,"purev2 always original20");check(!st::validateOriginalTorrentKey(only2,key(*h.v2)),"new original records never default to32");check(st::validateOriginalTorrentKey(only2,key(*h.v2),true).has_value(),"explicit full32 reader policy retained");auto hybridHashes=hashes(true,true);auto defaultHybrid=select(document(true,true),hybridHashes);check(!st::validateOriginalTorrentKey(defaultHybrid,key(std::span(*hybridHashes.v2).first(20))),"unselected hybrid20 is not an implicit alias");check(st::validateOriginalTorrentKey(defaultHybrid,key(*hybridHashes.v2),true).has_value(),"existing explicit fullv2 compatibility is caller optin");
}
void malformedAndUnrewritten(){
  auto h=hashes(true,true);auto wrong=document(true,true,b("1"));check(!st::selectOriginalTorrentIdentity(wrong,{take(st::originalTorrentInfoSubject(wrong)),h}),"selector byte string rejected");auto pending=document(true,true,{},true);auto result=st::selectOriginalTorrentIdentity(pending,{take(st::originalTorrentInfoSubject(pending)),h});check(!result&&result.error().code==st::ErrorCode::Unsupported,"hash override remains separate metadata-pending boundary");auto d=document(true,true);check(!st::selectOriginalTorrentIdentity(d,{"de",h}),"different raw hash subject rejected");check(!st::selectOriginalTorrentIdentity(d,{take(st::originalTorrentInfoSubject(d)),hashes(true,false)}),"missing fullv2 evidence rejected");auto changed=d;put(changed,"info","d"+b("name")+b("changed")+'e');check(!st::originalTorrentInfoSubject(changed),"replaced info requires fresh qualified parse/hash chain");auto outside=d;put(outside,"azureus_private_properties","d"+b("hybrid_hash_v2")+"i1ee");check(select(outside,h).selected==st::OriginalSelectedHash::V2,"outside-info original selector can change without rewriting info");auto duplicate=core::Document::parse("d"+b("info")+std::string(take(st::originalTorrentInfoSubject(d)))+b("info")+std::string(take(st::originalTorrentInfoSubject(d)))+'e');check(!duplicate&&duplicate.error().code=="duplicate_key","duplicate info ambiguity rejected");
}
void everyRequestedFullHash(){
  auto actual=hashes(true,true);check(st::matchOriginalFullHashes(actual,actual).has_value(),"both full requested hashes match");auto bad=actual;(*bad.v2)[31]=std::byte{0};check(!st::matchOriginalFullHashes(actual,bad),"wrong v2 suffix fails despite same20 prefix");bad=actual;(*bad.v1)[19]=std::byte{0};check(!st::matchOriginalFullHashes(actual,bad),"rightv2 cannot hide wrongv1");check(!st::matchOriginalFullHashes(hashes(false,true),actual),"missing requested hash fails");check(!st::matchOriginalFullHashes(actual,{}),"empty request never grants identity");
}
void collisionsAndAlternateAdmissions(){
  auto h=hashes(true,true);const auto a=select(document(true,true),h),same=select(document(true,true),h),alternate=select(document(true,true,"i1e"),h);check(take(st::compareOriginalTorrentIdentities(a,same))==st::OriginalIdentityRelation::SameSelected,"same selected identity classified");check(take(st::compareOriginalTorrentIdentities(a,alternate))==st::OriginalIdentityRelation::AlternateSelected,"same full metainfo different original selection is explicit alias");auto collision=h;std::copy(collision.v1->begin(),collision.v1->end(),collision.v2->begin());auto d=document(true,true);check(!st::selectOriginalTorrentIdentity(d,{take(st::originalTorrentInfoSubject(d)),collision}),"v1/v2 prefix collision rejected");auto suffix=h;(*suffix.v2)[31]=std::byte{0};const auto conflicting=select(document(true,true,"i1e"),suffix);check(!st::compareOriginalTorrentIdentities(alternate,conflicting),"same selected20 different fullv2 is conflict not merge");auto distinct=select(document(true,true),hashes(true,true,12));check(take(st::compareOriginalTorrentIdentities(a,distinct))==st::OriginalIdentityRelation::Distinct,"unrelated full identities distinct");
}
void exactFileTreeOrdering() {
  const std::string astral="\xf0\x90\x80\x80",bmp="\xee\x80\x80";
  const auto leaf=[](){return std::string("d0:d6:lengthi0eee");};
  const auto subject=[&](std::string tree,std::string extra={}) {
    return parse("d4:infod9:file tree"+tree+"12:meta versioni2e4:name1:f12:piece lengthi16384e"+extra+"ee");
  };
  auto java=subject("d"+b(astral)+leaf()+b(bmp)+leaf()+"e");
  const auto raw=java.serialize(); const auto info=std::string(take(st::originalTorrentInfoSubject(java)));
  check(take(st::originalTorrentInfoOrder(java))==st::OriginalInfoOrder::JavaUtf16FileTree,"actual Java String order has narrow explicit classification");
  check(java.serialize()==raw&&take(st::originalTorrentInfoSubject(java))==info,"classification preserves every original hashed byte");
  auto bytes=subject("d"+b(bmp)+leaf()+b(astral)+leaf()+"e");
  check(take(st::originalTorrentInfoOrder(bytes))==st::OriginalInfoOrder::ByteSorted,"external byte-canonical v2 remains valid input");
  const auto duplicateRaw="d4:infod9:file treed"+b(astral)+leaf()+b(astral)+leaf()+"e12:meta versioni2e4:name1:f12:piece lengthi16384eee";
  auto duplicate=core::Document::parse(duplicateRaw);
  check(!duplicate&&duplicate.error().code=="duplicate_key","duplicate tree components never gain an exception");
  const auto javaTree="d"+b(astral)+leaf()+b(bmp)+leaf()+"e";
  const auto byteTree="d"+b(bmp)+leaf()+b(astral)+leaf()+"e";
  auto mixed=subject("d1:a"+javaTree+"1:b"+byteTree+"e");
  check(!st::originalTorrentInfoOrder(mixed),"mixed dictionary ordering cannot be passed as original Java encoding");
  auto outside=parse("d4:infod4:name1:f9:file tree"+javaTree+"12:meta versioni2e12:piece lengthi16384eee");
  check(!st::originalTorrentInfoOrder(outside),"unsorted non-tree info remains rejected");
  auto notV2=parse("d4:infod9:file tree"+javaTree+"4:name1:fee");
  check(!st::originalTorrentInfoOrder(notV2),"unknown v1 file tree cannot mint v2 exception");
  auto badUtf8=subject("d"+b(astral)+leaf()+b(bmp)+leaf()+b(std::string("\xed\xa0\x80",3))+leaf()+"e");
  check(!st::originalTorrentInfoOrder(badUtf8),"encoded surrogate is not a Java UTF8 tree component");
  check(java.replaceValue("info",info).has_value()
      &&take(st::originalTorrentInfoOrder(java))==st::OriginalInfoOrder::JavaUtf16FileTree
      &&take(st::originalTorrentInfoSubject(java))==info,"exact-byte replacement is a pristine no-op");
  auto changedInfo=parse(info); put(changedInfo,"name",b("changed"));
  check(java.replaceValue("info",changedInfo.serialize()).has_value()
      &&!st::originalTorrentInfoSubject(java)&&!st::originalTorrentInfoOrder(java),
      "edited info still requires a fresh parse/hash chain");
  auto fresh=parse(java.serialize());
  check(take(st::originalTorrentInfoOrder(fresh))==st::OriginalInfoOrder::JavaUtf16FileTree
      &&take(st::originalTorrentInfoSubject(fresh))!=info&&java.originalBytes()==raw,
      "fresh parsing exposes the changed hash subject without altering the original backing bytes");
}

}
int main(){try{exactFileTreeOrdering();sourceSelectorIntegers();pureAndExplicitFullKeys();malformedAndUnrewritten();everyRequestedFullHash();collisionsAndAlternateAdmissions();std::cout<<"six original selected-identity/order families passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

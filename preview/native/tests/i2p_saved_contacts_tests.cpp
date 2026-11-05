// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_contacts.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace i=biglybtc::services::i2p;namespace d=i::dht;
namespace {
void check(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
template<class T>T take(i::Result<T> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
i::Bytes read(const std::filesystem::path& path){check(std::filesystem::file_size(path)<=512,"Public Destination fixture exceeds original bound");std::ifstream file(path,std::ios::binary);check(bool(file),"Missing owned public Destination fixture");i::Bytes out((std::istreambuf_iterator<char>(file)),{});check(out.size()<=512,"Public Destination fixture exceeds original bound");return out;}
// Independent Java DataOutputStream/DHTUtilsI2P field builder, not codec output.
void be(i::Bytes& out,std::uint64_t value,unsigned width){for(unsigned n=width;n>0;--n){out.push_back(static_cast<i::Byte>(value>>(8*(n-1))));}}
i::Bytes row(std::int64_t alive,std::uint8_t version,i::View nid,std::uint32_t port,i::View hash,i::View destination,std::uint32_t protocol){
  i::Bytes out;be(out,std::bit_cast<std::uint64_t>(alive),8);be(out,version,1);be(out,nid.size(),1);out.insert(out.end(),nid.begin(),nid.end());be(out,port,4);be(out,hash.size(),1);out.insert(out.end(),hash.begin(),hash.end());be(out,destination.size(),2);out.insert(out.end(),destination.begin(),destination.end());if(version){be(out,protocol,4);}return out;
}
i::Bytes file(std::span<const i::Bytes> rows){i::Bytes out;be(out,rows.size(),4);for(const auto& r:rows){out.insert(out.end(),r.begin(),r.end());}return out;}
i::Bytes file(const i::Bytes& r){return file(std::span(&r,1));}
d::SavedContact sample(){d::SavedContact value;value.contact.hash.fill(0x31);value.contact.port=0x1122;value.contact.id=take(d::makeNodeId(value.contact.hash,value.contact.port,i::Bytes(14,0x33)));value.timeAlive=-7;value.protocolVersion=4;return value;}
i::Bytes manual(const d::SavedContact& value,std::uint32_t protocol){return row(value.timeAlive,value.serializationVersion,value.contact.id,value.contact.port,value.contact.hash,value.contact.destination?i::View(value.contact.destination->wire):i::View{},protocol);}
i::Bytes hex(std::string_view text){i::Bytes out;int high=-1;for(const auto c:text){if(c==' '||c=='\n'){continue;}const int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;check(n>=0,"Invalid static golden hex");if(high<0){high=n;}else{out.push_back(static_cast<i::Byte>(high*16+n));high=-1;}}check(high<0,"Incomplete static golden hex");return out;}
void goldenAndVersions(){
  const auto value=sample();
  // u32 count, signed long, v1, u8 NID20, i32 port, u8 hash32,
  // u16 Destination0, i32 version4. No PRUDP/IP contact bytes occur.
  const auto golden=hex("00000001 fffffffffffffff9 01 14 3131313120133333333333333333333333333333 00001122 20 3131313131313131313131313131313131313131313131313131313131313131 0000 00000004");
  const auto decoded=take(d::decodeSavedContacts(golden));check(decoded.size()==1&&decoded[0].timeAlive==-7&&decoded[0].serializationVersion==1&&decoded[0].protocolVersion==4&&!decoded[0].contact.destination,"Static original-shaped v1 fields differ");
  check(decoded[0].contact.id==value.contact.id&&decoded[0].contact.hash==value.contact.hash&&decoded[0].contact.port==value.contact.port,"Static golden secure identity differs");check(take(d::encodeSavedContacts(std::span(&value,1)))==golden,"Native export differs from static original-shaped golden");
  auto legacy=value;legacy.serializationVersion=0;legacy.protocolVersion=0;const auto old=file(manual(legacy,0));auto parsed=take(d::decodeSavedContacts(old));check(parsed.size()==1&&parsed[0].serializationVersion==0&&parsed[0].protocolVersion==0&&take(d::encodeSavedContacts(parsed))==old,"Version0 must omit protocol field exactly");
  for(const std::uint32_t wireVersion:std::array<std::uint32_t,8>{0,4,127,128,255,0xffffffffU,0x01000004U,0x80000000U}){
    auto rows=take(d::decodeSavedContacts(file(manual(value,wireVersion))));const auto narrowed=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(wireVersion));check(rows[0].protocolVersion==narrowed,"Java int-to-byte narrowing differs");const auto again=take(d::encodeSavedContacts(rows));const auto expected=file(manual(value,static_cast<std::uint32_t>(static_cast<std::int32_t>(narrowed))));check(again==expected,"Java signed-byte-to-int export differs");
  }
}
void publicDestinationsAreOwnedAndChecked(const std::filesystem::path& fixtures){
  for(const auto* family:{"dsa","ed25519"}){
    auto value=sample();value.contact.destination=take(i::decodeDestination(read(fixtures/(std::string(family)+".sender"))));value.contact.hash=value.contact.destination->hash;value.contact.id=take(d::makeNodeId(value.contact.hash,value.contact.port,i::Bytes(14,0x44)));
    const auto expected=file(manual(value,4));check(expected[71]==1&&expected[72]==static_cast<i::Byte>(value.contact.destination->wire.size()-256),"Destination length must be original unsigned16");auto mutableInput=expected;auto decoded=take(d::decodeSavedContacts(mutableInput));std::fill(mutableInput.begin(),mutableInput.end(),0x55);check(take(d::encodeSavedContacts(decoded))==expected,"Decoded saved data borrows caller input");check(decoded[0].contact.destination&&decoded[0].contact.destination->wire==value.contact.destination->wire,"Exact public Destination not retained");
    for(std::size_t n=0;n<expected.size();++n){check(!d::decodeSavedContacts(i::View(expected).first(n)),"Truncated saved file was partially accepted");}
    auto mismatch=value;mismatch.contact.hash[10]^=1;mismatch.contact.destination->hash=mismatch.contact.hash;check(!d::encodeSavedContacts(std::span(&mismatch,1)),"Forged typed Destination hash accepted on export");check(!d::decodeSavedContacts(file(manual(mismatch,4))),"Serialized hash ignored for full Destination");
  }
}
void signedHintsOrderingAndBounds(){
  const auto value=sample();std::vector<d::SavedContact> rows;
  for(const auto alive:std::array<std::int64_t,4>{std::numeric_limits<std::int64_t>::min(),-1,0,std::numeric_limits<std::int64_t>::max()}){auto copy=value;copy.timeAlive=alive;rows.push_back(copy);}const auto decoded=take(d::decodeSavedContacts(take(d::encodeSavedContacts(rows))));for(std::size_t n=0;n<rows.size();++n){check(decoded[n].timeAlive==rows[n].timeAlive,"Signed original timeAlive or duplicate ordering changed");}
  rows.clear();for(std::size_t n=0;n<d::MaximumSavedContacts;++n){auto copy=value;copy.timeAlive=static_cast<std::int64_t>(n);copy.protocolVersion=static_cast<std::int8_t>(static_cast<int>(n)-16);rows.push_back(copy);}const auto full=take(d::decodeSavedContacts(take(d::encodeSavedContacts(rows))));check(full.size()==32,"Original storage32 bound changed");for(std::size_t n=0;n<full.size();++n){check(full[n].timeAlive==static_cast<std::int64_t>(n)&&full[n].protocolVersion==rows[n].protocolVersion,"Codec sorted/deduplicated saved order");}
  rows.push_back(value);check(!d::encodeSavedContacts(rows),"Oversize saved selection exported");i::Bytes count;be(count,33,4);check(!d::decodeSavedContacts(count),"Oversize count accepted before bounded allocation");count.clear();be(count,0xffffffffU,4);check(!d::decodeSavedContacts(count),"Negative Java count treated as a valid empty restore");check(!d::decodeSavedContacts(i::Bytes(d::MaximumSavedContactsBytes+1,0)),"Input byte bound ignored");check(take(d::decodeSavedContacts(i::Bytes(4,0))).empty(),"Valid empty saved file rejected");
}
void malformedIdentitiesAndVersions(){
  const auto value=sample();const auto valid=file(manual(value,4));auto trailing=valid;trailing.push_back(0);check(!d::decodeSavedContacts(trailing),"Trailing bytes accepted");
  for(const auto version:{std::uint8_t(2),std::uint8_t(127),std::uint8_t(255)}){auto copy=value;copy.serializationVersion=version;check(!d::decodeSavedContacts(file(manual(copy,4)))&&!d::encodeSavedContacts(std::span(&copy,1)),"Unsupported serialization version rewritten");}
  for(const std::uint16_t port:std::array<std::uint16_t,3>{1,9000,65534}){auto edge=value;edge.contact.port=port;edge.contact.id=take(d::makeNodeId(edge.contact.hash,port,i::Bytes(14,0x33)));check(take(d::decodeSavedContacts(file(manual(edge,4))))[0].contact.port==port,"Valid original port boundary changed");}
  for(const std::uint32_t port:std::array<std::uint32_t,4>{0,65535,0xffffffffU,0x7fffffffU}){check(!d::decodeSavedContacts(file(row(value.timeAlive,1,value.contact.id,port,value.contact.hash,{},4))),"Out-of-range signed port truncated to uint16");}
  for(const std::size_t length:std::array<std::size_t,4>{0,19,21,255}){check(!d::decodeSavedContacts(file(row(-7,1,i::Bytes(length,0x31),value.contact.port,value.contact.hash,{},4))),"Non20-byte NID accepted");}
  for(const std::size_t length:std::array<std::size_t,4>{0,31,33,255}){check(!d::decodeSavedContacts(file(row(-7,1,value.contact.id,value.contact.port,i::Bytes(length,0x31),{},4))),"Non32-byte hash accepted");}
  auto bad=value;bad.contact.id[4]^=1;check(!d::decodeSavedContacts(file(manual(bad,4)))&&!d::encodeSavedContacts(std::span(&bad,1)),"Insecure NID accepted");bad=value;bad.contact.hash.fill(0);check(!d::encodeSavedContacts(std::span(&bad,1)),"Zero hash fabricated a native contact");bad=value;bad.serializationVersion=0;check(!d::encodeSavedContacts(std::span(&bad,1)),"Version0 silently discarded nonzero protocol");
  check(!d::decodeSavedContacts(file(row(-7,1,value.contact.id,value.contact.port,value.contact.hash,i::Bytes(513,0),4))),"Destination513 bound accepted");check(!d::decodeSavedContacts(file(row(-7,1,value.contact.id,value.contact.port,value.contact.hash,i::Bytes(1,0),4))),"Malformed Destination accepted");
  auto incomplete=manual(value,4);incomplete.pop_back();const std::array<i::Bytes,2> mixed{manual(value,4),incomplete};check(!d::decodeSavedContacts(file(mixed)),"Valid prefix escaped a malformed later row");
}
void presenceFallbackIsOriginal(){
  const auto value=sample();const auto primary=file(manual(value,4));auto other=value;other.timeAlive=77;const auto saving=file(manual(other,4));const i::Bytes corrupt{0xff};
  auto selected=take(d::selectSavedContactsFile(i::View(primary),i::View(saving)));check(selected&&selected->source==d::SavedContactsSource::Primary&&selected->contacts[0].timeAlive==-7,"Existing primary did not win");
  selected=take(d::selectSavedContactsFile(std::nullopt,i::View(saving)));check(selected&&selected->source==d::SavedContactsSource::Saving&&selected->contacts[0].timeAlive==77,"Missing primary did not select saving");check(!d::selectSavedContactsFile(i::View(corrupt),i::View(saving)),"Corrupt existing primary fell back to saving");check(!d::selectSavedContactsFile(i::View{},i::View(saving)),"Existing empty primary treated as absent");
  selected=take(d::selectSavedContactsFile(i::View(primary),i::View(corrupt)));check(selected&&selected->source==d::SavedContactsSource::Primary,"Unused malformed saving damaged valid primary");check(!take(d::selectSavedContactsFile(std::nullopt,std::nullopt)),"Missing files fabricated a saved source");const i::Bytes empty(4,0);selected=take(d::selectSavedContactsFile(i::View(empty),std::nullopt));check(selected&&selected->contacts.empty(),"Actual empty primary lost presence");
}
}
int main(int argc,char** argv){try{check(argc==2,"Public Destination fixture directory required");goldenAndVersions();publicDestinationsAreOwnedAndChecked(argv[1]);signedHintsOrderingAndBounds();malformedIdentitiesAndVersions();presenceFallbackIsOriginal();std::cout<<"PASS five bounded original I2P saved-contact families\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_contacts.hpp"
#include <algorithm>
#include <bit>
#include <new>
#include <exception>

namespace biglybtc::services::i2p::dht {
namespace {
struct Rejected {Error error;};
void require(bool ok,ErrorCode code,const char* message){if(!ok){throw Rejected{{code,message}};}}
struct Input {
  View bytes;std::size_t at=0;
  View take(std::size_t n){require(n<=bytes.size()-at,ErrorCode::InvalidFormat,"Truncated saved I2P contacts");auto result=bytes.subspan(at,n);at+=n;return result;}
  std::uint64_t integer(std::size_t n){std::uint64_t result=0;for(auto byte:take(n)){result=(result<<8)|byte;}return result;}
};
void integer(Bytes& out,std::uint64_t value,unsigned n){for(unsigned i=n;i>0;--i){out.push_back(Byte(value>>(8*(i-1))));}}
void validate(const SavedContact& value){
  require(value.serializationVersion<=1,ErrorCode::Unsupported,"Unknown saved I2P contact version");
  require(value.serializationVersion||value.protocolVersion==0,ErrorCode::InvalidFormat,"Version0 cannot encode a protocol version");
  require(validContact(value.contact),ErrorCode::InvalidFormat,"Invalid saved I2P secure NID/hash/port");
  if(value.contact.destination){
    const auto& raw=value.contact.destination->wire;
    require(raw.size()<=MaximumSavedDestinationBytes,ErrorCode::TooLarge,"Saved I2P destination exceeds512bytes");
    auto actual=decodeDestination(raw);if(!actual){throw Rejected{actual.error()};}
    require(actual->hash==value.contact.hash,ErrorCode::KeyMismatch,"Saved I2P destination/hash mismatch");
  }
}
template<class F>auto guarded(F&& action)->decltype(action()){
  try{return action();}catch(const Rejected& error){return std::unexpected(error.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::TooLarge,"Saved I2P contacts allocation failed"});}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::InvalidFormat,"Saved I2P contacts codec failed"});}
}
}
Result<std::vector<SavedContact>> decodeSavedContacts(View wire){
  return guarded([&]()->Result<std::vector<SavedContact>>{
    require(wire.size()<=MaximumSavedContactsBytes,ErrorCode::TooLarge,"Saved I2P contacts exceed file bound");Input input{wire};
    const auto count=input.integer(4);require(count<=MaximumSavedContacts,ErrorCode::TooLarge,"Saved I2P contacts exceed32entries");
    std::vector<SavedContact> contacts;contacts.reserve(static_cast<std::size_t>(count));
    for(std::uint64_t n=0;n<count;++n){
      SavedContact value;value.timeAlive=std::bit_cast<std::int64_t>(input.integer(8));value.serializationVersion=static_cast<std::uint8_t>(input.integer(1));
      require(value.serializationVersion<=1,ErrorCode::Unsupported,"Unknown saved I2P contact version");
      require(input.integer(1)==value.contact.id.size(),ErrorCode::InvalidFormat,"Saved I2P NID must contain20bytes");auto id=input.take(value.contact.id.size());std::copy(id.begin(),id.end(),value.contact.id.begin());
      const auto port=input.integer(4);require(port>0&&port<65535,ErrorCode::InvalidFormat,"Invalid saved I2P contact port");value.contact.port=static_cast<std::uint16_t>(port);
      require(input.integer(1)==value.contact.hash.size(),ErrorCode::InvalidFormat,"Saved I2P hash must contain32bytes");auto hash=input.take(value.contact.hash.size());std::copy(hash.begin(),hash.end(),value.contact.hash.begin());
      const auto length=input.integer(2);require(length<=MaximumSavedDestinationBytes,ErrorCode::TooLarge,"Saved I2P destination exceeds512bytes");auto destination=input.take(static_cast<std::size_t>(length));
      if(!destination.empty()){auto decoded=decodeDestination(destination);if(!decoded){throw Rejected{decoded.error()};}require(decoded->hash==value.contact.hash,ErrorCode::KeyMismatch,"Saved I2P destination/hash mismatch");value.contact.destination=std::move(*decoded);}
      if(value.serializationVersion){value.protocolVersion=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(input.integer(4)));}
      require(validContact(value.contact),ErrorCode::InvalidFormat,"Invalid saved I2P secure NID/hash/port");contacts.push_back(std::move(value));
    }
    require(input.at==wire.size(),ErrorCode::InvalidFormat,"Trailing saved I2P contact bytes");return contacts;
  });
}
Result<Bytes> encodeSavedContacts(std::span<const SavedContact> contacts){
  return guarded([&]()->Result<Bytes>{
    require(contacts.size()<=MaximumSavedContacts,ErrorCode::TooLarge,"Saved I2P contacts exceed32entries");Bytes out;out.reserve(4+contacts.size()*((MaximumSavedContactsBytes-4)/MaximumSavedContacts));integer(out,contacts.size(),4);
    for(const auto& value:contacts){validate(value);integer(out,std::bit_cast<std::uint64_t>(value.timeAlive),8);integer(out,value.serializationVersion,1);
      integer(out,value.contact.id.size(),1);out.insert(out.end(),value.contact.id.begin(),value.contact.id.end());integer(out,value.contact.port,4);
      integer(out,value.contact.hash.size(),1);out.insert(out.end(),value.contact.hash.begin(),value.contact.hash.end());
      const auto destination=value.contact.destination?View(value.contact.destination->wire):View{};integer(out,destination.size(),2);out.insert(out.end(),destination.begin(),destination.end());
      if(value.serializationVersion){integer(out,static_cast<std::uint32_t>(static_cast<std::int32_t>(value.protocolVersion)),4);}
    }return out;
  });
}
Result<std::optional<SavedContactsFile>> selectSavedContactsFile(std::optional<View> primary,std::optional<View> saving){
  if(!primary&&!saving){return std::optional<SavedContactsFile>{};}
  const auto selected=primary?*primary:*saving;auto contacts=decodeSavedContacts(selected);if(!contacts){return std::unexpected(contacts.error());}
  return std::optional<SavedContactsFile>{SavedContactsFile{primary?SavedContactsSource::Primary:SavedContactsSource::Saving,std::move(*contacts)}};
}
} // namespace biglybtc::services::i2p::dht

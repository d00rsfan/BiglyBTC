// SPDX-License-Identifier: GPL-2.0-or-later
// Reference I2P9134f808337b401e8e53c73734c81fab04280c9d:
// I2PClientImpl.createDestination, I2PSessionImpl.readDestination,
// PrivateKeyFile.write and SigningPrivateKey.isOffline. No router activation.
#include "identity.hpp"
#include <algorithm>
#include <openssl/crypto.h>
#include <utility>

namespace biglybtc::services::i2p {
namespace {
std::unexpected<Error> error(ErrorCode code,const char* message){return std::unexpected(Error{code,message});}
std::size_t signingKeySize(SigningType type){return type==SigningType::DsaSha1?20:32;}
bool offlineKey(View key){Byte combined=0;for(const auto byte:key){combined|=byte;}return combined==0;}
Result<void> validateParts(const Destination& destination,View encryptionKey,View signingKey){
  if(destination.signingType!=SigningType::DsaSha1&&destination.signingType!=SigningType::Ed25519){
    return error(ErrorCode::Unsupported,"I2P client signing type is unsupported");
  }
  if(destination.encryptionType!=0){return error(ErrorCode::Unsupported,"Non-default I2P client encryption type is unsupported");}
  if(destination.wire.size()+DefaultClientEncryptionPrivateKeyBytes+signingKeySize(destination.signingType)>MaximumOnlineClientIdentityBytes){
    return error(ErrorCode::TooLarge,"I2P client identity exceeds owned storage");
  }
  if(encryptionKey.size()!=DefaultClientEncryptionPrivateKeyBytes||signingKey.size()!=signingKeySize(destination.signingType)){
    return error(ErrorCode::InvalidFormat,"I2P client private key length is invalid");
  }
  // An all-zero signing key is the original offline-file sentinel. Do not
  // reinterpret its following extension as an online identity or generate one.
  if(offlineKey(signingKey)){
    return error(ErrorCode::Unsupported,"Offline I2P client identities are unsupported");
  }
  auto publicKey=signingPublicKey(destination.signingType,signingKey);
  if(!publicKey){return std::unexpected(publicKey.error());}
  if(*publicKey!=destination.signingPublicKey){return error(ErrorCode::KeyMismatch,"I2P client signing key does not match its Destination");}
  return {};
}
} // namespace

ClientIdentity::ClientIdentity(Destination destination,View encryptionKey,View signingKey) noexcept
    :destination_(std::move(destination)){
  const auto publicSize=destination_.wire.size();
  std::copy(destination_.wire.begin(),destination_.wire.end(),wire_.begin());
  std::copy(encryptionKey.begin(),encryptionKey.end(),wire_.begin()+publicSize);
  std::copy(signingKey.begin(),signingKey.end(),wire_.begin()+publicSize+encryptionKey.size());
  size_=publicSize+encryptionKey.size()+signingKey.size();
}
ClientIdentity::~ClientIdentity(){clear();}
ClientIdentity::ClientIdentity(ClientIdentity&& other) noexcept
    :destination_(std::move(other.destination_)),wire_(other.wire_),size_(other.size_){other.clear();}
ClientIdentity& ClientIdentity::operator=(ClientIdentity&& other) noexcept{
  if(this!=&other){clear();destination_=std::move(other.destination_);wire_=other.wire_;size_=other.size_;other.clear();}
  return *this;
}
void ClientIdentity::clear() noexcept{OPENSSL_cleanse(wire_.data(),wire_.size());size_=0;}
View ClientIdentity::encryptionPrivateKey() const noexcept{
  return valid()?View(wire_).subspan(destination_.wire.size(),DefaultClientEncryptionPrivateKeyBytes):View{};
}
View ClientIdentity::signingPrivateKey() const noexcept{
  return valid()?View(wire_).subspan(destination_.wire.size()+DefaultClientEncryptionPrivateKeyBytes,signingKeySize(destination_.signingType)):View{};
}
Result<Bytes> ClientIdentity::makeDatagram2(const Hash& recipient,View payload) const{
  if(!valid()){return error(ErrorCode::KeyMismatch,"I2P client identity is empty or retired");}
  return encode2(destination_.wire,signingPrivateKey(),recipient,payload);
}
Result<ClientIdentity> makeClientIdentity(View publicDestination,View encryptionKey,View signingKey){
  auto destination=decodeDestination(publicDestination);if(!destination){return std::unexpected(destination.error());}
  auto valid=validateParts(*destination,encryptionKey,signingKey);if(!valid){return std::unexpected(valid.error());}
  return ClientIdentity(std::move(*destination),encryptionKey,signingKey);
}
Result<ClientIdentity> decodeClientIdentity(View bytes){
  if(bytes.size()>MaximumClientIdentityInputBytes){return error(ErrorCode::TooLarge,"I2P client identity exceeds bounded input");}
  if(bytes.size()<387){return error(ErrorCode::InvalidFormat,"I2P client Destination is truncated");}
  const auto certificateSize=(std::size_t(bytes[385])<<8)|bytes[386];const auto publicSize=387+certificateSize;
  if(bytes.size()<publicSize){return error(ErrorCode::InvalidFormat,"I2P client certificate is truncated");}
  auto destination=decodeDestination(bytes.first(publicSize));if(!destination){return std::unexpected(destination.error());}
  if(destination->encryptionType!=0){return error(ErrorCode::Unsupported,"Non-default I2P client encryption type is unsupported");}
  const auto totalSize=publicSize+DefaultClientEncryptionPrivateKeyBytes+signingKeySize(destination->signingType);
  if(bytes.size()<totalSize){return error(ErrorCode::InvalidFormat,"I2P client private keys are truncated");}
  const auto encryptionKey=bytes.subspan(publicSize,DefaultClientEncryptionPrivateKeyBytes);
  const auto signingKey=bytes.subspan(publicSize+DefaultClientEncryptionPrivateKeyBytes,signingKeySize(destination->signingType));
  if(offlineKey(signingKey)){return error(ErrorCode::Unsupported,"Offline I2P client identities are unsupported");}
  if(bytes.size()!=totalSize){return error(ErrorCode::InvalidFormat,"I2P client identity has trailing data");}
  auto valid=validateParts(*destination,encryptionKey,signingKey);if(!valid){return std::unexpected(valid.error());}
  return ClientIdentity(std::move(*destination),encryptionKey,signingKey);
}
} // namespace biglybtc::services::i2p

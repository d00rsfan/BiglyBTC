// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_identity_digest.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <new>
namespace biglybtc::storage {
std::expected<OriginalTorrentIdentity,Error> digestOriginalTorrentIdentity(const core::Document& source){
 try{
  const auto subject=originalTorrentInfoSubject(source);if(!subject){return std::unexpected(subject.error());}
  const auto* info=source.find("info");OriginalFullInfoHashes full;
  const auto digest=[&](const EVP_MD* algorithm,std::span<std::byte> output){
   unsigned size=0;std::array<unsigned char,EVP_MAX_MD_SIZE> value{};
   if(EVP_Digest(subject->data(),subject->size(),value.data(),&size,algorithm,nullptr)!=1||size!=output.size()){return false;}
   std::copy_n(reinterpret_cast<const std::byte*>(value.data()),output.size(),output.begin());return true;
  };
  if(info->find("pieces")){full.v1.emplace();if(!digest(EVP_sha1(),*full.v1)){return std::unexpected(Error{ErrorCode::Io,"Cannot compute original v1 identity"});}}
  if(info->find("file tree")){full.v2.emplace();if(!digest(EVP_sha256(),*full.v2)){return std::unexpected(Error{ErrorCode::Io,"Cannot compute original v2 identity"});}}
  return selectOriginalTorrentIdentity(source,{*subject,std::move(full)});
 }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Original identity digest memory unavailable"});}
}
}

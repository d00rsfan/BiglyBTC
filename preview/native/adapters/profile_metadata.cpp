// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile.hpp"
#include "parsed_config_cache.hpp"
#include <new>

namespace biglybtc::storage {
std::expected<EncodedDocument,CodecError> ProfileLease::decodeMetadataSnapshot(
    std::string_view key,std::string input,core::Limits limits) const {
  auto access=sourceAccess();if(!access){return std::unexpected(CodecError::Unavailable);}
  std::shared_ptr<ParsedConfigCache> cache;
  try{if(!ownership_->parsedConfigs){ownership_->parsedConfigs=std::make_shared<ParsedConfigCache>();}cache=ownership_->parsedConfigs;}catch(const std::bad_alloc&){/* Optional acceleration. */}
  if(cache){ParsedConfigCache::increment(cache->statistics.loads);
    if(auto found=cache->find<EncodedDocument>(key,limits,input)){return std::move(*found);}
    ParsedConfigCache::increment(cache->statistics.parses);
  }
  auto parsed=EncodedDocument::parse(std::move(input),limits);
  if(parsed&&cache){try{cache->remember(key,limits,*parsed);}catch(const std::bad_alloc&){/* Preserve actual decode result. */}}
  return parsed;
}

} // namespace biglybtc::storage

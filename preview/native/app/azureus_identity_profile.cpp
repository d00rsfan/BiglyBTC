// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_identity_profile.hpp"
#include "azureus_recent_address_area.hpp"
#include "recent_addresses.hpp"
namespace biglybtc::app {
namespace blocks=services::azureus_key_block;namespace identity=services::azureus_identity;
namespace {
auto error(storage::ErrorCode code,const char* text){return std::unexpected(storage::Error{code,text});}
}
std::expected<AzureusIdentityProfile,storage::Error> prepareAzureusIdentityProfile(const storage::AzureusValueVersions& versions,const storage::AzureusKeyBlockArea& area,std::span<const blocks::Record> records,const QHostAddress& actual,std::int64_t now,blocks::Verifier verifier,std::stop_token stop){
 try{
  if(stop.stop_requested()){return error(storage::ErrorCode::Cancelled,"DHT identity startup cancelled");}auto current=versions.validate();if(!current){return std::unexpected(current.error());}auto formatted=identity::originalHostAddress(actual);if(!formatted){return error(storage::ErrorCode::InvalidFormat,"Invalid measured DHT address");}AzureusIdentityProfile out{std::make_shared<blocks::State>(std::move(verifier)),area,false,false,false,{}};if(!out.blocks->restore(records)){return error(storage::ErrorCode::InvalidFormat,"Cannot restore signed DHT identity state");}
  auto addressArea=storage::readAzureusRecentAddressArea(versions);if(!addressArea){if(!versions.validate()){return std::unexpected(addressArea.error());}out.warning=addressArea.error();return out;}auto old=core::Document::parse(addressArea->primary().value_or(addressArea->pending().value_or("de")));if(!old){out.warning=storage::Error{storage::ErrorCode::InvalidFormat,"Saved DHT address cache is unreadable; files retained"};return out;}auto recent=identity::mostRecent(old->root());if(!recent){out.warning=storage::Error{storage::ErrorCode::InvalidFormat,"Saved DHT recent address is unsupported; files retained"};return out;}QHostAddress previous;if(*recent){previous=QHostAddress(QString::fromUtf8((**recent).data(),qsizetype((**recent).size())));}out.addressChanged=!previous.isNull()&&previous!=actual;
  if(out.addressChanged&&out.blocks->purgeDirect()){
   out.directBlocksPurged=true;if(stop.stop_requested()){return error(storage::ErrorCode::Cancelled,"DHT identity purge cancelled");}auto before=core::Document::parse(area.primary().value_or("de"));if(!before){return error(storage::ErrorCode::InvalidFormat,"Saved DHT key blocks are unreadable");}const auto frozen=out.blocks->records();auto encoded=blocks::encodeSaved(before->root(),frozen);if(!encoded){return error(storage::ErrorCode::InvalidFormat,"Cannot encode DHT identity block purge");}auto saved=storage::saveAzureusKeyBlockArea(versions,area,std::move(*encoded));if(!saved){return std::unexpected(saved.error());}out.blockArea=std::move(*saved);
  }
  // Original registers the cached address before reporting a changed address.
  // This is historical cache bookkeeping, never a claimed socket binding.
  if(*recent&&!previous.isNull()){auto historical=identity::remember(old->root(),**recent,now);if(!historical){out.warning=storage::Error{storage::ErrorCode::InvalidFormat,"Saved DHT address timestamps are unsupported; files retained"};return out;}auto refreshed=core::Document::parse(std::move(*historical));if(!refreshed){return error(storage::ErrorCode::InvalidFormat,"Cannot prepare DHT address history");}old=std::move(refreshed);}
  auto encoded=identity::remember(old->root(),*formatted,now);if(!encoded){out.warning=storage::Error{storage::ErrorCode::InvalidFormat,"Cannot encode DHT address history; files retained"};return out;}if(stop.stop_requested()){return error(storage::ErrorCode::Cancelled,"DHT address checkpoint cancelled");}auto saved=storage::saveAzureusRecentAddressArea(versions,*addressArea,std::move(*encoded));if(!saved){if(!versions.validate()){return std::unexpected(saved.error());}out.warning=saved.error();return out;}out.addressRecorded=true;return out;
 }catch(...){return error(storage::ErrorCode::Io,"Cannot prepare DHT local identity");}
}
}

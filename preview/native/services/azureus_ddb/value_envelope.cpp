// SPDX-License-Identifier: GPL-2.0-or-later
#include "value_envelope.hpp"
namespace biglybtc::services::azureus_ddb {
std::expected<ValueEnvelope,Error> decodeNormalValue(std::uint8_t flags,std::span<const std::uint8_t> raw)noexcept{
 if(raw.size()>512){return std::unexpected(Error::Limit);}ValueEnvelope out;
 if(flags!=4){out.entries[0]={0,static_cast<std::uint16_t>(raw.size())};out.count=1;return out;}
 out.packed=true;if(raw.empty()){out.fault=Fault::MissingHeader;return out;}
 std::size_t position=1;
 while(position<raw.size()){
  if(raw.size()-position<2){out.fault=Fault::TruncatedLength;return out;}
  const auto length=(std::size_t(raw[position])<<8)|raw[position+1];position+=2;
  if(length>raw.size()-position){out.fault=Fault::LengthExceedsPayload;break;}
  out.entries[out.count++]={static_cast<std::uint16_t>(position),static_cast<std::uint16_t>(length)};position+=length;
 }
 out.continuation=raw[0]==1;return out;
}
}

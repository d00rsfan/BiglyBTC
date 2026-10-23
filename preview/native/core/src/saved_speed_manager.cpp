#include "biglybtc/saved_speed_manager.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>
namespace biglybtc::core {
namespace {
std::int32_t low32(std::int64_t value){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));}
std::expected<std::shared_ptr<const Document>,SavedSpeedError> document(std::string_view bytes){
  if(bytes.size()>Limits{}.inputBytes){return std::unexpected(SavedSpeedError::Limit);}auto parsed=Document::parse(std::string(bytes),Limits{});
  if(!parsed){return std::unexpected(parsed.error().code.ends_with("_limit")?SavedSpeedError::Limit:SavedSpeedError::Format);}if(parsed->root().type!=Node::Type::Dictionary){return std::unexpected(SavedSpeedError::Root);}
  return std::make_shared<const Document>(std::move(*parsed));
}
std::optional<std::string> text(const Node* node,bool& malformed){if(!node){return {}; }if(node->type!=Node::Type::Bytes){malformed=true;return {};}
  if(node->bytes.size()>4096){throw SavedSpeedError::Limit;}return std::string(node->bytes);}
std::optional<std::int64_t> integer(const Node* node,bool& malformed){if(!node){return {};}if(node->type!=Node::Type::Integer){malformed=true;return {};}return node->integer;}
std::optional<float> decimal(const std::optional<std::string>& text,bool& malformed){
  if(!text){return {};}if(text->empty()||text->size()>128){if(text->size()>128){throw SavedSpeedError::Limit;}malformed=true;return {};}
  // Original writer emits ordinary ASCII numeric strings. Java's broader hex,
  // suffix and nonfinite input grammar is retained raw, with no typed claim.
  std::string_view input=*text;if(input.front()=='+'){input.remove_prefix(1);}double value=0;
  const auto result=std::from_chars(input.data(),input.data()+input.size(),value,std::chars_format::general);
  if(result.ec!=std::errc{}||result.ptr!=input.data()+input.size()||!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max()){malformed=true;return {};}
  return static_cast<float>(value);
}
SavedSpeedEstimate estimate(const Node* record){SavedSpeedEstimate result;if(!record){return result;}result.present=true;
  if(record->type!=Node::Type::Dictionary){result.malformed=true;return result;}
  result.rawSpeed=integer(record->find("s"),result.malformed);if(result.rawSpeed){result.bytesPerSecond=low32(*result.rawSpeed);}
  result.rawHits=integer(record->find("h"),result.malformed);if(result.rawHits){result.hits=low32(*result.rawHits);}
  result.whenMillis=integer(record->find("w"),result.malformed);
  result.rawMetric=text(record->find("m"),result.malformed);result.metric=decimal(result.rawMetric,result.malformed);
  if(result.metric){result.metric=std::max(-1.0f,std::min(1.0f,*result.metric));}
  const auto* type=record->find("t");result.rawType=text(type,result.malformed);
  if(!type){result.type=0;result.typeDefaulted=true;}else{result.type=decimal(result.rawType,result.malformed);}
  if(!result.rawSpeed||!result.rawHits||!result.whenMillis||!result.rawMetric){result.malformed=true;}
  return result;
}
}
std::expected<SavedSpeedSettings,SavedSpeedError> readSavedSpeedSettings(std::string_view bytes){try{
  const auto source=document(bytes);if(!source){return std::unexpected(source.error());}SavedSpeedSettings value;value.source=*source;
  const auto* details=value.source->find("ASN Details");const Node* first=nullptr;
  if(details){if(details->type!=Node::Type::List){value.malformed=true;}else if(!details->children.empty()){first=&details->children.front();}}
  if(!first && !value.malformed){value.asNumber="";value.asDefaulted=true;value.addressFamily=4;}
  else if(first&&first->type==Node::Type::Dictionary){
    value.asNumber=text(first->find("as"),value.malformed);value.asName=text(first->find("name"),value.malformed);value.bgpPrefix=text(first->find("bgp"),value.malformed);
    if(!value.asNumber||!value.asName||!value.bgpPrefix){value.malformed=true;}
    const auto* family=first->find("v");if(!family){value.addressFamily=4;}else if(const auto raw=integer(family,value.malformed)){value.addressFamily=low32(*raw)==4?4:6;}
  }else if(first){value.malformed=true;}
  if(value.asNumber){const auto& id=*value.asNumber;if(id.empty()){value.historyLeaf="pm_default.dat";}
    else if(id.size()<=20&&id.find_first_not_of("0123456789")==std::string::npos){value.historyLeaf="pm_"+id+".dat";}}
  if(const auto raw=integer(value.source->find("Auto Upload Speed Version"),value.malformed)){value.rawAlgorithm=*raw;const auto narrowed=low32(*raw);value.algorithm=narrowed==3?2:narrowed;}
  if(const auto raw=integer(value.source->find("Auto Upload Speed Enabled"),value.malformed)){value.rawEnabled=*raw;value.enabled=low32(*raw)!=0;}
  if(const auto raw=integer(value.source->find("Auto Upload Speed Seeding Enabled"),value.malformed)){value.rawSeedingEnabled=*raw;value.seedingEnabled=low32(*raw)!=0;}
  return value;
 }catch(SavedSpeedError error){return std::unexpected(error);}catch(const std::bad_alloc&){return std::unexpected(SavedSpeedError::Memory);}}
std::expected<SavedSpeedHistory,SavedSpeedError> readSavedSpeedHistory(std::string_view bytes){try{
  const auto source=document(bytes);if(!source){return std::unexpected(source.error());}SavedSpeedHistory value;value.source=*source;
  value.upload=estimate(value.source->find("upcap"));value.download=estimate(value.source->find("downcap"));
  if(const auto* pings=value.source->find("pings")){if(pings->type==Node::Type::List){value.savedPingRecords=pings->children.size();}else{value.malformedPings=true;}}
  return value;
 }catch(SavedSpeedError error){return std::unexpected(error);}catch(const std::bad_alloc&){return std::unexpected(SavedSpeedError::Memory);}}
} // namespace biglybtc::core

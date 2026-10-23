#include "biglybtc/saved_speed_map.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <new>
#include <utility>

namespace biglybtc::core {
namespace {
// Validate existing parsed ownership without cloning opaque strings/subtrees.
// Never dereference a caller bytes view until its address/size agree with the
// corresponding immutable raw span. Explicit stack also bounds edited trees.
std::expected<void,SavedSpeedMapError> pristine(const Document& source){
  const Limits limits;const auto& raw=source.originalBytes();
  if(raw.size()>limits.inputBytes){return std::unexpected(SavedSpeedMapError::Limit);}
  struct Frame {const Node* node;std::size_t next,cursor;};
  std::array<Frame,Limits{}.depth+1> stack{};std::size_t depth=0,count=0;
  const auto enter=[&](const Node& node)->std::expected<void,SavedSpeedMapError>{
    if(count==limits.nodes||node.children.size()>limits.nodes-count-1){return std::unexpected(SavedSpeedMapError::Limit);}++count;
    if(node.changed||node.origin||node.begin>=node.end||node.end>raw.size()){return std::unexpected(SavedSpeedMapError::EditedDocument);}
    const auto span=std::string_view(raw).substr(node.begin,node.end-node.begin);
    if(node.type==Node::Type::Bytes){
      if(!node.children.empty()){return std::unexpected(SavedSpeedMapError::EditedDocument);}
      if(node.bytes.size()>limits.stringBytes){return std::unexpected(SavedSpeedMapError::Limit);}
      const auto colon=span.substr(0,24).find(':');std::size_t length=0;
      if(colon==std::string_view::npos){return std::unexpected(SavedSpeedMapError::EditedDocument);}
      const auto number=std::from_chars(span.data(),span.data()+colon,length);
      if(number.ec!=std::errc{}||number.ptr!=span.data()+colon||length!=span.size()-colon-1
          ||node.bytes.size()!=length||node.bytes.data()!=span.data()+colon+1){return std::unexpected(SavedSpeedMapError::EditedDocument);}
    }else{
      if(!node.bytes.empty()){return std::unexpected(SavedSpeedMapError::EditedDocument);}
      if(node.type==Node::Type::Integer){
        if(span.size()<3||span.front()!='i'||span.back()!='e'||!node.children.empty()){return std::unexpected(SavedSpeedMapError::EditedDocument);}
        std::int64_t value=0;const auto number=std::from_chars(span.data()+1,span.data()+span.size()-1,value);
        if(number.ec!=std::errc{}||number.ptr!=span.data()+span.size()-1||value!=node.integer){return std::unexpected(SavedSpeedMapError::EditedDocument);}
      }else if((node.type!=Node::Type::List&&node.type!=Node::Type::Dictionary)||span.size()<2||span.back()!='e'
          ||span.front()!=(node.type==Node::Type::List?'l':'d')
          ||(node.type==Node::Type::Dictionary&&node.children.size()%2)){return std::unexpected(SavedSpeedMapError::EditedDocument);}
    }
    return {};
  };
  const auto& root=source.root();if(root.begin!=0||root.end!=raw.size()){return std::unexpected(SavedSpeedMapError::EditedDocument);}
  if(const auto checked=enter(root);!checked){return checked;}
  stack[0]={&root,0,root.begin+1};
  for(;;){
    auto& frame=stack[depth];const auto& node=*frame.node;
    if(frame.next==node.children.size()){
      if((node.type==Node::Type::List||node.type==Node::Type::Dictionary)&&frame.cursor!=node.end-1){return std::unexpected(SavedSpeedMapError::EditedDocument);}
      if(!depth){break;}--depth;continue;
    }
    if(depth==limits.depth){return std::unexpected(SavedSpeedMapError::Limit);}
    const auto index=frame.next++;const auto& child=node.children[index];
    if(child.begin!=frame.cursor||child.end>node.end-1||(node.type==Node::Type::Dictionary&&index%2==0&&child.type!=Node::Type::Bytes)){return std::unexpected(SavedSpeedMapError::EditedDocument);}
    if(const auto checked=enter(child);!checked){return checked;}
    frame.cursor=child.end;++depth;stack[depth]={&child,0,child.begin+1};
  }
  return {};
}
std::optional<std::int64_t> integer(const Node& row,std::string_view key){
  const auto* value=row.find(key);
  return value&&value->type==Node::Type::Integer?std::optional{value->integer}:std::nullopt;
}
std::int32_t javaInt(std::int64_t value)noexcept{
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}
double varianceRating(std::uint16_t value)noexcept{
  if(value<50){return 1.0;}if(value>=150){return -1.0;}
  return std::clamp(1.0-(static_cast<double>(value)-50.0)/50.0,-1.0,1.0);
}
SavedSpeedMapPoint point(const SavedSpeedMapRecord& row,SavedSpeedMapMode mode){
  SavedSpeedMapPoint value;
  value.x=static_cast<std::uint16_t>(javaInt(*row.rawX));
  value.y=static_cast<std::uint16_t>(javaInt(*row.rawY));
  auto metric=javaInt(*row.rawMetric);
  const auto maximum=mode==SavedSpeedMapMode::Variance?1500:30000;
  value.metricClamped=metric>maximum;if(value.metricClamped){metric=maximum;}
  value.metric=static_cast<std::uint16_t>(metric);
  value.uploadBytesPerSecond=std::uint32_t{value.x}*256;
  value.downloadBytesPerSecond=std::uint32_t{value.y}*256;
  if(mode==SavedSpeedMapMode::Variance){value.varianceRating=varianceRating(value.metric);}
  return value;
}
std::pair<std::uint16_t,std::uint16_t> sourceOrder(std::uint16_t first,std::uint16_t second)noexcept{
  if(std::bit_cast<std::int16_t>(second)<std::bit_cast<std::int16_t>(first)){std::swap(first,second);}
  return {first,second};
}
SavedSpeedMapZone zone(std::size_t firstIndex,const SavedSpeedMapPoint& first,const SavedSpeedMapPoint& second){
  SavedSpeedMapZone value;value.firstRecord=firstIndex;value.secondRecord=firstIndex+1;
  const auto x=sourceOrder(first.x,second.x),y=sourceOrder(first.y,second.y);
  value.uploadStartBytesPerSecond=std::uint32_t{x.first}*256;
  value.uploadEndBytesPerSecond=std::uint32_t{x.second}*256+255;
  value.downloadStartBytesPerSecond=std::uint32_t{y.first}*256;
  value.downloadEndBytesPerSecond=std::uint32_t{y.second}*256+255;
  value.metric=static_cast<std::uint16_t>((std::uint32_t{first.metric}+second.metric)/2);
  value.reversedUpload=value.uploadStartBytesPerSecond>value.uploadEndBytesPerSecond;
  value.reversedDownload=value.downloadStartBytesPerSecond>value.downloadEndBytesPerSecond;
  return value;
}
template<class T>void maximum(std::optional<T>& stored,T value){stored=stored?std::max(*stored,value):value;}
}

std::expected<SavedSpeedMap,SavedSpeedMapError> readSavedSpeedMap(std::string_view bytes,SavedSpeedMapMode mode){
  if(mode!=SavedSpeedMapMode::Variance&&mode!=SavedSpeedMapMode::Rtt){return std::unexpected(SavedSpeedMapError::Mode);}
  if(bytes.size()>Limits{}.inputBytes){return std::unexpected(SavedSpeedMapError::Limit);}
  try{
    auto parsed=Document::parse(std::string(bytes),Limits{});
    if(!parsed){return std::unexpected(parsed.error().code.ends_with("_limit")?SavedSpeedMapError::Limit:SavedSpeedMapError::Format);}
    return readSavedSpeedMap(std::make_shared<const Document>(std::move(*parsed)),mode);
  }catch(const std::bad_alloc&){return std::unexpected(SavedSpeedMapError::Memory);}
}
std::expected<SavedSpeedMap,SavedSpeedMapError> readSavedSpeedMap(std::shared_ptr<const Document> source,SavedSpeedMapMode mode){
  if(mode!=SavedSpeedMapMode::Variance&&mode!=SavedSpeedMapMode::Rtt){return std::unexpected(SavedSpeedMapError::Mode);}
  if(!source){return std::unexpected(SavedSpeedMapError::Root);}
  if(const auto checked=pristine(*source);!checked){return std::unexpected(checked.error());}
  if(source->root().type!=Node::Type::Dictionary){return std::unexpected(SavedSpeedMapError::Root);}
  try{
    SavedSpeedMap value;value.mode=mode;value.source=std::move(source);
    const auto* pings=value.source->find("pings");if(!pings){return value;}
    if(pings->type!=Node::Type::List){value.presence=SavedSpeedMapPresence::Malformed;return value;}
    if(pings->children.size()>SavedSpeedMapMaximumRecords){return std::unexpected(SavedSpeedMapError::Limit);}
    value.presence=SavedSpeedMapPresence::Present;value.records.reserve(pings->children.size());
    if(!pings->children.empty()){value.zones.reserve(pings->children.size()-1);}
    for(std::size_t index=0;index<pings->children.size();++index){
      const auto& node=pings->children[index];SavedSpeedMapRecord row;row.sourceIndex=index;
      if(node.type==Node::Type::Dictionary){row.rawX=integer(node,"x");row.rawY=integer(node,"y");row.rawMetric=integer(node,"m");}
      row.malformed=!row.rawX||!row.rawY||!row.rawMetric;
      if(row.malformed){++value.malformedRecords;}
      else{
        row.point=point(row,mode);const auto& actual=*row.point;
        maximum(value.maxUploadBytesPerSecond,actual.uploadBytesPerSecond);
        maximum(value.maxDownloadBytesPerSecond,actual.downloadBytesPerSecond);
        maximum(value.maxMetric,actual.metric);
        if(!value.records.empty()&&value.records.back().point){value.zones.push_back(zone(index-1,*value.records.back().point,actual));}
      }
      value.records.push_back(std::move(row));
    }
    if(!value.records.empty()&&value.records.back().point){
      const auto& latest=*value.records.back().point;
      value.currentMetricRating=mode==SavedSpeedMapMode::Variance?*latest.varianceRating:0.0;
    }
    return value;
  }catch(const std::bad_alloc&){return std::unexpected(SavedSpeedMapError::Memory);}
}
} // namespace biglybtc::core

// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/saved_download_tag_edits.hpp"
#include <unordered_set>
#include <span>
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <iterator>
#include <map>
#include <set>
#include <new>
#include <type_traits>
#include <utility>

namespace biglybtc::core {
namespace {
struct Failure { SavedTagEditError error; };
void require(bool value, SavedTagEditError error) { if (!value) { throw Failure{error}; } }
SavedTagEditError convert(SavedTagError error) {
  switch (error) {
    case SavedTagError::Format: return SavedTagEditError::Format;
    case SavedTagError::Root: return SavedTagEditError::Root;
    case SavedTagError::ManualType: return SavedTagEditError::ManualType;
    case SavedTagError::Limit: return SavedTagEditError::Limit;
    case SavedTagError::Memory: return SavedTagEditError::Memory;
  }
  return SavedTagEditError::Format;
}
SavedDownloadTags parsed(std::string_view bytes, const SavedTagLimits& limits) {
  auto result = readSavedDownloadTags(bytes, limits);
  if (!result) { throw Failure{convert(result.error())}; }
  return std::move(*result);
}
void append(std::string& output, std::string_view value, const SavedTagLimits& limits) {
  require(value.size() <= limits.document.inputBytes - output.size(), SavedTagEditError::Limit);
  output.append(value);
}
void bytes(std::string& output, std::string_view value, const SavedTagLimits& limits) {
  require(value.size() <= limits.document.stringBytes, SavedTagEditError::Limit);
  append(output, std::to_string(value.size()), limits); append(output, ":", limits); append(output, value, limits);
}
std::string raw(const Document& document, const Node& node) {
  return document.originalBytes().substr(node.begin, node.end - node.begin);
}
// Same raw-range reconstruction pattern as overall_document.cpp; the existing
// bounded Document parser remains the only bencode parser used by this editor.
using Change = std::pair<std::string, std::optional<std::string>>;
std::string dictionary(const Document& document, const Node* node,
    const std::vector<Change>& changes, const SavedTagLimits& limits) {
  std::string out; append(out, "d", limits); std::vector<bool> seen(changes.size(), false);
  if (node) {
    require(node->type == Node::Type::Dictionary, SavedTagEditError::MalformedRecord);
    for (std::size_t i = 0; i < node->children.size(); i += 2) {
      const auto& key = node->children[i]; const auto& value = node->children[i + 1];
      auto found = changes.end();
      for (auto it = changes.begin(); it != changes.end(); ++it) { if (it->first == key.bytes) { found = it; break; } }
      if (found == changes.end()) { append(out, raw(document, key), limits); append(out, raw(document, value), limits); }
      else {
        seen[static_cast<std::size_t>(found - changes.begin())] = true;
        if (found->second) { append(out, raw(document, key), limits); append(out, *found->second, limits); }
      }
    }
  }
  for (std::size_t i = 0; i < changes.size(); ++i) {
    if (!seen[i] && changes[i].second) { bytes(out, changes[i].first, limits); append(out, *changes[i].second, limits); }
  }
  append(out, "e", limits); return out;
}
// Canonical placement for a newly added selected field;existing bytes and
// relative key order remain intact,including unrecognised configuration.
std::string orderedField(const Document& document,const Node* node,const std::string& key,
    const std::string& value,const SavedTagLimits& limits){
  if(!node||node->find(key)){return dictionary(document,node,{{key,value}},limits);}
  require(node->type==Node::Type::Dictionary,SavedTagEditError::MalformedRecord);
  std::string out;append(out,"d",limits);bool inserted=false;
  for(std::size_t i=0;i<node->children.size();i+=2){const auto& old=node->children[i];
    if(!inserted&&key<old.bytes){bytes(out,key,limits);append(out,value,limits);inserted=true;}
    append(out,raw(document,old),limits);append(out,raw(document,node->children[i+1]),limits);
  }
  if(!inserted){bytes(out,key,limits);append(out,value,limits);}append(out,"e",limits);return out;
}
bool utf8(std::string_view value) {
  for (std::size_t i = 0; i < value.size();) {
    const auto first = static_cast<unsigned char>(value[i++]);
    if (first < 0x80) { continue; }
    const unsigned count = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : 0;
    if (count == 0 || count > value.size() - i) { return false; }
    std::uint32_t code = first & ((1U << (6 - count)) - 1);
    for (unsigned n = 0; n < count; ++n) {
      const auto next = static_cast<unsigned char>(value[i++]);
      if ((next & 0xc0) != 0x80) { return false; }
      code = (code << 6) | (next & 0x3f);
    }
    if (code < (count == 1 ? 0x80U : count == 2 ? 0x800U : 0x10000U)
        || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) { return false; }
  }
  return true;
}
std::string nameValue(std::string_view name, const SavedTagLimits& limits) {
  require(name.size() <= limits.fieldBytes && name.size() <= limits.textBytes, SavedTagEditError::Limit);
  require(utf8(name), SavedTagEditError::InvalidName); std::string out; bytes(out, name, limits); return out;
}
std::string integer(std::int64_t value) { return "i" + std::to_string(value) + "e"; }
const Node* configuration(const Node& record) {
  const auto* config = record.find("c");
  require(!config || config->type == Node::Type::Dictionary, SavedTagEditError::MalformedConfiguration); return config;
}
std::vector<Change> constraint(const Document& doc,const Node& record,
    const SavedTagSetConstraint& action,const SavedTagLimits& limits) {
  const auto* config=configuration(record);
  const auto* property=config?config->find("pp.constraint"):nullptr;
  const auto* enabled=config?config->find("pp.enabled.constraint"):nullptr;
  require(!property||property->type==Node::Type::List,SavedTagEditError::MalformedConfiguration);
  require(!enabled||enabled->type==Node::Type::Integer,SavedTagEditError::MalformedConfiguration);
  const auto count=property?property->children.size():0;
  for(std::size_t i=0;i<std::min<std::size_t>(count,2);++i){
    require(property->children[i].type==Node::Type::Bytes,SavedTagEditError::MalformedConfiguration);
  }
  for(const auto& value:{std::string_view(action.expression),std::string_view(action.options)}){
    require(value.size()<=limits.fieldBytes&&value.size()<=limits.textBytes,SavedTagEditError::Limit);
    require(utf8(value),SavedTagEditError::InvalidConstraint);
  }
  const std::string_view oldExpression=count?property->children[0].bytes:std::string_view{};
  const std::string_view oldOptions=count>1?property->children[1].bytes:std::string_view{};
  std::vector<Change> changes;
  if(oldExpression!=action.expression||oldOptions!=action.options){
    std::string list;append(list,"l",limits);bytes(list,action.expression,limits);bytes(list,action.options,limits);
    for(std::size_t i=2;i<count;++i){append(list,raw(doc,property->children[i]),limits);}
    append(list,"e",limits);changes.emplace_back("pp.constraint",std::move(list));
  }
  if((enabled?enabled->integer==1:true)!=action.enabled){
    changes.emplace_back("pp.enabled.constraint",integer(action.enabled?1:0));
  }
  if(changes.empty()){return {};}
  return {{"c",dictionary(doc,config,changes,limits)}};
}
std::string memberId(const TorrentHash& hash) {
  const auto source = hash.bytes(); require(source.size() == 20 || source.size() == 32, SavedTagEditError::InvalidMember);
  constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  std::string out; out.reserve((source.size() * 8 + 4) / 5); std::uint32_t bits = 0; unsigned count = 0;
  for (const auto value : source) {
    bits = (bits << 8) | std::to_integer<unsigned>(value); count += 8;
    while (count >= 5) { count -= 5; out.push_back(alphabet[(bits >> count) & 31]); }
    bits &= (std::uint32_t{1} << count) - 1;
  }
  if (count) { out.push_back(alphabet[(bits << (5 - count)) & 31]); }
  return out;
}
std::vector<Change> membershipBatch(const Document& doc,const Node& record,const SavedDownloadTag& tag,
    std::span<const TorrentHash> hashes,bool add,std::int64_t added,const SavedTagLimits& limits){
  require(hashes.size()<=limits.members,SavedTagEditError::Limit);
  std::unordered_set<std::string> wanted;std::vector<std::string> ordered;ordered.reserve(hashes.size());
  for(const auto& hash:hashes){auto id=memberId(hash);require(wanted.insert(id).second,SavedTagEditError::InvalidMember);ordered.push_back(std::move(id));}
  const auto* members=record.find("o");const auto* props=record.find("p");require(!members||members->type==Node::Type::List,SavedTagEditError::MalformedMembership);const auto count=members?members->children.size():0;
  require(!props||(props->type==Node::Type::List&&props->children.size()==count),SavedTagEditError::MalformedMembership);
  if(members){for(const auto& item:members->children){require(item.type==Node::Type::Bytes,SavedTagEditError::MalformedMembership);}}
  if(props){for(const auto& item:props->children){require(item.type==Node::Type::Dictionary,SavedTagEditError::MalformedMembership);const auto* time=item.find("a");require(!time||time->type==Node::Type::Integer,SavedTagEditError::MalformedMembership);}}
  std::unordered_set<std::string> existing;for(const auto& member:tag.members){require(!member.duplicate,SavedTagEditError::MalformedMembership);if(member.rawId){existing.insert(*member.rawId);}}
  bool changed=false;for(const auto& id:ordered){changed|=add!=existing.contains(id);}if(!changed){return {};}
  std::string nextMembers,nextProps;append(nextMembers,"l",limits);append(nextProps,"l",limits);
  for(std::size_t i=0;i<count;++i){if(!add&&wanted.contains(std::string(members->children[i].bytes))){continue;}append(nextMembers,raw(doc,members->children[i]),limits);append(nextProps,props?raw(doc,props->children[i]):"de",limits);}
  if(add){for(const auto& id:ordered){if(existing.contains(id)){continue;}bytes(nextMembers,id,limits);append(nextProps,"d1:a",limits);append(nextProps,integer(added),limits);append(nextProps,"e",limits);}}
  append(nextMembers,"e",limits);append(nextProps,"e",limits);return {{"o",std::move(nextMembers)},{"p",std::move(nextProps)}};
}
std::vector<Change> membership(const Document& doc,const Node& record,const SavedDownloadTag& tag,const TorrentHash& hash,bool add,std::int64_t added,const SavedTagLimits& limits){return membershipBatch(doc,record,tag,std::span(&hash,1),add,added,limits);}
} // namespace

bool ordinarySavedTagGroup(const SavedDownloadTags& tags,std::optional<std::string_view> group)noexcept{
  try{
    if(!tags.source||tags.source->root().changed){return false;}
    if(!group){return true;}
    if(group->size()>4096||!utf8(*group)){return false;}
    const auto* manual=tags.source->find("3");if(!manual||manual->type!=Node::Type::Dictionary){return false;}
    const auto* groups=manual->find("c");if(!groups){return true;}
    if(groups->type!=Node::Type::Dictionary){return false;}
    if(groups->children.empty()){return true;}
    // Original getGroupID uses JVM default encoding, not the UTF8 attribute
    // codec. Non-ASCII IDs with saved group settings need a proven mapping.
    if(std::any_of(group->begin(),group->end(),[](unsigned char c){return c>=128;})){return false;}
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string key;key.reserve((group->size()*8+4)/5);std::uint32_t bits=0;unsigned count=0;
    for(const auto byte:*group){bits=(bits<<8)|static_cast<unsigned char>(byte);count+=8;
      while(count>=5){count-=5;key.push_back(alphabet[(bits>>count)&31]);}bits&=(std::uint32_t{1}<<count)-1;}
    if(count){key.push_back(alphabet[(bits<<(5-count))&31]);}
    const auto* settings=groups->find(key);if(!settings){return true;}
    if(settings->type!=Node::Type::Dictionary){return false;}
    for(std::size_t i=0;i<settings->children.size();i+=2){
      const auto& name=settings->children[i].bytes;const auto& value=settings->children[i+1];
      // MapUtils.getMapBoolean treats only long1 as true. Colour/assign-root
      // have setter side effects even for empty values; unknowns are not defaults.
      if(name!="x"||value.type!=Node::Type::Integer||value.integer==1){return false;}
    }
    return true;
  }catch(...){return false;}
}

std::expected<std::optional<SavedTagRgb>,SavedTagEditError> readSavedTagColour(const Node& record)noexcept{
  const auto bad=[](){return std::unexpected(SavedTagEditError::MalformedConfiguration);};
  if(record.type!=Node::Type::Dictionary||record.children.size()%2){return bad();}
  const Node* config=nullptr;
  for(std::size_t i=0;i<record.children.size();i+=2){if(record.children[i].type!=Node::Type::Bytes){return bad();}if(record.children[i].bytes=="c"){
    if(config){return bad();}config=&record.children[i+1];}}
  if(!config){return std::optional<SavedTagRgb>{};}
  if(config->type!=Node::Type::Dictionary||config->children.size()%2){return bad();}
  const Node* stored=nullptr;
  for(std::size_t i=0;i<config->children.size();i+=2){if(config->children[i].type!=Node::Type::Bytes){return bad();}if(config->children[i].bytes=="col.rgb"){
    if(stored){return bad();}stored=&config->children[i+1];}}
  if(!stored){return std::optional<SavedTagRgb>{};}
  if(stored->type!=Node::Type::Bytes||stored->bytes.size()>4096){return bad();}
  std::string_view text=stored->bytes;
  // Java String.split(",") removes every trailing empty element.
  while(!text.empty()&&text.back()==','){text.remove_suffix(1);}
  SavedTagRgb rgb{};
  for(std::size_t i=0;i<rgb.size();++i){
    const auto comma=text.find(',');
    if((i+1<rgb.size())!=(comma!=std::string_view::npos)){return bad();}
    auto part=text.substr(0,comma);if(part.empty()){return bad();}
    bool negative=false;if(part.front()=='+'||part.front()=='-'){negative=part.front()=='-';part.remove_prefix(1);}
    if(part.empty()){return bad();}
    const std::uint64_t limit=negative?2147483648ULL:2147483647ULL;std::uint64_t value=0;
    for(const unsigned char digit:part){
      // Java's non-ASCII Character.digit repertoire requires a qualified
      // Unicode provider; never silently reinterpret/normalise those bytes.
      if(digit<'0'||digit>'9'||value>(limit-(digit-'0'))/10){return bad();}
      value=value*10+(digit-'0');
    }
    rgb[i]=negative?(value==2147483648ULL?std::numeric_limits<std::int32_t>::min():-static_cast<std::int32_t>(value)):static_cast<std::int32_t>(value);
    if(comma!=std::string_view::npos){text.remove_prefix(comma+1);}
  }
  return std::optional<SavedTagRgb>{rgb};
}

bool savedTagDisplayMetadata(std::string_view key,const Node& value)noexcept{
  if(key=="oname"||key=="img.id"||key=="img.file"||key=="desc"){return value.type==Node::Type::Bytes;}
  return key=="img.so"&&value.type==Node::Type::Integer;
}

bool savedTagLifecycleProperty(std::string_view key,const Node& value)noexcept{
  return value.type==Node::Type::Integer&&(key=="pp.2"||key=="pp.4"||key=="pp.8"||key=="pp.16"||key=="pp.64"||key=="pp.128"||key=="pp.32768");
}

bool supportedSavedTrackerTagNames(const Node& value)noexcept{
  if(value.type!=Node::Type::List||value.children.size()>64){return false;}std::size_t bytes=0;
  for(const auto& name:value.children){
    if(name.type!=Node::Type::Bytes||name.bytes.size()>2048||name.bytes.size()>65536-bytes){return false;}
    bytes+=name.bytes.size();for(unsigned char c:name.bytes){if(c>=128){return false;}}
  }return true;
}

static bool savedTagByteTotals(std::string_view key, const Node& value) noexcept {
  if ((key != "b.up" && key != "b.down") || value.type != Node::Type::List) { return false; }
  // TagBase loads these lists as accumulated statistics. Membership changes
  // retain the raw values and neither set a live rate nor dispatch an action.
  return std::ranges::all_of(value.children, [](const Node& total) {
    if (total.type != Node::Type::Bytes || total.bytes.empty()) { return false; }
    auto text = total.bytes;
    if (text.front() == '+') {
      text.remove_prefix(1);
      if (text.empty() || text.front() == '-') { return false; }
    }
    std::int64_t parsed = 0;
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, parsed);
    return result.ec == std::errc{} && result.ptr == end;
  });
}

static bool membershipEffectsPolicy(const SavedDownloadTags& tags,const Node& record,bool lifecycle,bool positiveRates=false)noexcept{
  if(!ordinarySavedTagGroup(tags,record)){return false;}
  const auto* config=record.find("c");if(!config){return true;}
  for(std::size_t i=0;i<config->children.size();i+=2){
    const auto& key=config->children[i].bytes;const auto& value=config->children[i+1];
    if(positiveRates&&(key=="rl.up"||key=="rl.down")&&value.type==Node::Type::Integer){
      const auto rate=normalizeSavedTagRateLimit(value.integer);
      if(rate>=-1&&rate<2147483647){continue;}
    }
    if(savedTagUnlimitedRateProperty(key,value)||savedTagDisplayMetadata(key,value)||savedTagByteTotals(key,value)
        ||(key=="gr"&&value.type==Node::Type::Bytes)){continue;}
    if(key=="col.rgb"&&readSavedTagColour(record)){continue;}
    if((key=="vis"||key=="pp.enabled.constraint")&&value.type==Node::Type::Integer){continue;}
    // The queue reads these limits from the committed membership snapshot on each owner.
    if((key=="rl.maxadl"||key=="rl.maxacd"||key=="rl.als")&&value.type==Node::Type::Integer){continue;}
    // TagBase persists sharing separately from configured assignment actions.
    // Related Content reads the committed flags and checks the source route.
    if(key=="pub"&&value.type==Node::Type::Integer&&(value.integer==0||value.integer==1)){continue;}
    if(key=="pp.trackers"&&supportedSavedTrackerTagNames(value)){
      const auto* constraint=config->find("pp.constraint");
      if(!value.children.empty()&&constraint&&(constraint->type!=Node::Type::List||!constraint->children.empty())){return false;}
      continue;
    }
    if(key=="pp.tracker_templates"&&value.type==Node::Type::List&&value.children.empty()){continue;}
    if(key=="pp.untagged"&&value.type==Node::Type::Integer){
      if(value.integer==1){
        for(const auto* field:{"pp.constraint","pp.trackers"}){const auto* other=config->find(field);
          if(other&&(other->type!=Node::Type::List||!other->children.empty())){return false;}}
      }continue;
    }
    if(savedTagLifecycleProperty(key,value)&&(lifecycle||value.integer!=1)){continue;}
    if(key=="pp.constraint"&&value.type==Node::Type::List&&value.children.size()<=64){
      bool valid=true;for(const auto& text:value.children){if(text.type!=Node::Type::Bytes||text.bytes.size()>65536||!utf8(text.bytes)){valid=false;break;}}
      if(valid){continue;}
    }
    // Rates, actions, tracker-template rewrites and unknown fields need their
    // real owner/executor. Saving a member cannot stand in for that dispatch.
    return false;
  }
  return true;
}

bool ordinarySavedTagMembershipEffects(const SavedDownloadTags& tags,const Node& record)noexcept{return membershipEffectsPolicy(tags,record,false);}
bool lifecycleSavedTagMembershipEffects(const SavedDownloadTags& tags,const Node& record)noexcept{return membershipEffectsPolicy(tags,record,true);}
bool rateOnlySavedTagMembershipEffects(const SavedDownloadTags& tags,const Node& record)noexcept{return membershipEffectsPolicy(tags,record,false,true);}
bool rateReadyLifecycleSavedTagMembershipEffects(const SavedDownloadTags& tags,const Node& record)noexcept{return membershipEffectsPolicy(tags,record,true,true); }

bool ordinarySavedTagGroup(const SavedDownloadTags& tags,const Node& record)noexcept{
  if(record.type!=Node::Type::Dictionary){return false;}
  const auto* config=record.find("c");if(!config){return ordinarySavedTagGroup(tags,std::nullopt);}
  if(config->type!=Node::Type::Dictionary){return false;}
  const auto* group=config->find("gr");if(!group){return ordinarySavedTagGroup(tags,std::nullopt);}
  if(group->type!=Node::Type::Bytes){return false;}
  return ordinarySavedTagGroup(tags,std::optional<std::string_view>{group->bytes});
}

namespace {
const Node* internalTrackerConfig(const SavedDownloadTags& source,std::int32_t id){
  require(id>0,SavedTagEditError::MissingTag);
  require(bool(source.source),SavedTagEditError::Format);
  const auto* type=source.source->find("5");
  require(!type||type->type==Node::Type::Dictionary,SavedTagEditError::MalformedRecord);
  const auto* row=type?type->find(std::to_string(id)):nullptr;
  if(!row){return nullptr;}
  require(row->type==Node::Type::Dictionary&&!row->find("n")&&!row->find("o")&&!row->find("p"),
      SavedTagEditError::MalformedRecord);
  return configuration(*row);
}
std::optional<std::vector<std::string>> internalTrackerSelectors(const Node* config){
  if(!config){return std::nullopt;}
  const auto* visible=config->find("vis");
  const auto* previous=config->find("pp.trackers");
  require(!visible||visible->type==Node::Type::Integer,SavedTagEditError::MalformedConfiguration);
  if(!previous){return std::nullopt;}
  require(supportedSavedTrackerTagNames(*previous),SavedTagEditError::MalformedConfiguration);
  std::vector<std::string> selectors;
  for(const auto& selected:previous->children){selectors.emplace_back(selected.bytes);}
  return selectors;
}
SavedTagEditResult setInternalTrackerSelection(SavedDownloadTags baseline,
    const SavedTagSetInternalTrackerSelection& selection,const SavedTagLimits& limits){
  const auto* config=internalTrackerConfig(baseline,selection.id);
  const auto previous=internalTrackerSelectors(config);
  const auto* visible=config?config->find("vis"):nullptr;
  const bool alreadyHidden=visible&&visible->integer!=1;
  if(previous&&*previous==selection.selectors&&alreadyHidden){
    return SavedTagEditResult{std::move(baseline),false,{},{},{}};
  }
  require(selection.selectors.size()<=64,SavedTagEditError::Limit);
  std::string list="l";
  std::size_t text=0;
  for(const auto& selector:selection.selectors){
    require(selector.size()<=std::min(limits.fieldBytes,std::size_t{2048})&&
        selector.size()<=std::min(limits.textBytes,std::size_t{65536})-text,SavedTagEditError::Limit);
    require(std::ranges::all_of(selector,[](unsigned char c){return c<128;}),SavedTagEditError::InvalidName);
    text+=selector.size();
    bytes(list,selector,limits);
  }
  append(list,"e",limits);
  const auto& doc=*baseline.source;
  const auto* type=doc.find("5");
  const auto id=std::to_string(selection.id);
  const auto* record=type?type->find(id):nullptr;
  const auto changedProperty=orderedField(doc,config,"pp.trackers",list,limits);
  auto hidden=changedProperty;
  if(!alreadyHidden){
    const auto configDoc=Document::parse(changedProperty,limits.document);
    require(bool(configDoc),SavedTagEditError::MalformedConfiguration);
    hidden=orderedField(*configDoc,&configDoc->root(),"vis",integer(0),limits);
  }
  const auto changedRecord=orderedField(doc,record,"c",hidden,limits);
  const auto changedType=orderedField(doc,type,id,changedRecord,limits);
  const auto final=dictionary(doc,&doc.root(),{{"5",changedType}},limits);
  return SavedTagEditResult{parsed(final,limits),true,{},{},{}};
}
SavedTagEditResult observeSwarm(SavedDownloadTags baseline, const SavedTagObserveSwarm& observation,
    const SavedTagLimits& limits) {
  require(observation.seenMilliseconds > 0, SavedTagEditError::InvalidMember);
  require(observation.names.size() <= 128, SavedTagEditError::Limit);
  const auto& doc = *baseline.source;
  const auto occurrences=[](const Node& dictionary,std::string_view key) {
    std::size_t matches=0;
    for (std::size_t i=0; i<dictionary.children.size(); i+=2) {
      if (dictionary.children[i].bytes==key) { ++matches; }
    }
    return matches;
  };
  require(occurrences(doc.root(),"7")<=1,SavedTagEditError::MalformedRecord);
  const auto* type = doc.find("7");
  require(!type || type->type == Node::Type::Dictionary, SavedTagEditError::MalformedRecord);
  constexpr std::string_view LastSeen = "swarmtag:last.seen";
  struct Row { std::int32_t id; const Node* record; };
  std::map<std::string_view,Row,std::less<>> rows;
  std::set<std::int32_t> ids;
  std::int32_t highest = -1;
  if (type) {
    for (std::size_t at=0; at<type->children.size(); at+=2) {
      const auto key = type->children[at].bytes;
      const auto& record = type->children[at+1];
      require(!key.empty() && static_cast<unsigned char>(key.front()) < 0x80,
          SavedTagEditError::AmbiguousId);
      if (key.front()<'0' || key.front()>'9') { continue; }
      std::int32_t id=-1;
      const auto parsedId=std::from_chars(key.data(),key.data()+key.size(),id);
      require(parsedId.ec==std::errc{} && parsedId.ptr==key.data()+key.size() && id>=0
          && key==std::to_string(id) && ids.insert(id).second,SavedTagEditError::AmbiguousId);
      require(record.type==Node::Type::Dictionary && occurrences(record,"n")==1
          && occurrences(record,"c")<=1,SavedTagEditError::MalformedRecord);
      const auto* name=record.find("n");
      require(name && name->type==Node::Type::Bytes && utf8(name->bytes) && !name->bytes.starts_with("tag."),
          SavedTagEditError::InvalidName);
      require(rows.size()<limits.tags && rows.emplace(name->bytes,Row{id,&record}).second,
          SavedTagEditError::MalformedRecord);
      highest=std::max(highest,id);
    }
  }
  std::int64_t next=observation.counter
      ? static_cast<std::int64_t>(*observation.counter) : static_cast<std::int64_t>(highest)+1;
  require(next>=0 && next>=highest,SavedTagEditError::AmbiguousId);
  std::unordered_set<std::string_view> accepted;
  std::vector<Change> changes;
  std::size_t newTags=0;
  const auto seenTime=integer(observation.seenMilliseconds);
  for (const auto& name : observation.names) {
    if (name.starts_with('_') && name.ends_with('_')) { continue; }
    if (!accepted.insert(name).second) { continue; }
    require(!name.empty() && !name.starts_with("tag."),SavedTagEditError::InvalidName);
    auto encoded=nameValue(name,limits);
    const auto saved=rows.find(name);
    if (saved!=rows.end()) {
      const auto* config=configuration(*saved->second.record);
      require(!config || occurrences(*config,LastSeen)<=1,SavedTagEditError::MalformedConfiguration);
      const auto* previous=config?config->find(LastSeen):nullptr;
      require(!previous || previous->type==Node::Type::Integer,SavedTagEditError::MalformedConfiguration);
      if (previous && previous->integer==observation.seenMilliseconds) { continue; }
      const auto updated=orderedField(doc,config,std::string(LastSeen),seenTime,limits);
      const auto record=orderedField(doc,saved->second.record,"c",updated,limits);
      changes.emplace_back(std::to_string(saved->second.id),record);
      continue;
    }
    require(++newTags<=limits.tags-rows.size() && next<std::numeric_limits<std::int32_t>::max(),
        SavedTagEditError::Limit);
    const auto config=dictionary(doc,nullptr,{{std::string(LastSeen),seenTime}},limits);
    auto record=dictionary(doc,nullptr,{{"c",config},{"n",std::move(encoded)}},limits);
    changes.emplace_back(std::to_string(++next),std::move(record));
  }
  SavedTagEditResult result;
  if (changes.empty()) {
    result.value=std::move(baseline);
    return result;
  }
  const auto modified=dictionary(doc,type,changes,limits);
  const auto saved=dictionary(doc,&doc.root(),{{"7",modified}},limits);
  result.value=parsed(saved,limits);
  result.changed=true;
  if (newTags) { result.allocationCounter=static_cast<std::int32_t>(next); }
  return result;
}
SavedTagEditResult retireSubject(SavedDownloadTags baseline, const TorrentHash& subject,
    const SavedTagLimits& limits) {
  require(subject.bytes().size() == 20, SavedTagEditError::InvalidMember);
  const auto& doc = *baseline.source;
  const auto* manual = doc.find("3");
  std::vector<Change> updates;
  for (const auto& tag : baseline.tags) {
    if (std::ranges::find(tag.uniqueHashes, subject) == tag.uniqueHashes.end()) { continue; }
    require(tag.id && !tag.duplicateId && tag.idKey == std::to_string(*tag.id),
        SavedTagEditError::AmbiguousId);
    const auto* record = manual ? manual->find(tag.idKey) : nullptr;
    require(record && record->type == Node::Type::Dictionary, SavedTagEditError::MalformedRecord);
    auto changes = membership(doc, *record, tag, subject, false, 0, limits);
    require(!changes.empty(), SavedTagEditError::MalformedMembership);
    updates.emplace_back(tag.idKey, dictionary(doc, record, changes, limits));
  }
  SavedTagEditResult result;
  if (updates.empty()) {
    result.value = std::move(baseline);
    return result;
  }
  const auto next = dictionary(doc, manual, updates, limits);
  const auto encoded = dictionary(doc, &doc.root(), {{"3",next}}, limits);
  result.value = parsed(encoded, limits);
  require(std::ranges::none_of(result.value.tags, [&](const auto& tag) {
    return std::ranges::find(tag.uniqueHashes, subject) != tag.uniqueHashes.end();
  }), SavedTagEditError::MalformedMembership);
  result.changed = true;
  return result;
}
} // namespace

std::expected<std::optional<std::vector<std::string>>,SavedTagEditError>
readSavedInternalTrackerSelection(const SavedDownloadTags& source,std::int32_t id){
  try{return internalTrackerSelectors(internalTrackerConfig(source,id));}
  catch(const Failure& error){return std::unexpected(error.error);}
  catch(const std::bad_alloc&){return std::unexpected(SavedTagEditError::Memory);}
}

std::expected<SavedTagEditResult, SavedTagEditError> editSavedDownloadTags(
    std::string_view pristine, const SavedTagEdit& edit, SavedTagLimits limits) {
  try {
    auto baseline = parsed(pristine, limits);
    if(const auto* internal=std::get_if<SavedTagSetInternalTrackerSelection>(&edit)){
      return setInternalTrackerSelection(std::move(baseline),*internal,limits);
    }
    if (const auto* swarm = std::get_if<SavedTagObserveSwarm>(&edit)) {
      return observeSwarm(std::move(baseline),*swarm,limits);
    }
    if (const auto* retirement = std::get_if<SavedTagRetireSubject>(&edit)) {
      return retireSubject(std::move(baseline), retirement->hash, limits);
    }
    const auto& doc = *baseline.source; const auto* manual = doc.find("3");
    std::int32_t highest = 0;
    for (const auto& tag : baseline.tags) {
      require(tag.id && !tag.duplicateId && tag.idKey == std::to_string(*tag.id), SavedTagEditError::AmbiguousId);
      highest = std::max(highest, *tag.id);
    }
    SavedTagEditResult result; std::string key; std::optional<std::string> recordReplacement;
    std::visit([&](const auto& action) {
      using T = std::decay_t<decltype(action)>;
      if constexpr (std::is_same_v<T,SavedTagRetireSubject> || std::is_same_v<T,SavedTagObserveSwarm>
          || std::is_same_v<T,SavedTagSetInternalTrackerSelection>) {
        // These operations process their entire selected type above.
      } else if constexpr (std::is_same_v<T, SavedTagCreate>) {
        // Original load creates only dictionary records; a bad numeric record
        // makes the reloaded allocator ambiguous, so do not guess around it.
        for (const auto& tag : baseline.tags) {
          const auto* loaded = manual->find(tag.idKey);
          require(loaded->type == Node::Type::Dictionary, SavedTagEditError::MalformedRecord);
          const auto* objects = loaded->find("o"); const auto* properties = loaded->find("p");
          require(!objects || objects->type == Node::Type::List, SavedTagEditError::MalformedMembership);
          require(!properties || properties->type == Node::Type::List, SavedTagEditError::MalformedMembership);
          if (objects) { for (const auto& item : objects->children) { require(item.type == Node::Type::Bytes, SavedTagEditError::MalformedMembership); } }
        }
        std::int32_t counter = 0;
        if (action.counter) {
          require(*action.counter >= 0 && *action.counter >= highest, SavedTagEditError::InvalidCounter); counter = *action.counter;
        } else if (!baseline.tags.empty()) {
          require(highest < std::numeric_limits<std::int32_t>::max(), SavedTagEditError::IdExhausted); counter = highest + 1;
        }
        require(counter < std::numeric_limits<std::int32_t>::max(), SavedTagEditError::IdExhausted);
        const auto id = counter + 1; key = std::to_string(id);
        require(!manual || !manual->find(key), SavedTagEditError::AmbiguousId);
        const auto name=nameValue(action.name,limits);
        std::vector<Change> fields;
        if (action.discovery) {
          constexpr std::string_view group="Discovery";
          require(action.discovery->hash.bytes().size()==20,SavedTagEditError::InvalidMember);
          require(!manual||ordinarySavedTagGroup(baseline,group),SavedTagEditError::UnsupportedGroupSettings);
          fields.emplace_back("c",dictionary(doc,nullptr,{
              {"gr",nameValue(group,limits)},{"pub",integer(1)},{"vis",integer(1)}},limits));
        } else if (action.publicTag.has_value()) {
          fields.emplace_back("c",dictionary(doc,nullptr,{{"pub",integer(*action.publicTag ? 1 : 0)}},limits));
        }
        fields.emplace_back("n",name);
        if (action.discovery) {
          const Node emptyRecord{Node::Type::Dictionary};
          auto member=membership(doc,emptyRecord,SavedDownloadTag{},action.discovery->hash,
              true,action.discovery->addedTimeSeconds,limits);
          fields.insert(fields.end(),std::make_move_iterator(member.begin()),std::make_move_iterator(member.end()));
        }
        recordReplacement = dictionary(doc,nullptr,fields,limits);
        result.createdId = id; result.createdUid = (std::int64_t{3} << 32) | id; result.allocationCounter = id;
      } else {
        require(action.id >= 0, SavedTagEditError::MissingTag); key = std::to_string(action.id);
        const auto found = std::find_if(baseline.tags.begin(), baseline.tags.end(), [&](const auto& tag) { return tag.id == action.id; });
        require(found != baseline.tags.end(), SavedTagEditError::MissingTag);
        const auto* record = manual->find(key); require(record && record->type == Node::Type::Dictionary, SavedTagEditError::MalformedRecord);
        if constexpr (std::is_same_v<T, SavedTagRemove>) { recordReplacement = std::nullopt; }
        else {
          std::vector<Change> changes;
          if constexpr (std::is_same_v<T, SavedTagRename>) {
            const auto* old = record->find("n"); require(!old || old->type == Node::Type::Bytes, SavedTagEditError::MalformedRecord);
            changes.emplace_back("n", nameValue(action.name, limits));
            if (old && old->bytes.starts_with("tag.")) {
              require(utf8(old->bytes), SavedTagEditError::InvalidName); const auto* config = configuration(*record); const auto* original = config ? config->find("oname") : nullptr;
              require(!original || original->type == Node::Type::Bytes, SavedTagEditError::MalformedConfiguration);
              if (!original) { std::string value; bytes(value, old->bytes, limits); changes.emplace_back("c", dictionary(doc, config, {{"oname", std::move(value)}}, limits)); }
            }
          } else if constexpr (std::is_same_v<T, SavedTagSetGroup>) {
            const auto* config=configuration(*record);const auto* old=config?config->find("gr"):nullptr;
            require(!old||old->type==Node::Type::Bytes,SavedTagEditError::MalformedConfiguration);
            std::optional<std::string> encoded;
            if(action.group){encoded=nameValue(*action.group,limits);}
            const bool same=old?bool(action.group)&&old->bytes==*action.group:!action.group;
            if(!same){
              require(ordinarySavedTagGroup(baseline,*record)
                  &&ordinarySavedTagGroup(baseline,action.group?std::optional<std::string_view>{*action.group}:std::nullopt),SavedTagEditError::UnsupportedGroupSettings);
              changes.emplace_back("c",dictionary(doc,config,{{"gr",std::move(encoded)}},limits));
            }
          } else if constexpr (std::is_same_v<T, SavedTagSetColour>) {
            const auto current=readSavedTagColour(*record);require(bool(current),SavedTagEditError::MalformedConfiguration);
            const auto* config=configuration(*record);const auto* old=config?config->find("col.rgb"):nullptr;
            std::optional<std::string> encoded;
            if(action.colour){std::string text;for(const auto channel:*action.colour){if(!text.empty()){text+=',';}text+=std::to_string(channel);}encoded=nameValue(text,limits);}
            const bool same=old?encoded&&raw(doc,*old)==*encoded:!encoded;
            // A null setter can inherit group gc even with no own key. Only
            // an explicit equal encoded value proves a no-event no-op alone.
            if(!same||!action.colour){require(ordinarySavedTagGroup(baseline,*record),SavedTagEditError::UnsupportedGroupSettings);}
            if(!same){changes.emplace_back("c",dictionary(doc,config,{{"col.rgb",std::move(encoded)}},limits));}
          } else if constexpr (std::is_same_v<T, SavedTagVisibility>) {
            const auto* config = configuration(*record); const auto* old = config ? config->find("vis") : nullptr;
            require(!old || old->type == Node::Type::Integer, SavedTagEditError::MalformedConfiguration);
            if (!old || (old->integer == 1) != action.visible) {
              changes.emplace_back("c", dictionary(doc, config, {{"vis", integer(action.visible ? 1 : 0)}}, limits));
            }
          } else if constexpr (std::is_same_v<T, SavedTagAddMember>) {
            changes = membership(doc, *record, *found, action.hash, true, action.addedTimeSeconds, limits);
            if(!changes.empty()){require(ordinarySavedTagGroup(baseline,*record),SavedTagEditError::UnsupportedGroupSettings);}
          } else if constexpr (std::is_same_v<T, SavedTagRemoveMember>) {
            changes = membership(doc, *record, *found, action.hash, false, 0, limits);
            if(!changes.empty()){require(ordinarySavedTagGroup(baseline,*record),SavedTagEditError::UnsupportedGroupSettings);}
          } else if constexpr (std::is_same_v<T, SavedTagMemberBatch>) {
            changes=membershipBatch(doc,*record,*found,action.hashes,action.add,action.addedTimeSeconds,limits);
            if(!changes.empty()){require(ordinarySavedTagGroup(baseline,*record),SavedTagEditError::UnsupportedGroupSettings);}
          } else if constexpr (std::is_same_v<T, SavedTagSetRateLimit>) {
            require(action.bytesPerSecond>=-1&&action.bytesPerSecond<2147483647,SavedTagEditError::MalformedConfiguration);
            const auto old=readSavedTagRateLimits(*record);require(old.has_value(),SavedTagEditError::MalformedConfiguration);
            const auto current=action.upload?old->upload:old->download;
            // The public original setter returns before writing/notifying when
            // its effective int32 value already equals the requested value.
            if(current!=action.bytesPerSecond){
              const auto* config=configuration(*record);const auto property=action.upload?"rl.up":"rl.down";
              changes.emplace_back("c",orderedField(doc,config,property,integer(action.bytesPerSecond),limits));
            }
          } else if constexpr (std::is_same_v<T, SavedTagSetAssignmentAction>) {
            const auto property="pp."+std::to_string(action.action);Node numeric(Node::Type::Integer);
            require(savedTagLifecycleProperty(property,numeric),SavedTagEditError::MalformedConfiguration);
            const auto* config=configuration(*record);const auto* old=config?config->find(property):nullptr;
            require(!old||old->type==Node::Type::Integer,SavedTagEditError::MalformedConfiguration);
            // Actual manager writer compares the exact Long,not its Boolean projection.
            const auto desired=action.enabled?1:0;
            if(!old||old->integer!=desired){changes.emplace_back("c",dictionary(doc,config,{{property,integer(desired)}},limits));}
          } else if constexpr (std::is_same_v<T, SavedTagSetUntagged>) {
            const auto* config=configuration(*record);const auto* old=config?config->find("pp.untagged"):nullptr;
            require(!old||old->type==Node::Type::Integer,SavedTagEditError::MalformedConfiguration);
            const bool same=action.enabled?(old&&old->integer==(*action.enabled?1:0)):!old;
            if(!same){std::optional<std::string> value;if(action.enabled){value=integer(*action.enabled?1:0);}
              changes.emplace_back("c",dictionary(doc,config,{{"pp.untagged",std::move(value)}},limits));}
          } else if constexpr (std::is_same_v<T, SavedTagSetTrackerNames>) {
            const auto* config=configuration(*record);const auto* old=config?config->find("pp.trackers"):nullptr;
            if(old){require(old->type==Node::Type::List&&old->children.size()<=64,SavedTagEditError::MalformedConfiguration);
              for(const auto& value:old->children){require(value.type==Node::Type::Bytes&&value.bytes.size()<=2048&&utf8(value.bytes),SavedTagEditError::MalformedConfiguration);}}
            std::optional<std::string> encoded;bool same=!old&&!action.names;
            if(action.names){
              require(action.names->size()<=64,SavedTagEditError::InvalidName);std::size_t count=0;encoded="l";
              same=old&&old->children.size()==action.names->size();
              for(std::size_t index=0;index<action.names->size();++index){const auto& value=(*action.names)[index];
                require(value.size()<=2048&&value.size()<=65536-count,SavedTagEditError::InvalidName);count+=value.size();
                for(unsigned char c:value){require(c<128,SavedTagEditError::InvalidName);}
                bytes(*encoded,value,limits);if(same&&old->children[index].bytes!=value){same=false;}
              }*encoded+='e';
            }
            if(!same){changes.emplace_back("c",dictionary(doc,config,{{"pp.trackers",std::move(encoded)}},limits));}
          } else if constexpr (std::is_same_v<T, SavedTagSetConstraint>) {
            changes = constraint(doc,*record,action,limits);
          }
          if constexpr(std::is_same_v<T,SavedTagSetRateLimit>){
            recordReplacement=changes.empty()?raw(doc,*record):orderedField(doc,record,"c",*changes.front().second,limits);
          }else{recordReplacement = changes.empty() ? raw(doc, *record) : dictionary(doc, record, changes, limits);}
        }
      }
    }, edit);
    const auto nextManual = dictionary(doc, manual, {{key, std::move(recordReplacement)}}, limits);
    const auto encoded = dictionary(doc, &doc.root(), {{"3", nextManual}}, limits);
    result.changed = encoded != pristine; result.value = parsed(encoded, limits); return result;
  } catch (const Failure& error) { return std::unexpected(error.error); }
  catch (const std::bad_alloc&) { return std::unexpected(SavedTagEditError::Memory); }
}
} // namespace biglybtc::core

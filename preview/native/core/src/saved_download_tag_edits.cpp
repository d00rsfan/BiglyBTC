// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/saved_download_tag_edits.hpp"
#include <algorithm>
#include <array>
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
std::vector<Change> membership(const Document& doc, const Node& record,
    const SavedDownloadTag& tag, const TorrentHash& hash, bool add,
    std::int64_t added, const SavedTagLimits& limits) {
  const auto id = memberId(hash); const auto* members = record.find("o"); const auto* props = record.find("p");
  require(!members || members->type == Node::Type::List, SavedTagEditError::MalformedMembership);
  const std::size_t count = members ? members->children.size() : 0;
  require(!props || (props->type == Node::Type::List && props->children.size() == count), SavedTagEditError::MalformedMembership);
  if (members) { for (const auto& item : members->children) { require(item.type == Node::Type::Bytes, SavedTagEditError::MalformedMembership); } }
  if (props) { for (const auto& item : props->children) {
    require(item.type == Node::Type::Dictionary, SavedTagEditError::MalformedMembership);
    const auto* time = item.find("a"); require(!time || time->type == Node::Type::Integer, SavedTagEditError::MalformedMembership);
  } }
  std::optional<std::size_t> selected;
  for (const auto& member : tag.members) {
    require(!member.duplicate, SavedTagEditError::MalformedMembership);
    if (member.rawId && *member.rawId == id) { selected = member.sourceIndex; }
  }
  if (add == selected.has_value()) { return {}; }
  std::string nextMembers, nextProps; append(nextMembers, "l", limits); append(nextProps, "l", limits);
  for (std::size_t i = 0; i < count; ++i) {
    if (!add && selected == i) { continue; }
    append(nextMembers, raw(doc, members->children[i]), limits);
    append(nextProps, props ? raw(doc, props->children[i]) : "de", limits);
  }
  if (add) { bytes(nextMembers, id, limits); append(nextProps, "d1:a", limits); append(nextProps, integer(added), limits); append(nextProps, "e", limits); }
  append(nextMembers, "e", limits); append(nextProps, "e", limits);
  return {{"o", std::move(nextMembers)}, {"p", std::move(nextProps)}};
}
} // namespace

bool ordinarySavedTagGroup(const SavedDownloadTags& tags,const Node& record)noexcept{
  try{
    if(!tags.source||tags.source->root().changed||record.type!=Node::Type::Dictionary){return false;}
    const auto* config=record.find("c");if(!config){return true;}
    if(config->type!=Node::Type::Dictionary){return false;}
    const auto* group=config->find("gr");if(!group){return true;}
    if(group->type!=Node::Type::Bytes||group->bytes.size()>4096||!utf8(group->bytes)){return false;}
    const auto* manual=tags.source->find("3");if(!manual||manual->type!=Node::Type::Dictionary){return false;}
    const auto* groups=manual->find("c");if(!groups){return true;}
    if(groups->type!=Node::Type::Dictionary){return false;}
    if(groups->children.empty()){return true;}
    // Original getGroupID uses JVM default encoding, not the UTF8 attribute
    // codec. Non-ASCII IDs with saved group settings need a proven mapping.
    if(std::any_of(group->bytes.begin(),group->bytes.end(),[](unsigned char c){return c>=128;})){return false;}
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string key;key.reserve((group->bytes.size()*8+4)/5);std::uint32_t bits=0;unsigned count=0;
    for(const auto byte:group->bytes){bits=(bits<<8)|static_cast<unsigned char>(byte);count+=8;
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

bool ordinarySavedTagMembershipEffects(const SavedDownloadTags& tags,const Node& record)noexcept{
  if(!ordinarySavedTagGroup(tags,record)){return false;}
  const auto* config=record.find("c");if(!config){return true;}
  for(std::size_t i=0;i<config->children.size();i+=2){
    const auto& key=config->children[i].bytes;const auto& value=config->children[i+1];
    if((key=="gr"||key=="oname")&&value.type==Node::Type::Bytes){continue;}
    if((key=="vis"||key=="pp.enabled.constraint")&&value.type==Node::Type::Integer){continue;}
    if((key=="pp.trackers"||key=="pp.tracker_templates")&&value.type==Node::Type::List&&value.children.empty()){continue;}
    if((key=="pp.untagged"||key=="pp.2")&&value.type==Node::Type::Integer&&value.integer!=1){continue;}
    if(key=="pp.constraint"&&value.type==Node::Type::List&&value.children.size()<=64){
      bool valid=true;for(const auto& text:value.children){if(text.type!=Node::Type::Bytes||text.bytes.size()>65536||!utf8(text.bytes)){valid=false;break;}}
      if(valid){continue;}
    }
    // Rates, actions, nonempty tracker rewrites and unknown fields need their
    // real owner/executor. Saving a member cannot stand in for that dispatch.
    return false;
  }
  return true;
}

std::expected<SavedTagEditResult, SavedTagEditError> editSavedDownloadTags(
    std::string_view pristine, const SavedTagEdit& edit, SavedTagLimits limits) {
  try {
    auto baseline = parsed(pristine, limits); const auto& doc = *baseline.source; const auto* manual = doc.find("3");
    std::int32_t highest = 0;
    for (const auto& tag : baseline.tags) {
      require(tag.id && !tag.duplicateId && tag.idKey == std::to_string(*tag.id), SavedTagEditError::AmbiguousId);
      highest = std::max(highest, *tag.id);
    }
    SavedTagEditResult result; std::string key; std::optional<std::string> recordReplacement;
    std::visit([&](const auto& action) {
      using T = std::decay_t<decltype(action)>;
      if constexpr (std::is_same_v<T, SavedTagCreate>) {
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
        recordReplacement = dictionary(doc, nullptr, {{"n", nameValue(action.name, limits)}}, limits);
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
          } else if constexpr (std::is_same_v<T, SavedTagSetConstraint>) {
            changes = constraint(doc,*record,action,limits);
          }
          recordReplacement = changes.empty() ? raw(doc, *record) : dictionary(doc, record, changes, limits);
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

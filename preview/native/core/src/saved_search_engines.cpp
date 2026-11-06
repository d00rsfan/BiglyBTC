// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/saved_search_engines.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <new>
namespace biglybtc::core {
namespace {
struct Failure {SavedSearchError error;};
void require(bool value,SavedSearchError error){if(!value){throw Failure{error};}}
SavedSearchLimits bounded(SavedSearchLimits l){
  const SavedSearchLimits m;
  require(l.document.inputBytes<=m.document.inputBytes&&l.document.stringBytes<=m.document.stringBytes&&l.document.nodes<=m.document.nodes&&l.document.depth<=m.document.depth&&l.engines<=m.engines&&l.fieldBytes<=m.fieldBytes&&l.textBytes<=m.textBytes&&l.issues<=m.issues,SavedSearchError::Limit);return l;
}
std::int32_t javaInt(std::int64_t value){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));}
SavedSearchEngines read(std::string_view bytes,const SavedSearchLimits& l){
  require(bytes.size()<=l.document.inputBytes,SavedSearchError::Limit);
  auto parsed=Document::parse(std::string(bytes),l.document);require(bool(parsed),SavedSearchError::Format);
  require(parsed->root().type==Node::Type::Dictionary,SavedSearchError::Root);
  SavedSearchEngines result;result.source=std::make_shared<const Document>(std::move(*parsed));
  const auto* list=result.source->find("engines");result.enginesPresent=list;result.pluginMapPresent=result.source->find("plugin_map");
  if(!list){return result;}
  require(list->type==Node::Type::List,SavedSearchError::List);require(list->children.size()<=l.engines,SavedSearchError::Limit);
  std::size_t textBytes=0;
  const auto issue=[&](SavedSearchIssueCode code,std::size_t row,std::string field){require(result.issues.size()<l.issues,SavedSearchError::Limit);result.issues.push_back({code,row,std::move(field)});};
  for(std::size_t i=0;i<list->children.size();++i){
    const auto& node=list->children[i];SavedSearchEngine row;row.sourceIndex=i;
    if(node.type!=Node::Type::Dictionary){issue(SavedSearchIssueCode::Record,i,"");result.engines.push_back(std::move(row));continue;}
    const auto integer=[&](std::string_view key,std::optional<std::int64_t> absent)->std::optional<std::int64_t>{
      const auto* value=node.find(key);if(!value){return absent;}
      if(value->type!=Node::Type::Integer){issue(SavedSearchIssueCode::Type,i,std::string(key));return {};}
      return value->integer;
    };
    const auto text=[&](std::string_view key)->std::optional<std::string>{
      const auto* value=node.find(key);if(!value){return {};}
      if(value->type!=Node::Type::Bytes){issue(SavedSearchIssueCode::Type,i,std::string(key));return {};}
      require(value->bytes.size()<=l.fieldBytes&&value->bytes.size()<=l.textBytes-textBytes,SavedSearchError::Limit);textBytes+=value->bytes.size();return std::string(value->bytes);
    };
    row.rawType=integer("type",{});if(!node.find("type")){issue(SavedSearchIssueCode::MissingType,i,"type");}
    if(row.rawType){row.type=javaInt(*row.rawType);switch(*row.type){case 1:row.kind=SavedSearchKind::Regex;break;case 2:row.kind=SavedSearchKind::Json;break;case 3:row.kind=SavedSearchKind::Plugin;break;case 4:row.kind=SavedSearchKind::Rss;break;default:issue(SavedSearchIssueCode::UnknownType,i,"type");}}
    row.id=integer("id",{});if(!node.find("id")){issue(SavedSearchIssueCode::MissingId,i,"id");}
    row.rawSelected=integer("selected",{});const auto selected=row.rawSelected?row.rawSelected:node.find("selected")?std::nullopt:std::optional<std::int64_t>{0};if(selected){row.selected=javaInt(*selected);}
    row.rawSource=integer("source",{});const auto origin=row.rawSource?row.rawSource:node.find("source")?std::nullopt:std::optional<std::int64_t>{0};if(origin){row.origin=javaInt(*origin);}
    row.rawAzVersion=integer("az_version",{});const auto version=row.rawAzVersion?row.rawAzVersion:node.find("az_version")?std::nullopt:std::optional<std::int64_t>{5};if(version){row.azVersion=javaInt(*version);if(*row.azVersion>5){issue(SavedSearchIssueCode::NewerVersion,i,"az_version");}}
    row.lastUpdated=integer("last_updated",0);row.uid=text("uid");row.name=text("name");row.searchUrl=text("web.search_url_format");
    result.engines.push_back(std::move(row));
  }
  std::map<std::int64_t,std::vector<std::size_t>> ids;std::map<std::string,std::vector<std::size_t>> uids;
  for(const auto& row:result.engines){if(row.id){ids[*row.id].push_back(row.sourceIndex);}if(row.uid){uids[*row.uid].push_back(row.sourceIndex);}}
  for(const auto& [id,indices]:ids){(void)id;if(indices.size()>1){for(const auto index:indices){result.engines[index].duplicateId=true;issue(SavedSearchIssueCode::DuplicateId,index,"id");}}}
  for(const auto& [uid,indices]:uids){(void)uid;if(indices.size()>1){for(const auto index:indices){result.engines[index].duplicateUid=true;issue(SavedSearchIssueCode::DuplicateUid,index,"uid");}}}
  return result;
}
}
std::expected<SavedSearchEngines,SavedSearchError> readSavedSearchEngines(std::string_view bytes,SavedSearchLimits limits){
  try{return read(bytes,bounded(limits));}catch(const Failure& failure){return std::unexpected(failure.error);}catch(const std::bad_alloc&){return std::unexpected(SavedSearchError::Memory);}
}
} // namespace biglybtc::core

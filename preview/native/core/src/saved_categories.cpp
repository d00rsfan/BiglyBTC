// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/saved_categories.hpp"
#include <algorithm>
#include <new>
#include <set>
#include <type_traits>
#include <utility>

namespace biglybtc::core {
namespace {
struct Failure {SavedCategoryError error;};
void require(bool value,SavedCategoryError error){if(!value){throw Failure{error};}}
SavedCategoryLimits bounded(SavedCategoryLimits value){
  const SavedCategoryLimits cap;
  value.document.inputBytes=std::min(value.document.inputBytes,cap.document.inputBytes);
  value.document.stringBytes=std::min(value.document.stringBytes,cap.document.stringBytes);
  value.document.nodes=std::min(value.document.nodes,cap.document.nodes);
  value.document.depth=std::min(value.document.depth,cap.document.depth);
  value.categories=std::min(value.categories,cap.categories);value.nameBytes=std::min(value.nameBytes,cap.nameBytes);value.textBytes=std::min(value.textBytes,cap.textBytes);return value;
}
bool utf8(std::string_view value){
  for(std::size_t i=0;i<value.size();){
    const auto first=static_cast<unsigned char>(value[i++]);if(first<0x80){continue;}
    const unsigned count=first>=0xc2&&first<=0xdf?1:first>=0xe0&&first<=0xef?2:first>=0xf0&&first<=0xf4?3:0;
    if(!count||count>value.size()-i){return false;}std::uint32_t code=first&((1U<<(6-count))-1);
    for(unsigned j=0;j<count;++j){const auto next=static_cast<unsigned char>(value[i++]);if((next&0xc0)!=0x80){return false;}code=(code<<6)|(next&0x3f);}
    if(code<(count==1?0x80U:count==2?0x800U:0x10000U)||code>0x10ffff||(code>=0xd800&&code<=0xdfff)){return false;}
  }return true;
}
void validName(std::string_view name,const SavedCategoryLimits& limits){require(name.size()<=limits.nameBytes&&name.size()<=limits.textBytes,SavedCategoryError::Limit);require(utf8(name),SavedCategoryError::Name);}
bool runtimeSpecial(std::string_view name){return name=="Categories.all"||name=="Categories.uncategorized";}
bool special(std::string_view name){return runtimeSpecial(name)||name==SavedAllCategory||name==SavedUncategorized;}
void userName(std::string_view name,const SavedCategoryLimits& limits){validName(name,limits);require(!special(name),SavedCategoryError::ReservedName);}
std::shared_ptr<const Document> document(std::string_view input,const SavedCategoryLimits& limits){
  require(input.size()<=limits.document.inputBytes,SavedCategoryError::Limit);
  auto parsed=Document::parse(std::string(input),limits.document);
  if(!parsed){const auto& why=parsed.error().code;throw Failure{why=="input_limit"||why=="string_limit"||why=="node_limit"||why=="depth_limit"?SavedCategoryError::Limit:SavedCategoryError::Format};}
  require(parsed->root().type==Node::Type::Dictionary,SavedCategoryError::Root);
  return std::make_shared<const Document>(std::move(*parsed));
}
SavedCategories categories(std::string_view input,const SavedCategoryLimits& limits){
  SavedCategories result;result.source=document(input,limits);const auto* list=result.source->find("categories");if(!list){return result;}
  require(list->type==Node::Type::List,SavedCategoryError::List);require(list->children.size()<=limits.categories,SavedCategoryError::Limit);result.listPresent=true;
  std::size_t total=0;std::set<std::string_view,std::less<>> names;
  for(std::size_t i=0;i<list->children.size();++i){
    const auto& node=list->children[i];require(node.type==Node::Type::Dictionary,SavedCategoryError::Record);const auto* name=node.find("name");require(name&&name->type==Node::Type::Bytes,SavedCategoryError::Name);validName(name->bytes,limits);
    require(!runtimeSpecial(name->bytes),SavedCategoryError::ReservedName);require(names.insert(name->bytes).second,SavedCategoryError::DuplicateName);
    require(name->bytes.size()<=limits.textBytes-total,SavedCategoryError::Limit);total+=name->bytes.size();
    SavedCategory row;row.sourceIndex=i;row.name=name->bytes;
    row.kind=name->bytes==SavedAllCategory?SavedCategoryKind::All:name->bytes==SavedUncategorized?SavedCategoryKind::Uncategorized:SavedCategoryKind::User;
    for(const auto key:{"maxup","maxdown"}){const auto* value=node.find(key);require(!value||value->type==Node::Type::Integer,SavedCategoryError::Record);if(value){(std::string_view(key)=="maxup"?row.rawMaxUpload:row.rawMaxDownload)=value->integer;}}
    const auto* attrs=node.find("attr");require(!attrs||attrs->type==Node::Type::Dictionary,SavedCategoryError::Attributes);row.attributesPresent=attrs!=nullptr;result.categories.push_back(std::move(row));
  }return result;
}
const SavedCategory* user(const SavedCategories& values,std::string_view name){const auto found=std::find_if(values.categories.begin(),values.categories.end(),[&](const auto& row){return row.kind==SavedCategoryKind::User&&row.name==name;});return found==values.categories.end()?nullptr:&*found;}
void append(std::string& output,std::string_view value,const SavedCategoryLimits& limits){require(output.size()<=limits.document.inputBytes&&value.size()<=limits.document.inputBytes-output.size(),SavedCategoryError::Limit);output.append(value);}
std::string bytes(std::string_view value,const SavedCategoryLimits& limits){require(value.size()<=limits.document.stringBytes,SavedCategoryError::Limit);std::string out;append(out,std::to_string(value.size()),limits);append(out,":",limits);append(out,value,limits);return out;}
std::string raw(const Document& source,const Node& value){return source.originalBytes().substr(value.begin,value.end-value.begin);}
using Change=std::pair<std::string,std::optional<std::string>>;
std::string dictionary(const Document& source,const Node* node,const std::vector<Change>& changes,const SavedCategoryLimits& limits){
  std::string out;append(out,"d",limits);std::vector<bool> used(changes.size(),false);
  if(node){require(node->type==Node::Type::Dictionary,SavedCategoryError::Attributes);
    for(std::size_t i=0;i<node->children.size();i+=2){const auto& key=node->children[i];const auto& value=node->children[i+1];const auto found=std::find_if(changes.begin(),changes.end(),[&](const auto& change){return change.first==key.bytes;});
      if(found==changes.end()){append(out,raw(source,key),limits);append(out,raw(source,value),limits);}
      else{used[static_cast<std::size_t>(found-changes.begin())]=true;if(found->second){append(out,raw(source,key),limits);append(out,*found->second,limits);}}
    }
  }
  for(std::size_t i=0;i<changes.size();++i){if(!used[i]&&changes[i].second){append(out,bytes(changes[i].first,limits),limits);append(out,*changes[i].second,limits);}}
  append(out,"e",limits);return out;
}
std::string created(std::string_view name,bool all,const SavedCategoryLimits& limits){
  std::string out="d4:attrde";if(!all){append(out,"7:maxdowni0e5:maxupi0e",limits);}append(out,"4:name",limits);append(out,bytes(name,limits),limits);append(out,"e",limits);return out;
}
SavedCategoryAssignment assignment(const SavedCategories& defs,std::string_view input,const SavedCategoryLimits& limits){
  SavedCategoryAssignment result;result.source=document(input,limits);const auto* attributes=result.source->find("attributes");require(!attributes||attributes->type==Node::Type::Dictionary,SavedCategoryError::Attributes);
  const auto* value=attributes?attributes->find("category"):nullptr;if(value){require(value->type==Node::Type::Bytes,SavedCategoryError::Assignment);validName(value->bytes,limits);result.declaredName=value->bytes;result.resolved=user(defs,*result.declaredName)!=nullptr;}return result;
}
SavedCategoryAssignmentEditResult assign(const SavedCategories& defs,std::string_view state,const std::optional<std::string>& target,const SavedCategoryLimits& limits){
  SavedCategoryAssignmentEditResult result;result.value=assignment(defs,state,limits);
  if(target){userName(*target,limits);require(user(defs,*target)!=nullptr,SavedCategoryError::MissingCategory);}
  if((target&&result.value.declaredName==target)||(!target&&!result.value.resolved)){return result;}
  const auto& source=*result.value.source;const auto* attrs=source.find("attributes");
  result.encodedAttributes=dictionary(source,attrs,{{"category",target?std::optional(bytes(*target,limits)):std::nullopt}},limits);
  auto next=dictionary(source,&source.root(),{{"attributes",*result.encodedAttributes}},limits);
  result.value=assignment(defs,next,limits);result.changed=true;return result;
}
}
std::expected<SavedCategories,SavedCategoryError> readSavedCategories(std::string_view input,SavedCategoryLimits limits){try{return categories(input,bounded(limits));}catch(const Failure& failure){return std::unexpected(failure.error);}catch(const std::bad_alloc&){return std::unexpected(SavedCategoryError::Memory);}}
std::expected<SavedCategoryEditResult,SavedCategoryError> editSavedCategories(std::string_view input,const SavedCategoryEdit& edit,SavedCategoryLimits limits){
  try{
    limits=bounded(limits);auto before=categories(input,limits);SavedCategoryEditResult result{before,false,{},{}};
    std::optional<std::string> add,remove;
    std::visit([&](const auto& command){userName(command.name,limits);using Type=std::decay_t<decltype(command)>;const auto* existing=user(before,command.name);
      if constexpr(std::is_same_v<Type,SavedCategoryCreate>){if(!existing){add=command.name;result.createdName=command.name;}}
      else{if(existing){remove=command.name;result.clearResolvedAssignments=SavedCategoryRemovalIntent{command.name,before.source};}}
    },edit);
    if(!add&&!remove){return result;}
    const auto& source=*before.source;const auto* old=source.find("categories");std::string list="l";bool inserted=false,hasAll=false,hasUncategorized=false;
    for(const auto& row:before.categories){
      if(add&&!inserted&&row.kind!=SavedCategoryKind::User){append(list,created(*add,false,limits),limits);inserted=true;}
      hasAll|=row.kind==SavedCategoryKind::All;hasUncategorized|=row.kind==SavedCategoryKind::Uncategorized;
      if(!remove||row.name!=*remove){append(list,raw(source,old->children[row.sourceIndex]),limits);}
    }
    if(add&&!inserted){append(list,created(*add,false,limits),limits);}
    // Original saveCategories always emits both built-ins with these defaults
    // if no saved record supplied their attributes/uncategorized speed limits.
    if(!hasUncategorized){append(list,created(SavedUncategorized,false,limits),limits);}if(!hasAll){append(list,created(SavedAllCategory,true,limits),limits);}append(list,"e",limits);
    result.value=categories(dictionary(source,&source.root(),{{"categories",std::move(list)}},limits),limits);result.changed=true;return result;
  }catch(const Failure& failure){return std::unexpected(failure.error);}catch(const std::bad_alloc&){return std::unexpected(SavedCategoryError::Memory);}
}
std::expected<SavedCategoryAssignment,SavedCategoryError> readSavedCategoryAssignment(std::string_view defs,std::string_view state,SavedCategoryLimits limits){try{limits=bounded(limits);return assignment(categories(defs,limits),state,limits);}catch(const Failure& failure){return std::unexpected(failure.error);}catch(const std::bad_alloc&){return std::unexpected(SavedCategoryError::Memory);}}
std::expected<SavedCategoryAssignmentEditResult,SavedCategoryError> assignSavedCategory(std::string_view defs,std::string_view state,const std::optional<std::string>& target,SavedCategoryLimits limits){try{limits=bounded(limits);return assign(categories(defs,limits),state,target,limits);}catch(const Failure& failure){return std::unexpected(failure.error);}catch(const std::bad_alloc&){return std::unexpected(SavedCategoryError::Memory);}}
std::expected<SavedCategoryAssignmentEditResult,SavedCategoryError> clearDeletedCategoryAssignment(const SavedCategoryRemovalIntent& intent,std::string_view state,SavedCategoryLimits limits){
  try{
    limits=bounded(limits);require(bool(intent.categoriesBefore),SavedCategoryError::MissingCategory);userName(intent.name,limits);
    const auto defs=categories(intent.categoriesBefore->originalBytes(),limits);require(user(defs,intent.name)!=nullptr,SavedCategoryError::MissingCategory);
    auto current=assignment(defs,state,limits);if(current.declaredName!=std::optional(intent.name)){return SavedCategoryAssignmentEditResult{std::move(current),false,{}};}
    return assign(defs,state,std::nullopt,limits);
  }catch(const Failure& failure){return std::unexpected(failure.error);}catch(const std::bad_alloc&){return std::unexpected(SavedCategoryError::Memory);}
}
} // namespace biglybtc::core

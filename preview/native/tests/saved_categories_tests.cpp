// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/saved_categories.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace c=biglybtc::core;using namespace std::string_view_literals;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,c::SavedCategoryError> value){check(bool(value),"Expected pure category value/edit");return std::move(*value);}
template<class T>void error(const std::expected<T,c::SavedCategoryError>& value,c::SavedCategoryError expected){check(!value&&value.error()==expected,"Wrong category refusal");}
std::string bytes(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string dict(std::initializer_list<std::pair<std::string,std::string>> fields){std::string out="d";for(const auto& [key,value]:fields){out+=bytes(key)+value;}return out+'e';}
std::string list(std::initializer_list<std::string> values){std::string out="l";for(const auto& value:values){out+=value;}return out+'e';}
std::string raw(const c::Document& source,const c::Node* node){check(node!=nullptr,"Missing original range");return source.originalBytes().substr(node->begin,node->end-node->begin);}
std::string category(std::string_view name){return dict({{"name",bytes(name)},{"maxup","i0e"},{"maxdown","i0e"},{"attr","de"}});}
std::string fixture(){
  const auto unknown=dict({{"binary",bytes("x\0y"sv)},{"other",list({"i-3e",bytes("keep")})}});
  const auto alpha=dict({{"name",bytes("Alpha")},{"maxup","i4294967297e"},{"maxdown","i-1e"},{"attr",dict({{"future",bytes("opaque")}})},{"future-row",unknown}});
  const auto all=dict({{"name",bytes(c::SavedAllCategory)},{"attr",dict({{"view",bytes("keep-all")}})},{"future-all",unknown}});
  const auto uncat=dict({{"name",bytes(c::SavedUncategorized)},{"maxup","i19e"},{"maxdown","i27e"},{"attr",dict({{"view",bytes("keep-uncat")}})}});
  return dict({{"future-root",unknown},{"categories",list({alpha,all,category("Beta"),uncat})}});
}
std::string state(const std::optional<std::string>& name){
  std::string attrs="d"+bytes("future")+list({bytes("\0opaque"sv),"i-7e"});if(name){attrs+=bytes("category")+bytes(*name);}attrs+='e';
  const auto info=dict({{"name",bytes("file.bin")},{"length","i3e"},{"piece length","i16384e"},{"pieces",bytes(std::string(20,'q'))}});
  return dict({{"attributes",attrs},{"info",info},{"future-state",list({bytes("preserve"),"de"})}});
}
const c::Node& named(const c::SavedCategories& defs,std::string_view name){
  const auto row=std::find_if(defs.categories.begin(),defs.categories.end(),[&](const auto& value){return value.name==name;});check(row!=defs.categories.end(),"Missing category");return defs.source->find("categories")->children[row->sourceIndex];
}
void createDefaultsAndNameIdentityHaveNoPersistedAllocator(){
  const auto missing=take(c::readSavedCategories("de"));check(!missing.listPresent&&missing.categories.empty(),"Missing list invented persisted built-ins");
  const auto first=take(c::editSavedCategories("de",c::SavedCategoryCreate{"Alpha"}));check(first.changed&&first.createdName=="Alpha"&&!first.clearResolvedAssignments,"Create result differs");check(first.value.categories.size()==3,"Original save did not supply both built-ins");
  const auto& added=named(first.value,"Alpha");check(added.find("name")->bytes=="Alpha"&&added.find("maxup")->integer==0&&added.find("maxdown")->integer==0&&added.find("attr")->children.empty(),"New category defaults differ");check(!added.find("id")&&!added.find("uid"),"Process-local category tag ID was persisted");
  const auto& all=named(first.value,c::SavedAllCategory);check(!all.find("maxup")&&!all.find("maxdown")&&all.find("attr"),"All-category disk fields differ");
  const auto wire=first.value.source->originalBytes();const auto same=take(c::editSavedCategories(wire,c::SavedCategoryCreate{"Alpha"}));check(!same.changed&&!same.createdName&&same.value.source->originalBytes()==wire,"Existing-name create rewrote source");
  const auto lower=take(c::editSavedCategories(wire,c::SavedCategoryCreate{"alpha"}));check(lower.changed&&lower.value.categories.size()==4,"Java case-sensitive identity was folded");
  const auto empty=take(c::editSavedCategories(wire,c::SavedCategoryCreate{""}));check(empty.createdName&&empty.createdName->empty(),"Original empty name was silently normalized/refused");
  const auto whitespace=take(c::editSavedCategories(wire,c::SavedCategoryCreate{" Alpha "}));check(whitespace.createdName==" Alpha ","Name whitespace was trimmed");
}
void definitionEditsPreserveUnknownRowsAndBuiltinsRaw(){
  const auto before=take(c::readSavedCategories(fixture()));const auto added=take(c::editSavedCategories(before.source->originalBytes(),c::SavedCategoryCreate{"New"}));check(added.changed,"New category not created");
  check(raw(*before.source,before.source->find("future-root"))==raw(*added.value.source,added.value.source->find("future-root")),"Root unknown bytes changed");
  for(const auto& row:before.categories){check(raw(*before.source,&named(before,row.name))==raw(*added.value.source,&named(added.value,row.name)),"Unedited category row/built-in was reencoded");}
  check(named(added.value,"Alpha").find("maxup")->integer==4294967297LL,"Original raw Long limit was narrowed on disk");
  const auto original=before.source->originalBytes();const auto removed=take(c::editSavedCategories(original,c::SavedCategoryDelete{"Alpha"}));check(removed.changed&&removed.clearResolvedAssignments&&removed.clearResolvedAssignments->name=="Alpha","Definition removal lost explicit cleanup intent");check(removed.clearResolvedAssignments->categoriesBefore->originalBytes()==original,"Deletion intent lost exact beforeimage");
  for(const auto name:{"Beta"sv,c::SavedAllCategory,c::SavedUncategorized}){check(raw(*before.source,&named(before,name))==raw(*removed.value.source,&named(removed.value,name)),"Delete changed unrelated/built-in row");}
  const auto absent=take(c::editSavedCategories(original,c::SavedCategoryDelete{"Missing"}));check(!absent.changed&&!absent.clearResolvedAssignments&&absent.value.source->originalBytes()==original,"Missing definition fabricated cleanup");
}
void selectedAssignmentPreservesInfoUnknownAndUnresolvedHints(){
  const auto defs=fixture(),initial=state(std::nullopt);const auto before=take(c::readSavedCategoryAssignment(defs,initial));check(!before.declaredName&&!before.resolved,"Absent assignment fabricated membership");
  const auto assigned=take(c::assignSavedCategory(defs,initial,std::string("Alpha")));check(assigned.changed&&assigned.encodedAttributes&&assigned.value.resolved&&assigned.value.declaredName=="Alpha","Selected user assignment failed");
  const auto after=assigned.value.source;check(raw(*before.source,before.source->find("info"))==raw(*after,after->find("info"))&&raw(*before.source,before.source->find("future-state"))==raw(*after,after->find("future-state")),"Assignment modified metadata/unknown state");
  check(raw(*before.source,before.source->find("attributes")->find("future"))==raw(*after,after->find("attributes")->find("future")),"Assignment reencoded unknown attributes");
  const auto unchanged=take(c::assignSavedCategory(defs,after->originalBytes(),std::string("Alpha")));check(!unchanged.changed&&!unchanged.encodedAttributes&&unchanged.value.source->originalBytes()==after->originalBytes(),"Same assignment rewrote bytes");
  const auto cleared=take(c::assignSavedCategory(defs,after->originalBytes(),std::nullopt));check(cleared.changed&&!cleared.value.declaredName&&!cleared.value.resolved&&cleared.encodedAttributes,"Resolved clear did not remove category");
  const auto orphan=state(std::string("Unknown plugin category"));const auto orphanClear=take(c::assignSavedCategory(defs,orphan,std::nullopt));check(!orphanClear.changed&&!orphanClear.value.resolved&&orphanClear.value.declaredName=="Unknown plugin category"&&orphanClear.value.source->originalBytes()==orphan,"Unresolved saved assignment was discarded");
  const auto explicitlyReassigned=take(c::assignSavedCategory(defs,orphan,std::string("Beta")));check(explicitlyReassigned.changed&&explicitlyReassigned.value.resolved&&explicitlyReassigned.value.declaredName=="Beta","Explicit selected assignment failed to replace old hint");
  error(c::assignSavedCategory(defs,initial,std::string("Absent")),c::SavedCategoryError::MissingCategory);
}
void deletionCleanupIsPerSuppliedResolvedStateAndNotAnInventoryClaim(){
  const auto defs=fixture(),member=state(std::string("Alpha")),other=state(std::string("Beta")),orphan=state(std::string("Never loaded"));
  const auto removed=take(c::editSavedCategories(defs,c::SavedCategoryDelete{"Alpha"}));check(removed.clearResolvedAssignments.has_value(),"Missing deletion intent");
  // The pure definition edit has never been passed these state strings and
  // cannot mutate/claim them. Only a caller-selected matching state is cleared.
  const auto& intent=*removed.clearResolvedAssignments;const auto cleared=take(c::clearDeletedCategoryAssignment(intent,member));check(cleared.changed&&!cleared.value.declaredName,"Matching prior USER assignment not cleared");
  for(const auto* untouched:{&other,&orphan}){const auto result=take(c::clearDeletedCategoryAssignment(intent,*untouched));check(!result.changed&&!result.encodedAttributes&&result.value.source->originalBytes()==*untouched,"Deletion cleaned unrelated/unresolved state");}
  const auto stillSaved=take(c::readSavedCategoryAssignment(removed.value.source->originalBytes(),member));check(stillSaved.declaredName=="Alpha"&&!stillSaved.resolved,"Unprovided/not-inventory assignment was invented away");
  auto invalid=intent;invalid.categoriesBefore=removed.value.source;error(c::clearDeletedCategoryAssignment(invalid,member),c::SavedCategoryError::MissingCategory);
}
void malformedReservedAndQuotaInputsRefuseWithoutMutation(){
  const auto defs=fixture();
  for(const auto name:{c::SavedAllCategory,c::SavedUncategorized,"Categories.all"sv,"Categories.uncategorized"sv}){
    error(c::editSavedCategories(defs,c::SavedCategoryCreate{std::string(name)}),c::SavedCategoryError::ReservedName);error(c::editSavedCategories(defs,c::SavedCategoryDelete{std::string(name)}),c::SavedCategoryError::ReservedName);error(c::assignSavedCategory(defs,state({}),std::string(name)),c::SavedCategoryError::ReservedName);
  }
  error(c::readSavedCategories(dict({{"categories",list({category("Alpha"),category("Alpha")})}})),c::SavedCategoryError::DuplicateName);
  error(c::readSavedCategories(dict({{"categories","de"}})),c::SavedCategoryError::List);
  error(c::readSavedCategories(dict({{"categories",list({"i1e"})}})),c::SavedCategoryError::Record);
  error(c::readSavedCategories(dict({{"categories",list({dict({{"name",bytes("A")},{"attr","i1e"}})})}})),c::SavedCategoryError::Attributes);
  error(c::editSavedCategories(defs,c::SavedCategoryCreate{std::string("\xc0\x80",2)}),c::SavedCategoryError::Name);
  error(c::readSavedCategories("d10:categoriesle10:categorieslee"),c::SavedCategoryError::Format);
  error(c::assignSavedCategory(defs,dict({{"attributes","i1e"}}),std::string("Alpha")),c::SavedCategoryError::Attributes);
  error(c::assignSavedCategory(defs,dict({{"attributes",dict({{"category","i1e"}})}}),std::string("Alpha")),c::SavedCategoryError::Assignment);
  c::SavedCategoryLimits limits;limits.nameBytes=2;error(c::readSavedCategories(defs,limits),c::SavedCategoryError::Limit);
  limits={};limits.document.inputBytes=2;error(c::editSavedCategories("de",c::SavedCategoryCreate{""},limits),c::SavedCategoryError::Limit);
  limits={};limits.categories=1;error(c::readSavedCategories(defs,limits),c::SavedCategoryError::Limit);
  std::string many="l";for(unsigned i=0;i<4097;++i){many+=category(std::to_string(i));}many+='e';limits={};limits.categories=100000;error(c::readSavedCategories(dict({{"categories",many}}),limits),c::SavedCategoryError::Limit);
  check(defs==fixture(),"Refused pure operation mutated input");
}
}
int main(){try{createDefaultsAndNameIdentityHaveNoPersistedAllocator();definitionEditsPreserveUnknownRowsAndBuiltinsRaw();selectedAssignmentPreservesInfoUnknownAndUnresolvedHints();deletionCleanupIsPerSuppliedResolvedStateAndNotAnInventoryClaim();malformedReservedAndQuotaInputsRefuseWithoutMutation();std::cout<<"PASS saved categories: five pure families\n";return EXIT_SUCCESS;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}}

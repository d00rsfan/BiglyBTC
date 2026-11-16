// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/saved_download_tag_edits.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace c=biglybtc::core;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
std::string bytes(std::string_view text){return std::to_string(text.size())+':'+std::string(text);}
std::string list(const c::Node& node){std::string out="l";for(const auto& value:node.children){out+=bytes(value.bytes);}return out+'e';}
void actualOriginalNullableOrderedPropertyWriter(){
 std::ifstream input(TRACKER_TAG_PROPERTY_FIXTURE,std::ios::binary);const std::string encoded{std::istreambuf_iterator<char>(input),{}};auto original=c::Document::parse(encoded);check(bool(original),"Actual original property fixture");unsigned cases=0;
 for(const auto& row:original->find("rows")->children){
  const auto before="d11:future-root4:keep1:3d1:1d1:n9:Generated1:cd6:future6:opaque"+std::string(row.find("before_present")->integer?"11:pp.trackers"+list(*row.find("before")):"")+"eeee";
  std::optional<std::vector<std::string>> names;if(row.find("value_present")->integer){names.emplace();for(const auto& value:row.find("value")->children){names->emplace_back(value.bytes);}}
  auto changed=c::editSavedDownloadTags(before,c::SavedTagSetTrackerNames{1,names});check(bool(changed),"Typed tracker property setter refused actual original case");check(changed->changed==(row.find("changed")->integer!=0),"Changed/noop differs from actual original writer");
  const auto& source=*changed->value.source;const auto* attrs=source.find("3")->find("1")->find("c");const auto* property=attrs->find("pp.trackers");check(bool(property)==bool(row.find("after_present")->integer),"Nullable versus explicit empty list changed");
  if(property){const auto& expected=row.find("after")->children;check(property->children.size()==expected.size(),"Property array size changed");for(std::size_t i=0;i<expected.size();++i){check(property->children[i].bytes==expected[i].bytes,"Raw case/space/order/duplicate property changed");}}
  check(attrs->find("future")->bytes=="opaque"&&source.find("future-root")->bytes=="keep","Property save lost unrelated data");if(!changed->changed){check(source.originalBytes()==before,"No-op changed source bytes");}++cases;
 }check(cases==64,"Original nullable property case count changed");
 const auto before="d1:3d1:1d1:n9:Generated1:cd11:pp.trackersli1eeeeee";
 check(bool(c::Document::parse(before)),"Malformed-field fixture must be valid bencode");
 check(!c::editSavedDownloadTags(before,c::SavedTagSetTrackerNames{1,std::nullopt}),"Malformed old property silently cleared");
 auto ordinary=c::Document::parse("d1:3d1:1d1:n9:Generatedeee");check(bool(ordinary),"Ordinary source fixture");
 for(const auto& value:{std::string(2049,'a'),std::string(1,char(0xff))}){check(!c::editSavedDownloadTags(ordinary->originalBytes(),c::SavedTagSetTrackerNames{1,std::vector<std::string>{value}}),"Unqualified new property saved");}
 check(!c::editSavedDownloadTags(ordinary->originalBytes(),c::SavedTagSetTrackerNames{1,std::vector<std::string>(65,"a")}),"Too many tracker property names saved");
}
}
int main(){try{actualOriginalNullableOrderedPropertyWriter();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"PASS64 actual original tracker property writes, nullable/noop/raw order and refusal boundaries\n";return 0;}

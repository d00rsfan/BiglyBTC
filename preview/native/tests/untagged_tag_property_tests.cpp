// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/saved_download_tag_edits.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace c=biglybtc::core;
namespace {void require(bool ok,const char* message){if(!ok){throw std::runtime_error(message);}}
void actualNullableBooleanWriter(){
 std::ifstream input(UNTAGGED_TAG_PROPERTY_FIXTURE,std::ios::binary);const std::string encoded{std::istreambuf_iterator<char>(input),{}};auto original=c::Document::parse(encoded);require(bool(original),"Actual original boolean fixture");unsigned count=0;
 for(const auto& row:original->find("rows")->children){
  const auto before="d11:future-root4:keep1:3d1:1d1:n9:Generated1:cd6:future6:opaque"+std::string(row.find("before_present")->integer?"11:pp.untaggedi"+std::to_string(row.find("before")->integer)+"e":"")+"eeee";
  std::optional<bool> value;if(row.find("value")->integer!=-1){value=row.find("value")->integer==1;}
  auto result=c::editSavedDownloadTags(before,c::SavedTagSetUntagged{1,value});require(bool(result),"Nullable boolean source save");require(result->changed==bool(row.find("changed")->integer),"Changed versus noop differs from Java");const auto& doc=*result->value.source;const auto* config=doc.find("3")->find("1")->find("c");const auto* flag=config->find("pp.untagged");require(bool(flag)==bool(row.find("after_present")->integer),"Nullable boolean presence differs");if(flag){require(flag->type==c::Node::Type::Integer&&flag->integer==row.find("after")->integer,"Saved boolean integer differs");}require(config->find("future")->bytes=="opaque"&&doc.find("future-root")->bytes=="keep","Boolean save lost opaque fields");if(!result->changed){require(doc.originalBytes()==before,"Boolean noop changed bytes");}++count;
 }require(count==18,"Actual original boolean count changed");
 const auto malformed="d1:3d1:1d1:n9:Generated1:cd11:pp.untagged1:xeeee";require(bool(c::Document::parse(malformed)),"Malformed-field source must parse");require(!c::editSavedDownloadTags(malformed,c::SavedTagSetUntagged{1,std::nullopt}),"Malformed old boolean silently erased");
 auto mixed=c::readSavedDownloadTags("d1:3d1:1d1:n9:Generated1:cd11:pp.untaggedi1e13:pp.constraintl4:trueeeeee");require(bool(mixed),"Mixed source");require(!c::ordinarySavedTagMembershipEffects(*mixed,*mixed->source->find("3")->find("1")),"Active Untagged and constraint effects falsely supported");
}
}
int main(){try{actualNullableBooleanWriter();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"PASS18 actual original Untagged boolean writes, nullable/noop/raw long, opaque and malformed/mixed refusal\n";return 0;}

// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_rate_limits.hpp>
#include <biglybtc/saved_download_tag_edits.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace b=biglybtc::core;
void need(bool value,const char* text){if(!value){throw std::runtime_error(text);}}
int main(){try{
 std::ifstream file(TAG_RATE_LIMITS_ORIGINAL_FIXTURE,std::ios::binary);need(bool(file),"Generated original rate fixture missing");
 auto fixture=b::Document::parse({std::istreambuf_iterator<char>(file),{}});need(bool(fixture),"Generated original rate fixture invalid");unsigned cases=0;
 for(const auto& row:fixture->find("rows")->children){const std::string raw(row.find("record")->bytes);auto record=b::Document::parse(raw);need(bool(record),"Actual original saved rate record invalid");
  auto limits=b::readSavedTagRateLimits(record->root());need(bool(limits),"Actual saved integer rate was refused");const bool up=row.find("upload")->integer==1;
  const auto expected=row.find("result")->integer;need((up?limits->upload:limits->download)==expected,"Actual original Long narrowing/rate normalization differs");need(record->serialize()==raw,"Passive rate projection edited source");
  auto tags=b::readSavedDownloadTags("d1:3d1:1"+raw+"ee");need(bool(tags),"Generated rate membership source invalid");const auto& saved=*tags->source->find("3")->find("1");
  need(b::ordinarySavedTagMembershipEffects(*tags,saved)==(expected==0),"Active rate was silently ignored or unlimited metadata refused");
  need(b::lifecycleSavedTagMembershipEffects(*tags,saved)==(expected==0),"Lifecycle actor bypassed a real rate owner requirement");
  const auto key=up?"rl.up":"rl.down";if(const auto* value=record->find("c")->find(key)){need(b::savedTagUnlimitedRateProperty(key,*value)==(expected==0),"Unlimited-property boundary differs");}++cases;
 }
 need(cases==78,"Actual original rate cases lost");
 for(const auto raw:{"i0e","d1:c1:xe","d1:cd5:rl.up1:0ee","d1:cd7:rl.downleee"}){auto record=b::Document::parse(raw);need(bool(record),"Malformed-type fixture must parse");need(!b::readSavedTagRateLimits(record->root()),"Malformed selected rate became a live value");}
 auto absent=b::Document::parse("de");need(absent&&b::readSavedTagRateLimits(absent->root())->unlimited(),"Missing rate defaults differ");
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
 std::cout<<"PASS78 actual original saved rate normalization cases;int32 narrowing,negative disable,unlimited-only source policy and malformed refusal\n";
}

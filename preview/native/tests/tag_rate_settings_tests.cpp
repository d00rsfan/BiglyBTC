// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/saved_download_tag_edits.hpp>
#include <biglybtc/tag_rate_limits.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace c=biglybtc::core;
void need(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
int main(){try{
 std::ifstream file(TAG_RATE_SETTINGS_ORIGINAL_FIXTURE,std::ios::binary);need(bool(file),"Actual original settings fixture");auto reference=c::Document::parse({std::istreambuf_iterator<char>(file),{}});need(bool(reference),"Actual original fixture parse");unsigned count=0;
 for(const auto& row:reference->find("rows")->children){const std::string before(row.find("before")->bytes),after(row.find("after")->bytes);const bool upload=row.find("upload")->integer==1;const auto desired=static_cast<std::int32_t>(row.find("desired")->integer);const auto input="d1:3d1:1"+before+"ee";
  auto edited=c::editSavedDownloadTags(input,c::SavedTagSetRateLimit{1,upload,desired});need(bool(edited),"Original supported setter refused");const auto* record=edited->value.source->find("3")->find("1");const auto encoded=c::encodeValue(*record);if(!encoded||*encoded!=after){std::cerr<<"ROW="<<count<<" upload="<<upload<<" desired="<<desired<<" before="<<before<<" expected="<<after<<" actual="<<(encoded?*encoded:"<unavailable>")<<"\n";throw std::runtime_error("Public original setter bytes differ");}need(edited->changed==(before!=after),"Public original effective-value no-op differs");need((row.find("metadata")->integer!=0)==edited->changed,"Original metadata notification boundary differs");auto rate=c::readSavedTagRateLimits(*record);need(rate&&(upload?rate->upload:rate->download)==desired,"Original effective setter value differs");++count;
 }
 need(count==420,"Original public setter cases lost");
 const std::string baseline="d1:3d1:1d1:n9:Generated1:oleeee";need(bool(c::readSavedDownloadTags(baseline)),"Valid unsupported-setting fixture");
 for(auto value:{-2,2147483647}){need(!c::editSavedDownloadTags(baseline,c::SavedTagSetRateLimit{1,true,value}),"Unavailable rate setting was accepted");}
 std::cout<<"PASS420 actual original public rate setter cases;effective no-op,other direction/raw metadata,bytes and notification boundary\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

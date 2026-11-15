// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_db_runtime/survey_policy.hpp"
#include <biglybtc/bencode.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace core=biglybtc::core;namespace survey=biglybtc::services::azureus_db_runtime::survey;
namespace {
void require(bool valid,const std::string& why){if(!valid){throw std::runtime_error(why);}}
const core::Node& field(const core::Node& row,std::string_view key){const auto* value=row.find(key);require(value!=nullptr,"Original survey policy field missing");return *value;}
}
int main(int argc,char** argv){try{
 require(argc==2,"Original survey policy fixture required");std::ifstream input(argv[1],std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(input)),{});auto document=core::Document::parse(bytes);require(bool(document),"Original survey policy fixture parse");const auto& cases=field(document->root(),"cases");require(cases.children.size()==23,"Original survey policy case count");unsigned gates=0;
 for(const auto& row:cases.children){const std::string name(field(row,"name").bytes);auto flags=survey::savedFeatureFlags(field(row,"config"));require(bool(flags)&&*flags==field(row,"flags").integer,name+": saved original flags differ");require(survey::surveyEnabled(55,0,true,*flags)==bool(field(row,"feature").integer),name+": feature decision differs");for(const auto& gate:field(row,"gates").children){require(survey::surveyEnabled(std::int8_t(field(gate,"protocol").integer),std::int32_t(field(gate,"network").integer),field(gate,"adapter").integer!=0,*flags)==bool(field(gate,"enabled").integer),name+": actual DB constructor gate differs");++gates;}}
 require(gates==552,"Original constructor decision count");for(const auto& raw:{"i1e","d21:versioncheck.cache.v4i1ee","d21:versioncheck.cache.v4d10:feat_flagsi1eee","d21:versioncheck.cache.v4de21:versioncheck.cache.v4dee"}){auto malformed=core::Document::parse(raw);if(!malformed){require(malformed.error().code=="duplicate_key","Malformed-field fixture parse");continue;}require(!survey::savedFeatureFlags(malformed->root()),"Malformed/ambiguous saved flags accepted");}
 std::cout<<"SURVEY_POLICY original_saved_cases="<<cases.children.size()<<" actual_constructor_gates="<<gates<<" malformed_groups=4 PASS\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

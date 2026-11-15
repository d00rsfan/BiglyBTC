// SPDX-License-Identifier: GPL-2.0-or-later
#include "related_content.hpp"
#include "../services/related_content/cache_slices.hpp"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using namespace biglybtc;namespace rc=services::related_content;
namespace {void check(bool value,const char* text){if(!value){throw std::runtime_error(text);}}}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 auto config=core::Document::parse("d15:rcm.max_resultsi2000000ee");check(bool(config),"Generated original2M setting");auto settings=rc::settings(*config);check(settings&&settings->maximum==2000000,"Original2M maximum imports unchanged");
 std::string encoded="l";encoded.reserve(6000002);for(int i=0;i<2000000;++i){encoded+="i0e";}encoded+='e';{auto values=rc::detail::CacheSlices(encoded).list();check(values.size()==2000000,"Original2M-entry list accepted without a whole recursive node tree");}encoded.insert(encoded.size()-1,"i0e");bool rejected=false;try{rc::detail::CacheSlices(encoded).list();}catch(const std::exception&){rejected=true;}check(rejected,"Over-budget input is rejected without trimming source data");
 const auto limits=rc::cacheFileLimits();check(limits.inputBytes>=700U*1024U*1024U&&limits.stringBytes==limits.inputBytes,"Stored encrypted envelopes share the expanded byte budget");
 std::cout<<"PASS original2M setting,2M streaming list,over-budget retention and envelope budget\n";return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}

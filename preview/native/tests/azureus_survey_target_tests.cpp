// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_db_runtime/survey_targets.hpp"
#include <biglybtc/bencode.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace core=biglybtc::core;namespace survey=biglybtc::services::azureus_db_runtime::survey;
int main(int argc,char** argv){try{if(argc!=2){throw std::runtime_error("Original target fixture required");}std::ifstream in(argv[1],std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});auto document=core::Document::parse(std::move(bytes));if(!document){throw std::runtime_error("Original target fixture parse");}const auto* cases=document->root().find("cases");if(!cases||cases->children.size()!=5){throw std::runtime_error("Original target case count");}for(const auto& row:cases->children){const auto* input=row.find("input");const auto* output=row.find("output");if(!input||!output||input->bytes.size()!=20||output->bytes.size()!=20){throw std::runtime_error("Original target widths");}std::array<std::uint8_t,20> plain;std::copy(input->bytes.begin(),input->bytes.end(),plain.begin());const auto actual=survey::obfuscatedTarget(plain);if(!std::equal(actual.begin(),actual.end(),output->bytes.begin(),[](auto a,char b){return a==static_cast<unsigned char>(b);})){throw std::runtime_error("Actual original obfuscated target differs");}}std::cout<<"SURVEY_TARGETS original_cases=5 PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

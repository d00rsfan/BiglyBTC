// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/bencode.hpp>
#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace b=biglybtc::core;
int main(){try{
 std::ifstream file(BENCODE_MINIMUM_ORIGINAL_FIXTURE,std::ios::binary);if(!file){throw std::runtime_error("Generated encoder fixture missing");}
 auto fixture=b::Document::parse({std::istreambuf_iterator<char>(file),{}});if(!fixture){throw std::runtime_error("Encoder fixture unreadable");}
 unsigned rows=0,sourceFailures=0;
 for(const auto& row:fixture->find("rows")->children){
  const auto& text=row.find("value")->bytes;std::int64_t value=0;
  auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
  if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()){throw std::runtime_error("Boundary value invalid");}
  auto doc=b::Document::parse("d1:ni0ee");if(!doc){throw std::runtime_error("Generated initial document invalid");}
  doc->setInteger("n",value);const auto encoded=doc->serialize();
  if(encoded!="d1:ni"+std::string(text)+"ee"){throw std::runtime_error("Native integer boundary serialization differs");}
  auto decoded=b::Document::parse(encoded);if(!decoded||decoded->find("n")->integer!=value){throw std::runtime_error("Native boundary round trip differs");}
  if(row.find("failed")->integer){if(value!=-2147483648LL){throw std::runtime_error("Unexpected original failure");}++sourceFailures;}
  else if(encoded!=row.find("encoded")->bytes){throw std::runtime_error("Native encoding differs from original control");}
  ++rows;
 }
 if(rows!=11||sourceFailures!=1){throw std::runtime_error("Encoder boundary controls lost");}
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
 std::cout<<"PASS11 actual original integer boundaries;Native encodes INT32_MIN correctly and matches all10 original successful controls\n";
}

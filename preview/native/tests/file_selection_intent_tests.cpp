#include "biglybtc/file_selection_intent.hpp"
#include <cstdlib>
#include <iostream>

namespace {
using namespace biglybtc;
std::size_t assertions=0;
void check(bool value,const char* message){++assertions;if(!value){std::cerr<<message<<'\n';std::exit(1);}}
core::Document parse(const std::string& raw){auto value=core::Document::parse(raw);check(value.has_value(),"Generated intent parses");return std::move(*value);}
}
int main(){
  for(const bool skipped:{false,true}){
    const std::vector<std::uint8_t> mask{1,0,1};auto encoded=compat::encodeOriginalFileSelectionIntent(mask,skipped);check(encoded.has_value(),"Complete selected mask encodes");
    const auto document=parse(*encoded);auto restored=compat::readOriginalFileSelectionIntent(document.root(),3);
    check(restored&&restored->changed==mask&&restored->skipped==skipped,"Versioned intent fields round trip");
    check(!compat::readOriginalFileSelectionIntent(document.root(),2),"Partial census rejects");
    check(!compat::readOriginalFileSelectionIntent(document.root(),100001),"Oversized census rejects");
  }
  for(const auto& mask:std::vector<std::vector<std::uint8_t>>{{},{0},{1,2},{255}}){check(!compat::encodeOriginalFileSelectionIntent(mask,false),"Missing/nonboolean selections refuse");}
  const std::vector<std::uint8_t> large(100001,1);check(!compat::encodeOriginalFileSelectionIntent(large,false),"Encoder bounds work before producing output");
  const auto raw=[](std::string_view value){return std::string("d4:mask1:")+char(1)+std::string(value);};
  for(const auto& text:std::vector<std::string>{"de",raw("7:skippedi4294967296ee"),raw("7:skippedi-1ee"),raw("7:skipped1:0e"),raw("7:skippedi0e1:xi1ee"),std::string("d4:mask1:")+char(2)+"7:skippedi0ee",std::string("d4:mask1:")+char(0)+"7:skippedi0ee"}){
    const auto document=parse(text);check(!compat::readOriginalFileSelectionIntent(document.root(),1),"Malformed,widened or nonboolean journal intent refuses");
  }
  std::cout<<"PASS_FILE_SELECTION_INTENT "<<assertions<<" assertions\n";
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "sam_protocol.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace biglybtc::services::i2p;
namespace sam=biglybtc::services::i2p::provider;
namespace {
void require(bool ok,const char* message){if(!ok){throw std::runtime_error(message);}}
void commands(){
  static_assert(!std::is_copy_constructible_v<sam::Command>);
  const auto hello="HELLO VERSION MIN=3.1 MAX=3.1 BIGLYBTC_OWNER="+std::string(64,'1')+" GENERATION=7\n";
  auto parsed=sam::parseCommand(hello);require(parsed&&parsed->kind==sam::CommandKind::Hello,"Owned HELLO rejected");
  for(const auto& bad:{hello+"\n",hello.substr(0,hello.size()-1),hello+"HELLO VERSION MIN=3.1\n",std::string("HELLO VERSION MIN=3.1 MAX=3.1 BIGLYBTC_OWNER=x GENERATION=7 MIN=3.1\n"),std::string("SESSION CREATE STYLE=STREAM ID=x DESTINATION=TRANSIENT\n"),std::string("NAMING LOOKUP NAME=example.i2p\n"),std::string("NAMING LOOKUP NAME=ME PURPOSE=CONTROL TO_PORT=0 FROM_PORT=0 EXTRA=1\n"),std::string("NAMING LOOKUP NAME=\"ME\" PURPOSE=CONTROL TO_PORT=0 FROM_PORT=0\n"),std::string("HELLO VERSION MIN=3.1 MAX=3.1 BIGLYBTC_OWNER=x GENERATION=7\r\n"),std::string(2049,'x')+"\n"}){require(!sam::parseCommand(bad),"Unsupported SAM command accepted");}
  auto nul=hello;nul[8]='\0';require(!sam::parseCommand(nul),"NUL accepted");
  require(sam::parseCommand("NAMING LOOKUP NAME=ME PURPOSE=CONTROL TO_PORT=0 FROM_PORT=0\n").has_value(),"Explicit owned lookup rejected");
  require(sam::parseCommand("STREAM CONNECT ID=a DESTINATION=AAAA== PURPOSE=PEER TO_PORT=6881 FROM_PORT=6881\n").has_value(),"Destination padding rejected by token parser");
  require(!sam::decimal("01",65535)&&!sam::decimal("-1",65535)&&!sam::decimal("65536",65535)&&sam::decimal("0",65535)==0,"Exact port/decimal bounds lost");
  require(!sam::hex(std::string(64,'F'),32)&&!sam::hex(std::string(63,'a'),32),"Noncanonical capability accepted");
  const Bytes a{0,1,2},b{0,1,3};require(sam::constantEqual(a,a)&&!sam::constantEqual(a,b)&&!sam::constantEqual(a,View(a).first(2)),"Capability equality failure");
}
void publicNames(const char* path){
  Hash hash{};for(std::size_t i=0;i<hash.size();++i){hash[i]=static_cast<Byte>(i*31+7);}const auto name=sam::hashName(hash);require(sam::hashName(name)==hash,"Hash name roundtrip");auto noncanonical=name;noncanonical[51]='b';require(!sam::hashName(noncanonical),"Nonzero base32 padding accepted");
  std::ifstream input(path,std::ios::binary);require(bool(input),"Synthetic Destination fixture missing");Bytes bytes((std::istreambuf_iterator<char>(input)),{});
  auto destination=decodeDestination(bytes);require(bool(destination),"Synthetic Destination malformed");const auto encoded=sam::destinationText(bytes);auto decoded=sam::destinationText(encoded);require(decoded&&decoded->wire==bytes,"Canonical full Destination roundtrip");
  require(!sam::destinationText(encoded+".i2p")&&!sam::destinationText(encoded+"\n")&&!sam::destinationText(std::string(1028,'a')),"Destination injection/bound accepted");
}
}
int main(int argc,char** argv){try{require(argc==2,"Explicit synthetic Destination path required");commands();publicNames(argv[1]);std::cout<<"PASS owned SAM parser/capability bounds\nPASS exact public Destination/name codec\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

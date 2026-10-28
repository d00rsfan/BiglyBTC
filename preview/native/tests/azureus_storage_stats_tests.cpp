// SPDX-License-Identifier: GPL-2.0-or-later
#include "storage_stats.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace stats=biglybtc::services::azureus_storage_stats;
namespace {
void require(bool b,const char* reason){if(!b){throw std::runtime_error(reason);}}
std::vector<std::string_view> split(std::string_view s){std::vector<std::string_view> out;do{const auto i=s.find('\t');out.push_back(s.substr(0,i));if(i==s.npos){break;}s.remove_prefix(i+1);}while(true);return out;}
std::int64_t integer(std::string_view s){std::int64_t n=0;const auto [p,ec]=std::from_chars(s.data(),s.data()+s.size(),n);require(ec==std::errc{}&&p==s.data()+s.size(),"integer");return n;}
std::string unhex(std::string_view s){require(s.size()%2==0,"hex");std::string out;for(std::size_t i=0;i<s.size();i+=2){unsigned n=0;const auto [p,ec]=std::from_chars(s.data()+i,s.data()+i+2,n,16);require(ec==std::errc{}&&p==s.data()+i+2,"hex");out+=static_cast<char>(n);}return out;}
std::string hex(std::string_view s){constexpr char d[]="0123456789abcdef";std::string out;for(unsigned char b:s){out+=d[b>>4];out+=d[b&15];}return out;}
std::int32_t integer32(std::string_view s){const auto value=integer(s);require(value>=std::numeric_limits<std::int32_t>::min()&&value<=std::numeric_limits<std::int32_t>::max(),"int32");return static_cast<std::int32_t>(value);}
void observation(std::string_view label,const stats::Stats& s,std::uint8_t version,std::string_view tail){std::cout<<label<<'\t'<<s.entries<<'\t'<<s.bytes<<'\t'<<s.readsPerMinute<<'\t'<<static_cast<int>(s.diversification)<<'\t'<<static_cast<unsigned>(version)<<'\t'<<hex(stats::encode(s))<<'\t'<<hex(tail)<<'\n';}
void replay(const char* name){std::ifstream file(name);require(file.good(),"tape");stats::Key key;std::string line;std::size_t count=0;
 while(std::getline(file,line)){require(++count<=4096&&line.size()<=2048,"tape bound");const auto f=split(line);
  if(f[0]=="N"){require(f.size()==1,"reset");key=stats::Key{};}
  else if(f[0]=="V"){require(f.size()==3,"value");key.changed(integer32(f[1]),integer32(f[2]));}
  else if(f[0]=="R"){require(f.size()==3,"read");auto raw=unhex(f[2]);require(key.read({reinterpret_cast<const std::uint8_t*>(raw.data()),raw.size()},integer(f[1])).has_value(),"IP");}
  else if(f[0]=="S"){require(f.size()==2,"serialize");observation(f[1],key.stats(),0,{});}
  else if(f[0]=="D"){require(f.size()==3,"decode");auto decoded=stats::decode(unhex(f[2]));require(decoded.has_value(),"decode");observation(f[1],decoded->value,decoded->wireVersion,decoded->trailing);}
  else{throw std::runtime_error("opcode");}
 }require(file.eof(),"tape IO");
}
void units(){stats::Stats state{-1,std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max(),-1};const auto encoded=stats::encode(state);require(encoded.size()==14,"wire14");auto decoded=stats::decode(encoded);require(decoded&&decoded->value==state&&decoded->wireVersion==0&&decoded->trailing.empty(),"signed codec");
 std::string unknown=encoded;unknown[0]=static_cast<char>(255);unknown+="tail";decoded=stats::decode(unknown);require(decoded&&decoded->value==state&&decoded->wireVersion==255&&decoded->trailing=="tail"&&stats::encode(decoded->value)==encoded,"version ignored/tail preserved/serializer0");
 for(std::size_t n=0;n<14;++n){require(!stats::decode(encoded.substr(0,n)),"truncated");}require(!stats::decode(std::string(513,'x')),"envelope bound");
 stats::Key key;key.changed(std::numeric_limits<std::int32_t>::max(),1);key.changed(1,-2);require(key.stats().entries==0&&key.stats().bytes==0,"source overflow and negative clamp");
 const std::array<std::uint8_t,4> ip{8,8,8,8};require(key.read(ip,0).has_value()&&key.read(ip,180000).has_value(),"window equality");require(key.stats().readsPerMinute==0,"not a read counter");require(key.read(ip,180001).has_value()&&key.stats().readsPerMinute==1,"source minimum activity/drop boundary");
 require(key.read(ip,360002).has_value()&&key.stats().readsPerMinute==1,"source keeps previous rate without a filter");require(!key.read(std::span(ip).first(3),0),"IP bound");
}
}
int main(int argc,char** argv){try{if(argc==3&&std::string_view(argv[1])=="--replay"){replay(argv[2]);return 0;}require(argc==1,"arguments");units();std::cout<<"PASS storage stats codec/source counter boundaries\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "loaded_mapping_snapshot.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace biglybtc::engine::detail;
namespace {
std::size_t assertions=0,families=0;
void require(bool condition,const char* message){++assertions;if(!condition){throw std::runtime_error(message);}}
std::optional<LoadedMappings> read(const std::string& text,std::span<const std::uintptr_t> addresses){
 std::unique_ptr<FILE,int(*)(FILE*)> file(std::tmpfile(),&std::fclose);require(bool(file),"Owned anonymous mapping fixture");require(std::fwrite(text.data(),1,text.size(),file.get())==text.size(),"Write owned map fixture");std::rewind(file.get());return readLoadedMappings(file.get(),addresses);
}
const std::string first="1000-4000 r-xp 00001000 08:02 42 /generated/provider.so\n";
const std::string second="6000-8000 r-xp 00005000 08:02 42 /generated/provider.so\n";
template<class F>void family(const char* name,F&& run){run();++families;std::cout<<"PASS "<<name<<'\n';}
}
int main(){try{
 family("all qualified symbols share one snapshot",[]{std::array<std::uintptr_t,47> addresses{};for(std::size_t n=0;n<addresses.size();++n){addresses[n]=0x1000+n*16;}auto mappings=read(first,addresses);require(bool(mappings),"All 47 symbols present");for(const auto& m:*mappings){require(m.begin==0x1000&&m.end==0x4000&&m.offset==0x1000&&m.deviceMajor==8&&m.deviceMinor==2&&m.inode==42,"Every original identity field retained");}});
 family("split executable regions and aliases",[]{const std::array<std::uintptr_t,4> addresses{0x1000,0x3fff,0x6000,0x6000};auto mappings=read(first+second,addresses);require(mappings&&(*mappings)[0].begin==0x1000&&(*mappings)[2].begin==0x6000&&(*mappings)[2]==(*mappings)[3],"Split mappings and actual symbol aliases retained");const std::array<std::uintptr_t,1> edge{0x4000};require(!read(first,edge),"End address exclusive");});
 family("missing or duplicate containing range refuses",[]{const std::array<std::uintptr_t,2> addresses{0x1000,0x9000};require(!read(first,addresses),"No missing export mapping");const std::array<std::uintptr_t,1> address{0x2000};require(!read(first+first,address),"Duplicate matches refused");require(!read(first+"1800-2800 r-xp 0 08:02 42 /overlap\n",address),"Overlapping second match refused");});
 family("matching readable executable file remains mandatory",[]{const std::array<std::uintptr_t,1> address{0x1000};for(const auto* invalid:{"1000-4000 rw-p 0 08:02 42 /file\n","1000-4000 --xp 0 08:02 42 /file\n","1000-4000 r-x 0 08:02 42 /file\n","1000-4000 r-xp 0 08:02 0 /anonymous\n"}){require(!read(invalid,address),"Original permission/inode requirement retained");}});
 family("all numeric fields parsed strictly",[]{const std::array<std::uintptr_t,1> address{0x1000};for(const auto* invalid:{"z000-4000 r-xp 0 08:02 42 /file\n","4000-1000 r-xp 0 08:02 42 /file\n","1000-4000 r-xp x 08:02 42 /file\n","1000-4000 r-xp 0 zz:02 42 /file\n","1000-4000 r-xp 0 08:xx 42 /file\n","1000-4000 r-xp 0 0802 42 /file\n","1000-4000 r-xp 0 08:02 42x /file\n","1000-4000 r-xp 0 08:02 18446744073709551616 /file\n"}){require(!read(invalid,address),"Invalid numeric field refused");}});
 family("unrelated invalid rows cannot hide after a match",[]{const std::array<std::uintptr_t,1> address{0x1000};require(!read(first+"unreadable row\n",address),"Continue validating after all exports found");require(!read("unreadable row\n"+first,address),"Malformed prefix refused");require(!read(first+"9000-a000 r--p nothex 08:02 2 /unrelated\n",address),"Unrelated numeric invalidity refused");});
 family("newline and line-length bounds unchanged",[]{const std::array<std::uintptr_t,1> address{0x1000};auto missing=first;missing.pop_back();require(!read(missing,address),"Partial final row refused");require(!read(first+std::string(8192,'x')+'\n',address),"Oversized row refused");require(!read(first+std::string("a\0b\n",4),address),"Embedded NUL cannot terminate validated text early");});
 family("all original input bounds apply",[]{const std::array<std::uintptr_t,1> address{0x1000};std::string many=first;for(int n=0;n<16384;++n){many+="9000-a000 r--p 0 08:02 2 /unrelated\n";}require(!read(many,address),"Line count bound retained");const auto row="9000-a000 r--p 0 08:02 2 "+std::string(7900,'p')+'\n';std::string large=first;for(int n=0;n<270;++n){large+=row;}require(!read(large,address),"Byte bound retained independently");});
 family("before and after snapshots detect changed source fields",[]{const std::array<std::uintptr_t,1> address{0x1000};auto before=read(first,address),after=read("1000-4000 r-xp 2000 08:02 42 /generated/provider.so\n",address);require(before&&after&&*before!=*after,"Mapping offset change detected");after=read("1000-4000 r-xp 1000 08:02 43 /generated/provider.so\n",address);require(after&&*before!=*after,"Mapped inode change detected");});
 family("empty oversized or unavailable subjects refuse",[]{const std::array<std::uintptr_t,1> address{0x1000};require(!readLoadedMappings(nullptr,address),"No unavailable process map accepted");require(!read(first,{}),"Empty symbol set refused");std::array<std::uintptr_t,48> tooMany{};require(!read(first,tooMany),"Symbol cap retained");require(!read("",address),"Missing all maps refused");});
 std::cout<<"PASS_LOADED_MAPPING_SNAPSHOT "<<families<<" families "<<assertions<<" assertions\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL_LOADED_MAPPING_SNAPSHOT "<<e.what()<<'\n';return 1;}}

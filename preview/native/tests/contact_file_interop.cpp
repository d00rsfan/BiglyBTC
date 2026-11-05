// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_contacts.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
namespace p=biglybtc::services::i2p;namespace d=p::dht;
namespace {
void require(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
template<class T>T take(p::Result<T> result){require(bool(result),result?"":result.error().message.c_str());return std::move(*result);}
p::Bytes read(const std::filesystem::path& path,std::size_t limit){
  require(std::filesystem::is_regular_file(std::filesystem::symlink_status(path)),"Regular public fixture required");const auto n=std::filesystem::file_size(path);require(n>0&&n<=limit,"Public fixture bound");std::ifstream in(path,std::ios::binary);require(bool(in),"Open fixture");p::Bytes bytes(static_cast<std::size_t>(n));in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));require(in&&in.peek()==std::char_traits<char>::eof(),"Read fixture exact size");return bytes;
}
void write(const std::filesystem::path& path,p::View bytes){
  const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);require(fd>=0,"Fresh public output required");
  std::size_t at=0;bool ok=true;while(at<bytes.size()){const auto n=::write(fd,bytes.data()+at,bytes.size()-at);if(n<0&&errno==EINTR){continue;}if(n<=0){ok=false;break;}at+=static_cast<std::size_t>(n);}if(::close(fd)!=0){ok=false;}require(ok,"Write fixture");
}
std::vector<d::SavedContact> rows(std::string_view name,const std::array<p::Destination,2>& destinations){
  std::vector<d::SavedContact> result;const unsigned count=name=="empty"?0:name=="max32"?32:3;
  for(unsigned n=0;n<count;++n){d::SavedContact row;row.contact.destination=destinations[n%2];row.contact.hash=destinations[n%2].hash;row.contact.port=static_cast<std::uint16_t>(n==0?1:n==1?65534:18300+n);
    row.contact.id=take(d::makeNodeId(row.contact.hash,row.contact.port,p::Bytes(14,static_cast<p::Byte>(0x40+n))));if(n%3==2){row.contact.destination.reset();}
    row.timeAlive=n==0?std::numeric_limits<std::int64_t>::min():n==1?std::numeric_limits<std::int64_t>::max():-static_cast<std::int64_t>(n);
    row.serializationVersion=name=="v0"?0:1;row.protocolVersion=row.serializationVersion?static_cast<std::int8_t>(n%3==0?-128:n%3==1?127:4):0;result.push_back(std::move(row));
  }return result;
}
}
int main(int argc,char** argv){
  try{
    require(argc==4,"emit|verify work public-destination-fixture-directory");const std::string mode=argv[1];require(mode=="emit"||mode=="verify","Fixed mode");const std::filesystem::path work=argv[2],fixtures=argv[3];
    const std::array<p::Destination,2> destinations{take(p::decodeDestination(read(fixtures/"dsa.sender",512))),take(p::decodeDestination(read(fixtures/"ed25519.sender",512)))};
    if(mode=="emit"){write(work/"public-local.dest",destinations[0].wire);}
    for(const auto* name:{"v0","v1","empty","max32"}){
      auto expected=rows(name,destinations);const auto original=take(d::encodeSavedContacts(expected));
      if(mode=="emit"){write(work/(std::string("native-")+name+".dat"),original);continue;}
      require(read(work/(std::string("native-")+name+".dat"),d::MaximumSavedContactsBytes)==original,"Original Native fixture changed");
      for(auto& row:expected){row.timeAlive=0;row.serializationVersion=1;}
      const auto actual=read(work/(std::string("java-")+name+".dat"),d::MaximumSavedContactsBytes);const auto parsed=take(d::decodeSavedContacts(actual));require(parsed.size()==expected.size(),"Original export count differs");
      for(std::size_t n=0;n<expected.size();++n){const auto& a=parsed[n];const auto& b=expected[n];require(a.timeAlive==b.timeAlive&&a.serializationVersion==1&&a.protocolVersion==b.protocolVersion&&a.contact.id==b.contact.id&&a.contact.port==b.contact.port&&a.contact.hash==b.contact.hash&&bool(a.contact.destination)==bool(b.contact.destination),"Original saved contact fields differ");if(a.contact.destination){require(a.contact.destination->wire==b.contact.destination->wire,"Original public Destination differs");}}
      auto normalized=take(d::encodeSavedContacts(parsed));require(normalized==actual&&normalized==take(d::encodeSavedContacts(expected)),"Actual Java output not exact Native wire");write(work/(std::string("native-again-")+name+".dat"),normalized);
    }
    std::cout<<"{\"status\":\"PASS_NATIVE_CONTACT_FILE_INTEROP\",\"cases\":4,\"network_calls\":0}\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

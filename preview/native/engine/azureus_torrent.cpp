// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_torrent.hpp"
#include "biglybtc/azureus_endpoint.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <charconv>
namespace biglybtc::engine {
namespace {
std::string_view trim(std::string_view s){while(!s.empty()&&s.front()==' '){s.remove_prefix(1);}while(!s.empty()&&s.back()==' '){s.remove_suffix(1);}return s;}
std::optional<std::uint16_t> port(std::string_view s){s=trim(s);unsigned value=0;const auto parsed=std::from_chars(s.data(),s.data()+s.size(),value);if(s.empty()||parsed.ec!=std::errc{}||parsed.ptr!=s.data()+s.size()||!value||value>65535){return {};}return static_cast<std::uint16_t>(value);}
bool loopback(const core::SharedUdpEndpoint& p){return p.length==4?p.address[0]==127:p.length==16&&std::all_of(p.address.begin(),p.address.end()-1,[](auto v){return v==0;})&&p.address.back()==1;}
}
std::string encodeAzureusPeer(std::uint16_t tcp,std::uint16_t udp,std::uint16_t dht,bool crypto){
 if(!tcp||!udp||!dht){return {};}
 auto out=std::to_string(tcp);if(crypto){out+=";C";}if(udp!=dht){out+=';';out+=std::to_string(udp);}return out;
}
std::optional<AzureusPeer> decodeAzureusPeer(std::string_view text,std::uint8_t flags,core::SharedUdpEndpoint origin,bool owned){
 if(text.empty()||text.size()>512||(flags!=1&&flags!=2)||(origin.length!=4&&origin.length!=16)||!origin.port
     ||std::ranges::any_of(text,[](unsigned char c){return c<32||c>126;})){return {};}
 const auto split=text.find(';');auto first=trim(text.substr(0,split));const auto colon=first.find(':');
 if(colon!=std::string_view::npos){
   // Original tracker overrides use a numeric IPv4 prefix. Never resolve names
   // supplied by DHT values or reinterpret ambiguous IPv6 colon strings.
   const std::string ip(first.substr(0,colon));origin.address={};origin.length=4;
   if(::inet_pton(AF_INET,ip.c_str(),origin.address.data())!=1){return {};}
   first.remove_prefix(colon+1);
 }
 const auto tcp=port(first);if(!tcp||(owned?!loopback(origin):!core::azureusPublicAddress(std::span(origin.address).first(origin.length)))){return {};}
 AzureusPeer result{origin,origin.port,false,flags==2};result.tcp.port=*tcp;
 auto remaining=split==std::string_view::npos?std::string_view{}:text.substr(split+1);
 while(!remaining.empty()){
   const auto end=remaining.find(';');const auto token=trim(remaining.substr(0,end));
   if(!token.empty()){
     if(token.front()>='0'&&token.front()<='9'){const auto udp=port(token);if(!udp){return {};}result.udpPort=*udp;}
     else{result.crypto=token.find('C')!=std::string_view::npos;}
   }
   if(end==std::string_view::npos){break;}remaining.remove_prefix(end+1);
 }return result;
}
}

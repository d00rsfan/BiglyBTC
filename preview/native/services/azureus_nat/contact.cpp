// SPDX-License-Identifier: GPL-2.0-or-later
#include "contact.hpp"
#include <algorithm>
namespace biglybtc::services::azureus_nat {
namespace {
bool sameIp(const udp::Address& a,const udp::Address& b){return (a.length==4||a.length==16)&&a.length==b.length&&a.port&&b.port&&std::equal(a.bytes.begin(),a.bytes.begin()+a.length,b.bytes.begin());}
}
Result<RoutedContact> RoutedContact::observed(nodes::Contact announced,udp::Address actual,nodes::NodeCache& cache){
 announced.address=canonicalAddress(announced.address);actual=canonicalAddress(actual);
 if(!sameIp(announced.address,actual)){return std::unexpected(Error::Contact);}auto id=cache.lookup({announced.address,{}},announced.protocol);if(!id){return std::unexpected(Error::Contact);}announced.id=id->id;return RoutedContact(std::move(announced),std::move(actual));
}
Result<RoutedContact> RoutedContact::withTransport(udp::Address actual)const{
 actual=canonicalAddress(actual);if(!sameIp(external_.address,actual)){return std::unexpected(Error::Contact);}return RoutedContact(external_,std::move(actual));
}
}

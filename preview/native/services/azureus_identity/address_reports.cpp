// SPDX-License-Identifier: GPL-2.0-or-later
// Original transport policy: Copyright (C) Azureus Software, Inc.
#include "address_reports.hpp"
#include <bit>
namespace biglybtc::services::azureus_identity {
namespace {
QHostAddress normalized(QHostAddress value){if(value.protocol()!=QAbstractSocket::IPv6Protocol){return value;}const auto v=value.toIPv6Address();bool mapped=v.c[10]==255&&v.c[11]==255;for(unsigned i=0;i<10;++i){mapped&=v.c[i]==0;}if(mapped){std::uint32_t ip=0;for(unsigned i=12;i<16;++i){ip=(ip<<8)|v.c[i];}return QHostAddress(ip);}return value;}
bool invalid(const QHostAddress& address){
 if(address.protocol()==QAbstractSocket::IPv4Protocol){const auto ip=address.toIPv4Address();return (ip>>24)==127||(ip>>16)==0xa9fe||(ip>>24)==10||(ip>>20)==0xac1||(ip>>16)==0xc0a8;}
 const auto ip=address.toIPv6Address();if(ip.c[0]==0xfe&&((ip.c[1]&0xc0)==0x80||(ip.c[1]&0xc0)==0xc0)){return true;}bool loop=ip.c[15]==1;for(unsigned i=0;i<15;++i){loop&=ip.c[i]==0;}return loop;
}
std::int64_t elapsed(std::int64_t a,std::int64_t b){return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)-std::bit_cast<std::uint64_t>(b));}
}
std::expected<ReportDecision,ReportError> addressReport(ReportState state,const ReportInput& input){
 auto reported=normalized(input.reported);if(reported.protocol()!=QAbstractSocket::IPv4Protocol&&reported.protocol()!=QAbstractSocket::IPv6Protocol){return std::unexpected(ReportError::Unresolved);}ReportDecision result{state,ReportAction::Ignore};if((reported.protocol()==QAbstractSocket::IPv6Protocol)!=input.ipv6||reported==normalized(input.current)){return result;}if(elapsed(input.wallMilliseconds,state.lastChange)<state.minimumPeriod){return result;}
 if(input.observedContacts<32&&!input.force){if(!state.deferred){result.state.deferred=true;result.action=ReportAction::Defer;}return result;}if(invalid(reported)||reported==normalized(input.reporter)){return result;}result.state.lastChange=input.wallMilliseconds;if(state.minimumPeriod==300000){result.state.minimumPeriod=600000;}result.action=ReportAction::Recheck;return result;
}
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "network_identity.hpp"
#include "biglybtc/azureus_endpoint.hpp"
#include <algorithm>
namespace biglybtc::services::network_identity {
namespace {
bool validNumber(const QString& text){if(text.isEmpty()||text.size()>10||std::ranges::any_of(text,[](QChar c){return c<'0'||c>'9';})){return false;}bool ok=false;const auto v=text.toULongLong(&ok);return ok&&v>0&&v<=4294967295ULL;}
QString text(const core::Node* node){if(!node||node->type!=core::Node::Type::Bytes||node->bytes.size()>4096){return {};}const auto out=QString::fromUtf8(node->bytes.data(),qsizetype(node->bytes.size()));return out.contains(QChar(0))?QString{}:out;}
}
bool publicAddress(const QHostAddress& address){if(address.protocol()==QAbstractSocket::IPv4Protocol){const auto ip=address.toIPv4Address();std::array<std::uint8_t,4> bytes{};for(int i=0;i<4;++i){bytes[i]=std::uint8_t(ip>>(24-8*i));}return core::azureusPublicAddress(bytes);}if(address.protocol()==QAbstractSocket::IPv6Protocol){const auto ip=address.toIPv6Address();return core::azureusPublicAddress(std::span(reinterpret_cast<const std::uint8_t*>(ip.c),16));}return false;}
QString originQuery(const QHostAddress& address){QString out;if(address.protocol()==QAbstractSocket::IPv4Protocol){auto ip=address.toIPv4Address();for(int i=0;i<4;++i){out+=QString::number((ip>>(8*i))&255)+'.';}return out+"origin.asn.cymru.com";}if(address.protocol()==QAbstractSocket::IPv6Protocol){const auto ip=address.toIPv6Address();for(int i=15;i>=0;--i){out+=QString::number(ip[i]&15,16)+'.'+QString::number(ip[i]>>4,16)+'.';}return out+"origin6.asn.cymru.com";}return {};}
bool matches(const Asn& asn,const QHostAddress& address){if(!validNumber(asn.number)||asn.prefix.size()>128){return false;}const auto subnet=QHostAddress::parseSubnet(asn.prefix);return subnet.second>=0&&address.protocol()==subnet.first.protocol()&&address.isInSubnet(subnet);}
std::optional<Asn> parseOrigin(const QHostAddress& address,QString record){if(record.size()>4096){return {};}const auto fields=record.split('|');if(fields.size()<2){return {};}Asn out{fields[0].trimmed(),{},fields[1].trimmed()};if(!matches(out,address)){return {};}return out;}
std::optional<QString> parseName(QString number,QString record){if(record.size()>4096){return {};}const auto fields=record.split('|');if(fields.size()<5||fields[0].trimmed()!=number){return {};}auto name=fields.back().trimmed();if(name.isEmpty()||name.contains(QChar(0))){return {};}return name;}
std::vector<Asn> saved(const core::Document* doc){std::vector<Asn> out;if(!doc){return out;}const auto* rows=doc->find("ASN Details");if(!rows||rows->type!=core::Node::Type::List||rows->children.size()>64){return out;}for(const auto& row:rows->children){if(row.type!=core::Node::Type::Dictionary){continue;}Asn asn{text(row.find("as")),text(row.find("name")),text(row.find("bgp"))};if(validNumber(asn.number)&&!asn.name.isEmpty()&&asn.name.size()<=4096&&QHostAddress::parseSubnet(asn.prefix).second>=0){out.push_back(std::move(asn));}}return out;}
}

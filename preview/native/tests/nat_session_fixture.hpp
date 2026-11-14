// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../services/azureus_nat/session.hpp"
#include "shared_udp_fixture.hpp"
#include <QScopeGuard>
namespace NatTest {
namespace nat=services::azureus_nat;
inline nat::udp::Address addr(udp::Peer p){nat::udp::Address a;a.port=p.port;if(p.address.protocol()==QAbstractSocket::IPv4Protocol){a.length=4;auto ip=p.address.toIPv4Address();for(unsigned i=0;i<4;++i){a.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}else{a.length=16;auto ip=p.address.toIPv6Address();std::copy_n(ip.c,16,a.bytes.begin());}return a;}
inline nat::RoutedContact contact(Actor& a){nat::nodes::Contact external;external.protocol=55;external.address=addr(a.transport.endpoint());nat::nodes::NodeCache cache;auto c=nat::RoutedContact::observed(external,addr(a.actualEndpoint),cache);need(bool(c),"Owned external/physical contact");return std::move(*c);}
inline nat::SessionHooks hooks(Actor& a,QObject* context){nat::SessionHooks h;
 h.revision=[&a]()->std::optional<std::uint64_t>{need(a.sourceHeld,"Revision read only under source permit");return a.permit?std::optional{a.revision}:std::nullopt;};
 h.afterRelease=[&a,context](auto fn){return QMetaObject::invokeMethod(context,[&a,fn=std::move(fn)]()mutable{need(!a.sourceHeld,"Decode after source release");fn();},Qt::QueuedConnection);};
 h.withPermit=[&a](auto end,auto fn){need(!a.sourceHeld,"Never nest source permit acquisition");if(!a.permit||std::chrono::steady_clock::now()>=end){return false;}a.sourceHeld=true;auto reset=qScopeGuard([&a]{a.sourceHeld=false;});fn();return true;};return h;
}
inline std::unique_ptr<nat::Session> session(Actor& a,QObject* ctx){auto s=nat::Session::open(a.transport,a.revision,hooks(a,ctx));need(bool(s),"Owned NAT registration");return std::move(*s);}
inline udp::TransferPeerLease grant(Actor& a,Actor& remote,nat::Session& owner,std::chrono::milliseconds duration=60000ms){
 a.sourceHeld=true;auto reset=qScopeGuard([&a]{a.sourceHeld=false;});auto lease=a.transport.leaseTransferPeer(remote.actualEndpoint,nat::handlerKey(),contact(remote).exportSnapshot(),&owner,a.revision,std::chrono::steady_clock::now()+duration);need(bool(lease),"Source-held narrow DATA grant");return *lease;
}
inline udp::Peer physical(const nat::udp::Address& a){QHostAddress ip;if(a.length==4){ip=QHostAddress((quint32(a.bytes[0])<<24)|(quint32(a.bytes[1])<<16)|(quint32(a.bytes[2])<<8)|a.bytes[3]);}else{Q_IPV6ADDR v{};std::copy_n(a.bytes.begin(),16,v.c);ip=QHostAddress(v);}return {ip,a.port};}
inline std::unique_ptr<nat::Session> automaticSession(Actor& a,QObject* context){
 auto owner=std::make_shared<nat::Session*>(nullptr);auto h=hooks(a,context);h.peer=[&a,owner](const auto& target,auto end){need(a.sourceHeld,"Route grant inside source permit");return bool(a.transport.leaseTransferPeer(physical(target.transport()),nat::handlerKey(),target.exportSnapshot(),*owner,a.revision,end));};
 auto result=nat::Session::open(a.transport,a.revision,std::move(h));need(bool(result),"Automatic source-scoped ingress session");*owner=result->get();QPointer<nat::Session> weak(result->get());a.intercept=[&a,weak](auto peer,auto bytes){if(weak&&weak->ingress(peer,bytes)){return true;}return a.transport.receiveShared(std::move(peer),std::move(bytes));};return std::move(*result);
}
inline void admit(Actor& a,Actor& b){need(bool(a.transport.allowPeer(b.actualEndpoint)),"Owned existing peer admission");need(bool(b.transport.allowPeer(a.actualEndpoint)),"Owned existing reverse admission");}
}

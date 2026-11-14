// SPDX-License-Identifier: GPL-2.0-or-later
#include "wire.hpp"
#include <charconv>
#include <algorithm>
#include <set>
namespace biglybtc::services::azureus_nat {
namespace {
udp::Address canonical(udp::Address a){if(a.length==16&&std::ranges::all_of(a.bytes.begin(),a.bytes.begin()+10,[](auto b){return b==0;})&&a.bytes[10]==255&&a.bytes[11]==255){const auto v=a.bytes;a.bytes={};std::copy(v.begin()+12,v.end(),a.bytes.begin());a.length=4;}return a;}
std::string b(std::string_view v){return std::to_string(v.size())+":"+std::string(v);}
bool reply(Type t){return t==Type::BindReply||t==Type::PunchReply||t==Type::ConnectReply||t==Type::QueryReply||t==Type::CloseReply;}
Result<core::Document> dictionary(std::string text){
 if(text.size()>MaximumMessage){return std::unexpected(Error::Limit);}
 auto d=core::Document::parse(std::move(text),{MaximumMessage,MaximumMessage,512,16});
 if(!d||d->root().type!=core::Node::Type::Dictionary){return std::unexpected(Error::Input);}
 const auto unique=[](const auto& self,const core::Node& n)->bool{if(n.type==core::Node::Type::Dictionary){std::set<std::string_view> seen;for(std::size_t i=0;i<n.children.size();i+=2){if(!seen.insert(n.children[i].bytes).second){return false;}}}for(const auto& v:n.children){if(!self(self,v)){return false;}}return true;};
 if(!unique(unique,d->root())){return std::unexpected(Error::Input);}return std::move(*d);
}
bool shape(const Message& m){
 if(static_cast<unsigned>(m.type)>11||reply(m.type)!=m.ok.has_value()){return false;}
 if(m.type==Type::Punch&&(!m.target||m.target->empty()||m.target->size()>128)){return false;}
 if(m.type==Type::Connect&&!m.origin){return false;}
 if(m.type==Type::QueryReply&&*m.ok&&(!m.ip||!m.port||m.ip->empty()||m.ip->size()>64)){return false;}
 return (!m.target||m.target->size()<=128)&&(!m.origin||m.origin->size()<=32)&&(!m.ip||m.ip->size()<=64);
}
}
std::string_view handlerKey()noexcept{static constexpr char key[]="\x53\x7f\x6b\x2f\x6e\x1e\x05\xc6\x74\xda\x9d\xee\xf0\x0d\xfb\x18\x25\xe9\x61\x90";return {key,20};}
Result<std::string> socketAddress(const udp::Address& input){
 const auto a=canonical(input);
 if((a.length!=4&&a.length!=16)||!a.port){return std::unexpected(Error::Contact);}std::string out="/";
 if(a.length==4){for(unsigned i=0;i<4;++i){if(i){out+='.';}out+=std::to_string(a.bytes[i]);}}
 else{out+='[';for(unsigned i=0;i<16;i+=2){if(i){out+=':';}char hex[4];const auto value=static_cast<unsigned>((a.bytes[i]<<8)|a.bytes[i+1]);const auto result=std::to_chars(hex,hex+4,value,16);out.append(hex,result.ptr);}out+=']';}
 return out+":"+std::to_string(a.port);
}
Result<std::string> publishKey(std::span<const std::uint8_t> id){if(id.size()!=20){return std::unexpected(Error::Input);}return std::string(reinterpret_cast<const char*>(id.data()),id.size())+":DHTNATPuncher";}
Result<std::string> encodePublishValue(const nodes::Snapshot& contact){auto c=contact;if(c.address){c.address=canonical(*c.address);}auto bytes=nodes::encodeContact(c);if(!bytes||!contact.address||!contact.address->port){return std::unexpected(Error::Contact);}return std::string(1,'\0')+*bytes;}
Result<nodes::Contact> decodePublishValue(std::string_view value,nodes::NodeCache& cache){
 if(value.empty()||value[0]!=0||value.size()>32){return std::unexpected(Error::Input);}auto contact=nodes::decodeContact(value.substr(1),cache);if(!contact||contact->consumed!=value.size()-1||!contact->contact.address.port){return std::unexpected(Error::Contact);}return contact->contact;
}
Result<std::string> encode(const Message& m){
 if(!shape(m)){return std::unexpected(Error::Input);}std::string out="d";
 if(m.clientData){auto d=dictionary(*m.clientData);if(!d){return std::unexpected(d.error());}out+="11:client_data"+*m.clientData;}
 if(m.ip){out+="2:ip"+b(*m.ip);}if(m.ok){out+="2:oki"+std::to_string(*m.ok?1:0)+"e";}
 if(m.origin){nodes::NodeCache cache;auto c=nodes::decodeContact(*m.origin,cache);if(!c||c->consumed!=m.origin->size()||!c->contact.address.port){return std::unexpected(Error::Contact);}out+="6:origin"+b(*m.origin);}
 if(m.port){out+="4:porti"+std::to_string(*m.port)+"e";}if(m.target){out+="6:target"+b(*m.target);}out+="4:typei"+std::to_string(static_cast<unsigned>(m.type))+"ee";
 if(out.size()>MaximumMessage){return std::unexpected(Error::Limit);}return out;
}
Result<Message> decode(std::string value,nodes::NodeCache& cache){
 auto d=dictionary(std::move(value));if(!d){return std::unexpected(d.error());}const auto* type=d->find("type");
 if(!type||type->type!=core::Node::Type::Integer||type->integer<0||type->integer>11||d->find("_debug")){return std::unexpected(Error::Input);}
 Message out;out.type=static_cast<Type>(type->integer);
 for(auto [key,dest]:{std::pair{"target",&out.target},std::pair{"origin",&out.origin},std::pair{"ip",&out.ip}}){if(const auto* v=d->find(key)){if(v->type!=core::Node::Type::Bytes){return std::unexpected(Error::Input);}*dest=v->bytes;}}
 if(const auto* v=d->find("ok")){if(v->type!=core::Node::Type::Integer||(v->integer!=0&&v->integer!=1)){return std::unexpected(Error::Input);}out.ok=v->integer==1;}
 if(const auto* v=d->find("port")){if(v->type!=core::Node::Type::Integer||v->integer<0||v->integer>65535){return std::unexpected(Error::Input);}out.port=static_cast<std::uint16_t>(v->integer);}
 if(const auto* v=d->find("client_data")){if(v->type!=core::Node::Type::Dictionary){return std::unexpected(Error::Input);}out.clientData=d->originalBytes().substr(v->begin,v->end-v->begin);}
 if(!shape(out)){return std::unexpected(Error::Input);}if(out.origin){auto c=nodes::decodeContact(*out.origin,cache);if(!c||c->consumed!=out.origin->size()||!c->contact.address.port){return std::unexpected(Error::Contact);}}
 return out;
}
}

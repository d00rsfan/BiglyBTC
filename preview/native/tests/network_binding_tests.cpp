#include "network_binding.hpp"
#include "live_settings.hpp"
#include "biglybtc/application_settings.hpp"
#include <iostream>
#include <stdexcept>
using namespace biglybtc;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,engine::NetworkPolicyError> value){if(!value){throw std::runtime_error(value.error().field);}return std::move(*value);}
core::Document document(std::string_view binds,bool ipv6=true){
  const std::string raw="d7:Bind IP"+std::to_string(binds.size())+":"+std::string(binds)+"19:IPV6 Enable Supporti"+(ipv6?"1":"0")+"e15:Enforce Bind IPi1ee";
  auto result=core::Document::parse(raw);check(bool(result),"literal config parses");return std::move(*result);
}
engine::NetworkPolicy policy(std::string_view text,bool ipv6=true){auto source=document(text,ipv6);return take(engine::readNetworkPolicy(&source,{engine::NetworkMode::Normal}));}
std::vector<engine::NetworkInterfaceAddress> inventory(){return {{"lo","127.0.0.1",1,true},{"lo","::1",1,true},{"owned0","192.0.2.7",7,true},{"owned0","2001:db8::7",7,true}};}
}
int main(){try{
  // Explicit synthetic inventory policy fixtures; no live-provider credit.
  auto raw=document("lo[IPv4];owned0[v6]");const auto original=raw.originalBytes();const auto requested=take(engine::readNetworkPolicy(&raw,{engine::NetworkMode::Normal}));
  check(raw.serialize()==original&&requested.enforceBind&&requested.checkBindOnStart,"read-only exact defaults/config preservation");
  check(requested.bindAddresses==std::vector<std::string>{"lo[v4]","owned0[v6]"},"original family selectors normalized without OS lookup");
  check(engine::validateNetworkPolicy(requested).has_value()&&!engine::makeNetworkSettings(requested),"unresolved request valid but cannot silently reach LT");
  auto devices=inventory();const auto bound=take(engine::resolveNetworkBinding(requested,devices));const auto settings=take(engine::makeNetworkSettings(bound.effective));
  check(bound.pins.size()==2&&settings.get_str(libtorrent::settings_pack::listen_interfaces)=="127.0.0.1:6881,[2001:db8::7]:6881"&&settings.get_str(libtorrent::settings_pack::outgoing_interfaces)=="127.0.0.1,2001:db8::7","exact selected TCP/uTP IPv4/6 endpoints, no wildcards");
  check(!settings.get_bool(libtorrent::settings_pack::listen_system_port_fallback)&&settings.get_int(libtorrent::settings_pack::max_retry_port_bind)==0,"bind/port fallback disabled");
  const auto indexed=take(engine::resolveNetworkBinding(policy("owned0[1]"),devices));check(indexed.effective.bindAddresses==std::vector<std::string>{"2001:db8::7"},"index selects exactly one entry from same owned snapshot");
  check(!engine::resolveNetworkBinding(policy("owned0[1]",false),devices),"disabled IPv6 index never silently selects another address");
  check(!engine::resolveNetworkBinding(policy("absent[v4]"),devices)&&!engine::resolveNetworkBinding(policy("192.0.2.99"),devices),"missing interface/address never falls back");
  for(const auto text:{"lo[-1]","lo[4096]","lo[]","lo[0]junk","lo[[v4]","lo[v7]","lo,other"}){check(!engine::parseNetworkBindSelector(text),"hostile selector rejected");}
  check(engine::networkBindingCurrent(bound,devices),"accepted pinned inventory current");
  for(int fault=0;fault<4;++fault){auto changed=devices;if(fault==0){changed.erase(changed.begin());}if(fault==1){changed[0].index=9;}if(fault==2){changed[0].up=false;}if(fault==3){changed[0].address="127.0.0.2";}check(!engine::networkBindingCurrent(bound,changed),"loss/recreated identity/down/address change fails closed");}
  auto added=devices;added.push_back({"owned1","192.0.2.8",8,true});check(engine::networkBindingCurrent(bound,added)&&bound.effective.bindAddresses.size()==2,"new addresses never widen pinned policy");
  auto ambiguous=devices;ambiguous.push_back({"other","127.0.0.1",9,true});check(!engine::resolveNetworkBinding(policy("127.0.0.1"),ambiguous),"same numeric IP on different interfaces is diagnosed");
  auto scoped=devices;scoped.push_back({"owned0","fe80::7%7",7,true});const auto link=take(engine::resolveNetworkBinding(policy("fe80::7%7"),scoped));check(link.pins.size()==1&&take(engine::makeNetworkSettings(link.effective)).get_str(libtorrent::settings_pack::listen_interfaces)=="[fe80::7%7]:6881","explicit IPv6 numeric scope preserved");
  auto proxy=requested;proxy.proxy.kind=engine::NetworkProxyKind::Socks5;proxy.proxy.host="127.0.0.1";proxy.proxy.port=1080;proxy.proxy.peers=proxy.proxy.trackers=proxy.proxy.remoteDns=true;check(!engine::validateNetworkPolicy(proxy),"interface selectors cannot bypass existing peer-proxy source-bind denial");
  auto tooMany=devices;tooMany.resize(engine::MaximumNetworkInterfaceAddresses+1);check(!engine::resolveNetworkBinding(requested,tooMany),"bounded interface snapshot");
  auto wildcard=take(engine::readNetworkPolicy(nullptr,{engine::NetworkMode::Normal}));check(take(engine::resolveNetworkBinding(wildcard,{})).pins.empty(),"ordinary unspecified policy needs no invented pin");
  wildcard.enforceBind=true;check(!engine::validateNetworkPolicy(wildcard),"enforced wildcard cannot pretend interface restriction");
  auto mixed=policy("127.0.0.1");mixed.enforceBind=false;mixed.bindAddresses.push_back("0.0.0.0");check(!engine::resolveNetworkBinding(mixed,devices),"explicit/wildcard mixture cannot widen route");
  const auto isolated=take(engine::readNetworkPolicy(&raw));check(take(engine::resolveNetworkBinding(isolated,{})).pins.empty()&&take(engine::makeNetworkSettings(isolated)).get_str(libtorrent::settings_pack::listen_interfaces)=="127.0.0.1:0","isolated path ignores user routing and remains unchanged");
  const auto* field=core::applicationSetting(core::SettingId::CheckBindOnStart);check(field&&field->key=="Check Bind IP On Start"&&std::get<bool>(*core::applicationSettingDefault(*field)),"Options exposes pinned original check default without materializing config");
  auto next=requested;next.checkBindOnStart=false;const auto live=engine::prepareLiveSettings(requested,next);check(live&&(live->restartFields&static_cast<std::uint32_t>(engine::LiveRestartField::Binding))&&live->effectivePolicy.checkBindOnStart,"binding preference change is restart-only; live rate planner never resolves routes");
  std::cout<<"network bind selector, routing, loss, limits and settings projections passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

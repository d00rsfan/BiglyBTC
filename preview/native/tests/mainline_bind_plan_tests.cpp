// SPDX-License-Identifier: GPL-2.0-or-later
#include "mainline_bind_plan.hpp"
#include "network_policy.hpp"
#include "plugin_settings.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <ifaddrs.h>
#include <iostream>
#include <net/if.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
using namespace biglybtc;
using namespace biglybtc::engine;
namespace {
int interfaceCalls=0,socketCalls=0;
bool useInjectedInterface=false;
ifaddrs injected{};sockaddr_in injectedAddress{};
void require(bool ok,const char* message){if(!ok){throw std::runtime_error(message);}}
MainlineBindOptions options(){MainlineBindOptions o;o.pluginDirectory="mldht";o.originalBinds={"0.0.0.0"};return o;}
MainlineInterfaceFact entry(std::string ip,unsigned i=2,bool up=true,bool loop=false){return {"eth"+std::to_string(i),std::move(ip),i,0,up,loop};}
MainlineBindFacts facts(){
  MainlineBindFacts f;f.interfaces={entry("10.0.0.9"),entry("2001:db8::9",3),entry("2001:db8::2",4),entry("127.0.0.1",1,true,true),entry("fe80::1",5)};
  f.routes[0]={MainlineRouteState::Selected,"10.0.0.9",0};return f;
}
MainlineBindPlan select(const MainlineBindOptions& o,const MainlineBindFacts& f){auto p=selectMainlineStartupBindPlan(o,f);require(p.has_value(),"Expected complete desired plan");return std::move(*p);}
void defaultFamiliesRemainDistinctAndAllSlotsAreRetained(){
  const auto o=options();const auto f=facts();const auto p=select(o,f);
  require(p.families[0].state==MainlineFamilyBindState::Ready,"v4 route");
  require(p.families[0].bindings.size()==1&&p.families[0].bindings[0].address=="10.0.0.9"&&p.families[0].bindings[0].slot==0,"Private numeric default route is original-supported");
  require(p.families[1].bindings.size()==2,"Do not truncate default v6 multihome to V1");
  require(p.families[1].bindings[0].address=="2001:db8::2"&&p.families[1].bindings[0].slot==0,"Unsigned bytes sorting");
  require(p.families[1].bindings[1].address=="2001:db8::9"&&p.families[1].bindings[1].slot==1,"Independent v6 slots");
  require(p.families[1].bindings[1].port==49001,"Exact dedicated port");
}
void wildcardIsBothFamilyPermissionNotASocket(){
  auto o=options();o.originalBinds={"::"};auto f=facts();auto p=select(o,f);
  require(p.families[0].bindings.size()==1&&p.families[1].bindings.size()==2,"Wrapper accepts any core wildcard for both families");
  f.routes[0]={MainlineRouteState::Unavailable,{},0};p=select(o,f);
  require(p.families[0].state==MainlineFamilyBindState::UnsupportedWildcardFallback&&p.families[0].bindings.empty(),"No arbitrary interface instead of original wildcard fallback");
  f.routes[0]={};p=select(o,f);require(p.families[0].state==MainlineFamilyBindState::MissingRouteFact,"Unknown route must not become original failed-route fallback");
}
void explicitOriginalRestrictionAndV4FallbackOrder(){
  auto o=options();o.autostart6=false;o.originalBinds={"192.168.4.2","10.1.0.3"};auto f=facts();
  f.interfaces.insert(f.interfaces.begin(),entry("10.1.0.3",8));f.interfaces.push_back(entry("192.168.4.2",9));
  auto p=select(o,f);require(p.families[0].bindings[0].address=="10.1.0.3","Rejected default route uses original nonlocal enumeration order");
  f.routes[0]={MainlineRouteState::Selected,"192.168.4.2",0};p=select(o,f);
  require(p.families[0].bindings[0].address=="192.168.4.2","Permitted default route overrides first interface");
  o.originalBinds.clear();p=select(o,f);require(p.families[0].state==MainlineFamilyBindState::NoEligibleAddress,"Empty explicit restriction grants nothing");
}
void exactJavaEligibleAddressClasses(){
  auto o=options();o.multihoming4=true;MainlineBindFacts f;
  for(const auto* ip:{"0.1.2.3","10.0.0.1","172.16.0.1","172.31.255.255","192.168.1.1","169.254.1.1","127.2.3.4","224.0.0.1","239.255.255.255"}){f.interfaces.push_back(entry(ip));}
  for(const auto* ip:{"8.8.8.8","172.15.0.1","172.32.0.1","192.167.1.1","240.0.0.1"}){f.interfaces.push_back(entry(ip));}
  for(const auto* ip:{"::","::1","::192.0.2.1","fe80::1","febf::1","fec0::1","feff::1","ff02::1"}){f.interfaces.push_back(entry(ip,3));}
  for(const auto* ip:{"2001:db8::1","2001::1","fc00::1","fd00::1"}){f.interfaces.push_back(entry(ip,3));}
  f.interfaces.push_back(entry("9.9.9.9",5,false));f.interfaces.push_back(entry("11.0.0.1",6,true,true));
  const auto p=select(o,f);require(p.families[0].bindings.size()==5,"Java IPv4 site/link/loop/multicast/zero rules; classE not invented bogon");
  require(p.families[1].bindings.size()==4,"Java IPv6 compatible/site/link/multicast rules; ULA not blanket rejected");
  for(unsigned i=0;i<5;++i){require(p.families[0].bindings[i].slot==i,"Every desired v4 derived slot retained");}
}
void singleV6UsesRouteThenGlobalThenExplicitUnsupportedWildcard(){
  auto o=options();o.autostart4=false;o.multihoming6=false;auto f=facts();f.routes[1]={MainlineRouteState::Selected,"2001:db8::9",0};
  auto p=select(o,f);require(p.families[1].bindings.size()==1&&p.families[1].bindings[0].address=="2001:db8::9","v6 default route");
  f.routes[1]={MainlineRouteState::Unavailable,{},0};p=select(o,f);require(p.families[1].bindings[0].address=="2001:db8::2","Sequential permitted sorted-global fallback");
  f.interfaces.clear();p=select(o,f);require(p.families[1].state==MainlineFamilyBindState::UnsupportedWildcardFallback,"v6 last wildcard remains explicit unsupported");
}
void disabledAndMissingInterfacesAreNeverFabricated(){
  auto o=options();auto f=facts();o.enabled=false;auto p=select(o,f);
  require(p.families[0].state==MainlineFamilyBindState::Disabled&&p.families[1].state==MainlineFamilyBindState::Disabled,"disabled plugin");
  o.enabled=true;o.autostart4=false;f.interfaces.clear();p=select(o,f);
  require(p.families[0].state==MainlineFamilyBindState::Disabled&&p.families[1].state==MainlineFamilyBindState::NoEligibleAddress,"No global v6 means zero original sockets");
  o.loaded=false;p=select(o,f);require(p.families[1].state==MainlineFamilyBindState::Disabled,"startup false");
}
void boundsAndUnprovenRepeatedOrMappedAddressesRefuse(){
  auto o=options();auto f=facts();o.port=0;require(!selectMainlineStartupBindPlan(o,f),"port zero");o.port=65536;require(!selectMainlineStartupBindPlan(o,f),"port overflow");o.port=49001;
  o.originalBinds={"eth0"};require(!selectMainlineStartupBindPlan(o,f),"Unresolved selector must not become DNS");o=options();
  f.routes[0]={MainlineRouteState::Selected,"2001:db8::2",0};require(!selectMainlineStartupBindPlan(o,f),"Wrong-family route fact");f=facts();
  f.interfaces.push_back(entry("::ffff:192.0.2.1"));require(!selectMainlineStartupBindPlan(o,f),"Unprojected Java mapped address type");f=facts();
  f.interfaces.push_back(entry("2001:db8::2",6));require(!selectMainlineStartupBindPlan(o,f),"Duplicate eligible address lacks registration ordering proof");
  f=facts();f.interfaces.clear();for(unsigned i=1;i<=65;++i){f.interfaces.push_back(entry("2001:db8::"+std::to_string(i)));}
  require(!selectMainlineStartupBindPlan(o,f),"More than64 desired sockets refuses rather than truncates");
  f.interfaces.resize(MaximumMainlineInterfaceFacts+1);require(!selectMainlineStartupBindPlan(o,f),"Fact bound");
}
void pinningRetainsCurrentServerWithoutBlindRouteOrSlotRefresh(){
  auto o=options();auto f=facts();auto p=select(o,f);auto current=f.interfaces;
  require(mainlineBindInterfacesCurrent(o,p,f,current).has_value(),"Exact capture current");
  current.push_back(entry("10.9.9.9",9));require(mainlineBindInterfacesCurrent(o,p,f,current).has_value(),"Unrelated single-home addition does not rebind a current server");
  current.push_back(entry("2001:db8::7",9));require(!mainlineBindInterfacesCurrent(o,p,f,current),"New eligible multihome address requires explicit new plan, no silent omission");
  current=f.interfaces;current[0].index=99;require(!mainlineBindInterfacesCurrent(o,p,f,current),"Named interface identity replacement");
  current=f.interfaces;current[0].up=false;require(!mainlineBindInterfacesCurrent(o,p,f,current),"Selected interface down");
  current=f.interfaces;current.erase(current.begin());require(!mainlineBindInterfacesCurrent(o,p,f,current),"Selected address vanished");
  auto unassigned=f;unassigned.interfaces.erase(unassigned.interfaces.begin());require(!mainlineBindInterfacesCurrent(o,p,unassigned,unassigned.interfaces),"Route string alone is not local assignment authority");
}
void completeProjectionKeepsPayloadSeparateAndNeverTruncates(){
  auto o=options();auto f=facts();f.interfaces.erase(f.interfaces.begin()+2); // One eligible v6 socket.
  const auto plan=select(o,f);NetworkPolicy payload;payload.mode=NetworkMode::Normal;payload.dht=true;
  payload.ipv6=false;payload.bindAddresses={"0.0.0.0"};payload.mainlinePort=o.port;
  const auto before=payload.bindAddresses;auto projected=projectMainlineBindPlan(payload,plan);
  require(projected.has_value()&&projected->mainlineIpv4&&projected->mainlineIpv6&&projected->ipv6,"Both original families execute on Mainline-only policy");
  require(!payload.ipv6&&payload.bindAddresses==before,"Payload wildcard and core IPv6 remain unchanged");
  require(projected->bindAddresses.size()==2,"Complete one-per-family projection");
  auto larger=plan;larger.families[1].bindings.push_back(larger.families[1].bindings.front());
  require(!projectMainlineBindPlan(payload,larger),"Multiple original sockets refuse all, no truncation");
  auto unresolved=plan;unresolved.families[0].state=MainlineFamilyBindState::UnsupportedWildcardFallback;
  require(!projectMainlineBindPlan(payload,unresolved),"Original wildcard fallback cannot become stock socket");
  auto shifted=plan;shifted.families[0].bindings.front().slot=1;
  require(!projectMainlineBindPlan(payload,shifted),"Executor cannot silently replace original derived slot");
  auto v6only=plan;v6only.families[0]={4,MainlineFamilyBindState::Disabled,{}};
  auto only=projectMainlineBindPlan(payload,v6only);require(only.has_value()&&!only->mainlineIpv4&&only->mainlineIpv6,"Original v6-only startup survives core IPv6 false");
}
void originalTypedFlagsAndPreEffectCaptureGates(){
  storage::OriginalMldhtSettings settings;settings.binding.directoryName="custom";settings.port=49002;settings.multihoming4=true;settings.multihoming6=false;
  const std::vector<std::string> binds{"0.0.0.0"};auto copied=mainlineBindOptions(settings,binds);require(copied.has_value()&&copied->multihoming4&&!copied->multihoming6&&copied->port==49002,"Exact original typed settings");
  settings.wrongTypes.push_back("multihoming");require(!mainlineBindOptions(settings,binds),"Malformed original boolean refuses");
  std::array<char,64> path{};const std::string pattern="/tmp/biglybtc-mainline-plan-XXXXXX";std::copy(pattern.begin(),pattern.end(),path.begin());
  require(::mkdtemp(path.data())!=nullptr,"Owned profile");
  struct Root {std::filesystem::path path;~Root(){std::error_code ec;std::filesystem::remove_all(path,ec);}} root{path.data()};
  auto lease=storage::ProfileLease::acquire(root.path);require(lease.has_value(),"Actual profile lease");NetworkPolicy policy;auto o=options();
  auto denied=[&]{auto r=MainlineBindCapture::capture(*lease,policy,o);require(!r&&r.error().code==storage::ErrorCode::Unsupported,"Pre-effect gate refusal");require(interfaceCalls==0&&socketCalls==0,"No interface/route effects before authority");};
  denied();policy.mode=NetworkMode::Normal;denied();policy.dht=true;policy.bindAddresses=o.originalBinds;policy.proxy.kind=NetworkProxyKind::Socks5;denied();
  policy.proxy.kind=NetworkProxyKind::None;o.loaded=false;denied();o.loaded=true;o.pluginDirectory="different";denied();o=options();o.originalBinds={"::"};denied();o=options();
  auto unavailable=MainlineBindCapture::capture(*lease,policy,o);require(!unavailable&&unavailable.error().code==storage::ErrorCode::Io,"Actual capture entry propagates injected OS failure");
  require(interfaceCalls==1&&socketCalls==0,"No route socket after failed interface capture");
}
void captureRetainsActualLeaseAndRejectsReplacementBeforeAnyResample(){
  std::array<char,64> path{};const std::string pattern="/tmp/biglybtc-mainline-plan-lease-XXXXXX";std::copy(pattern.begin(),pattern.end(),path.begin());
  require(::mkdtemp(path.data())!=nullptr,"Owned profile");
  struct Root {std::filesystem::path path;~Root(){std::error_code ec;std::filesystem::remove_all(path,ec);}} root{path.data()};
  auto acquired=storage::ProfileLease::acquire(root.path);require(acquired.has_value(),"Actual profile exclusion");
  std::optional<storage::ProfileLease> borrowed;borrowed.emplace(std::move(*acquired));
  auto o=options();o.multihoming4=true;o.autostart6=false;NetworkPolicy policy;policy.mode=NetworkMode::Normal;policy.dht=true;policy.bindAddresses=o.originalBinds;
  struct Inject {Inject(){useInjectedInterface=true;interfaceCalls=0;socketCalls=0;}~Inject(){useInjectedInterface=false;}} inject;
  auto capture=MainlineBindCapture::capture(*borrowed,policy,o);require(capture.has_value(),"Injected interface capture, actual retained lease");
  require(capture->plan().families[0].bindings.size()==1&&socketCalls==0,"Multihome capture never probes route");
  borrowed.reset();require(capture->validate().has_value(),"Caller object destruction preserves SAME OFD");
  auto excluded=storage::ProfileLease::acquire(root.path);require(!excluded&&excluded.error().code==storage::ErrorCode::Busy,"Capture retains actual exclusion");
  std::filesystem::rename(root.path/".azlock",root.path/".retained-lock");
  auto replacement=storage::ProfileLease::acquire(root.path);require(replacement.has_value(),"Distinct replacement lock");
  const int before=interfaceCalls;auto stale=capture->validate();
  require(!stale&&stale.error().code==storage::ErrorCode::Conflict,"New owner cannot refresh old capture authority");
  require(interfaceCalls==before&&socketCalls==0,"Changed lock refuses before any interface/route work");
}
}
extern "C" int __real_getifaddrs(ifaddrs**);
extern "C" void __real_freeifaddrs(ifaddrs*);
extern "C" unsigned __real_if_nametoindex(const char*);
extern "C" int __wrap_getifaddrs(ifaddrs** out){
  ++interfaceCalls;if(!useInjectedInterface){errno=EIO;return -1;}
  injectedAddress={};injectedAddress.sin_family=AF_INET;injectedAddress.sin_addr.s_addr=htonl(0x08080808);
  injected={};injected.ifa_name=const_cast<char*>("fixture0");injected.ifa_flags=IFF_UP;injected.ifa_addr=reinterpret_cast<sockaddr*>(&injectedAddress);*out=&injected;return 0;
}
extern "C" void __wrap_freeifaddrs(ifaddrs* list){if(list!=&injected){__real_freeifaddrs(list);}}
extern "C" unsigned __wrap_if_nametoindex(const char* name){return useInjectedInterface&&std::string_view(name)=="fixture0"?777U:__real_if_nametoindex(name);}
extern "C" int __wrap_socket(int,int,int){++socketCalls;errno=EIO;return -1;}
int main(){
  try{
    defaultFamiliesRemainDistinctAndAllSlotsAreRetained();wildcardIsBothFamilyPermissionNotASocket();explicitOriginalRestrictionAndV4FallbackOrder();
    exactJavaEligibleAddressClasses();singleV6UsesRouteThenGlobalThenExplicitUnsupportedWildcard();disabledAndMissingInterfacesAreNeverFabricated();
    boundsAndUnprovenRepeatedOrMappedAddressesRefuse();pinningRetainsCurrentServerWithoutBlindRouteOrSlotRefresh();completeProjectionKeepsPayloadSeparateAndNeverTruncates();originalTypedFlagsAndPreEffectCaptureGates();captureRetainsActualLeaseAndRejectsReplacementBeforeAnyResample();
    std::cout<<"Mainline bind-plan deterministic cases passed\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

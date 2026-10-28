// SPDX-License-Identifier: GPL-2.0-or-later
// Selection: paired067 RPCServerManager117-197 / AddressUtils55-60,146-238.
#include "mainline_bind_plan.hpp"
#include "network_policy.hpp"
#include "plugin_settings.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <charconv>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
namespace biglybtc::engine {
namespace {
using Code=storage::ErrorCode;
std::unexpected<storage::Error> error(Code code,const char* text){return std::unexpected(storage::Error{code,text});}
struct Address {
  unsigned family=4,scope=0;
  std::array<unsigned char,16> bytes{};
  std::string text;
};
std::optional<Address> address(std::string_view value,unsigned scope=0){
  if(value.empty()||value.size()>=128){return {};}
  const auto percent=value.find('%');
  if(percent!=std::string_view::npos){
    const auto suffix=value.substr(percent+1);unsigned selected=0;
    const auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),selected);
    if(scope||suffix.empty()||parsed.ec!=std::errc{}||parsed.ptr!=suffix.data()+suffix.size()||!selected){return {};}
    scope=selected;value=value.substr(0,percent);
  }
  std::array<char,128> input{};std::copy(value.begin(),value.end(),input.begin());
  Address result;int type=AF_INET;
  if(::inet_pton(type,input.data(),result.bytes.data())!=1){type=AF_INET6;if(::inet_pton(type,input.data(),result.bytes.data())!=1){return {};}}
  if(type==AF_INET&&scope){return {};}
  // Java InetAddress represents mapped IPv6 as IPv4; a native interface record
  // claiming it as IPv6 cannot be projected here without that type conversion.
  if(type==AF_INET6&&std::all_of(result.bytes.begin(),result.bytes.begin()+10,[](auto b){return !b;})&&result.bytes[10]==255&&result.bytes[11]==255){return {};}
  result.family=type==AF_INET?4U:6U;result.scope=scope;
  std::array<char,INET6_ADDRSTRLEN> output{};if(!::inet_ntop(type,result.bytes.data(),output.data(),output.size())){return {};}
  result.text=output.data();return result;
}
bool zero(const Address& a){return std::all_of(a.bytes.begin(),a.bytes.begin()+(a.family==4?4:16),[](auto b){return !b;});}
bool loopback(const Address& a){return a.family==4?a.bytes[0]==127:std::all_of(a.bytes.begin(),a.bytes.end()-1,[](auto b){return !b;})&&a.bytes[15]==1;}
bool global(const Address& a){
  const auto& b=a.bytes;
  if(zero(a)||loopback(a)){return false;}
  if(a.family==4){return b[0]!=0&&!(b[0]>=224&&b[0]<=239)&&!(b[0]==169&&b[1]==254)&&b[0]!=10&&!(b[0]==172&&b[1]>=16&&b[1]<=31)&&!(b[0]==192&&b[1]==168);}
  return b[0]!=255&&!(b[0]==254&&(b[1]&0xc0)==0x80)&&!(b[0]==254&&(b[1]&0xc0)==0xc0)
      &&!std::all_of(b.begin(),b.begin()+12,[](auto c){return !c;});
}
bool equal(const Address& a,const Address& b){return a.family==b.family&&a.bytes==b.bytes;}
bool allowed(const Address& a,const std::vector<Address>& restrictions){
  return std::ranges::any_of(restrictions,[&](const auto& b){return zero(b)||equal(a,b);});
}
struct Parsed {
  std::vector<Address> restrictions;
  std::vector<std::pair<MainlineInterfaceFact,Address>> interfaces;
};
std::expected<Parsed,storage::Error> parse(const MainlineBindOptions& options,const MainlineBindFacts& facts){
  if(options.port<1||options.port>65535||options.originalBinds.size()>64||facts.interfaces.size()>MaximumMainlineInterfaceFacts){return error(Code::Unsupported,"Mainline bind-plan bounds");}
  if(!storage::originalPluginBinding("mldht",options.pluginDirectory)){return error(Code::InvalidFormat,"Mainline original plugin directory");}
  Parsed result;
  for(const auto& text:options.originalBinds){auto a=address(text);if(!a){return error(Code::Unsupported,"Mainline original bind requires numeric resolution");}result.restrictions.push_back(std::move(*a));}
  for(const auto& fact:facts.interfaces){
    auto a=address(fact.address,fact.scope);
    if(!a||!fact.index||fact.name.empty()||fact.name.size()>=IFNAMSIZ){return error(Code::InvalidFormat,"Mainline interface fact");}
    result.interfaces.emplace_back(fact,std::move(*a));
  }
  return result;
}
std::vector<Address> globals(const Parsed& facts,unsigned family,bool* duplicate=nullptr){
  std::vector<Address> result;
  for(const auto& [fact,a]:facts.interfaces){if(a.family==family&&fact.up&&!fact.loopback&&global(a)&&allowed(a,facts.restrictions)){result.push_back(a);}}
  std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.bytes<b.bytes;});
  if(duplicate){*duplicate=std::adjacent_find(result.begin(),result.end(),equal)!=result.end();}
  result.erase(std::unique(result.begin(),result.end(),equal),result.end());return result;
}
std::expected<std::vector<MainlineInterfaceFact>,storage::Error> interfaces(){
  ifaddrs* head=nullptr;if(::getifaddrs(&head)){return error(Code::Io,"Cannot capture Mainline interfaces");}
  const std::unique_ptr<ifaddrs,decltype(&::freeifaddrs)> owned(head,&::freeifaddrs);
  std::vector<MainlineInterfaceFact> result;std::size_t visited=0;
  for(const auto* row=head;row;row=row->ifa_next){
    if(++visited>MaximumMainlineInterfaceFacts){return error(Code::Unsupported,"Mainline interface capture bound");}
    if(!row->ifa_addr||!row->ifa_name||(row->ifa_addr->sa_family!=AF_INET&&row->ifa_addr->sa_family!=AF_INET6)){continue;}
    const auto family=row->ifa_addr->sa_family;std::array<char,INET6_ADDRSTRLEN> text{};unsigned scope=0;
    const void* raw=nullptr;
    if(family==AF_INET){raw=&reinterpret_cast<const sockaddr_in*>(row->ifa_addr)->sin_addr;}
    else{const auto* a=reinterpret_cast<const sockaddr_in6*>(row->ifa_addr);raw=&a->sin6_addr;scope=a->sin6_scope_id;}
    const auto index=::if_nametoindex(row->ifa_name);
    if(!index||!::inet_ntop(family,raw,text.data(),text.size())){return error(Code::Io,"Cannot capture Mainline interface identity");}
    result.push_back({row->ifa_name,text.data(),index,scope,bool(row->ifa_flags&IFF_UP),bool(row->ifa_flags&IFF_LOOPBACK)});
  }
  return result;
}
MainlineRouteFact route(unsigned family){
  struct Socket {int fd;~Socket(){if(fd>=0){::close(fd);}}};
  Socket owned{::socket(family==4?AF_INET:AF_INET6,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0)};
  if(owned.fd<0){return {MainlineRouteState::Unavailable,{},{}};}
  sockaddr_storage target{};socklen_t length=0;
  if(family==4){auto& a=reinterpret_cast<sockaddr_in&>(target);a.sin_family=AF_INET;a.sin_port=htons(63);::inet_pton(AF_INET,"8.8.8.8",&a.sin_addr);length=sizeof(a);}
  else{auto& a=reinterpret_cast<sockaddr_in6&>(target);a.sin6_family=AF_INET6;a.sin6_port=htons(63);::inet_pton(AF_INET6,"2001:4860:4860::8888",&a.sin6_addr);length=sizeof(a);}
  // UDP connect only selects the default destination/source. No data syscall,
  // DNS, peer/tracker request or public bootstrap is performed by this helper.
  if(::connect(owned.fd,reinterpret_cast<const sockaddr*>(&target),length)){return {MainlineRouteState::Unavailable,{},{}};}
  sockaddr_storage local{};length=sizeof(local);
  if(::getsockname(owned.fd,reinterpret_cast<sockaddr*>(&local),&length)){return {MainlineRouteState::Unavailable,{},{}};}
  std::array<char,INET6_ADDRSTRLEN> text{};const void* raw=nullptr;unsigned scope=0;
  if(local.ss_family==AF_INET&&family==4){raw=&reinterpret_cast<const sockaddr_in*>(&local)->sin_addr;}
  else if(local.ss_family==AF_INET6&&family==6){const auto* a=reinterpret_cast<const sockaddr_in6*>(&local);raw=&a->sin6_addr;scope=a->sin6_scope_id;}
  else{return {MainlineRouteState::Unavailable,{},{}};}
  if(!::inet_ntop(family==4?AF_INET:AF_INET6,raw,text.data(),text.size())){return {MainlineRouteState::Unavailable,{},{}};}
  return {MainlineRouteState::Selected,text.data(),scope};
}
}
std::expected<MainlineBindOptions,storage::Error> mainlineBindOptions(const storage::OriginalMldhtSettings& source,std::span<const std::string> binds){
  try{
  if(!source.wrongTypes.empty()){return error(Code::InvalidFormat,"Malformed original Mainline bind settings");}
  MainlineBindOptions result;result.pluginDirectory=source.binding.directoryName;result.port=source.port;
  result.loaded=source.loadedAtStartup;result.enabled=source.enabled;result.autostart4=source.autostart4;result.autostart6=source.autostart6;
  result.multihoming4=source.multihoming4;result.multihoming6=source.multihoming6;result.originalBinds.assign(binds.begin(),binds.end());return result;
  }catch(const std::bad_alloc&){return error(Code::Io,"Cannot copy original Mainline bind settings");}
}
std::expected<MainlineBindPlan,storage::Error> selectMainlineStartupBindPlan(const MainlineBindOptions& options,const MainlineBindFacts& facts){
  try{
    auto parsed=parse(options,facts);if(!parsed){return std::unexpected(parsed.error());}MainlineBindPlan plan;
    for(unsigned i=0;i<2;++i){
      auto& selected=plan.families[i];const unsigned family=i?6:4;
      if(!options.loaded||!options.enabled||!(i?options.autostart6:options.autostart4)){continue;}
      bool duplicate=false;auto candidates=globals(*parsed,family,&duplicate);
      if((i?options.multihoming6:options.multihoming4)&&duplicate){return error(Code::Unsupported,"Repeated eligible Mainline address needs original registration ordering proof");}
      if(!(i?options.multihoming6:options.multihoming4)){
        const auto& input=facts.routes[i];
        if(input.state==MainlineRouteState::NotCaptured){selected.state=MainlineFamilyBindState::MissingRouteFact;continue;}
        if(input.state==MainlineRouteState::Selected){
          auto a=address(input.address,input.scope);
          if(!a||a->family!=family||zero(*a)){return error(Code::InvalidFormat,"Mainline route fact family/address");}
          if(allowed(*a,parsed->restrictions)){candidates.assign(1,std::move(*a));}
          else{candidates.clear();}
        }else if(input.state==MainlineRouteState::Unavailable){candidates.clear();}
        else{return error(Code::InvalidFormat,"Mainline route fact state");}
        if(candidates.empty()){
          Address any;any.family=family;
          // v6 tries any eligible global address first; v4 tries wildcard first.
          if(family==6){candidates=globals(*parsed,family);if(!candidates.empty()){candidates.resize(1);}}
          if(candidates.empty()&&allowed(any,parsed->restrictions)){selected.state=MainlineFamilyBindState::UnsupportedWildcardFallback;continue;}
          if(candidates.empty()&&family==4){
            for(const auto& [fact,a]:parsed->interfaces){if(a.family==4&&fact.up&&!zero(a)&&!loopback(a)&&allowed(a,parsed->restrictions)){candidates.push_back(a);break;}}
          }
        }
      }
      if(candidates.size()>MaximumMainlineFamilyBinds){return error(Code::Unsupported,"Mainline desired socket bound; no truncation");}
      selected.state=candidates.empty()?MainlineFamilyBindState::NoEligibleAddress:MainlineFamilyBindState::Ready;
      for(const auto& a:candidates){selected.bindings.push_back({family,a.scope,static_cast<unsigned>(selected.bindings.size()),a.text,static_cast<std::uint16_t>(options.port)});}
    }
    return plan;
  }catch(const std::bad_alloc&){return error(Code::Io,"Cannot allocate Mainline bind plan");}
}
std::expected<NetworkPolicy,storage::Error> projectMainlineBindPlan(const NetworkPolicy& source,const MainlineBindPlan& plan){
  try{
    if(source.mode!=NetworkMode::Normal||!source.dht||source.proxy.kind!=NetworkProxyKind::None){return error(Code::Unsupported,"Mainline bind projection requires owned direct policy");}
    NetworkPolicy result=source;result.bindAddresses.clear();result.mainlineIpv4=false;result.mainlineIpv6=false;
    for(std::size_t i=0;i<plan.families.size();++i){const auto& family=plan.families[i];
      if(family.family!=(i==0?4U:6U)){return error(Code::InvalidFormat,"Mainline bind-plan family order");}
      if(family.state==MainlineFamilyBindState::Disabled||family.state==MainlineFamilyBindState::NoEligibleAddress){
        if(!family.bindings.empty()){return error(Code::InvalidFormat,"Inactive Mainline family has bindings");}continue;
      }
      if(family.state!=MainlineFamilyBindState::Ready||family.bindings.size()!=1){return error(Code::Unsupported,"Complete original Mainline plan exceeds one numeric socket per family");}
      const auto& binding=family.bindings.front();const auto numeric=address(binding.address,binding.scope);
      if(!numeric||numeric->family!=family.family||binding.family!=family.family||binding.slot||binding.scope
          ||zero(*numeric)||binding.port!=source.mainlinePort
          ||(numeric->family==4&&numeric->bytes[0]>=224)
          ||(numeric->family==6&&(numeric->bytes[0]==255||(numeric->bytes[0]==254&&(numeric->bytes[1]&0xc0)==0x80)))){
        return error(Code::Unsupported,"Original Mainline binding cannot be executed exactly");
      }
      result.bindAddresses.push_back(numeric->text);
      (i==0?result.mainlineIpv4:result.mainlineIpv6)=true;
    }
    if(result.bindAddresses.empty()){return error(Code::Unsupported,"Original Mainline plan has no active numeric socket");}
    result.ipv6=result.mainlineIpv6; // Mainline-only copy; payload policy is untouched.
    return result;
  }catch(const std::bad_alloc&){return error(Code::Io,"Cannot retain complete Mainline bind policy");}
}
std::expected<void,storage::Error> mainlineBindInterfacesCurrent(const MainlineBindOptions& options,const MainlineBindPlan& plan,const MainlineBindFacts& captured,std::span<const MainlineInterfaceFact> current){
  try{
    auto approved=selectMainlineStartupBindPlan(options,captured);
    if(!approved){return std::unexpected(approved.error());}
    if(*approved!=plan){return error(Code::Conflict,"Mainline plan differs from its captured source facts");}
    MainlineBindFacts latest;latest.interfaces.assign(current.begin(),current.end());latest.routes=captured.routes;
    auto a=parse(options,captured),b=parse(options,latest);if(!a){return std::unexpected(a.error());}if(!b){return std::unexpected(b.error());}
    for(unsigned i=0;i<2;++i){
      const auto& family=plan.families[i];
      if(family.state==MainlineFamilyBindState::Disabled){continue;}
      if(i?options.multihoming6:options.multihoming4){
        bool duplicate=false;const auto expected=globals(*a,i?6:4),now=globals(*b,i?6:4,&duplicate);
        if(duplicate||expected.size()!=now.size()||!std::equal(expected.begin(),expected.end(),now.begin(),[](const auto& x,const auto& y){return equal(x,y)&&x.scope==y.scope;})){return error(Code::Conflict,"Mainline multihome eligible set changed");}
      }
      for(const auto& binding:family.bindings){
        const auto wanted=address(binding.address,binding.scope);if(!wanted){return error(Code::InvalidFormat,"Mainline desired binding");}
        bool observed=false;
        for(const auto& [fact,ip]:a->interfaces){
          if(!equal(ip,*wanted)||ip.scope!=wanted->scope){continue;}observed=true;
          if(!fact.up||std::ranges::find(latest.interfaces,fact)==latest.interfaces.end()){return error(Code::Conflict,"Mainline selected interface changed");}
        }
        if(!observed){return error(Code::Conflict,"Mainline route source is not assigned locally");}
      }
    }
    return {};
  }catch(const std::bad_alloc&){return error(Code::Io,"Cannot validate Mainline bind facts");}
}
struct MainlineBindCapture::Impl {
  std::shared_ptr<const storage::ProfileLease> lease;
  MainlineBindOptions options;
  MainlineBindFacts facts;
  MainlineBindPlan plan;
};
std::expected<MainlineBindCapture,storage::Error> MainlineBindCapture::capture(const storage::ProfileLease& lease,const NetworkPolicy& policy,const MainlineBindOptions& options){
  try{
    if(policy.mode!=NetworkMode::Normal||!policy.dht||policy.proxy.kind!=NetworkProxyKind::None||!options.loaded||!options.enabled||(!options.autostart4&&!options.autostart6)||policy.mainlineDirectory!=options.pluginDirectory||policy.mainlinePort!=options.port||policy.bindAddresses!=options.originalBinds){return error(Code::Unsupported,"Mainline bind capture requires explicit owned direct activation");}
    auto checked=parse(options,MainlineBindFacts{});if(!checked){return std::unexpected(checked.error());}
    auto held=lease.retain();if(!held){return std::unexpected(held.error());}
    auto impl=std::make_shared<Impl>();impl->lease=std::move(*held);impl->options=options;
    auto facts=interfaces();if(!facts){return std::unexpected(facts.error());}impl->facts.interfaces=std::move(*facts);
    if(options.autostart4&&!options.multihoming4){impl->facts.routes[0]=route(4);}
    if(options.autostart6&&!options.multihoming6){impl->facts.routes[1]=route(6);}
    auto selected=selectMainlineStartupBindPlan(options,impl->facts);if(!selected){return std::unexpected(selected.error());}impl->plan=std::move(*selected);
    MainlineBindCapture result(std::move(impl));auto valid=result.validate();if(!valid){return std::unexpected(valid.error());}return result;
  }catch(const std::bad_alloc&){return error(Code::Io,"Cannot retain Mainline bind capture");}
}
const MainlineBindFacts& MainlineBindCapture::facts()const{return impl_->facts;}
const MainlineBindPlan& MainlineBindCapture::plan()const{return impl_->plan;}
const MainlineBindOptions& MainlineBindCapture::options()const{return impl_->options;}
std::expected<void,storage::Error> MainlineBindCapture::validate()const{
  try{
  auto held=impl_->lease->retain();if(!held){return std::unexpected(held.error());}
  auto current=interfaces();if(!current){return std::unexpected(current.error());}
  auto valid=mainlineBindInterfacesCurrent(impl_->options,impl_->plan,impl_->facts,*current);
  if(!valid){return valid;}
  auto after=impl_->lease->retain();if(!after){return std::unexpected(after.error());}
  return {};
  }catch(const std::bad_alloc&){return error(Code::Io,"Cannot validate retained Mainline bind capture");}
}
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "i2p_discovery_options.hpp"
#include "file_io.hpp"
#include "plugin_settings.hpp"
#include "../services/i2p_provider/sam_protocol.hpp"
#include <openssl/rand.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <charconv>
#include <map>

namespace biglybtc::app {
namespace {
namespace dht=services::i2p::dht;
using Error=storage::Error;using Code=storage::ErrorCode;
std::unexpected<Error> bad(const char* text){return std::unexpected(Error{Code::InvalidFormat,text});}
std::string_view trim(std::string_view value){
  const auto first=value.find_first_not_of(" \t\r\n\f");
  if(first==std::string_view::npos){return {};}
  return value.substr(first,value.find_last_not_of(" \t\r\n\f")-first+1);
}
std::optional<dht::Id> nodeId(std::string_view value){
  if(value.size()!=32){return {};}
  dht::Id result{};unsigned acc=0,bits=0;std::size_t at=0;
  for(char c:value){const auto n=std::string_view("abcdefghijklmnopqrstuvwxyz234567").find(c);
    if(n==std::string_view::npos){return {};}acc=(acc<<5)|static_cast<unsigned>(n);bits+=5;
    if(bits>=8){bits-=8;result[at++]=static_cast<services::i2p::Byte>((acc>>bits)&255U);}}
  return at==result.size()&&!bits?std::optional{result}:std::nullopt;
}
// These two fields are emitted as plain ASCII by the original writer. Refuse
// ambiguous escaped/continued relevant keys instead of silently replacing them.
std::expected<std::map<std::string,std::string>,Error> properties(std::string_view bytes){
  std::map<std::string,std::string> values;
  while(!bytes.empty()){
    const auto newline=bytes.find('\n');const auto line=trim(bytes.substr(0,newline));
    bytes=newline==std::string_view::npos?std::string_view{}:bytes.substr(newline+1);
    if(line.empty()||line.front()=='#'||line.front()=='!'){continue;}
    const auto split=line.find_first_of("=: \t");const auto key=line.substr(0,split);
    if(key!="port"&&key!="nid"){continue;}
    auto value=split==std::string_view::npos?std::string_view{}:trim(line.substr(split));
    if(!value.empty()&&(value.front()=='='||value.front()==':')){value=trim(value.substr(1));}
    if(value.empty()||value.find('\\')!=std::string_view::npos){return bad("Unsupported original I2P DHT properties encoding");}
    values[std::string(key)]=std::string(value);
  }
  return values;
}
}
std::expected<std::optional<dht::Actor::Options>,storage::Error> readI2pDiscoveryOptions(
    const storage::ProfileLease& lease,bool mixed,const I2pServiceOptions& options){
  auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
  auto config=storage::ConfigFile::load(lease,"biglybt.config");if(!config){return std::unexpected(config.error());}
  auto binding=storage::originalPluginBinding("azneti2phelper","azneti2phelper");
  if(!binding){return bad("Invalid original I2P binding");}
  storage::OriginalPluginSettings saved(config->document(),std::move(*binding));
  if(!saved.boolean("azi2phelper.dht.enable",true)){return std::optional<dht::Actor::Options>{};}
  if(!saved.wrongTypes().empty()){return bad("Malformed original I2P DHT switch");}
  dht::Actor::Options result;
  if(options.discovery){
    result.queryPort=options.discovery->queryPort;result.localId=options.discovery->nodeId;
    for(const auto& seed:options.discovery->seeds){
      dht::Contact value;value.id=seed.nodeId;value.hash=seed.destinationHash;value.port=seed.queryPort;
      if(!seed.destination.empty()){
        auto destination=services::i2p::decodeDestination(seed.destination);
        if(!destination||destination->hash!=value.hash){return bad("Explicit DHT contact Destination mismatch");}
        value.destination=std::move(*destination);
      }
      if(!dht::validContact(value)){return bad("Invalid explicit I2P DHT contact");}result.seeds.push_back(std::move(value));
    }
    if(!result.queryPort||result.queryPort>=65534||result.seeds.empty()||result.seeds.size()>20){return bad("Invalid explicit I2P DHT routing options");}
    return std::optional{std::move(result)};
  }
  storage::detail::Descriptor plugins(::openat(lease.directoryHandle(),"plugins",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  storage::detail::Descriptor plugin(plugins.get()<0?-1: ::openat(plugins.get(),"azneti2phelper",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  struct stat st{};
  if(plugin.get()<0||::fstat(plugin.get(),&st)!=0||st.st_uid!=::geteuid()){return bad("Cannot read held original I2P DHT directory");}
  auto bytes=storage::detail::readFile(plugin.get(),mixed?"dht.config":"dht1.config",64*1024);
  if(!bytes){return std::unexpected(bytes.error());}
  if(*bytes){
    auto values=properties(**bytes);if(!values){return std::unexpected(values.error());}
    if(values->contains("port")!=values->contains("nid")){return bad("Incomplete original I2P DHT identity");}
    if(values->contains("port")){
      const auto& value=values->at("port");unsigned port=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),port);
      auto id=nodeId(values->at("nid"));
      if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||port<1||port>=65534||!id){return bad("Malformed original I2P DHT identity");}
      result.queryPort=static_cast<std::uint16_t>(port);result.localId=*id;
    }
  }
  if(!result.queryPort){
    std::uint16_t random=0;if(RAND_bytes(reinterpret_cast<unsigned char*>(&random),sizeof(random))!=1){return bad("Cannot generate an I2P DHT port");}
    result.queryPort=static_cast<std::uint16_t>(10000+random%55533);
    // Zero localId asks the actual owner to derive a secure NID from its key.
  }
  // Exact original helper33d97 bootstrap; an untrusted routing hint, never a
  // peer/source grant. Pure I2P uses only I2CP, without OS DNS/Public fallback.
  constexpr auto bootstrap="N0e4jfsxy~NYzyr-0bY1nwpnhTza8fn1wWr6IHHOmaIEnbEvgltJvyJn8LWvwlu589mUPhQXQb9BtMrkEan8RZSL4Vo2iFgMCxjTOnfA2dW1~JpL0ddGM28OQITya-1YDgNZFmyX0Me-~RjJjTg31YNozDoosIQ-Uvz2s5aUrzI0gt0r3M4PFUThb0eefd51Yb-eEQMpBb-Hd~EU07yw46ljy2uP4tiEPlWt0l0YR8nbeH0Eg6i3fCoSVgWpSeRjJ9vJeHvwGymO2rPHCSCPgIVwwyqNYpgkqGWnn9Qg97Wc-zrTBiRJp0Dn4lcYvkbbeBrblZDOy6PnPFp33-WZ7lcaVeR6uNGqphQxCYv8pbti5Q9QYcc6IzYpvzsgDCbIVhuzQ9Px2-l6qVg6S-i-cYwQfxBYnVSyVmryuGSkIha2AezYJk2~0k7-byeJ0q57Re~aZy6boIDa2qtaOyi-RDbCWAoIIfOycwkAvqf5nG8KOVwGzvFEjYuExyP3f9ZlAAAA";
  auto destination=services::i2p::provider::destinationText(bootstrap);auto id=nodeId("6d3dh2bwrafjdx4ba46zb6jvbnnt2g3r");
  if(!destination||!id){return bad("Invalid pinned original I2P bootstrap");}
  dht::Contact seed{*id,destination->hash,52896,std::move(destination)};
  if(!dht::validContact(seed)){return bad("Invalid pinned original I2P bootstrap node binding");}
  result.seeds.push_back(std::move(seed));return std::optional{std::move(result)};
}
}

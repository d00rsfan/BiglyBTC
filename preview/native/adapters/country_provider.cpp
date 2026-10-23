#include "country_provider.hpp"
#include "biglybtc/plugin_host.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <thread>
#include <utility>

namespace biglybtc::storage {
struct CountryProvider::Impl {
  plugins::Module module;
  const std::thread::id worker=std::this_thread::get_id();
  explicit Impl(plugins::Module&& value):module(std::move(value)){}
  bool onWorker() const noexcept{return worker==std::this_thread::get_id();}
  bt_status probe(bt_location_database_result_v1& result) noexcept {
    if(!onWorker()){return BT_INVALID;}
    try {const bt_location_database_request_v1 request{};
      return module.call(BT_LOCATION_DATABASE_STATUS,&request,sizeof(request),&result,sizeof(result));
    }catch(...){return BT_FAILED;}
  }
};
CountryProvider::CountryProvider(std::unique_ptr<Impl> value) noexcept:impl_(std::move(value)){}
CountryProvider::~CountryProvider()=default; // Worker-affine Module stop/destroy.
std::expected<std::unique_ptr<CountryProvider>,CountryProviderError> CountryProvider::bind(plugins::Module&& module){
  try{
    auto impl=std::make_unique<Impl>(std::move(module));bt_location_database_result_v1 state{};
    // A moved-from Module returns BT_INVALID without dereferencing its API.
    const auto status=impl->probe(state);
    if(status==BT_INVALID||impl->module.id()!="azlocprov"){return std::unexpected(CountryProviderError::ModuleUnavailable);}
    return std::unique_ptr<CountryProvider>(new CountryProvider(std::move(impl)));
  }catch(const std::bad_alloc&){return std::unexpected(CountryProviderError::Memory);}
  catch(...){return std::unexpected(CountryProviderError::ModuleUnavailable);}
}
bool CountryProvider::available() noexcept {
  bt_location_database_result_v1 result{};
  return impl_->probe(result)==BT_OK&&result.available==1&&result.reserved==0;
}
core::CountryLookup CountryProvider::lookup(const core::PeerNumericEndpoint& endpoint) noexcept {
  core::CountryLookup result;
  if(!available()){return result;}
  bt_location_request_v1 request{};
  if(endpoint.family==core::PeerAddressFamily::V4){request.family=4;}
  else if(endpoint.family==core::PeerAddressFamily::V6){request.family=6;}
  else{return result;}
  std::copy(endpoint.address.begin(),endpoint.address.end(),request.address);std::memcpy(request.locale,"en",3);
  bt_location_result_v1 raw{};bt_status status=BT_FAILED;
  try{status=impl_->module.call(BT_LOCATION_LOOKUP_CHECKED,&request,sizeof(request),&raw,sizeof(raw));}catch(...){return result;}
  // No cached readiness success, including failures after a prior good lookup.
  if(!available()){return result;}
  if(status==BT_NOT_FOUND){result.kind=core::CountryLookupKind::Unknown;return result;}
  if(status!=BT_OK||raw.iso3166[0]<'A'||raw.iso3166[0]>'Z'||raw.iso3166[1]<'A'||raw.iso3166[1]>'Z'||raw.iso3166[2]){return result;}
  const auto length=::strnlen(raw.country,sizeof(raw.country));if(length==sizeof(raw.country)){return result;}
  result.kind=core::CountryLookupKind::Country;std::copy_n(raw.iso3166,3,result.code.begin());
  std::copy_n(raw.country,length,result.name.begin());return result;
}
std::expected<core::CountryStatistics,core::CountryStatisticsError> CountryProvider::snapshot(const core::PeerPayloadSnapshot& ledger) noexcept {
  if(ledger.count>core::MaximumPeerPayloadRecords){return std::unexpected(core::CountryStatisticsError::InvalidLedger);}
  if(!available()){return core::projectCountryStatistics(ledger,{},false);}
  std::array<core::CountryResolvedPeer,core::MaximumPeerPayloadRecords> locations{};std::size_t count=0;
  for(std::size_t i=0;i<ledger.count;++i){const auto& peer=ledger.records[i];
    if(peer.network==core::PeerNetworkClass::Local){continue;}
    locations[count++]={peer.connection,lookup(peer.remote)};
  }
  if(!available()){return core::projectCountryStatistics(ledger,{},false);}
  return core::projectCountryStatistics(ledger,{locations.data(),count},true);
}
} // namespace biglybtc::storage

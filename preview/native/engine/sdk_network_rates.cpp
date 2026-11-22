// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdk_network_rates.hpp"
#include <libtorrent/aux_/session_impl.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/aux_/bandwidth_limit.hpp>
#include <libtorrent/settings_pack.hpp>
#include <boost/asio/post.hpp>
#include <chrono>
#include <future>
#include <thread>
#include <stdexcept>
namespace biglybtc::engine {
namespace {using Error=SdkSharedQuotaError;namespace lt=libtorrent;}
#if __has_include(<libtorrent/biglybtc_shared_quota.hpp>)
struct SdkNetworkRates::Impl {
 const std::thread::id worker=std::this_thread::get_id();
 std::weak_ptr<lt::aux::session_impl> sdk;
 std::shared_ptr<SdkSharedTagBudget> global,lan;
 explicit Impl(lt::session_handle session,std::shared_ptr<SdkSharedTagBudget> a,
     std::shared_ptr<SdkSharedTagBudget> b):sdk(session.native_handle()),global(std::move(a)),lan(std::move(b)){}
 template<class Operation>std::expected<void,Error> invoke(Operation operation){
  if(worker!=std::this_thread::get_id()){return std::unexpected(Error::Thread);}
  auto session=sdk.lock();if(!session||!global->current()||!lan->current()){return std::unexpected(Error::Lifetime);}
  auto promise=std::make_shared<std::promise<bool>>();auto done=promise->get_future();
  try{boost::asio::post(session->get_context(),[session,promise,operation=std::move(operation)]{
    try{operation(*session);promise->set_value(true);}catch(...){promise->set_value(false);}
  });}catch(...){return std::unexpected(Error::Engine);}
  if(done.wait_for(std::chrono::seconds(5))!=std::future_status::ready||!done.get()){return std::unexpected(Error::Engine);}
  return {};
 }
 void notify(lt::aux::session_impl& session){
  for(const auto& handle:session.get_torrents()){
   const auto torrent=handle.native_handle();if(!torrent){continue;}
   for(auto* peer:*torrent){if(!peer->is_disconnecting()&&global->refreshPeer(peer)<0){throw std::runtime_error("Common network quota refresh failed");}}
  }
 }
};
SdkNetworkRates::SdkNetworkRates(std::shared_ptr<Impl> value):impl_(std::move(value)){}
SdkNetworkRates::~SdkNetworkRates()=default;
std::expected<std::unique_ptr<SdkNetworkRates>,Error> SdkNetworkRates::create(
    lt::session_handle session,std::shared_ptr<SdkSharedTagBudget> global,
    std::shared_ptr<SdkSharedTagBudget> lan,bool includeUploadProtocol,bool includeDownloadProtocol){
 if(!SdkSharedTagBudget::supportsNetworkBindings()
     ||((!includeUploadProtocol||!includeDownloadProtocol)&&!supportsOriginalProtocol())){return std::unexpected(Error::Unsupported);}
 if(!global||!lan||global->tag()!=GlobalKey||lan->tag()!=LanKey){return std::unexpected(Error::Limits);}
 for(const auto& budget:{global,lan}){const auto limits=budget->limits();if(!limits||limits->upload<0||limits->download<0){return std::unexpected(Error::Limits);}}
 try{
  auto value=std::shared_ptr<Impl>(new Impl(session,std::move(global),std::move(lan)));
  // Original transfer processors count transport bytes,not estimated IP/TCP
  // headers. Submit this narrow setting before the same-context bind barrier.
  lt::settings_pack settings;settings.set_bool(lt::settings_pack::rate_limit_ip_overhead,false);
  session.apply_settings(std::move(settings));
  auto bound=value->invoke([value,includeUploadProtocol,includeDownloadProtocol](lt::aux::session_impl& sdk){
   // Admission precedes any torrent or storage lifetime. Check both default
   // classes before changing either binding on this serialized SDK context.
   if(!sdk.get_torrents().empty()){throw std::runtime_error("Common network quota requires a fresh SDK session");}
   for(const auto& [group,budget]:{std::pair{lt::session_handle::global_peer_class_id,value->global},std::pair{lt::session_handle::local_peer_class_id,value->lan}}){
    const auto* actual=sdk.peer_classes().at(group);if(!actual){throw std::runtime_error("Missing SDK network class");}
    for(const auto& channel:actual->channel){if(const auto* old=channel.shared_operations();old&&!budget->matchesBinding(old->scope,old->budget)){throw std::runtime_error("Foreign SDK network class budget");}}
   }
   const int protocolMask=(!includeUploadProtocol?1:0)|(!includeDownloadProtocol?2:0);
   if((protocolMask||supportsOriginalProtocol())&&value->global->setProtocolPolicy(static_cast<lt::aux::session_interface*>(&sdk),protocolMask)!=0){throw std::runtime_error("SDK protocol quota policy refused");}
   if(value->global->bind(static_cast<lt::aux::session_interface*>(&sdk),static_cast<std::uint32_t>(lt::session_handle::global_peer_class_id))!=0
       ||value->lan->bind(static_cast<lt::aux::session_interface*>(&sdk),static_cast<std::uint32_t>(lt::session_handle::local_peer_class_id))!=0){throw std::runtime_error("SDK common network quota binding refused");}
  });
  if(!bound){return std::unexpected(bound.error());}
  return std::unique_ptr<SdkNetworkRates>(new SdkNetworkRates(std::move(value)));
 }catch(...){return std::unexpected(Error::Engine);}
}
std::expected<void,Error> SdkNetworkRates::refresh(){
 const auto value=impl_;return value->invoke([value](lt::aux::session_impl& sdk){value->notify(sdk);});
}
std::expected<void,Error> SdkNetworkRates::configureProtocol(bool includeUpload,bool includeDownload){
 if(!supportsOriginalProtocol()){return std::unexpected(Error::Unsupported);}
 const auto value=impl_;const int mask=(!includeUpload?1:0)|(!includeDownload?2:0);
 return value->invoke([value,mask](lt::aux::session_impl& sdk){
  if(value->global->setProtocolPolicy(static_cast<lt::aux::session_interface*>(&sdk),mask)!=0){throw std::runtime_error("SDK protocol policy acknowledgement failed");}
  value->notify(sdk);
 });
}
bool SdkNetworkRates::supportsOriginalProtocol()noexcept{return SdkSharedTagBudget::supportsProtocolPolicy();}
#else
struct SdkNetworkRates::Impl {};
SdkNetworkRates::SdkNetworkRates(std::shared_ptr<Impl> value):impl_(std::move(value)){}
SdkNetworkRates::~SdkNetworkRates()=default;
std::expected<std::unique_ptr<SdkNetworkRates>,Error> SdkNetworkRates::create(
    lt::session_handle session,std::shared_ptr<SdkSharedTagBudget> global,std::shared_ptr<SdkSharedTagBudget> lan,bool includeUploadProtocol,bool includeDownloadProtocol){
 (void)session;(void)global;(void)lan;(void)includeUploadProtocol;(void)includeDownloadProtocol;return std::unexpected(Error::Unsupported);
}
std::expected<void,Error> SdkNetworkRates::refresh(){return std::unexpected(Error::Unsupported);}
std::expected<void,Error> SdkNetworkRates::configureProtocol(bool,bool){return std::unexpected(Error::Unsupported);}
bool SdkNetworkRates::supportsOriginalProtocol()noexcept{return false;}
#endif
}

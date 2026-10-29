#pragma once
#include "biglybtc/queue_observations.hpp"
#include "original_torrent_identity.hpp"
#include "persistent_runtime_guard.hpp"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#if defined(__linux__) && __has_include(<libtorrent/biglybtc_tracker_response.hpp>) && !defined(TORRENT_DISABLE_EXTENSIONS)
#include <libtorrent/biglybtc_tracker_response.hpp>
#include <libtorrent/extensions.hpp>
#include <dlfcn.h>
#define BIGLYBTC_TRACKER_RESPONSE_BRIDGE 1
#else
#define BIGLYBTC_TRACKER_RESPONSE_BRIDGE 0
#endif
namespace biglybtc::engine::detail {
// Full subject, selected/full identity, URL and exact held profile directory.
// The reader and BEFORE-activation attachment compute the same value.
inline std::optional<std::string> originalTrackerResponseKey(const storage::ProfileLease& lease,
    const storage::OriginalTorrentIdentity& identity,std::string_view rawInfo,std::string_view url){
  struct stat profile{};if(!lease.valid()||::fstat(lease.directoryHandle(),&profile)!=0||!S_ISDIR(profile.st_mode)
      ||identity.originalKey.bytes().size()!=20||rawInfo.empty()||url.empty()||url.size()>2048){return {};}
  std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!ctx||EVP_DigestInit_ex(ctx.get(),EVP_sha256(),nullptr)!=1){return {};}
  const auto part=[&](std::string_view bytes){const auto length=std::to_string(bytes.size())+":";
    return EVP_DigestUpdate(ctx.get(),length.data(),length.size())==1&&EVP_DigestUpdate(ctx.get(),bytes.data(),bytes.size())==1;};
  if(!part(std::to_string(profile.st_dev))||!part(std::to_string(profile.st_ino))||!part(identity.originalKey.hex())
      ||!part(identity.selected==storage::OriginalSelectedHash::V1?"v1":"v2")||!part(rawInfo)||!part(url)){return {};}
  for(const auto value:identity.full.v1.value_or(std::array<std::byte,20>{})){const auto byte=static_cast<unsigned char>(value);if(EVP_DigestUpdate(ctx.get(),&byte,1)!=1){return {};}}
  for(const auto value:identity.full.v2.value_or(std::array<std::byte,32>{})){const auto byte=static_cast<unsigned char>(value);if(EVP_DigestUpdate(ctx.get(),&byte,1)!=1){return {};}}
  std::array<unsigned char,32> digest{};unsigned size=0;if(EVP_DigestFinal_ex(ctx.get(),digest.data(),&size)!=1||size!=digest.size()){return {};}
  constexpr char hex[]="0123456789abcdef";std::string key;key.reserve(64);for(const auto byte:digest){key+=hex[byte>>4];key+=hex[byte&15];}return identity.originalKey.hex()+":"+key;
}
inline std::optional<std::array<unsigned char,32>> originalTrackerUrlDigest(std::string_view url){
  std::array<unsigned char,32> out{};unsigned size=0;if(EVP_Digest(url.data(),url.size(),out.data(),&size,EVP_sha256(),nullptr)!=1||size!=out.size()){return {};}return out;
}
inline std::optional<std::string> singleEffectiveTracker(const libtorrent::add_torrent_params& params){
  std::optional<std::string> one;bool valid=true;
  const auto take=[&](std::string_view url){if(url.empty()){return;}if(url.size()>2048||(!url.starts_with("http://")&&!url.starts_with("https://")&&!url.starts_with("udp://"))||(one&&*one!=url)){valid=false;return;}one=std::string(url);};
  for(const auto& url:params.trackers){take(url);}
  if(params.ti&&!bool(params.flags&libtorrent::torrent_flags::override_trackers)){for(const auto& tracker:params.ti->trackers()){take(tracker.url);}}
  return valid?one:std::nullopt;
}
// An immutable before-activation inventory. This binds callback URLs only;
// it does not identify the announcer's current/best URL or grant scrape rights.
inline std::optional<std::vector<std::string>> effectiveTrackerResponseUrls(const libtorrent::add_torrent_params& params){
  std::vector<std::string> urls;bool valid=true;
  const auto take=[&](std::string_view url){
    if(url.empty()){return;}
    if(url.size()>2048||(!url.starts_with("http://")&&!url.starts_with("https://")&&!url.starts_with("udp://"))){valid=false;return;}
    if(std::ranges::find(urls,url)==urls.end()){
      if(urls.size()==8){valid=false;return;}urls.emplace_back(url);
    }
  };
  for(const auto& url:params.trackers){take(url);}
  if(params.ti&&!bool(params.flags&libtorrent::torrent_flags::override_trackers)){for(const auto& tracker:params.ti->trackers()){take(tracker.url);}}
  return valid&&!urls.empty()?std::optional{std::move(urls)}:std::nullopt;
}
inline std::optional<std::vector<std::string>> singleTierUdpResponseUrls(const libtorrent::add_torrent_params& params){
  auto urls=effectiveTrackerResponseUrls(params);
  if(!urls||urls->size()<2||std::ranges::any_of(*urls,[](const auto& url){return !url.starts_with("udp://");})
      ||(!params.tracker_tiers.empty()&&params.tracker_tiers.size()!=params.trackers.size())
      ||std::ranges::any_of(params.tracker_tiers,[](int tier){return tier!=0;})){return {};}
  if(params.ti&&!bool(params.flags&libtorrent::torrent_flags::override_trackers)
      &&std::ranges::any_of(params.ti->trackers(),[](const auto& tracker){return tracker.tier!=0;})){return {};}
  return urls;
}
class ManagedTrackerResponse final {
public:
  struct Value {std::uint64_t admission=0,sequence=0,request=0;unsigned protocol=0;
    std::array<unsigned char,32> tracker{};std::int64_t requestStartedNanos=0,observedNanos=0;core::QueueTrackerTerminal terminal;};
  struct Binding {std::string url,responseKey;std::array<unsigned char,32> tracker{};
    std::shared_ptr<core::QueueOriginalTrackerResponse> response;std::optional<Value> latest;};
  struct UrlTerminal {std::string url;core::QueueOriginalScrapeResponse response;Value terminal;};
  using AnnounceDeadline=std::optional<std::int64_t>(*)(const core::QueueTrackerTerminal&);
  static constexpr std::uint64_t Capacity=256;
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free&&std::atomic_bool::is_always_lock_free);
  const std::uint64_t admission;const unsigned protocol;const std::array<unsigned char,32> tracker;
  const std::shared_ptr<core::QueueOriginalTrackerResponse> response;
  std::atomic_bool installed{false},lost{false};
  ManagedTrackerResponse(std::uint64_t id,unsigned version,std::array<unsigned char,32> url,
      std::shared_ptr<core::QueueOriginalTrackerResponse> value,AnnounceDeadline deadline=nullptr)
      :admission(id),protocol(version),tracker(url),response(std::move(value)),deadline_(deadline),worker_(std::this_thread::get_id()){}
  ManagedTrackerResponse(std::uint64_t id,unsigned version,std::vector<Binding> bindings,AnnounceDeadline deadline=nullptr)
      :admission(id),protocol(version),tracker(bindings.empty()?std::array<unsigned char,32>{}:bindings.front().tracker),
       response(bindings.empty()?nullptr:bindings.front().response),deadline_(deadline),worker_(std::this_thread::get_id()),bindings_(std::move(bindings)){
    if(bindings_.empty()||bindings_.size()>8||!response){lost=true;return;}
    for(std::size_t i=0;i<bindings_.size();++i){const auto& value=bindings_[i];
      if(value.url.empty()||value.responseKey.empty()||!value.response){lost=true;return;}
      for(std::size_t j=0;j<i;++j){if(bindings_[j].tracker==value.tracker||bindings_[j].responseKey==value.responseKey){lost=true;return;}}
    }
  }
  bool sole()const noexcept{return bindings_.size()<=1;}
  // Collected BEFORE the sole pop_alerts call. A callback written after this
  // cut cannot be selected until a later collection has delivered its dispatch.
  void beginDispatchCollection()noexcept{
    if(std::this_thread::get_id()!=worker_){return;}dispatchCut_=written_.load(std::memory_order_acquire);
  }
  void loseDispatch()noexcept{dispatchLost_=true;currentDispatch_.reset();}
  void observeUdpDispatch(std::string_view url,unsigned version,std::int64_t alertStamp)noexcept{
    if(std::this_thread::get_id()!=worker_||sole()||dispatchLost_||version!=protocol){return;}
    if(alertStamp<=0||alertStamp<lastDispatchStamp_||dispatchGeneration_==UINT64_MAX){loseDispatch();return;}
    const auto found=std::ranges::find(bindings_,url,&Binding::url);
    if(found==bindings_.end()||!url.starts_with("udp://")){loseDispatch();return;}
    lastDispatchStamp_=alertStamp;++dispatchGeneration_;currentDispatch_=found->tracker;
  }
  // The sole original helper's current URL comes ONLY from dispatch alerts,
  // never from the newest response. History for a different URL stays separate.
  std::optional<UrlTerminal> currentUdpTerminal()const{
    if(std::this_thread::get_id()!=worker_||sole()||dispatchLost_||!currentDispatch_
        ||!installed.load(std::memory_order_acquire)||lost.load(std::memory_order_acquire)
        ||read_.load(std::memory_order_acquire)!=dispatchCut_||written_.load(std::memory_order_acquire)!=dispatchCut_){return {};}
    const auto found=std::ranges::find(bindings_,*currentDispatch_,&Binding::tracker);
    if(found==bindings_.end()||!found->latest){return {};}
    return UrlTerminal{found->url,found->response->response(),*found->latest};
  }
  void cancelRequests()noexcept{
    if(bindings_.empty()){if(response){response->cancelRequest();}return;}
    for(auto& value:bindings_){if(value.response){value.response->cancelRequest();}}
  }
  // Worker-only owned copies of actual response-changing terminals. There is
  // deliberately no selected/best response in this multi-URL observation API.
  std::vector<UrlTerminal> urlTerminals()const{
    if(std::this_thread::get_id()!=worker_||!installed.load(std::memory_order_acquire)||lost.load(std::memory_order_acquire)){return {};}
    std::vector<UrlTerminal> result;
    for(const auto& value:bindings_){if(value.latest){result.push_back({value.url,value.response->response(),*value.latest});}}
    return result;
  }
  void observe(const Value& value)noexcept{
    if(lost.load(std::memory_order_acquire)){return;}const auto end=written_.load(std::memory_order_relaxed),begin=read_.load(std::memory_order_acquire);
    if(end==UINT64_MAX||end-begin>=Capacity){lost.store(true,std::memory_order_release);return;}events_[end%Capacity]=value;written_.store(end+1,std::memory_order_release);
  }
  bool drain()noexcept{
    if(std::this_thread::get_id()!=worker_||!installed.load(std::memory_order_acquire)){return false;}
    if(lost.load(std::memory_order_acquire)){unavailable();return false;}
    auto begin=read_.load(std::memory_order_relaxed);const auto end=written_.load(std::memory_order_acquire);
    while(begin<end){const auto& value=events_[begin%Capacity];++begin;
      if(previous_==UINT64_MAX||value.sequence!=previous_+1||value.admission!=admission
          ||!value.request||value.observedNanos<lastNanos_||value.requestStartedNanos<=0||value.requestStartedNanos>value.observedNanos){lost=true;break;}
      previous_=value.sequence;lastNanos_=value.observedNanos;
      if(value.protocol!=protocol){continue;}
      auto* destination=response.get();auto* latest=&latest_;
      if(!bindings_.empty()){
        const auto found=std::ranges::find(bindings_,value.tracker,&Binding::tracker);
        if(found==bindings_.end()){lost=true;break;}destination=found->response.get();latest=&found->latest;
      }else if(value.tracker!=tracker){lost=true;break;}
      const bool accepted=(deadline_||!sole())?destination->observeChronological(value.terminal,value.observedNanos):(destination->observe(value.terminal),true);
      // Projection runs only on the existing worker drain, for every accepted
      // terminal in order. It cannot supply counts, epochs or network rights.
      if(deadline_){try{if(const auto due=deadline_(value.terminal)){if(destination->announceTimingCurrent(value.observedNanos)&&!destination->deferRegularUntil(*due,value.observedNanos)){lost=true;break;}if(accepted){*latest=value;}}}
        catch(...){lost=true;break;}}
      if(accepted&&(value.terminal.kind==core::QueueTrackerTerminalKind::Scrape||value.terminal.kind==core::QueueTrackerTerminalKind::ScrapeFailed)
          &&(value.terminal.comparable||value.terminal.definitiveFailure)){*latest=value;}
      if(!sole()&&accepted&&value.terminal.kind==core::QueueTrackerTerminalKind::Announce){*latest=value;}
    }
    read_.store(begin,std::memory_order_release);if(lost.load(std::memory_order_acquire)){unavailable();return false;}return true;
  }
  // Last actual response-changing selected terminal; never drains or samples.
  std::optional<Value> latestTerminal()const noexcept{
    if(std::this_thread::get_id()!=worker_||!sole()||!installed.load(std::memory_order_acquire)||lost.load(std::memory_order_acquire)){return {};}return bindings_.empty()?latest_:bindings_.front().latest;
  }
private:
  void unavailable()noexcept{
    if(bindings_.empty()){if(response){response->unavailable();}return;}
    for(auto& value:bindings_){if(value.response){value.response->unavailable();}}
  }
  std::optional<Value> latest_;const AnnounceDeadline deadline_;const std::thread::id worker_;std::array<Value,Capacity> events_{};std::atomic<std::uint64_t> written_{0},read_{0};
  std::vector<Binding> bindings_;std::uint64_t dispatchCut_=0,dispatchGeneration_=0;
  std::int64_t lastDispatchStamp_=0;bool dispatchLost_=false;std::optional<std::array<unsigned char,32>> currentDispatch_;
  std::uint64_t previous_=0;std::int64_t lastNanos_=0;
};
using LibraryTrackerResponses=std::map<std::string,std::shared_ptr<core::QueueOriginalTrackerResponse>>;
inline std::shared_ptr<ManagedTrackerResponse> attachManagedTrackerResponse(libtorrent::add_torrent_params& params,
    const storage::ProfileLease& lease,const storage::OriginalTorrentIdentity& identity,std::uint64_t admission,LibraryTrackerResponses& library,ManagedTrackerResponse::AnnounceDeadline deadline=nullptr)noexcept{
#if BIGLYBTC_TRACKER_RESPONSE_BRIDGE
  namespace lt=libtorrent;
  try{
    const auto urls=effectiveTrackerResponseUrls(params);if(!urls||!params.ti||!admission){return {};}
    const auto section=params.ti->info_section();std::vector<ManagedTrackerResponse::Binding> bindings;
    LibraryTrackerResponses additions;
    for(const auto& url:*urls){
      const auto key=originalTrackerResponseKey(lease,identity,{section.data(),static_cast<std::size_t>(section.size())},url);
      const auto digest=originalTrackerUrlDigest(url);if(!key||!digest){return {};}
      const auto found=library.find(*key);auto response=found==library.end()?std::make_shared<core::QueueOriginalTrackerResponse>():found->second;
      if(found==library.end()){additions.emplace(*key,response);}
      bindings.push_back({url,*key,*digest,std::move(response),{}});
    }
    if(additions.size()>256||library.size()>256-additions.size()){return {};}
    auto pin=managedTrackerResponseRuntimePin();if(!pin){return {};}
    const auto abi=reinterpret_cast<decltype(&lt::biglybtc_tracker_response_abi_v1)>(::dlsym(pin.get(),"biglybtc_tracker_response_abi_v1"));
    const auto attach=reinterpret_cast<decltype(&lt::biglybtc_attach_tracker_response_observer_v1)>(::dlsym(pin.get(),"biglybtc_attach_tracker_response_observer_v1"));
    if(!abi||!attach||reinterpret_cast<void*>(abi)!=::dlsym(RTLD_DEFAULT,"biglybtc_tracker_response_abi_v1")
        ||reinterpret_cast<void*>(attach)!=::dlsym(RTLD_DEFAULT,"biglybtc_attach_tracker_response_observer_v1")||abi()!=1){return {};}
    auto ring=std::make_shared<ManagedTrackerResponse>(admission,identity.selected==storage::OriginalSelectedHash::V1?1u:2u,std::move(bindings),deadline);
    if(ring->lost.load(std::memory_order_acquire)){return {};}
    library.merge(additions);
    struct Sink final:lt::BiglybtcTrackerResponseObserverV1 {
      std::shared_ptr<void> pin;std::shared_ptr<ManagedTrackerResponse> ring;
      Sink(std::shared_ptr<void> p,std::shared_ptr<ManagedTrackerResponse> r):pin(std::move(p)),ring(std::move(r)){}
      void observe(const lt::BiglybtcTrackerResponseEventV1& event)noexcept override{
        ManagedTrackerResponse::Value value;value.admission=event.admission;value.sequence=event.sequence;value.request=event.request;value.protocol=event.protocol;value.tracker=event.tracker_sha256;
        value.requestStartedNanos=event.request_started_nanoseconds;value.observedNanos=event.observed_nanoseconds;
        const auto kind=static_cast<unsigned>(event.kind);if(kind<1||kind>4){ring->lost=true;return;}
        auto& target=value.terminal;target.kind=static_cast<core::QueueTrackerTerminalKind>(kind-1);
        target.complete=event.fields.complete;target.incomplete=event.fields.incomplete;target.downloaded=event.fields.downloaded;target.present=event.fields.present;
        target.comparable=event.fields.meaning==lt::BiglybtcTrackerResponseMeaningV1::OriginalHttpFields||event.fields.meaning==lt::BiglybtcTrackerResponseMeaningV1::OriginalUdpFields;target.definitiveFailure=event.fields.meaning==lt::BiglybtcTrackerResponseMeaningV1::DefinitiveFailure;
        target.observedMillis=event.observed_nanoseconds/1000000;target.observedWallMillis=event.observed_wall_milliseconds;target.requestStartedWallMillis=event.request_started_wall_milliseconds;
        ring->observe(value);
      }
    };
    std::shared_ptr<lt::BiglybtcTrackerResponseObserverV1> sink=std::make_shared<Sink>(pin,ring);
    params.extensions.push_back([ring,sink,attach](const lt::torrent_handle& handle,lt::client_data_t)->std::shared_ptr<lt::torrent_plugin>{
      if(attach(&handle,&sink,ring->admission)==static_cast<std::uint32_t>(lt::BiglybtcTrackerResponseStatusV1::Accepted)){ring->installed.store(true,std::memory_order_release);}
      else{ring->lost=true;ring->installed.store(true,std::memory_order_release);}return {};
    });return ring;
  }catch(...){return {};}
#else
  (void)params;(void)lease;(void)identity;(void)admission;(void)library;(void)deadline;return {};
#endif
}
} // namespace biglybtc::engine::detail

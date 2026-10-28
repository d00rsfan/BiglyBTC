#pragma once
#include "biglybtc/queue_observations.hpp"
#include "original_torrent_identity.hpp"
#include "persistent_runtime_guard.hpp"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <openssl/evp.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
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
class ManagedTrackerResponse final {
public:
  struct Value {std::uint64_t admission=0,sequence=0,request=0;unsigned protocol=0;
    std::array<unsigned char,32> tracker{};std::int64_t requestStartedNanos=0,observedNanos=0;core::QueueTrackerTerminal terminal;};
  static constexpr std::uint64_t Capacity=256;
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free&&std::atomic_bool::is_always_lock_free);
  const std::uint64_t admission;const unsigned protocol;const std::array<unsigned char,32> tracker;
  const std::shared_ptr<core::QueueOriginalTrackerResponse> response;
  std::atomic_bool installed{false},lost{false};
  ManagedTrackerResponse(std::uint64_t id,unsigned version,std::array<unsigned char,32> url,
      std::shared_ptr<core::QueueOriginalTrackerResponse> value)
      :admission(id),protocol(version),tracker(url),response(std::move(value)),worker_(std::this_thread::get_id()){}
  void observe(const Value& value)noexcept{
    if(lost.load(std::memory_order_acquire)){return;}const auto end=written_.load(std::memory_order_relaxed),begin=read_.load(std::memory_order_acquire);
    if(end==UINT64_MAX||end-begin>=Capacity){lost.store(true,std::memory_order_release);return;}events_[end%Capacity]=value;written_.store(end+1,std::memory_order_release);
  }
  bool drain()noexcept{
    if(std::this_thread::get_id()!=worker_||!installed.load(std::memory_order_acquire)){return false;}
    if(lost.load(std::memory_order_acquire)){response->unavailable();return false;}
    auto begin=read_.load(std::memory_order_relaxed);const auto end=written_.load(std::memory_order_acquire);
    while(begin<end){const auto& value=events_[begin%Capacity];++begin;
      if(previous_==UINT64_MAX||value.sequence!=previous_+1||value.admission!=admission
          ||!value.request||value.observedNanos<lastNanos_||value.requestStartedNanos<=0||value.requestStartedNanos>value.observedNanos){lost=true;break;}
      previous_=value.sequence;lastNanos_=value.observedNanos;
      if(value.protocol!=protocol){continue;}
      if(value.tracker!=tracker){lost=true;break;}response->observe(value.terminal);
    }
    read_.store(begin,std::memory_order_release);if(lost.load(std::memory_order_acquire)){response->unavailable();return false;}return true;
  }
private:
  const std::thread::id worker_;std::array<Value,Capacity> events_{};std::atomic<std::uint64_t> written_{0},read_{0};
  std::uint64_t previous_=0;std::int64_t lastNanos_=0;
};
using LibraryTrackerResponses=std::map<std::string,std::shared_ptr<core::QueueOriginalTrackerResponse>>;
inline std::shared_ptr<ManagedTrackerResponse> attachManagedTrackerResponse(libtorrent::add_torrent_params& params,
    const storage::ProfileLease& lease,const storage::OriginalTorrentIdentity& identity,std::uint64_t admission,LibraryTrackerResponses& library)noexcept{
#if BIGLYBTC_TRACKER_RESPONSE_BRIDGE
  namespace lt=libtorrent;
  try{
    const auto url=singleEffectiveTracker(params);if(!url||!params.ti||!admission){return {};}
    const auto section=params.ti->info_section();const auto key=originalTrackerResponseKey(lease,identity,{section.data(),static_cast<std::size_t>(section.size())},*url);
    const auto digest=originalTrackerUrlDigest(*url);if(!key||!digest){return {};}
    auto pin=managedTrackerResponseRuntimePin();if(!pin){return {};}
    const auto abi=reinterpret_cast<decltype(&lt::biglybtc_tracker_response_abi_v1)>(::dlsym(pin.get(),"biglybtc_tracker_response_abi_v1"));
    const auto attach=reinterpret_cast<decltype(&lt::biglybtc_attach_tracker_response_observer_v1)>(::dlsym(pin.get(),"biglybtc_attach_tracker_response_observer_v1"));
    if(!abi||!attach||reinterpret_cast<void*>(abi)!=::dlsym(RTLD_DEFAULT,"biglybtc_tracker_response_abi_v1")
        ||reinterpret_cast<void*>(attach)!=::dlsym(RTLD_DEFAULT,"biglybtc_attach_tracker_response_observer_v1")||abi()!=1){return {};}
    auto row=library.find(*key);if(row==library.end()){
      if(library.size()==256){return {};}row=library.emplace(*key,std::make_shared<core::QueueOriginalTrackerResponse>()).first;
    }
    auto ring=std::make_shared<ManagedTrackerResponse>(admission,identity.selected==storage::OriginalSelectedHash::V1?1u:2u,*digest,row->second);
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
  (void)params;(void)lease;(void)identity;(void)admission;(void)library;return {};
#endif
}
} // namespace biglybtc::engine::detail

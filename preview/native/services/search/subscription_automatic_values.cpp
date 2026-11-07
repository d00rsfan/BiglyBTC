// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_automatic_values.hpp"
#include "biglybtc/bencode.hpp"
#include "biglybtc/torrent_layout.hpp"
#include "biglybtc/torrent_intake_options.hpp"
#include <bit>
#include <filesystem>
#include <new>
#include <set>

namespace biglybtc::search::detail {
namespace {
using V=SubscriptionAutomaticValueError;
struct Failure {V error;};
void require(bool ok,V error){if(!ok){throw Failure{error};}}
std::int32_t integer(const core::Document& doc,std::string_view key,std::int32_t fallback){
  const auto* value=doc.find(key);if(!value){return fallback;}
  require(value->type==core::Node::Type::Integer,V::Malformed);
  // ConfigurationManager.getIntParameter uses Long.intValue(), then global
  // boolean parameters test the resulting int !=0 (not history's Long==1).
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value->integer));
}
std::string path(std::string_view value){
  require(!value.empty(),V::Unsupported);require(value.size()<=4096,V::Limit);
  require(value.find('\0')==std::string_view::npos,V::Malformed);
  std::filesystem::path result(value);require(result.is_absolute(),V::Unsupported);
  for(const auto& part:result){const auto text=part.string();if(text=="/"||text=="."||text.empty()){continue;}
    require(text!=".."&&core::validTorrentIntakeSaveName(text),V::Unsupported);}
  result=result.lexically_normal();if(result!=result.root_path()&&result.filename().empty()){result=result.parent_path();}
  return result.string();
}
std::string directory(const core::Document& doc,std::string_view key,std::string_view fallback){
  const auto* value=doc.find(key);if(!value){return path(fallback);}
  require(value->type==core::Node::Type::Bytes,V::Malformed);return path(value->bytes);
}
std::string joined(std::string_view parent,std::string_view leaf){
  require(core::validTorrentIntakeSaveName(leaf),V::Unsupported);
  auto value=(std::filesystem::path(parent)/leaf).string();require(value.size()<=4096,V::Limit);return value;
}
}
std::expected<SubscriptionAutomaticPolicyValues,V> readSubscriptionAutomaticPolicy(
    const core::Document& settings,std::string_view actualProfileDirectory){try{
  require(settings.root().type==core::Node::Type::Dictionary,V::Malformed);
  require(settings.originalBytes().size()<=1024*1024,V::Limit);
  std::set<std::string_view> keys;for(std::size_t i=0;i<settings.root().children.size();i+=2){
    require(i+1<settings.root().children.size()&&settings.root().children[i].type==core::Node::Type::Bytes,V::Malformed);
    require(keys.insert(settings.root().children[i].bytes).second,V::Malformed);}
  SubscriptionAutomaticPolicyValues out;
  out.autoStart=integer(settings,"subscriptions.auto.start.downloads",1)!=0;
  out.defaultStartStopped=integer(settings,"Default Start Torrents Stopped",0)!=0;
  out.defaultStartAutoPause=integer(settings,"Default Start Torrents Stopped Auto Pause",0)!=0;
  out.minStartMiB=integer(settings,"subscriptions.auto.start.min.mb",0);
  out.maxStartMiB=integer(settings,"subscriptions.auto.start.max.mb",0);
  out.addHashNames=integer(settings,"subscriptions.auto.dl.add.hashes",0)!=0;
  out.markReadAfterDays=integer(settings,"subscriptions.auto.dl.mark.read.days",0);
  require(out.markReadAfterDays<=0,V::Unsupported);
  out.saveTorrentFiles=integer(settings,"Save Torrent Files",1)!=0;
  require(out.saveTorrentFiles,V::Unsupported);
  out.deleteSavedTorrentFiles=integer(settings,"Delete Saved Torrent Files",0)!=0;
  require(!out.deleteSavedTorrentFiles,V::Unsupported);
  // Deliberate open compatibility row: Original derives a platform-specific
  // missing data directory. Native has no qualified resolver for that default.
  out.defaultSaveDirectory=directory(settings,"Default save path",{});
  const auto defaultTorrentDirectory=joined(path(actualProfileDirectory),"torrents");
  out.torrentSaveDirectory=directory(settings,"General_sDefaultTorrent_Directory",defaultTorrentDirectory);
  return out;
}catch(const Failure& error){return std::unexpected(error.error);}
 catch(const std::bad_alloc&){return std::unexpected(V::Memory);}
 catch(const std::filesystem::filesystem_error&){return std::unexpected(V::Unsupported);}}
bool subscriptionAutomaticShouldStart(const SubscriptionAutomaticPolicyValues& policy,std::int64_t size)noexcept{
  // Original left-associative int*1024*1024L: first multiply wraps int32.
  const auto bytes=[](std::int32_t mb){const auto blocks=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::uint32_t>(mb))*1024));return static_cast<std::int64_t>(blocks)*1024;};
  const auto min=bytes(policy.minStartMiB),max=bytes(policy.maxStartMiB);
  return policy.autoStart&&!(min>0&&size<min)&&!(max>0&&size>max);
}
std::expected<SubscriptionAutomaticGeometry,V> subscriptionAutomaticGeometry(
    const SubscriptionAutomaticPolicyValues& policy,std::string_view name,
    const std::array<std::byte,20>& hash,bool multifile,std::int64_t size,
    std::string_view conversions,AutomaticDataLocationKind kind){try{
  require(size>=0&&policy.saveTorrentFiles,V::Unsupported);
  require(name.size()<=4096,V::Limit);require(core::validTorrentIntakeSaveName(name),V::Unsupported);
  const auto base=path(policy.defaultSaveDirectory),torrentDirectory=path(policy.torrentSaveDirectory);
  auto twice=compat::originalFilenameComponent(name,conversions);require(bool(twice),V::Unsupported);
  require(core::validTorrentIntakeSaveName(*twice),V::Unsupported);
  std::string suffix;constexpr char Hex[]="0123456789ABCDEF";
  if(policy.addHashNames){suffix='_';for(std::size_t i=0;i<4;++i){const auto value=std::to_integer<unsigned>(hash[i]);suffix+=Hex[value>>4];suffix+=Hex[value&15];}}
  const auto externalName=*twice+suffix;
  SubscriptionAutomaticGeometry out;out.multifile=multifile;out.autoStart=subscriptionAutomaticShouldStart(policy,size);out.queued=out.autoStart&&!policy.defaultStartStopped;out.paused=!out.queued&&policy.defaultStartAutoPause;
  out.torrentFile=joined(torrentDirectory,externalName+".torrent");
  out.destinationDirectory=base;out.saveName=std::string(name);
  if(policy.addHashNames){
    require(kind!=AutomaticDataLocationKind::Unsupported,V::Unsupported);
    if(multifile){require(kind!=AutomaticDataLocationKind::RegularFile,V::Unsupported);out.destinationDirectory=joined(base,externalName);}
    else if(kind==AutomaticDataLocationKind::Directory){out.destinationDirectory=joined(base,externalName);}
    else{out.saveName=externalName;}
  }else{
    require(kind==AutomaticDataLocationKind::Directory,V::Unsupported);
    // PIF ensures the ordinary Default save path directory; core avoids
    // duplicate nesting when that existing directory already has display name.
    if(multifile&&std::filesystem::path(base).filename()==name){out.destinationDirectory=std::filesystem::path(base).parent_path().string();}
  }
  out.dataLocation=joined(out.destinationDirectory,out.saveName);return out;
}catch(const Failure& error){return std::unexpected(error.error);}
 catch(const std::bad_alloc&){return std::unexpected(V::Memory);}
 catch(const std::filesystem::filesystem_error&){return std::unexpected(V::Unsupported);}}
} // namespace biglybtc::search::detail

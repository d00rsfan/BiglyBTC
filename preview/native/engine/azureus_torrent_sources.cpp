// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_torrent.hpp"
#include "file_operation_io.hpp"
#include "library_view.hpp"
#include "state_file_io.hpp"
#include "torrent_state.hpp"
#include "original_torrent_identity_lt.hpp"
#include <libtorrent/torrent_info.hpp>
#include <algorithm>
#include <map>
#include <set>
namespace biglybtc::engine {
namespace {
using storage::Error;using storage::ErrorCode;using core::Node;
std::unexpected<Error> error(const char* text){return std::unexpected(Error{ErrorCode::InvalidFormat,text});}
std::string stamp(storage::detail::StateHandle directory, const std::string& name) {
  const auto named = storage::detail::stateStampAt(directory, name);
  if (!named || !*named) { return {}; }
  const auto& snapshot = **named;
  if (!snapshot.regular || !snapshot.owned || snapshot.links != 1) { return {}; }
#ifdef _WIN32
  const auto witness = storage::detail::openStateWitness(directory, name);
  if (!witness || !storage::fileop::nonOwnerWritesBlocked(witness->get())) { return {}; }
  const auto held = storage::detail::stateStamp(witness->get());
  if (!held || !storage::detail::sameStateSnapshot(snapshot, *held)) { return {}; }
#else
  if ((snapshot.mode & 0002) != 0) { return {}; }
#endif
  return std::to_string(snapshot.device) + ':' + std::to_string(snapshot.inode)
      + ':' + std::to_string(snapshot.mode) + ':' + std::to_string(snapshot.size)
      + ':' + std::to_string(snapshot.modified.tv_sec) + ':' + std::to_string(snapshot.modified.tv_nsec)
      + ':' + std::to_string(snapshot.changed.tv_sec) + ':' + std::to_string(snapshot.changed.tv_nsec);
}
bool unique(const Node& value){if(value.type!=Node::Type::Dictionary||value.children.size()%2){return false;}std::set<std::string_view> keys;for(std::size_t i=0;i<value.children.size();i+=2){if(value.children[i].type!=Node::Type::Bytes||!keys.insert(value.children[i].bytes).second){return false;}}return true;}
std::optional<bool> boolean(const Node* node,bool fallback){if(!node){return fallback;}if(node->type!=Node::Type::Integer){return {};}return static_cast<std::uint32_t>(node->integer)!=0;}
std::optional<bool> listHas(const Node* node,std::string_view key,bool fallback){if(!node){return fallback;}if(node->type!=Node::Type::List||node->children.size()>16){return {};}
 bool found=false;for(const auto& item:node->children){if(item.type!=Node::Type::Bytes){return {};}found|=item.bytes==key;}return found;}
}
struct AzureusTorrentSources::Impl {
 std::shared_ptr<const storage::ProfileLease> lease;std::string baseStamp;std::size_t parses=0;
 std::shared_ptr<const storage::ConfigFile> config;std::optional<storage::LibrarySourceSnapshot> library;
 std::map<std::string,const storage::LibraryItem*> rows;std::set<std::string> duplicates,paused;
 std::map<std::string,const Node*> cache;
 struct Metadata {std::string stamp;bool ready=false,allowed=false,decentralised=false,requested=false;storage::OriginalFullInfoHashes full;};std::map<std::string,Metadata> metadata;
 std::expected<void,Error> refresh(){
   const auto root=lease->directoryHandle();const auto configStamp=stamp(root,"biglybt.config"),inventoryStamp=stamp(root,"downloads.config");
   if(configStamp.empty()||inventoryStamp.empty()){return error("Current primary DHT tracker settings/inventory required");}
   const auto active = storage::detail::openStateDirectory(root, "active");
   if (!active) { return error("Original active-state directory unavailable"); }
   const auto cacheStamp = stamp(active->get(), "cache.dat");
   const auto next=configStamp+'|'+inventoryStamp+'|'+cacheStamp;
   if(next==baseStamp&&config&&library){return {};}
   auto cfg=storage::ConfigFile::load(*lease,"biglybt.config");auto source=storage::readLibrarySource(*lease);
   if(!cfg||!source||cfg->recoverySource()!=storage::RecoverySource::Primary||!unique(cfg->document().root())||source->view.cacheWarning){return error("Cannot authenticate current DHT tracker source");}
   if(stamp(root,"biglybt.config")!=configStamp||stamp(root,"downloads.config")!=inventoryStamp){return error("DHT tracker source changed while reading");}
   config=std::make_shared<storage::ConfigFile>(std::move(*cfg));library=std::move(*source);rows.clear();duplicates.clear();paused.clear();cache.clear();metadata.clear();
   for(const auto& row:library->view.items){if(row.saved.hash){const auto key=row.saved.hash->hex();if(!rows.emplace(key,&row).second){duplicates.insert(key);}}}
   for(const auto& row:library->view.pauses){paused.insert(row.hash.hex());}
   if(library->cacheDocument){const auto* list=library->cacheDocument->document().find("state");if(list&&list->type==Node::Type::List){for(const auto& item:list->children){const auto* key=item.find("hash");if(key&&key->type==Node::Type::Bytes){const auto hash=core::TorrentHash::parse(key->bytes);if(hash){cache.insert_or_assign(hash->hex(),&item);}}}}}
   baseStamp=next;++parses;return {};
 }
};
AzureusTorrentSources::AzureusTorrentSources(std::unique_ptr<Impl> value):impl_(std::move(value)){}
AzureusTorrentSources::~AzureusTorrentSources()=default;
std::expected<std::unique_ptr<AzureusTorrentSources>,Error> AzureusTorrentSources::create(const storage::ProfileLease& lease){auto held=lease.retain();if(!held){return std::unexpected(held.error());}auto out=std::make_unique<Impl>();out->lease=std::move(*held);return std::unique_ptr<AzureusTorrentSources>(new AzureusTorrentSources(std::move(out)));}
std::size_t AzureusTorrentSources::parses()const noexcept{return impl_->parses;}
std::expected<bool,Error> AzureusTorrentSources::check(const AzureusTorrentTarget& target,bool withdrawing){
 if(!target.live||(!withdrawing&&!target.live->load())||!target.admission||!target.tcpPort||!target.udpPort){return false;}
 auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto fresh=impl_->refresh();if(!fresh){return std::unexpected(fresh.error());}
 const auto& config=impl_->config->document();const auto enabled=boolean(config.find("Plugin.DHT.dht.enabled"),true),defaultDht=boolean(config.find("Peer Source Selection Default.DHT"),true);
 if(!enabled||!*enabled||!defaultDht){return false;}
 const auto key=target.hash.hex();const auto row=impl_->rows.find(key);if(row==impl_->rows.end()||impl_->duplicates.contains(key)||(!withdrawing&&impl_->paused.contains(key))||!row->second->saved.persistent||(!withdrawing&&row->second->saved.startup==core::StartupState::Stopped)){return false;}
 const auto encryption=boolean(config.find("network.transport.encrypted.require"),false);if(!encryption||*encryption!=target.crypto){return false;}
 const auto* port=config.find("TCP.Listen.Port");if(port&&(port->type!=Node::Type::Integer||port->integer!=target.tcpPort)){return false;}
 const auto active = storage::detail::openStateDirectory(impl_->lease->directoryHandle(), "active");
 if (!active) { return false; }
 const auto fileStamp = stamp(active->get(), key + ".dat");
 if (fileStamp.empty()) { return false; }
 auto& memo=impl_->metadata[key];if(memo.stamp!=fileStamp||!memo.ready){
   auto store=storage::TorrentStateStore::open(*impl_->lease);if(!store){return std::unexpected(store.error());}auto state=store->load(*impl_->lease,target.hash);if(!state||state->recoveredBackup()){return false;}
   auto document=state->document();const auto cached=impl_->cache.find(key);
   if(cached!=impl_->cache.end()){for(auto [from,to]:{std::pair{"attributes","attributes"},{"azp","azureus_properties"},{"azpp","azureus_private_properties"}}){if(const auto* field=cached->second->find(from)){auto encoded=core::encodeValue(*field,storage::compatibleConfigLimits());if(field->type!=Node::Type::Dictionary||!encoded||!document.replaceValue(to,std::move(*encoded))){return false;}}}}
   const auto* subject=document.find("info");if(!unique(document.root())||!subject||!unique(*subject)){return false;}
   libtorrent::error_code ec;const auto raw=document.serialize();libtorrent::torrent_info info(libtorrent::span<const char>(raw.data(),raw.size()),ec,libtorrent::from_span);if(ec){return false;}
   const auto identity=storage::originalTorrentIdentityFromLibtorrent(document,info);
   if(!identity||!storage::validateOriginalTorrentKey(*identity,target.hash,true)||identity->full!=target.full||info.priv()){return false;}
   const auto* attributes=document.find("attributes"),*properties=document.find("azureus_properties");
   if((attributes&&!unique(*attributes))||(properties&&!unique(*properties))){return false;}
   const auto publicNetwork=listHas(attributes?attributes->find("networks"):nullptr,"Public",true),dht=listHas(attributes?attributes->find("peersources"):nullptr,"DHT",*defaultDht),denied=listHas(attributes?attributes->find("peersourcesdenied"):nullptr,"DHT",false);
   const auto* announce=document.find("announce");const bool decentralised=announce&&announce->type==Node::Type::Bytes&&announce->bytes.starts_with("dht:");
   const auto backup=boolean(properties?properties->find("dht_backup_enable"):nullptr,true),requested=boolean(properties?properties->find("dht_backup_requested"):nullptr,false);
   memo.allowed=publicNetwork&&*publicNetwork&&dht&&denied&&backup&&requested
       &&(decentralised||(*backup&&*dht&&!*denied));
   memo.decentralised=decentralised;memo.requested=requested&&*requested;memo.full=identity->full;memo.ready=true;memo.stamp=fileStamp;++impl_->parses;
 }
 if(memo.full!=target.full||!memo.allowed){return false;}
 if(withdrawing||memo.decentralised||memo.requested){return true;}
 const auto offline=boolean(config.find("Plugin.DHT Tracker.dhttracker.tracknormalwhenoffline"),true),limited=boolean(config.find("Plugin.DHT Tracker.dhttracker.tracklimitedwhenonline"),true);
 if(!offline||!limited){return false;}if(!*offline||!target.trackerHealthy){return true;}
 return *limited&&target.swarmSize&&(*target.swarmSize<=16||target.selection%*target.swarmSize<16);
}
}

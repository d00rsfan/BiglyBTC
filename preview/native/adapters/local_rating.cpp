// SPDX-License-Identifier: GPL-2.0-or-later
#include "local_rating.hpp"
#include "original_torrent_identity.hpp"
#include "torrent_state.hpp"
#include "file_operations.hpp"
#include "linear_to_compact.hpp"
#include <charconv>
#include <cmath>
#include <openssl/evp.h>
#include <algorithm>
#include <limits>
#include <new>
#include <unordered_set>

namespace biglybtc::storage {
namespace {
namespace rating = plugins::rating;
constexpr std::string_view ScoreKey = "Plugin.azrating.rating";
constexpr std::string_view CommentKey = "Plugin.azrating.comment";
std::unexpected<Error> bad(ErrorCode code, const char* message) { return std::unexpected(Error{code,message}); }
std::string encoded(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::expected<ConfigFile, Error> savedTarget(const ProfileLease& lease, const core::TorrentHash& hash) {
  auto file = ConfigFile::load(lease, "downloads.config");
  if (!file) { return std::unexpected(file.error()); }
  if (file->recoverySource() == RecoverySource::Saving) { return bad(ErrorCode::Conflict,"Resolve saved library recovery before editing a rating"); }
  auto library = core::readLibrary(file->document());
  if (!library) { return bad(ErrorCode::InvalidFormat,"Invalid saved library"); }
  for (const auto& issue : library->issues) {
    if (issue.pauseEntry || !library->legacyMap) { return bad(ErrorCode::InvalidFormat,"Invalid saved library entry"); }
    const auto& value=file->document().root().children.at(issue.sourceIndex*2+1);
    const auto key=file->document().root().children.at(issue.sourceIndex*2).bytes;
    if (key!="pause_data" && (value.find("torrent") || value.find("torrent_hash") || value.find("save_dir") || value.find("path"))) {
      return bad(ErrorCode::InvalidFormat,"Malformed legacy saved download");
    }
  }
  std::unordered_set<std::string> seen;
  bool found = false;
  for (const auto& item : library->downloads) {
    if (!item.hash || !seen.insert(item.hash->hex()).second) { return bad(ErrorCode::Conflict,"Missing or duplicate saved download identity"); }
    found |= *item.hash == hash;
  }
  if (!found) { return bad(ErrorCode::Conflict,"Rating download is no longer in the saved library"); }
  return std::move(*file);
}
template<std::size_t N>
std::expected<std::array<std::byte,N>, Error> hashInfo(std::string_view raw, const EVP_MD* method) {
  std::array<std::byte,N> result{}; unsigned length = 0;
  if (EVP_Digest(raw.data(),raw.size(),reinterpret_cast<unsigned char*>(result.data()),&length,method,nullptr) != 1 || length != N) {
    return bad(ErrorCode::Io,"Unable to authenticate original torrent metadata");
  }
  return result;
}
std::expected<void, Error> identity(const TorrentStateFile& state) {
  auto raw = originalTorrentInfoSubject(state.document());
  if (!raw) { return std::unexpected(raw.error()); }
  const auto* info = state.document().find("info");
  const auto* version = info->find("meta version");
  const bool v2 = version && version->type == core::Node::Type::Integer && version->integer == 2;
  OriginalFullInfoHashes hashes;
  if (!v2 || info->find("pieces")) {
    auto value = hashInfo<20>(*raw,EVP_sha1()); if (!value) { return std::unexpected(value.error()); } hashes.v1 = *value;
  }
  if (v2) {
    auto value = hashInfo<32>(*raw,EVP_sha256()); if (!value) { return std::unexpected(value.error()); } hashes.v2 = *value;
  }
  auto selected = selectOriginalTorrentIdentity(state.document(), {*raw,hashes});
  if (!selected) { return std::unexpected(selected.error()); }
  return validateOriginalTorrentKey(*selected,state.hash(),true);
}
std::expected<std::u16string, Error> nicknameValue(const core::Document* config) {
  const auto* value = config ? config->find("Plugin.azrating.nick") : nullptr;
  if (value && value->type != core::Node::Type::Bytes) { return bad(ErrorCode::InvalidFormat,"Invalid saved rating nickname"); }
  auto text = rating::decodeAttributeText(value ? value->bytes : std::string_view{});
  if (!text) { return bad(ErrorCode::InvalidFormat,"Saved rating nickname exceeds limits"); }
  // String.trim(), not Unicode whitespace stripping; default is source literal.
  std::size_t begin = 0; while (begin < text->size() && (*text)[begin] <= 0x20) { ++begin; }
  if (begin == text->size()) { return std::u16string(u"Anonymous"); }
  auto end = text->size(); while (end > begin && (*text)[end-1] <= 0x20) { --end; }
  return text->substr(begin,end-begin);
}
std::expected<std::u16string, Error> nickname(const ProfileLease& lease) {
  auto config=ConfigFile::load(lease,"biglybt.config");
  if(!config&&config.error().code!=ErrorCode::Missing){return std::unexpected(config.error());}
  return nicknameValue(config?&config->document():nullptr);
}
std::expected<core::Document, Error> attributes(const TorrentStateFile& state) {
  const auto* node = state.document().find("attributes");
  if (node && node->type != core::Node::Type::Dictionary) { return bad(ErrorCode::InvalidFormat,"Invalid original torrent attributes"); }
  std::string source = "de";
  if (node) {
    const auto& backing = node->origin ? *node->origin : state.document().originalBytes();
    source = backing.substr(node->begin,node->end-node->begin);
  }
  auto result = core::Document::parse(std::move(source),compatibleConfigLimits());
  if (!result) { return bad(ErrorCode::InvalidFormat,"Invalid original attributes"); }
  return std::move(*result);
}
std::expected<rating::Text, Error> textAttribute(const core::Document& values, std::string_view name) {
  const auto* node = values.find(name);
  // Original getStringAttribute returns null for any non-byte[] value. Keep it
  // opaque unless an explicit edit replaces this particular attribute.
  if (!node || node->type != core::Node::Type::Bytes) { return rating::Text{}; }
  auto value = rating::decodeAttributeText(node->bytes);
  if (!value) { return bad(ErrorCode::InvalidFormat,"Rating text exceeds limits"); }
  return rating::Text{std::move(*value)};
}
std::expected<LocalRatingSnapshot, Error> snapshot(const TorrentStateFile& state,
    const core::Document& attrs, const std::u16string& nick) {
  auto score = textAttribute(attrs,ScoreKey), comment = textAttribute(attrs,CommentKey);
  if (!score) { return std::unexpected(score.error()); }
  if (!comment) { return std::unexpected(comment.error()); }
  auto local = rating::loadLocalRating({*score,*comment,nick});
  if (!local) { return bad(ErrorCode::InvalidFormat,"Rating exceeds native text limits"); }
  // This is an editor token, not the state writer's independent full-file CAS.
  // Merge unrelated current counters/attributes from the freshly loaded state.
  std::string tokenSource="BiglyBTC.local-rating-edit.v2:"+encoded(state.hash().hex());
  for(const auto key:{ScoreKey,CommentKey}){
    std::size_t count=0;for(std::size_t i=0;i<attrs.root().children.size();i+=2){count+=attrs.root().children[i].bytes==key;}
    if(count>1){return bad(ErrorCode::InvalidFormat,"Ambiguous local rating attribute");}
    if(const auto* field=attrs.find(key)){auto value=core::encodeValue(*field);if(!value){return bad(ErrorCode::InvalidFormat,"Invalid local rating attribute");}tokenSource+='1';tokenSource+=encoded(*value);}else{tokenSource+='0';}
  }
  auto nickBytes = rating::encodeAttributeText(nick);
  if (!nickBytes) { return bad(ErrorCode::InvalidFormat,"Rating nickname exceeds native limits"); }
  tokenSource += encoded(*nickBytes);
  auto revision = hashInfo<32>(tokenSource,EVP_sha256());
  if (!revision) { return std::unexpected(revision.error()); }
  std::array<std::uint8_t,32> token{};
  std::transform(revision->begin(),revision->end(),token.begin(),[](std::byte b) { return std::to_integer<std::uint8_t>(b); });
  return LocalRatingSnapshot{state.hash(),std::move(*local),token,state.recoveredBackup()};
}
}
static std::expected<DistributedRatingSnapshot,Error> distributedRating(const ProfileLease& lease,const core::TorrentHash& hash,bool shared){
  try{
    const auto unique=[](const core::Node& node){
      if(node.type!=core::Node::Type::Dictionary||node.children.size()%2){return false;}
      std::unordered_set<std::string_view> seen;for(std::size_t i=0;i<node.children.size();i+=2){if(node.children[i].type!=core::Node::Type::Bytes||!seen.insert(node.children[i].bytes).second){return false;}}return true;
    };
    if(shared){auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}}
    auto inventory=savedTarget(lease,hash);if(!inventory){return std::unexpected(inventory.error());}
    if(inventory->recoverySource()!=RecoverySource::Primary||!unique(inventory->document().root())){return bad(ErrorCode::Conflict,"Resolve rating inventory recovery first");}
    const auto& root=inventory->document().root();
    if(const auto* rows=inventory->document().find("downloads")){
      if(rows->type!=core::Node::Type::List){return bad(ErrorCode::InvalidFormat,"Invalid rating inventory list");}
      for(const auto& row:rows->children){if(!unique(row)){return bad(ErrorCode::InvalidFormat,"Duplicate or invalid rating inventory row");}}
    }else{
      for(std::size_t i=0;i<root.children.size();i+=2){const auto& row=root.children[i+1];
        if(row.type==core::Node::Type::Dictionary&&(row.find("torrent")||row.find("torrent_hash")||row.find("save_dir")||row.find("path"))&&!unique(row)){
          return bad(ErrorCode::InvalidFormat,"Duplicate legacy rating inventory row");
        }
      }
    }
    auto store=TorrentStateStore::open(lease);if(!store){return std::unexpected(store.error());}
    auto state=store->loadAttributes(lease,hash);if(!state){return std::unexpected(state.error());}
    if(state->recoveredBackup()||!unique(state->document().root())){return bad(ErrorCode::Conflict,"Resolve rating state recovery first");}
    auto valid=identity(*state);if(!valid){return std::unexpected(valid.error());}
    auto attrs=attributes(*state);if(!attrs){return std::unexpected(attrs.error());}
    const auto* info=state->document().find("info");if(!info||!unique(*info)||!unique(attrs->root())){return bad(ErrorCode::InvalidFormat,"Invalid distributed rating metadata");}
    if(const auto* flags=attrs->find("flags")){if(flags->type!=core::Node::Type::Integer){return bad(ErrorCode::InvalidFormat,"Invalid rating download flags");}
      if((static_cast<std::uint64_t>(flags->integer)&0x200U)!=0){return bad(ErrorCode::Unsupported,"Metadata downloads cannot publish ratings");}}
    // DDBaseImpl selects Public by the exact canonical saved network constant.
    // No private-torrent or Mainline peer-source gate is invented here. The
    // initial explicit branch requires present saved networks, not an inferred
    // chooser/default from an uninitialised original DownloadManager.
    const auto* networks=attrs->find("networks");if(!networks||networks->type!=core::Node::Type::List||networks->children.size()>16){return bad(ErrorCode::Unsupported,"Explicit saved Public DDB network required");}
    bool publicNetwork=false;for(const auto& item:networks->children){if(item.type!=core::Node::Type::Bytes){return bad(ErrorCode::InvalidFormat,"Invalid rating network value");}publicNetwork|=item.bytes=="Public";}
    if(!publicNetwork){return bad(ErrorCode::Unsupported,"Rating Public DDB is not enabled");}
    auto config=ConfigFile::load(lease,"biglybt.config");if(!config&&config.error().code!=ErrorCode::Missing){return std::unexpected(config.error());}
    if(config&&(config->recoverySource()!=RecoverySource::Primary||!unique(config->document().root()))){return bad(ErrorCode::Conflict,"Resolve rating configuration recovery first");}
    if(const auto* enabled=config?config->document().find("Plugin.DHT.dht.enabled"):nullptr){
      if(enabled->type!=core::Node::Type::Integer){return bad(ErrorCode::InvalidFormat,"Invalid original DHT availability");}
      if(static_cast<std::uint32_t>(enabled->integer)==0){return bad(ErrorCode::Unsupported,"Original Azureus DHT is disabled");}}
    if(shared){if(const auto* enabled=config?config->document().find("PluginInfo.azrating.enabled"):nullptr){if(enabled->type!=core::Node::Type::Integer){return bad(ErrorCode::InvalidFormat,"Invalid original rating plugin switch");}if(static_cast<std::uint32_t>(enabled->integer)==0){return bad(ErrorCode::Unsupported,"Original rating plugin is disabled");}}}
    auto nick=nicknameValue(config?&config->document():nullptr);if(!nick){return std::unexpected(nick.error());}
    auto local=snapshot(*state,*attrs,*nick);if(!local){return std::unexpected(local.error());}
    const auto hashBytes=hash.bytes();auto key=rating::lookupKey({reinterpret_cast<const std::uint8_t*>(hashBytes.data()),hashBytes.size()});if(!key){return bad(ErrorCode::InvalidFormat,"Invalid selected rating hash");}
    auto infoBytes=originalTorrentInfoSubject(state->document());if(!infoBytes){return std::unexpected(infoBytes.error());}
    std::string scope;
    if(shared){scope="BiglyBTC.shared-rating-subject.v1:"+encoded(hash.hex())+encoded(*infoBytes)
        +encoded(std::string_view(reinterpret_cast<const char*>(key->data()),key->size()))
        +encoded(std::string_view(reinterpret_cast<const char*>(local->attributesRevision.data()),local->attributesRevision.size()))+encoded("Public");}
    else{scope=encoded(inventory->document().serialize())+encoded(*infoBytes)+encoded(attrs->serialize())
        +encoded(config?config->document().serialize():std::string("missing"));}
    const auto digest=hashInfo<32>(scope,EVP_sha256());if(!digest){return std::unexpected(digest.error());}
    std::array<std::uint8_t,32> revision{};std::transform(digest->begin(),digest->end(),revision.begin(),[](std::byte v){return std::to_integer<std::uint8_t>(v);});
    auto held=lease.retain();if(!held){return std::unexpected(held.error());}
    return DistributedRatingSnapshot{std::move(*local),revision,*key};
  }catch(const std::bad_alloc&){return bad(ErrorCode::Io,"Insufficient memory for distributed rating");}
}
std::expected<DistributedRatingSnapshot,Error> readDistributedRating(const ProfileLease& lease,const core::TorrentHash& hash){return distributedRating(lease,hash,false);}
std::expected<DistributedRatingSnapshot,Error> readSharedDistributedRating(const ProfileLease& lease,const core::TorrentHash& hash){return distributedRating(lease,hash,true);}
std::expected<SharedRatingSettings,Error> readSharedRatingSettings(const ProfileLease& lease){
  try{
  auto config=ConfigFile::load(lease,"biglybt.config");if(!config){if(config.error().code==ErrorCode::Missing){return SharedRatingSettings{};}return std::unexpected(config.error());}
  if(config->recoverySource()!=RecoverySource::Primary){return bad(ErrorCode::Conflict,"Resolve rating configuration recovery first");}
  const auto& root=config->document().root();if(root.type!=core::Node::Type::Dictionary||root.children.size()%2){return bad(ErrorCode::InvalidFormat,"Invalid rating configuration root");}std::unordered_set<std::string_view> keys;for(std::size_t n=0;n<root.children.size();n+=2){if(!keys.insert(root.children[n].bytes).second){return bad(ErrorCode::InvalidFormat,"Ambiguous rating configuration");}}
  SharedRatingSettings out;for(auto entry:{std::pair{"PluginInfo.azrating.enabled",&out.pluginEnabled},{"Plugin.DHT.dht.enabled",&out.dhtEnabled},{"azbuddy.dchat.ui.enable.auto.dl.chat",&out.autoChat}}){if(const auto* value=config->document().find(entry.first)){if(value->type!=core::Node::Type::Integer){return bad(ErrorCode::InvalidFormat,"Invalid original rating boolean");}*entry.second=static_cast<std::uint32_t>(value->integer)!=0;}}return out;
  }catch(const std::bad_alloc&){return bad(ErrorCode::Io,"Insufficient memory for rating settings");}
}
std::expected<PublicRatingHashSnapshot,Error> readPublicRatingHash(const ProfileLease& lease,const core::TorrentHash& hash){
 try{
  auto settings=readSharedRatingSettings(lease);if(!settings){return std::unexpected(settings.error());}if(!settings->enabled()){return bad(ErrorCode::Unsupported,"Public ratings are disabled");}
  const auto bytes=hash.bytes();std::vector<std::uint8_t> input;input.reserve(bytes.size());for(auto byte:bytes){input.push_back(std::to_integer<std::uint8_t>(byte));}auto key=rating::lookupKey(input);if(!key){return bad(ErrorCode::InvalidFormat,"Invalid Public rating hash");}
  std::string token="BiglyBTC.public-rating-hash.v1:"+encoded(hash.hex());token.append(reinterpret_cast<const char*>(key->data()),key->size());auto revision=hashInfo<32>(token,EVP_sha256());if(!revision){return std::unexpected(revision.error());}PublicRatingHashSnapshot out{hash,{},*key};std::transform(revision->begin(),revision->end(),out.sourceRevision.begin(),[](std::byte b){return std::to_integer<std::uint8_t>(b);});return out;
 }catch(const std::bad_alloc&){return bad(ErrorCode::Io,"Insufficient memory for Public rating hash");}
}
std::expected<DownloadChatSnapshot,Error> readDownloadChat(const ProfileLease& lease,const core::TorrentHash& hash){
 try{
  auto pending=requireNoPendingSubscriptionAcquisition(lease);if(!pending){return std::unexpected(pending.error());}auto inventory=savedTarget(lease,hash);if(!inventory){return std::unexpected(inventory.error());}
  const auto unique=[](const core::Node& node){if(node.type!=core::Node::Type::Dictionary||node.children.size()%2){return false;}std::unordered_set<std::string_view> keys;for(std::size_t n=0;n<node.children.size();n+=2){if(node.children[n].type!=core::Node::Type::Bytes||!keys.insert(node.children[n].bytes).second){return false;}}return true;};
  if(inventory->recoverySource()!=RecoverySource::Primary||!unique(inventory->document().root())){return bad(ErrorCode::Conflict,"Resolve download chat inventory recovery");}if(const auto* rows=inventory->document().find("downloads")){if(rows->type!=core::Node::Type::List){return bad(ErrorCode::InvalidFormat,"Invalid download chat inventory");}for(const auto& row:rows->children){if(!unique(row)){return bad(ErrorCode::InvalidFormat,"Ambiguous download chat inventory row");}}}else{const auto& root=inventory->document().root();for(std::size_t n=0;n<root.children.size();n+=2){const auto& row=root.children[n+1];if(row.type==core::Node::Type::Dictionary&&(row.find("torrent")||row.find("torrent_hash")||row.find("save_dir")||row.find("path"))&&!unique(row)){return bad(ErrorCode::InvalidFormat,"Ambiguous legacy download chat row");}}}
  auto store=TorrentStateStore::open(lease);if(!store){return std::unexpected(store.error());}auto state=store->loadAttributes(lease,hash);if(!state){return std::unexpected(state.error());}if(state->recoveredBackup()||!unique(state->document().root())){return bad(ErrorCode::Conflict,"Resolve download chat state recovery");}auto checked=identity(*state);if(!checked){return std::unexpected(checked.error());}auto attrs=attributes(*state);if(!attrs||!unique(attrs->root())){return bad(ErrorCode::InvalidFormat,"Invalid download chat attributes");}if(const auto* flags=attrs->find("flags")){if(flags->type!=core::Node::Type::Integer){return bad(ErrorCode::InvalidFormat,"Invalid download chat flags");}if(static_cast<std::uint64_t>(flags->integer)&0x200U){return bad(ErrorCode::Unsupported,"Metadata downloads have no canonical download chat");}}
  const auto* networks=attrs->find("networks");if(!networks||networks->type!=core::Node::Type::List||networks->children.size()>16){return bad(ErrorCode::Unsupported,"Explicit saved Public chat network required");}bool allowed=false;for(const auto& value:networks->children){if(value.type!=core::Node::Type::Bytes){return bad(ErrorCode::InvalidFormat,"Invalid download chat network");}allowed|=value.bytes=="Public";}if(!allowed){return bad(ErrorCode::Unsupported,"Public is not enabled for this download");}
  const auto* info=state->document().find("info");if(!info||!unique(*info)){return bad(ErrorCode::InvalidFormat,"Invalid canonical download chat info");}if(const auto* flag=info->find("private");flag){if(flag->type!=core::Node::Type::Integer){return bad(ErrorCode::InvalidFormat,"Invalid original private flag");}if(static_cast<std::uint32_t>(flag->integer)!=0){return bad(ErrorCode::Unsupported,"Private torrents have no download chat");}}
  const auto* name=info->find("name.utf-8");if(!name){name=info->find("name");}if(!name||name->type!=core::Node::Type::Bytes){return bad(ErrorCode::InvalidFormat,"Missing canonical download chat name");}auto decoded=rating::decodeAttributeText(name->bytes);if(!decoded){return bad(ErrorCode::InvalidFormat,"Download chat name exceeds its bound");}auto utf8=rating::encodeAttributeText(*decoded);if(!utf8){return bad(ErrorCode::InvalidFormat,"Cannot encode download chat name");}std::string key="Download: "+*utf8+" {"+hash.hex()+"}";if(key.size()>4096){return bad(ErrorCode::Unsupported,"Download chat key exceeds its bound");}
  auto config=ConfigFile::load(lease,"biglybt.config");if(!config&&config.error().code!=ErrorCode::Missing){return std::unexpected(config.error());}if(config&&(config->recoverySource()!=RecoverySource::Primary||!unique(config->document().root()))){return bad(ErrorCode::Conflict,"Resolve download chat config recovery");}if(const auto* enabled=config?config->document().find("Plugin.DHT.dht.enabled"):nullptr){if(enabled->type!=core::Node::Type::Integer){return bad(ErrorCode::InvalidFormat,"Invalid original DHT availability");}if(static_cast<std::uint32_t>(enabled->integer)==0){return bad(ErrorCode::Unsupported,"Original Azureus DHT is disabled");}}
  auto infoBytes=originalTorrentInfoSubject(state->document());if(!infoBytes){return std::unexpected(infoBytes.error());}auto digest=hashInfo<32>("BiglyBTC.download-chat-subject.v1:"+encoded(hash.hex())+encoded(*infoBytes)+encoded(key)+encoded("Public"),EVP_sha256());if(!digest){return std::unexpected(digest.error());}DownloadChatSnapshot out{hash,std::move(key),{}};std::transform(digest->begin(),digest->end(),out.sourceRevision.begin(),[](std::byte b){return std::to_integer<std::uint8_t>(b);});return out;
 }catch(const std::bad_alloc&){return bad(ErrorCode::Io,"Insufficient memory for download chat source");}
}
std::expected<RatingChatSnapshot,Error> readRatingChat(const ProfileLease& lease,const core::TorrentHash& hash){
 try{
 auto settings=readSharedRatingSettings(lease);if(!settings){return std::unexpected(settings.error());}if(!settings->autoChat){return bad(ErrorCode::Unsupported,"Automatic download chats are disabled");}
 auto rating=readSharedDistributedRating(lease,hash);if(!rating){return std::unexpected(rating.error());}if(!rating->local.local.value.needsPublishing()||rating->local.local.value.score>5){return bad(ErrorCode::Unsupported,"No scored rating notice");}
 auto store=TorrentStateStore::open(lease);if(!store){return std::unexpected(store.error());}auto state=store->loadAttributes(lease,hash);if(!state){return std::unexpected(state.error());}auto attrs=attributes(*state);if(!attrs){return std::unexpected(attrs.error());}
 auto raw=originalTorrentInfoSubject(state->document());if(!raw){return std::unexpected(raw.error());}auto info=core::Document::parse(std::string(*raw));if(!info){return bad(ErrorCode::InvalidFormat,"Invalid canonical chat metadata");}
 if(const auto* privateFlag=info->find("private");privateFlag&&privateFlag->type==core::Node::Type::Integer&&static_cast<std::uint32_t>(privateFlag->integer)!=0){return bad(ErrorCode::Unsupported,"Private torrents have no automatic download chat");}
 const auto* name=info->find("name.utf-8");if(!name){name=info->find("name");}if(!name||name->type!=core::Node::Type::Bytes){return bad(ErrorCode::InvalidFormat,"Missing canonical download chat name");}auto decoded=plugins::rating::decodeAttributeText(name->bytes);if(!decoded){return bad(ErrorCode::InvalidFormat,"Canonical download chat name exceeds its bound");}auto utf8=plugins::rating::encodeAttributeText(*decoded);if(!utf8){return bad(ErrorCode::InvalidFormat,"Cannot encode canonical download chat name");}
 auto chatState=textAttribute(*attrs,"Plugin.azrating.chatState");if(!chatState){return std::unexpected(chatState.error());}std::string key="Download: "+*utf8+" {"+hash.hex()+"}";if(key.size()>4096){return bad(ErrorCode::Unsupported,"Download chat key exceeds its bound");}
 std::string subject="BiglyBTC.rating-chat-subject.v1:"+encoded(key);subject.append(reinterpret_cast<const char*>(rating->sourceRevision.data()),rating->sourceRevision.size());if(*chatState){auto encodedState=plugins::rating::encodeAttributeText(**chatState);if(!encodedState){return bad(ErrorCode::InvalidFormat,"Cannot encode rating chat state");}subject+='1';subject+=encoded(*encodedState);}else{subject+='0';}auto digest=hashInfo<32>(subject,EVP_sha256());if(!digest){return std::unexpected(digest.error());}
 RatingChatSnapshot out{std::move(*rating),std::move(key),*chatState&&**chatState==u"w",{}};std::transform(digest->begin(),digest->end(),out.sourceRevision.begin(),[](std::byte b){return std::to_integer<std::uint8_t>(b);});return out;
 }catch(const std::bad_alloc&){return bad(ErrorCode::Io,"Insufficient memory for a rating chat notice");}
}
std::expected<bool,Error> saveRatingChatWritten(const ProfileLease& lease,const core::TorrentHash& hash,const std::array<std::uint8_t,32>& expected){
 try{
 if(hasPendingFileOperation(lease)||hasPendingOriginalCompaction(lease)){return bad(ErrorCode::Conflict,"Finish pending file operations before updating chat state");}auto source=readRatingChat(lease,hash);if(!source){return std::unexpected(source.error());}if(source->sourceRevision!=expected){return bad(ErrorCode::Conflict,"Rating chat source changed before saving");}if(source->written){return false;}
 auto store=TorrentStateStore::open(lease);if(!store){return std::unexpected(store.error());}auto state=store->loadAttributes(lease,hash);if(!state){return std::unexpected(state.error());}auto attrs=attributes(*state);if(!attrs){return std::unexpected(attrs.error());}if(!attrs->replaceValue("Plugin.azrating.chatState",encoded("w"))){return bad(ErrorCode::InvalidFormat,"Cannot encode original rating chat state");}
 source=readRatingChat(lease,hash);if(!source){return std::unexpected(source.error());}if(source->sourceRevision!=expected||hasPendingFileOperation(lease)||hasPendingOriginalCompaction(lease)){return bad(ErrorCode::Conflict,"Rating chat source changed before commit");}auto saved=store->saveAttributes(lease,*state,attrs->serialize());if(!saved){return std::unexpected(saved.error());}return true;
 }catch(const std::bad_alloc&){return bad(ErrorCode::Io,"Insufficient memory while saving rating chat state");}
}
std::expected<bool,Error> saveSharedRatingAverage(const ProfileLease& lease,const core::TorrentHash& hash,const std::array<std::uint8_t,32>& expected,float average){
  try{
  if(!std::isfinite(average)||average<0){return bad(ErrorCode::InvalidFormat,"Invalid derived rating average");}
  if(hasPendingFileOperation(lease)||hasPendingOriginalCompaction(lease)){return bad(ErrorCode::Conflict,"Finish the pending file operation before saving rating statistics");}
  auto source=readSharedDistributedRating(lease,hash);if(!source){return std::unexpected(source.error());}if(source->sourceRevision!=expected){return bad(ErrorCode::Conflict,"Rating source changed before saving statistics");}
  auto store=TorrentStateStore::open(lease);if(!store){return std::unexpected(store.error());}auto state=store->loadAttributes(lease,hash);if(!state){return std::unexpected(state.error());}auto attrs=attributes(*state);if(!attrs){return std::unexpected(attrs.error());}
  char buffer[64];const auto converted=std::to_chars(std::begin(buffer),std::end(buffer),average,std::chars_format::general,std::numeric_limits<float>::max_digits10);if(converted.ec!=std::errc{}){return bad(ErrorCode::InvalidFormat,"Cannot encode rating average");}std::string text(buffer,converted.ptr);if(text.find_first_of(".eE")==std::string::npos){text+=".0";}
  constexpr std::string_view key="Plugin.azrating.globalRating";if(const auto* old=attrs->find(key);old&&old->type==core::Node::Type::Bytes&&old->bytes==text){return false;}if(!attrs->replaceValue(key,encoded(text))){return bad(ErrorCode::InvalidFormat,"Cannot stage rating average");}
  source=readSharedDistributedRating(lease,hash);if(!source){return std::unexpected(source.error());}if(source->sourceRevision!=expected||hasPendingFileOperation(lease)||hasPendingOriginalCompaction(lease)){return bad(ErrorCode::Conflict,"Rating source changed before statistics commit");}
  auto saved=store->saveAttributes(lease,*state,attrs->serialize());if(!saved){return std::unexpected(saved.error());}return true;
  }catch(const std::bad_alloc&){return bad(ErrorCode::Io,"Insufficient memory while saving rating statistics");}
}
std::expected<LocalRatingSnapshot, Error> readLocalRating(const ProfileLease& lease, const core::TorrentHash& hash) {
  try {
    auto target = savedTarget(lease,hash); if (!target) { return std::unexpected(target.error()); }
    auto store = TorrentStateStore::open(lease); if (!store) { return std::unexpected(store.error()); }
    auto state = store->loadAttributes(lease,hash); if (!state) { return std::unexpected(state.error()); }
    auto valid = identity(*state); if (!valid) { return std::unexpected(valid.error()); }
    auto attrs = attributes(*state); if (!attrs) { return std::unexpected(attrs.error()); }
    auto nick = nickname(lease); if (!nick) { return std::unexpected(nick.error()); }
    return snapshot(*state,*attrs,*nick);
  } catch (const std::bad_alloc&) { return bad(ErrorCode::Io,"Insufficient memory for local rating"); }
}
std::expected<LocalRatingSaved, Error> saveLocalRating(const ProfileLease& lease, const core::TorrentHash& hash,
    const std::array<std::uint8_t,32>& expected, std::int32_t score, std::u16string comment) {
  try {
    auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
    if (score > 5 || comment.size() > rating::maximumLocalTextUnits) { return bad(ErrorCode::InvalidFormat,"Rating or comment exceeds supported limits"); }
    auto target = savedTarget(lease,hash); if (!target) { return std::unexpected(target.error()); }
    auto store = TorrentStateStore::open(lease); if (!store) { return std::unexpected(store.error()); }
    auto state = store->loadAttributes(lease,hash); if (!state) { return std::unexpected(state.error()); }
    auto valid = identity(*state); if (!valid) { return std::unexpected(valid.error()); }
    auto attrs = attributes(*state); if (!attrs) { return std::unexpected(attrs.error()); }
    auto nick = nickname(lease); if (!nick) { return std::unexpected(nick.error()); }
    auto before = snapshot(*state,*attrs,*nick); if (!before) { return std::unexpected(before.error()); }
    if (before->attributesRevision != expected) { return bad(ErrorCode::Conflict,"Rating attributes changed; reopen the editor"); }
    auto oldScore = textAttribute(*attrs,ScoreKey), oldComment = textAttribute(*attrs,CommentKey);
    if (!oldScore || !oldComment) { return bad(ErrorCode::InvalidFormat,"Invalid rating attributes"); }
    const auto replacement = rating::RatingData::create(score,*nick,std::move(comment));
    auto plan = rating::planLocalRatingStore({*oldScore,*oldComment,*nick},replacement);
    if (!plan || plan->failure) { return bad(ErrorCode::InvalidFormat,"Cannot prepare the original local rating update"); }
    LocalRatingSaved result{std::move(*before),!plan->writes.empty(),plan->publishUrgently,false};
    if (!result.changed) { return result; }
    for (const auto& write : plan->writes) {
      if (!write.value) { return bad(ErrorCode::InvalidFormat,"Null local rating write is unsupported"); }
      auto text = rating::encodeAttributeText(*write.value);
      if (!text || !attrs->replaceValue(write.attribute == rating::LocalAttribute::Rating ? ScoreKey : CommentKey,encoded(*text))) {
        return bad(ErrorCode::InvalidFormat,"Local rating edit exceeds limits");
      }
    }
    auto after = snapshot(*state,*attrs,*nick); if (!after) { return std::unexpected(after.error()); }
    allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){return std::unexpected(allowed.error());}
    auto saved = store->saveAttributes(lease,*state,attrs->serialize());
    if (!saved) { return std::unexpected(saved.error()); }
    result.snapshot = std::move(*after);
    result.snapshot.recoveredBackup = false;
    return result;
  } catch (const std::bad_alloc&) {
    // As with the existing resilient writer, publication errors do not prove
    // that no replacement occurred. Refresh before retrying a failed save.
    return bad(ErrorCode::Io,"Insufficient memory while saving local rating; reload before retrying");
  }
}
}

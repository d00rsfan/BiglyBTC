// SPDX-License-Identifier: GPL-2.0-or-later
#include "state_file_io.hpp"
#include "swarm_discovery_profile.hpp"
#include "category_store.hpp"
#include <biglybtc/category_tags.hpp>
#include <biglybtc/download_state_names.hpp>
#include "library_view.hpp"
#include "torrent_state.hpp"
#include "original_torrent_identity_lt.hpp"
#include "tag_manager_startup.hpp"
#include <biglybtc/saved_download_tags.hpp>
#include <biglybtc/stock_tag_names.hpp>
#include <biglybtc/tag_constraints.hpp>
#include <libtorrent/torrent_info.hpp>
#include <QDateTime>
#include <QCryptographicHash>
#include <QUrl>
#include <QString>
#include <openssl/rand.h>
#include <algorithm>
#include <bit>
#include <map>
#include <set>
#include <unordered_set>
#include <limits>
#include <mutex>
namespace biglybtc::app {
namespace {
using storage::Error;using storage::ErrorCode;using Node=core::Node;
namespace state = storage::detail;
struct Failure{Error error;};
void need(bool v,const char* why){if(!v){throw Failure{{ErrorCode::InvalidFormat,why}};}}
template<class T>T take(std::expected<T,Error> value){if(!value){throw Failure{value.error()};}return std::move(*value);}
template<class T>T model(rcm::Result<T> value){if(!value){throw Failure{{ErrorCode::InvalidFormat,value.error().message}};}return std::move(*value);}
void done(std::expected<void,Error> value){if(!value){throw Failure{value.error()};}}
template<class F>auto guard(F&& f)->decltype(f()){try{return f();}catch(const Failure& e){return std::unexpected(e.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Discovery operation failed; existing files retained"});}}
std::string b(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}std::string n(std::int64_t v){return "i"+std::to_string(v)+"e";}
std::string text(const Node* v){return v&&v->type==Node::Type::Bytes?std::string(v->bytes):std::string{};}
std::int64_t number(const Node* v,std::int64_t fallback=0){return v&&v->type==Node::Type::Integer?v->integer:fallback;}
bool unique(const Node& v){if(v.type!=Node::Type::Dictionary||v.children.size()%2){return false;}std::set<std::string_view> keys;for(std::size_t i=0;i<v.children.size();i+=2){if(v.children[i].type!=Node::Type::Bytes||!keys.insert(v.children[i].bytes).second){return false;}}return true;}
std::string stamp(state::StateHandle dir, const char* name) {
  const auto info = take(state::stateStampAt(dir, name));
  if (!info) { return {}; }
  need(info->regular && info->owned && info->links == 1, "Unsafe related-content source file");
#ifndef _WIN32
  need(!(info->mode & 0002), "Unsafe related-content source file");
#endif
  const auto& value = *info;
  return std::to_string(value.device) + ':' + std::to_string(value.inode) + ':' + std::to_string(value.size)
      + ':' + std::to_string(value.modified.tv_sec) + ':' + std::to_string(value.modified.tv_nsec)
      + ':' + std::to_string(value.changed.tv_sec) + ':' + std::to_string(value.changed.tv_nsec);
}
std::string resilientStamp(state::StateHandle root, const char* name) {
  const std::string base = name;
  return stamp(root, name) + '|' + stamp(root, (base + ".bak").c_str()) + '|' + stamp(root, (base + ".saving").c_str());
}
bool presentResilient(const std::string& value){return value!="||";}
bool categoryDefinitionsSettled(const storage::ProfileLease& owner) {
 if (storage::hasPendingCategoryDeletion(owner)) { return false; }
 const auto directory=owner.directoryHandle();
 for (const auto* candidate : {"categories.config.new","categories.config.saving",
                               "categories.config.bak.saving"}) {
  if (!stamp(directory,candidate).empty()) { return false; }
 }
 return true;
}
std::optional<bool> savedPublicFlag(const Node* record,bool publicDefault) {
 if (!record) { return publicDefault; }
 if (!unique(*record)) { return {}; }
 const auto* attributes=record->find("c");
 if (!attributes) { return publicDefault; }
 if (!unique(*attributes)) { return {}; }
 const auto* published=attributes->find("pub");
 if (!published) { return publicDefault; }
 if (published->type!=Node::Type::Integer) { return {}; }
 return published->integer==1;
}
bool publicManualTag(const Node& record,bool publicDefault) {
 if (!savedPublicFlag(&record,publicDefault).value_or(false)) { return false; }
 const auto* attributes=record.find("c");
 if (!attributes) { return true; }
 const auto* trackers=attributes->find("pp.trackers");
 if (trackers && (trackers->type!=Node::Type::List || !trackers->children.empty())) { return false; }
 const auto* untagged=attributes->find("pp.untagged");
 if (untagged && (untagged->type!=Node::Type::Integer || untagged->integer==1)) { return false; }
 const auto* constraint=attributes->find("pp.constraint");
 if (!constraint) { return true; }
 if (constraint->type!=Node::Type::List) { return false; }
 if (constraint->children.empty()) { return true; }
 const auto* enabled=attributes->find("pp.enabled.constraint");
 if (enabled && enabled->type!=Node::Type::Integer) { return false; }
 if (enabled && enabled->integer!=1) { return true; }
 const auto& expression=constraint->children.front();
 if (expression.type!=Node::Type::Bytes) { return false; }
 if (std::ranges::all_of(expression.bytes,[](unsigned char c){return c<=32;})) { return true; }
 if (constraint->children.size()<2 || constraint->children[1].type!=Node::Type::Bytes) { return false; }
 return core::tags::constraints::parseOptions(constraint->children[1].bytes).newOnly;
}
std::optional<QString> manualTagName(const core::SavedDownloadTag& row,
    const std::optional<std::string>& locale) {
 if (!row.rawName) { return {}; }
 auto name=QString::fromUtf8(row.rawName->data(),qsizetype(row.rawName->size()));
 if (name.toUtf8().toStdString()!=*row.rawName) { return {}; }
 if (row.nameMayRequireLocalization) {
  if (!locale) { return {}; }
  const auto localized=core::tags::stockTagName(*locale,name.toStdU16String());
  if (!localized) { return {}; }
  name=QString::fromStdU16String(std::u16string(*localized));
 }
 return name;
}
void replace(core::Document& d,std::string_view key,std::string v){need(bool(d.replaceValue(key,std::move(v))),"Invalid related-content field update");}
constexpr std::string_view RandomId = "rcm.random.id";
std::optional<std::string> swarmTagSourceDigest(const storage::ProfileLease& owner,
    const storage::TorrentStateFile& state,bool savedLabels) {
 const auto config=storage::ConfigFile::load(owner,"biglybt.config");
 if (!config || config->recoverySource()!=storage::RecoverySource::Primary) { return {}; }
 const auto settings=rcm::settings(config->document());
 const auto random=number(config->document().find(RandomId),-1);
 if (!settings || !settings->enabled || random==-1) { return {}; }

 QCryptographicHash hash(QCryptographicHash::Sha256);
 const auto add=[&](std::string_view bytes) {
  const auto separator=std::to_string(bytes.size())+':';
  hash.addData(QByteArrayView(separator.data(),qsizetype(separator.size())));
  hash.addData(QByteArrayView(bytes.data(),qsizetype(bytes.size())));
 };
 const auto stateBytes=state.document().serialize();
 add(stateBytes);
 add(std::to_string(random));
 if (savedLabels) {
  const auto* visibility=config->document().find("tag.manager.pub.default");
  if (visibility && visibility->type!=Node::Type::Integer) { return {}; }
  add(std::to_string(visibility ? visibility->integer : 1));
  const auto directory=owner.directoryHandle();
  const auto preimage=resilientStamp(directory,"tag.config");
  if (!stamp(directory,"tag.config.saving").empty()) { return {}; }
  const auto* attributes=state.document().find("attributes");
  const bool category=attributes && !text(attributes->find("category")).empty();
  if (category) {
   if (!categoryDefinitionsSettled(owner)) { return {}; }
   add(resilientStamp(directory,"categories.config"));
  }
  if (presentResilient(preimage)) {
   const auto file=storage::ConfigFile::load(owner,"tag.config",core::Limits{});
   if (!file || file->recoverySource()!=storage::RecoverySource::Primary ||
       !unique(file->document().root()) || resilientStamp(directory,"tag.config")!=preimage) { return {}; }
   if (const auto* manual=file->document().find("3")) {
    if (!unique(*manual)) { return {}; }
    add(std::string_view(file->document().originalBytes()).substr(manual->begin,manual->end-manual->begin));
   }
   if (const auto* categories=category ? file->document().find("1") : nullptr) {
    if (!unique(*categories)) { return {}; }
    add(std::string_view(file->document().originalBytes()).substr(categories->begin,categories->end-categories->begin));
   }
  }
 }
 return hash.result().toStdString();
}
bool ensureRandomId(core::Document& config) {
 if (number(config.find(RandomId), -1) != -1) { return false; }
 std::uint64_t bits = 0;
 do {
  need(RAND_bytes(reinterpret_cast<unsigned char*>(&bits), sizeof(bits)) == 1,
       "Cannot generate original discovery identity");
 } while (bits == std::numeric_limits<std::uint64_t>::max());
 replace(config, RandomId, n(std::bit_cast<std::int64_t>(bits)));
 return true;
}
std::string encoded(const Node& v,core::Limits limits=storage::compatibleConfigLimits()){auto x=core::encodeValue(v,limits);need(bool(x),"Discovery source metadata exceeds its bound");return std::move(*x);}
std::string domains(const core::Document& d){std::set<std::string> hosts;const auto add=[&](const Node* v){if(!v||v->type!=Node::Type::Bytes){return;}const QUrl url(QString::fromUtf8(v->bytes.data(),qsizetype(v->bytes.size())));if(url.scheme()!="http"&&url.scheme()!="udp"){return;}const auto host=url.host().toLower();if(host.contains(':')){return;}const auto bits=host.split('.');if(bits.size()<2||std::ranges::all_of(bits.back(),[](QChar c){return c.isDigit();})){return;}hosts.insert((bits[bits.size()-2]+"."+bits.back()).toStdString());};add(d.find("announce"));if(const auto* list=d.find("announce-list");list&&list->type==Node::Type::List){for(const auto& tier:list->children){if(tier.type==Node::Type::List){for(const auto& url:tier.children){add(&url);}}}}std::string out;for(const auto& domain:hosts){if(out.size()==32){break;}std::uint32_t hash=0;for(auto c:QString::fromUtf8(domain)){hash=31*hash+c.unicode();}for(int shift=24;shift>=0;shift-=8){out+=char(hash>>shift);}}return out;}
}
namespace {
std::vector<core::tags::runtime::Text> categoryUserNames(const core::SavedCategories& definitions) {
 std::vector<core::tags::runtime::Text> names;
 for (const auto& row : definitions.categories) {
  if (row.kind==core::SavedCategoryKind::User) {
   names.push_back(QString::fromUtf8(row.name.data(),qsizetype(row.name.size())).toStdU16String());
  }
 }
 return names;
}
}
std::expected<std::optional<std::int32_t>,storage::Error> findSavedManualTag(
    const core::SavedDownloadTags& source,const std::optional<std::string>& locale,
    std::string_view exactName) {
 return guard([&]() -> std::expected<std::optional<std::int32_t>,storage::Error> {
  need(source.issues.empty(),"Saved manual tag names require review");
  std::optional<std::int32_t> id;
  for(const auto& row:source.tags){
   const auto title=manualTagName(row,locale);
   need(title.has_value(),"An original manual tag name could not be read");
   if(title->toUtf8().toStdString()!=exactName){continue;}
   need(row.id.has_value()&&!id,"The original manual tag name is ambiguous");
   id=row.id;
  }
  return id;
 });
}
struct SwarmDiscoveryProfile::Impl {
 std::shared_ptr<const storage::ProfileLease> lease;std::optional<storage::ConfigFile> config,cacheFile,deletedFile;
 std::optional<rcm::Catalog> catalog;rcm::Settings settings;std::string cacheStamp,deletedStamp,configStamp,libraryStamp;
 std::optional<storage::LibrarySourceSnapshot> library;std::map<std::string,const storage::LibraryItem*> rows;std::map<std::string,const Node*> cached;
 std::vector<rcm::Hash> owned;std::set<std::string> duplicates;std::size_t cursor=0;std::uint64_t revision=0;std::string status="Saved discoveries";
 std::shared_ptr<const SwarmDiscoverySnapshot> snapshot;std::uint64_t publishedRevision=0;
 std::unordered_set<std::string> publicHashes;std::uint64_t publicHashesRevision=0;
 bool publicHash(std::string_view hash){if(publicHashesRevision!=revision){std::unordered_set<std::string> fresh;const auto rows=catalog->items();fresh.reserve(rows.size());for(const auto& row:rows){if(row.hash.size()==20&&(row.networks&1)){fresh.insert(row.hash);}}publicHashes=std::move(fresh);publicHashesRevision=revision;}return publicHashes.contains(std::string(hash));}std::string tableLayout="de",tableStamp;
 struct Metadata {
  std::string stamp;
  std::int64_t randomId=0;
  bool torrentNames=false;
  std::optional<SwarmDiscoverySource> value;
 };std::map<std::string,Metadata> metadata;
 std::optional<storage::TagManagerStartupMode> tagStartup;
 std::optional<core::SavedDownloadTags> publicManualSource;
 std::optional<storage::ConfigFile> publicTagFile;
 std::string publicManualStamp;
 mutable std::mutex categoryMutex;
 std::optional<core::tags::category::Roster> publicCategoryRoster;
 storage::CategorySnapshot publicCategorySource;

 void capturePublicCategories() {
  if (!tagStartup || !tagStartup->enabled() || !tagStartup->stockTagLocale() ||
      !categoryDefinitionsSettled(*lease)) { return; }
  const auto root=lease->directoryHandle();
  const auto current=resilientStamp(root,"categories.config");
  const auto store=storage::CategoryStore::open(*lease);
  if (!store) { return; }
  auto source=(*store)->snapshot();
  if (!source || !source->writable() || resilientStamp(root,"categories.config")!=current ||
      !source->matchesDefinitionSource(*lease)) { return; }
  core::tags::category::Roster roster;
  if (roster.observe(categoryUserNames(*source->categories())) && roster.enumeration()) {
   publicCategoryRoster=std::move(roster);
   publicCategorySource=std::move(*source);
  }
 }

 // The saved field resolves to an original Category before the plugin exposes
 // a name. The localized first-match lookup can then select a different ID.
 bool appendPublicCategoryTag(rcm::Item& item,std::string_view assigned) const {
  if (assigned.empty() || (tagStartup && !tagStartup->enabled())) { return true; }
  std::lock_guard lock(categoryMutex);
  if (!publicCategoryRoster || !publicCategoryRoster->enumeration() || !tagStartup ||
      !tagStartup->stockTagLocale() || !categoryDefinitionsSettled(*lease) ||
      !publicCategorySource.matchesDefinitionSource(*lease) ||
      (presentResilient(publicManualStamp) && !publicTagFile)) { return false; }
  const auto raw=QString::fromUtf8(assigned.data(),qsizetype(assigned.size()));
  if (raw.toUtf8().toStdString()!=assigned) { return false; }
  const auto name=raw.toStdU16String();
  namespace category=core::tags::category;
  const auto& entries=publicCategoryRoster->entries();
  const auto live=std::ranges::find(entries,name,&category::Entry::rawName);
  if (live==entries.end() || live->kind!=category::Kind::User) { return true; }
  const auto& locale=*tagStartup->stockTagLocale();
  const auto builtins=core::tags::stockCategoryTagNames(locale);
  if (!builtins) { return false; }
  const auto* global=config->document().find("tag.manager.pub.default");
  if (global && global->type!=Node::Type::Integer) { return false; }
  const auto* saved=publicTagFile ? publicTagFile->document().find("1") : nullptr;
  if (saved && !unique(*saved)) { return false; }
  for (const auto index : *publicCategoryRoster->enumeration()) {
   const auto& entry=entries[index];
   std::optional<std::u16string_view> localized=entry.rawName;
   if (entry.kind!=category::Kind::User) {
    localized=(*builtins)[entry.kind==category::Kind::All ? 0 : 1];
   } else if (entry.rawName.starts_with(u"tag.")) {
    localized=core::tags::stockTagName(locale,entry.rawName);
   }
   if (!localized) { return false; }
   if (*localized!=name) { continue; }
   if (entry.kind!=category::Kind::User) { return true; }
   const auto* record=saved ? saved->find(std::to_string(entry.id)) : nullptr;
   const auto visible=savedPublicFlag(record,!global || static_cast<std::uint32_t>(global->integer)!=0);
   if (!visible) { return false; }
   if (*visible) {
    auto label=raw.toLower().toUtf8().toStdString();
    if (std::ranges::find(item.tags,label)==item.tags.end()) { item.tags.push_back(std::move(label)); }
   }
   return true;
  }
  return true;
 }
 std::string obs()const{return text(config->document().find("core.crypto.obs.id"));}
 void openPublicManualTags() {
  if (!tagStartup || !tagStartup->enabled() || !tagStartup->matches(*lease)) { return; }
  const auto root=lease->directoryHandle();
  const auto current=resilientStamp(root,"tag.config");
  if (current==publicManualStamp) { return; }
  publicManualSource.reset();
  publicTagFile.reset();
  publicManualStamp=current;
  if (!presentResilient(current) || !stamp(root,"tag.config.saving").empty()) { return; }
  auto file=storage::ConfigFile::load(*lease,"tag.config",core::Limits{});
  if (!file || file->recoverySource()!=storage::RecoverySource::Primary
      || !unique(file->document().root()) || resilientStamp(root,"tag.config")!=current) { return; }
  publicTagFile.emplace(std::move(*file));
  auto read=core::readSavedDownloadTags(publicTagFile->document().originalBytes());
  if (!read) { return; }
  const auto* manual=read->source->find("3");
  if (manual && unique(*manual)) { publicManualSource=std::move(*read); }
 }
 void appendPublicManualTags(rcm::Item& item) const {
  if (!publicManualSource || !tagStartup) { return; }
  const auto hash=core::TorrentHash::parse(item.hash);
  const auto* global=config->document().find("tag.manager.pub.default");
  if (!hash || hash->bytes().size()!=20 || (global && global->type!=Node::Type::Integer)) { return; }
  const bool publicDefault=!global || static_cast<std::uint32_t>(global->integer)!=0;
  const auto* manual=publicManualSource->source->find("3");
  for (const auto& row : publicManualSource->tags) {
   if (!row.id || row.duplicateId || row.membersMalformed
       || std::ranges::find(row.uniqueHashes,*hash)==row.uniqueHashes.end()) { continue; }
   const auto* record=manual->find(row.idKey);
   if (!record || !unique(*record) || !publicManualTag(*record,publicDefault)) { continue; }
   const auto shown=manualTagName(row,tagStartup->stockTagLocale());
   if(!shown||shown->isEmpty()){continue;}
   auto name=shown->toLower().toUtf8().toStdString();
   if (std::ranges::find(item.tags,name)==item.tags.end()) { item.tags.push_back(std::move(name)); }
  }
 }
 std::optional<std::vector<std::string>> assignedTagDiscoveryNames(const core::TorrentHash& hash) {
  if (!tagStartup||!tagStartup->enabled()||!tagStartup->matches(*lease)) { return std::nullopt; }
  openPublicManualTags();
  const auto root=lease->directoryHandle();
  if (!stamp(root,"tag.config.saving").empty()||!stamp(root,"tag.config.bak.saving").empty()||
      resilientStamp(root,"tag.config")!=publicManualStamp) { return std::nullopt; }
  std::vector<std::string> names;
  if (!presentResilient(publicManualStamp)) { return names; }
  if (!publicTagFile) { return std::nullopt; }
  if (!publicTagFile->document().find("3")) { return names; }
  if (!publicManualSource||!publicManualSource->issues.empty()) { return std::nullopt; }
  for(const auto& row:publicManualSource->tags) {
   if(std::ranges::find(row.uniqueHashes,hash)==row.uniqueHashes.end()){continue;}
   const auto name=manualTagName(row,tagStartup->stockTagLocale());
   if(!name){return std::nullopt;}
   names.push_back(name->toUtf8().toStdString());
  }
  return names;
 }
 void openSettings(){
  const auto root=lease->directoryHandle();const auto cfg=stamp(root,"biglybt.config");
  need(!cfg.empty(),"Current original configuration required for discoveries");
  if(!config||configStamp!=cfg){
   // Publish the memo only after validation. A malformed replacement must not
   // leave a stale permission cached under the new source identity.
   auto latest=take(storage::ConfigFile::load(*lease,"biglybt.config"));
   need(latest.recoverySource()==storage::RecoverySource::Primary&&unique(latest.document().root()),"Current unambiguous discovery settings required");
   auto current=model(rcm::settings(latest.document()));
   const bool changed=!config||settings!=current;config=std::move(latest);settings=std::move(current);configStamp=cfg;if(changed){++revision;}
  }
 }
 void open(){
  openSettings();
  const auto root=lease->directoryHandle();const auto cache=resilientStamp(root,"rcm.config"),del=resilientStamp(root,"rcmx.config");
  const auto tables=resilientStamp(root,"tables.config");if(tables!=tableStamp){tableLayout="de";if(presentResilient(tables)){auto file=take(storage::ConfigFile::load(*lease,"tables.config"));const auto& doc=file.document();const auto* table=doc.find("Table.RCM");if(!table){table=doc.find("TableDefault.RCM");}if(table){need(unique(*table),"Ambiguous discoveries table layout");tableLayout=encoded(*table);}}tableStamp=tables;++revision;}
  if(!catalog||cacheStamp!=cache||deletedStamp!=del){need(!catalog||(!catalog->dirty()&&!catalog->deletedDirty()),"Discovery cache changed during a pending save; retained without overwrite");std::string raw="de",deleted="de";
   if(presentResilient(cache)){cacheFile=take(storage::ConfigFile::load(*lease,"rcm.config",rcm::cacheFileLimits()));raw=cacheFile->document().serialize();}else{cacheFile.reset();}
   if(presentResilient(del)){deletedFile=take(storage::ConfigFile::load(*lease,"rcmx.config"));deleted=deletedFile->document().serialize();}else{deletedFile.reset();}
   if(cacheFile&&cacheFile->document().find("d")){const auto* key=config->document().find("core.crypto.obs.id");need(key&&key->type==Node::Type::Bytes,"Original core.crypto.obs.id required for the existing discoveries cache");}
   catalog=model(rcm::Catalog::decode(std::move(raw),obs(),std::move(deleted)));cacheStamp=cache;deletedStamp=del;catalog->enforceMaximum(settings.maximum);++revision;
  }
 }
 void inventory(){
  const auto root = lease->directoryHandle();
  const auto downloads = stamp(root, "downloads.config");
  need(!downloads.empty(), "Current saved library required for discovery permission");
  const auto active = take(state::openStateDirectory(root, "active"));
  const auto current = downloads + '|' + stamp(active.get(), "cache.dat");
  if(library&&current==libraryStamp){return;}library=take(storage::readLibrarySource(*lease));need(!library->view.cacheWarning,"Original state cache is unreadable; discovery source withheld");rows.clear();cached.clear();owned.clear();duplicates.clear();metadata.clear();
  for(const auto& row:library->view.items){if(!row.saved.hash){continue;}const auto key=row.saved.hash->hex();if(!rows.emplace(key,&row).second){duplicates.insert(key);}const auto raw=row.saved.hash->bytes();owned.emplace_back(reinterpret_cast<const char*>(raw.data()),raw.size());}
  if(library->cacheDocument){if(const auto* list=library->cacheDocument->document().find("state");list&&list->type==Node::Type::List){for(const auto& entry:list->children){const auto raw=text(entry.find("hash"));if(auto hash=core::TorrentHash::parse(raw)){cached.insert_or_assign(hash->hex(),&entry);}}}}
  libraryStamp=current;++revision;
 }
 std::optional<SwarmDiscoverySource> source(std::string_view bytes){
  open();inventory();if(!settings.enabled){return {};}const auto hash=core::TorrentHash::parse(bytes);if(!hash){return {};}const auto key=hash->hex();auto found=rows.find(key);if(found==rows.end()||duplicates.contains(key)||!found->second->saved.persistent){return {};}
  const auto active = take(state::openStateDirectory(lease->directoryHandle(), "active"));
  const auto sourceStamp = stamp(active.get(), (key + ".dat").c_str());
  if (sourceStamp.empty()) { return {}; }
  const auto randomId=number(config->document().find(RandomId));
  auto& memo=metadata[key];
  if (memo.stamp==sourceStamp && memo.torrentNames==settings.useTorrentNames && memo.randomId==randomId) {
   return memo.value;
  }
  memo={sourceStamp,randomId,settings.useTorrentNames,{}};
  auto store=take(storage::TorrentStateStore::open(*lease));auto state=take(store.load(*lease,*hash));if(state.recoveredBackup()){return {};}auto doc=state.document();if(const auto c=cached.find(key);c!=cached.end()){need(unique(*c->second),"Ambiguous source cache record");for(auto [from,to]:{std::pair{"attributes","attributes"},{"azp","azureus_properties"},{"azpp","azureus_private_properties"}}){if(const auto* v=c->second->find(from)){need(unique(*v),"Ambiguous discovery metadata field");replace(doc,to,encoded(*v));}}}
  const auto* info=doc.find("info"),*attrs=doc.find("attributes"),*properties=doc.find("azureus_properties");need(unique(doc.root())&&info&&unique(*info)&&(!attrs||unique(*attrs))&&(!properties||unique(*properties)),"Ambiguous discovery source identity");
  const auto encoded=doc.serialize();libtorrent::error_code error;libtorrent::torrent_info torrent(libtorrent::span<const char>(encoded.data(),encoded.size()),error,libtorrent::from_span);if(error){return {};}const auto identity=storage::originalTorrentIdentityFromLibtorrent(doc,torrent);if(!identity||!storage::validateOriginalTorrentKey(*identity,*hash,true)||torrent.priv()||(identity->full.v1&&identity->full.v2&&identity->selected==storage::OriginalSelectedHash::V2)){return {};}
  for(const auto* field:{properties?properties->find("torrent_flags"):nullptr,attrs?attrs->find("flags"):nullptr,info->find("private")}){if(field&&field->type!=Node::Type::Integer){return {};}}
  if((number(properties?properties->find("torrent_flags"):nullptr)&4)||(number(attrs?attrs->find("flags"):nullptr)&0x210)){return {};}
  std::uint8_t networks=0;if(const auto* list=attrs?attrs->find("networks"):nullptr;list&&list->type==Node::Type::List){for(const auto& net:list->children){need(net.type==Node::Type::Bytes,"Malformed original source networks");if(net.bytes=="Public"){networks|=1;}else if(net.bytes=="I2P"){networks|=2;}else if(net.bytes=="Tor"){networks|=4;}}}if(!(networks&1)){return {};}
  SwarmDiscoverySource result;auto& item=result.item;const auto raw=hash->bytes();item.hash.assign(reinterpret_cast<const char*>(raw.data()),std::min<std::size_t>(20,raw.size()));item.key=rcm::base32(item.hash);item.title=torrent.name();if(!settings.useTorrentNames){if(const auto display=text(attrs?attrs->find("displayname"):nullptr);!display.empty()){item.title=display;}}
  result.savedCategory=text(attrs ? attrs->find("category") : nullptr);
  item.size=torrent.total_size();item.networks=networks;item.trackerKeys=domains(doc);item.firstSeen=QDateTime::currentSecsSinceEpoch();item.lastSeen=item.firstSeen;
  if (identity->full.v2) {
   item.tags.emplace_back(identity->full.v1 ? "_hybrid_" : "_v2_");
  }
  const auto* params=attrs?attrs->find("parameters"):nullptr;const auto random=std::uint64_t(randomId)^std::uint64_t(number(params?params->find("rand"):nullptr));item.random=std::bit_cast<std::int32_t>(std::uint32_t(random));
  const auto scrape=number(attrs?attrs->find("scrapecache"):nullptr,-1);if(scrape!=-1){item.packedPeers=std::bit_cast<std::int32_t>((std::uint32_t(std::uint64_t(scrape)>>32)<<16)|(std::uint32_t(scrape)&65535));}item.publishedHours=std::int32_t(number(doc.find("creation date"))/3600);
  for(int index=0;index<torrent.num_files();++index){const auto size=torrent.files().file_size(libtorrent::file_index_t(index));if(size>=50*1024*1024){result.fileSizes.push_back(size);}}
  memo.value=std::move(result);return memo.value;
 }
 void publish(){if(snapshot&&publishedRevision==revision){return;}auto value=std::make_shared<SwarmDiscoverySnapshot>();value->revision=revision;value->settings=settings;value->tableLayout=tableLayout;value->items=catalog->items();value->unread=catalog->unread();value->knownLibrary=owned.size();value->status=status;snapshot=std::move(value);publishedRevision=revision;}
 void write(std::string name,std::string bytes,std::optional<storage::ConfigFile>& file,std::string& current){
  const auto limits=name=="rcm.config"?rcm::cacheFileLimits():storage::compatibleConfigLimits();auto doc=core::Document::parse(bytes,limits);need(bool(doc),"Discovery save cannot be parsed");if(file){need(unique(doc->root()),"Ambiguous discovery save");for(std::size_t at=0;at<doc->root().children.size();at+=2){replace(file->document(),doc->root().children[at].bytes,encoded(doc->root().children[at+1],limits));}done(file->save(*lease));}else{file=take(storage::ConfigFile::createMissing(*lease,name,bytes,limits));}current=resilientStamp(lease->directoryHandle(),name.c_str());
 }
 void flush(){if(!catalog->dirty()&&!catalog->deletedDirty()){return;}
  if(catalog->deletedDirty()){write("rcmx.config",model(catalog->encodeDeleted()),deletedFile,deletedStamp);}
  if(catalog->dirty()){
   if(!config->document().find("core.crypto.obs.id")){std::array<unsigned char,20> key{};need(RAND_bytes(key.data(),key.size())==1,"Cannot generate original obfuscation identity");replace(config->document(),"core.crypto.obs.id",b(std::string_view(reinterpret_cast<const char*>(key.data()),key.size())));done(config->save(*lease));configStamp=stamp(lease->directoryHandle(),"biglybt.config");}
   need(config->document().find("core.crypto.obs.id")->type==Node::Type::Bytes,"Invalid original obfuscation identity type");write("rcm.config",model(catalog->encode(obs())),cacheFile,cacheStamp);
  }
  catalog->saved();replace(config->document(),"rcm.numunread.cache",n(catalog->unread()));done(config->save(*lease));configStamp=stamp(lease->directoryHandle(),"biglybt.config");
 }
};
SwarmDiscoveryProfile::SwarmDiscoveryProfile(std::unique_ptr<Impl> value):impl_(std::move(value)){}
SwarmDiscoveryProfile::~SwarmDiscoveryProfile()=default;
std::expected<std::shared_ptr<SwarmDiscoveryProfile>, Error>
SwarmDiscoveryProfile::create(const storage::ProfileLease& lease) {
 return guard([&]() -> std::expected<std::shared_ptr<SwarmDiscoveryProfile>, Error> {
  auto out = std::make_unique<Impl>();
  out->lease = take(lease.retain());
  auto access = take(lease.sourceAccess());
  out->openSettings();
  if (auto startup=storage::TagManagerStartupMode::capture(lease)) { out->tagStartup=std::move(*startup); }
  // Category IDs belong to startup construction. A changed definition source
  // remains unavailable until its committed live lineage is available here.
  static_cast<void>(guard([&]() -> std::expected<void,Error> { out->capturePublicCategories(); return {}; }));
  // The original manager initializes this ID when it opens. Complete the
  // configuration save before starting the background discovery worker and
  // before network owners bind that file as their admitted source.
  if (out->settings.enabled && ensureRandomId(out->config->document())) {
   done(out->config->save(lease));
   out->configStamp = stamp(lease.directoryHandle(), "biglybt.config");
  }
  return std::shared_ptr<SwarmDiscoveryProfile>(new SwarmDiscoveryProfile(std::move(out)));
 });
}
std::expected<rcm::Settings,Error> SwarmDiscoveryProfile::searchPermission(){return guard([&]()->std::expected<rcm::Settings,Error>{auto access=take(impl_->lease->sourceAccess());impl_->openSettings();return impl_->settings;});}
std::expected<SwarmSearchSources,Error> SwarmDiscoveryProfile::searchItems(std::span<const std::string> values){return guard([&]()->std::expected<SwarmSearchSources,Error>{auto access=take(impl_->lease->sourceAccess());impl_->open();SwarmSearchSources out;out.settings=impl_->settings;if(!out.settings.enabled){return out;}need(values.size()<=8192,"DHT search snapshot exceeds its bound");impl_->inventory();auto allowed=[&](rcm::Item& item){if(item.hash.size()!=20||!(item.networks&1)){return false;}const auto hash=core::TorrentHash::parse(item.hash);if(hash&&impl_->rows.contains(hash->hex())&&!impl_->source(item.hash)){return false;}item.tracker.clear();return true;};for(auto item:impl_->catalog->items()){if(allowed(item)){out.local.push_back(std::move(item));}}std::size_t bytes=0;for(const auto& raw:values){bytes+=raw.size();need(raw.size()<=512&&bytes<=4*1024*1024,"DHT search payload exceeds its bound");auto item=rcm::decodeAssociation(raw,{},1,false,std::int32_t(QDateTime::currentSecsSinceEpoch()));if(item&&allowed(*item)){out.dht.push_back(std::move(*item));}}return out;});}
std::expected<std::shared_ptr<const SwarmDiscoverySnapshot>,Error> SwarmDiscoveryProfile::refresh(){return guard([&]()->std::expected<std::shared_ptr<const SwarmDiscoverySnapshot>,Error>{auto access=take(impl_->lease->sourceAccess());impl_->open();if(!impl_->snapshot||impl_->snapshot->revision!=impl_->revision){impl_->publish();}return impl_->snapshot;});}
std::expected<void,Error> SwarmDiscoveryProfile::change(const SwarmDiscoveryChanges& changes){return guard([&]()->std::expected<void,Error>{auto access=take(impl_->lease->sourceAccess(true));impl_->open();if(changes.settings){auto latest=take(storage::ConfigFile::load(*impl_->lease,"biglybt.config"));const auto& s=*changes.settings;need(s.maximum>=0&&s.depth>=1&&s.depth<=100&&s.sources.size()<=1024,"Discovery settings exceed supported bounds");for(auto [key,value]:{std::pair{"rcm.max_results",std::int64_t(s.maximum)},{"Plugin.aercm.rcm.config.max_results",s.maximum},{"rcm.max_search_level",s.depth},{"Plugin.aercm.rcm.config.max_level",s.depth},{"rcm.overall.enabled",s.enabled?1:0},{"Plugin.aercm.rcm.ui.enable",s.uiEnabled?1:0},{"rcm.global.filter.active_only",s.activeOnly?1:0},{"rcm.use.bigly.dht",s.useBigly?1:0},{"rcm.use.torrent.names",s.useTorrentNames?1:0}}){replace(latest.document(),key,n(value));}std::string sources="l";for(const auto& source:s.sources){need(source.size()<=1024,"Discovery source filter exceeds its bound");sources+=b(source);}sources+='e';replace(latest.document(),"Plugin.aercm.sources.setlist",std::move(sources));const auto defaults=model(rcm::settings(*core::Document::parse("de"))).sources;const bool isDefault=s.sources.size()==defaults.size()&&std::ranges::all_of(defaults,[&](const auto& value){return std::ranges::find(s.sources,value)!=s.sources.end();});replace(latest.document(),"Plugin.aercm.sources.isdefault",n(isDefault?1:0));if(s.enabled){ensureRandomId(latest.document());}done(latest.save(*impl_->lease));impl_->config=std::move(latest);impl_->configStamp=stamp(impl_->lease->directoryHandle(),"biglybt.config");impl_->settings=model(rcm::settings(impl_->config->document()));impl_->catalog->enforceMaximum(s.maximum);}
 if(changes.tableLayout){auto parsed=core::Document::parse(*changes.tableLayout,storage::compatibleConfigLimits());need(parsed&&unique(parsed->root())&&changes.tableLayout->size()<=1024*1024,"Invalid discoveries table layout");const auto stampValue=resilientStamp(impl_->lease->directoryHandle(),"tables.config");if(presentResilient(stampValue)){auto tables=take(storage::ConfigFile::load(*impl_->lease,"tables.config"));replace(tables.document(),"Table.RCM",*changes.tableLayout);done(tables.save(*impl_->lease));}else{auto file=take(storage::ConfigFile::createMissing(*impl_->lease,"tables.config","d9:Table.RCM"+*changes.tableLayout+"e"));}impl_->tableLayout=*changes.tableLayout;impl_->tableStamp=resilientStamp(impl_->lease->directoryHandle(),"tables.config");}
 ++impl_->revision;impl_->catalog->read(changes.read);impl_->catalog->read(changes.unread,true);impl_->catalog->remove(changes.removed);if(changes.clearDeleted){impl_->catalog->resetDeleted();}impl_->flush();impl_->publish();return {};});}
std::expected<std::optional<SwarmDiscoverySource>,Error> SwarmDiscoveryProfile::source(std::string_view hash){return guard([&]()->std::expected<std::optional<SwarmDiscoverySource>,Error>{auto access=take(impl_->lease->sourceAccess());return impl_->source(hash);});}
void SwarmDiscoveryProfile::observeCategoryDefinitionChange(const storage::ProfileLease& owner,
    const storage::CategorySnapshot& before,const storage::CategorySnapshot& after,
    std::string_view name,bool added) noexcept {
 try {
  if (!impl_->lease->sharesOwnership(owner) || !before.valid() || !after.writable()) { return; }
  const core::SavedCategoryEdit edit=added ? core::SavedCategoryEdit(core::SavedCategoryCreate{std::string(name)})
      : core::SavedCategoryEdit(core::SavedCategoryDelete{std::string(name)});
  const auto expected=core::editSavedCategories(before.categories()->source->originalBytes(),edit);
  if (!expected || !expected->changed ||
      expected->value.source->originalBytes()!=after.categories()->source->originalBytes()) { return; }
  using Change=core::tags::category::DefinitionChange;
  const auto target=QString::fromUtf8(name.data(),qsizetype(name.size())).toStdU16String();
  const Change change{added?Change::Operation::Added:Change::Operation::Removed,target};
  const auto names=categoryUserNames(*after.categories());
  std::lock_guard lock(impl_->categoryMutex);
  if (!impl_->publicCategoryRoster || !impl_->publicCategorySource.sameDefinitionSource(before)) { return; }
  if (impl_->publicCategoryRoster->observe(names,change)) {
   impl_->publicCategorySource=after;
  }
 } catch (...) {
  // An unobserved change leaves the old physical source mismatched.
 }
}
std::expected<std::optional<std::string>,Error> SwarmDiscoveryProfile::publication(
    std::string_view parent,std::string_view child,std::int64_t fileSize) {
 if (parent.size()!=20 || child.size()!=20) { return std::nullopt; }
 return guard([&]() -> std::expected<std::optional<std::string>,Error> {
  auto access=take(impl_->lease->sourceAccess());
  auto from=impl_->source(parent);
  if (!from || number(impl_->config->document().find(RandomId),-1)==-1) { return std::nullopt; }
  auto to=parent==child ? from : impl_->source(child);
  if (!to || (fileSize && std::ranges::find(to->fileSizes,fileSize)==to->fileSizes.end())) {
   return std::nullopt;
  }
  impl_->openPublicManualTags();
  static_cast<void>(impl_->appendPublicCategoryTag(to->item,to->savedCategory));
  impl_->appendPublicManualTags(to->item);
  if (!fileSize) {
   static_cast<void>(impl_->appendPublicCategoryTag(from->item,from->savedCategory));
   impl_->appendPublicManualTags(from->item);
  }
  // The size-association job verifies a child and its file size.
  return model(rcm::encodeAssociation(to->item,fileSize ? nullptr : &from->item));
 });
}
bool SwarmTagUpdate::current(const storage::ProfileLease& owner,const storage::TorrentStateFile& state) const {
 if (!lease || !lease->sharesOwnership(owner) || std::chrono::steady_clock::now()>=end ||
     state.recoveredBackup() || state.hash()!=hash) { return false; }
 const auto matched=guard([&]() -> std::expected<bool,Error> {
  auto access=take(owner.sourceAccess());
  return swarmTagSourceDigest(owner,state,savedLabels)==stateDigest;
 });
 return matched && *matched;
}

std::expected<std::optional<std::string>,Error> SwarmTagUpdate::encodeCurrentAttributes(
    const storage::ProfileLease& owner,const storage::TorrentStateFile& state) const {
 return guard([&]() -> std::expected<std::optional<std::string>,Error> {
  if (!current(owner,state)) { return std::nullopt; }
  const auto* attributes=state.document().find("attributes");
  if (!attributes || !unique(*attributes)) { return std::nullopt; }
  const auto limits=storage::compatibleConfigLimits();
  auto original=core::encodeValue(*attributes,limits);
  if (!original) { return std::nullopt; }
  auto edited=core::Document::parse(std::move(*original),limits);
  if (!edited) { return std::nullopt; }
  std::string tags="l";
  for (const auto& name : proposed) { tags+=b(name); }
  if (!edited->replaceValue("stag",tags+'e')) { return std::nullopt; }
  return edited->serialize();
 });
}

std::expected<std::optional<SwarmTagProbe>,Error> SwarmDiscoveryProfile::beginSwarmTagProbe(
    std::string_view parent,SwarmTagLookup lookup) {
 if (parent.size()!=20) { return std::nullopt; }
 return guard([&]() -> std::expected<std::optional<SwarmTagProbe>,Error> {
  auto access=take(impl_->lease->sourceAccess());
  auto source=impl_->source(parent);
  if (!source || source->item.networks!=1 || number(impl_->config->document().find(RandomId),-1)==-1) {
   return std::nullopt;
  }
  const auto hash=core::TorrentHash::parse(parent);
  if (!hash) { return std::nullopt; }
  auto store=take(storage::TorrentStateStore::open(*impl_->lease));
  auto state=take(store.loadAttributes(*impl_->lease,*hash));
  const auto* attributes=state.document().find("attributes");
  if (state.recoveredBackup() || !attributes || !unique(*attributes)) { return std::nullopt; }
  const auto* category=attributes->find("category");
  if (category && category->type!=Node::Type::Bytes) { return std::nullopt; }
  SwarmTagProbe capture;
  capture.lookup=lookup;
  if (const auto* prior=attributes->find("stag")) {
   if (prior->type!=Node::Type::List || prior->children.size()>128 ||
       (lookup==SwarmTagLookup::Publication && prior->children.size()>=16)) { return std::nullopt; }
   for (const auto& element : prior->children) {
    if (element.type!=Node::Type::Bytes || element.bytes.size()>128) { return std::nullopt; }
    capture.oldTags.emplace_back(element.bytes);
   }
  }
  impl_->openPublicManualTags();
  if (!stamp(impl_->lease->directoryHandle(),"tag.config.saving").empty() ||
      !impl_->appendPublicCategoryTag(source->item,text(category))) { return std::nullopt; }
  impl_->appendPublicManualTags(source->item);
  capture.lease=impl_->lease;
  capture.parent=std::move(source->item);
  capture.hash=*hash;
  if (lookup!=SwarmTagLookup::Publication) { capture.cachedTags=impl_->catalog->tagsForHash(parent); }
  if (lookup==SwarmTagLookup::LibraryScan) {
   if (!impl_->assignedTagDiscoveryNames(*hash)) { return std::nullopt; }
   const auto title=text(attributes->find("displayname"));
   if (!title.empty()) { capture.parent.title=title; }
  }
  capture.savedLabels=impl_->tagStartup && impl_->tagStartup->enabled();
  const auto digest=swarmTagSourceDigest(*impl_->lease,state,capture.savedLabels);
  if (!digest) { return std::nullopt; }
  capture.stateDigest=*digest;
  return capture;
 });
}

bool SwarmDiscoveryProfile::isCurrentTagProbe(const SwarmTagProbe& capture) {
 if (!capture.lease || !capture.lease->sharesOwnership(*impl_->lease)) { return false; }
 const auto current=take(beginSwarmTagProbe(capture.parent.hash,capture.lookup));
 return current && current->hash==capture.hash && current->stateDigest==capture.stateDigest &&
     current->savedLabels==capture.savedLabels && current->parent.random==capture.parent.random &&
     current->parent.tags==capture.parent.tags;
}
SwarmTagUpdate SwarmDiscoveryProfile::savedTagUpdate(
    const SwarmTagProbe& capture,std::vector<std::string> names) {
 SwarmTagUpdate update;
 update.hash=capture.hash;
 update.lease=capture.lease;
 update.stateDigest=capture.stateDigest;
 update.proposed=std::move(names);
 update.savedLabels=capture.savedLabels;
 update.end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 return update;
}
std::expected<std::optional<SwarmTagUpdate>,Error> SwarmDiscoveryProfile::completeSwarmTagProbe(
    const SwarmTagProbe& capture,std::span<const std::string> replies) {
 return guard([&]() -> std::expected<std::optional<SwarmTagUpdate>,Error> {
  auto access=take(impl_->lease->sourceAccess());
  if (capture.lookup!=SwarmTagLookup::Publication || !isCurrentTagProbe(capture)) { return std::nullopt; }
  auto names=model(rcm::mergePublicationTags(replies,capture.oldTags,capture.parent));
  return names ? std::optional{savedTagUpdate(capture,std::move(*names))} : std::nullopt;
 });
}
std::expected<std::optional<SwarmAttributeResult>,Error> SwarmDiscoveryProfile::completeDirectTagLookup(
    const SwarmTagProbe& capture,std::span<const std::string> replies) {
 return guard([&]() -> std::expected<std::optional<SwarmAttributeResult>,Error> {
  auto access=take(impl_->lease->sourceAccess());
  if (capture.lookup==SwarmTagLookup::Publication || !isCurrentTagProbe(capture)) { return std::nullopt; }
  auto parsed=model(rcm::explicitAttributeTags(replies,capture.oldTags,capture.parent.tags,capture.cachedTags));
  SwarmAttributeResult result{std::move(parsed.found),{},capture.parent.title};
  if (capture.lookup==SwarmTagLookup::LibraryScan) {
   const auto assigned=impl_->assignedTagDiscoveryNames(capture.hash);
   if (!assigned) { return std::nullopt; }
   result.found=model(rcm::tagDiscoverySuggestions(result.found,*assigned));
  }
  if (parsed.saved) { result.saved=savedTagUpdate(capture,std::move(*parsed.saved)); }
  return result;
 });
}
std::expected<bool,Error> SwarmDiscoveryProfile::permitted(std::string_view hash,bool derived){return guard([&]()->std::expected<bool,Error>{auto access=take(impl_->lease->sourceAccess());impl_->open();impl_->inventory();if(!impl_->settings.enabled||hash.size()!=20){return false;}const auto parsed=core::TorrentHash::parse(hash);if(parsed&&impl_->rows.contains(parsed->hex())){return impl_->source(hash).has_value();}if(!derived){return false;}return impl_->publicHash(hash);});}
std::expected<std::vector<std::string>,Error> SwarmDiscoveryProfile::libraryTagScanSubjects(){
 return guard([&]() -> std::expected<std::vector<std::string>,Error> {
  auto access=take(impl_->lease->sourceAccess());
  impl_->open();
  if(!impl_->settings.enabled||!impl_->tagStartup||!impl_->tagStartup->enabled()||
      !impl_->tagStartup->matches(*impl_->lease)){return std::vector<std::string>{};}
  impl_->inventory();
  std::vector<std::string> subjects;
  for(const auto& row:impl_->library->view.items){
   if(!row.saved.hash||!row.saved.persistent||impl_->duplicates.contains(row.saved.hash->hex())){continue;}
   const auto hash=row.saved.hash->bytes();
   if(hash.size()==20){subjects.emplace_back(reinterpret_cast<const char*>(hash.data()),hash.size());}
  }
  return subjects;
 });
}
std::expected<std::vector<SwarmDiscoverySource>, Error>
SwarmDiscoveryProfile::candidates(std::size_t count) {
 return guard([&]() -> std::expected<std::vector<SwarmDiscoverySource>, Error> {
  auto access = take(impl_->lease->sourceAccess());
  impl_->open();
  impl_->inventory();
  std::vector<SwarmDiscoverySource> out;
  if (!impl_->settings.enabled || impl_->owned.empty()) { return out; }
  need(number(impl_->config->document().find(RandomId), -1) != -1,
       "Original discovery identity is unavailable; reopen the profile");
  const auto max = std::min({count, std::size_t(16), impl_->owned.size()});
  for (std::size_t i = 0; i < max; ++i) {
   const auto& hash = impl_->owned[impl_->cursor++ % impl_->owned.size()];
   if (auto candidate = impl_->source(hash)) { out.push_back(std::move(*candidate)); }
  }
  return out;
 });
}
std::expected<bool,Error> SwarmDiscoveryProfile::ingest(std::span<const std::string> values,std::string_view parent,int level,bool explicitLookup){return guard([&]()->std::expected<bool,Error>{auto access=take(impl_->lease->sourceAccess(true));impl_->open();impl_->inventory();if(!impl_->settings.enabled){return false;}need(values.size()<=64,"Discovery result bound exceeded");bool changed=false;const auto now=std::int32_t(QDateTime::currentSecsSinceEpoch());for(const auto& value:values){if(auto item=rcm::decodeAssociation(value,parent,level,explicitLookup,now)){changed|=impl_->catalog->ingest(std::move(*item),impl_->settings,impl_->owned);}}if(changed){++impl_->revision;impl_->publish();}return changed;});}
std::expected<void,Error> SwarmDiscoveryProfile::flush(){return guard([&]()->std::expected<void,Error>{auto access=take(impl_->lease->sourceAccess(true));impl_->open();impl_->flush();impl_->publish();return {};});}
}

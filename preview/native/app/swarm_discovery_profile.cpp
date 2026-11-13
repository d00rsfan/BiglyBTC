// SPDX-License-Identifier: GPL-2.0-or-later
#include "swarm_discovery_profile.hpp"
#include "library_view.hpp"
#include "torrent_state.hpp"
#include "original_torrent_identity_lt.hpp"
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
#include <limits>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
namespace biglybtc::app {
namespace {
using storage::Error;using storage::ErrorCode;using Node=core::Node;
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
std::string stamp(int dir,const char* name){struct stat v{};if(::fstatat(dir,name,&v,AT_SYMLINK_NOFOLLOW)){return {};}need(S_ISREG(v.st_mode)&&v.st_uid==::geteuid()&&v.st_nlink==1&&!(v.st_mode&0002),"Unsafe related-content source file");return std::to_string(v.st_dev)+":"+std::to_string(v.st_ino)+":"+std::to_string(v.st_size)+":"+std::to_string(v.st_mtim.tv_sec)+":"+std::to_string(v.st_mtim.tv_nsec)+":"+std::to_string(v.st_ctim.tv_sec)+":"+std::to_string(v.st_ctim.tv_nsec);}
std::string resilientStamp(int root,const char* name){std::string base=name;return stamp(root,name)+"|"+stamp(root,(base+".bak").c_str())+"|"+stamp(root,(base+".saving").c_str());}
bool presentResilient(const std::string& value){return value!="||";}
void replace(core::Document& d,std::string_view key,std::string v){need(bool(d.replaceValue(key,std::move(v))),"Invalid related-content field update");}
std::string encoded(const Node& v){auto x=core::encodeValue(v,storage::compatibleConfigLimits());need(bool(x),"Discovery source metadata exceeds its bound");return std::move(*x);}
std::string domains(const core::Document& d){std::set<std::string> hosts;const auto add=[&](const Node* v){if(!v||v->type!=Node::Type::Bytes){return;}const QUrl url(QString::fromUtf8(v->bytes.data(),qsizetype(v->bytes.size())));if(url.scheme()!="http"&&url.scheme()!="udp"){return;}const auto host=url.host().toLower();if(host.contains(':')){return;}const auto bits=host.split('.');if(bits.size()<2||std::ranges::all_of(bits.back(),[](QChar c){return c.isDigit();})){return;}hosts.insert((bits[bits.size()-2]+"."+bits.back()).toStdString());};add(d.find("announce"));if(const auto* list=d.find("announce-list");list&&list->type==Node::Type::List){for(const auto& tier:list->children){if(tier.type==Node::Type::List){for(const auto& url:tier.children){add(&url);}}}}std::string out;for(const auto& domain:hosts){if(out.size()==32){break;}std::uint32_t hash=0;for(auto c:QString::fromUtf8(domain)){hash=31*hash+c.unicode();}for(int shift=24;shift>=0;shift-=8){out+=char(hash>>shift);}}return out;}
}
struct SwarmDiscoveryProfile::Impl {
 std::shared_ptr<const storage::ProfileLease> lease;std::optional<storage::ConfigFile> config,cacheFile,deletedFile;
 std::optional<rcm::Catalog> catalog;rcm::Settings settings;std::string cacheStamp,deletedStamp,configStamp,libraryStamp;
 std::optional<storage::LibrarySourceSnapshot> library;std::map<std::string,const storage::LibraryItem*> rows;std::map<std::string,const Node*> cached;
 std::vector<rcm::Hash> owned;std::set<std::string> duplicates;std::size_t cursor=0;std::uint64_t revision=0;std::string status="Saved discoveries";
 std::shared_ptr<const SwarmDiscoverySnapshot> snapshot;std::string tableLayout="de",tableStamp;
 struct Metadata {std::string stamp;bool torrentNames=false;std::optional<SwarmDiscoverySource> value;};std::map<std::string,Metadata> metadata;
 std::string obs()const{return text(config->document().find("core.crypto.obs.id"));}
 void open(){
  const int root=lease->directoryHandle();const auto cfg=stamp(root,"biglybt.config"),cache=resilientStamp(root,"rcm.config"),del=resilientStamp(root,"rcmx.config");
  need(!cfg.empty(),"Current original configuration required for discoveries");
  if(!config||configStamp!=cfg){config=take(storage::ConfigFile::load(*lease,"biglybt.config"));need(config->recoverySource()==storage::RecoverySource::Primary&&unique(config->document().root()),"Current unambiguous discovery settings required");settings=model(rcm::settings(config->document()));configStamp=cfg;++revision;}
  const auto tables=resilientStamp(root,"tables.config");if(tables!=tableStamp){tableLayout="de";if(presentResilient(tables)){auto file=take(storage::ConfigFile::load(*lease,"tables.config"));const auto& doc=file.document();const auto* table=doc.find("Table.RCM");if(!table){table=doc.find("TableDefault.RCM");}if(table){need(unique(*table),"Ambiguous discoveries table layout");tableLayout=encoded(*table);}}tableStamp=tables;++revision;}
  if(!catalog||cacheStamp!=cache||deletedStamp!=del){need(!catalog||(!catalog->dirty()&&!catalog->deletedDirty()),"Discovery cache changed during a pending save; retained without overwrite");std::string raw="de",deleted="de";
   if(presentResilient(cache)){cacheFile=take(storage::ConfigFile::load(*lease,"rcm.config"));raw=cacheFile->document().serialize();}else{cacheFile.reset();}
   if(presentResilient(del)){deletedFile=take(storage::ConfigFile::load(*lease,"rcmx.config"));deleted=deletedFile->document().serialize();}else{deletedFile.reset();}
   if(cacheFile&&cacheFile->document().find("d")){const auto* key=config->document().find("core.crypto.obs.id");need(key&&key->type==Node::Type::Bytes,"Original core.crypto.obs.id required for the existing discoveries cache");}
   catalog=model(rcm::Catalog::decode(std::move(raw),obs(),std::move(deleted)));cacheStamp=cache;deletedStamp=del;catalog->enforceMaximum(settings.maximum);++revision;
  }
 }
 void inventory(){
  const int root=lease->directoryHandle();const auto downloads=stamp(root,"downloads.config");need(!downloads.empty(),"Current saved library required for discovery permission");int active=::openat(root,"active",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);need(active>=0,"Original active directory required for discovery");std::string cache;try{cache=stamp(active,"cache.dat");}catch(...){::close(active);throw;}::close(active);const auto current=downloads+"|"+cache;
  if(library&&current==libraryStamp){return;}library=take(storage::readLibrarySource(*lease));need(!library->view.cacheWarning,"Original state cache is unreadable; discovery source withheld");rows.clear();cached.clear();owned.clear();duplicates.clear();metadata.clear();
  for(const auto& row:library->view.items){if(!row.saved.hash){continue;}const auto key=row.saved.hash->hex();if(!rows.emplace(key,&row).second){duplicates.insert(key);}const auto raw=row.saved.hash->bytes();owned.emplace_back(reinterpret_cast<const char*>(raw.data()),raw.size());}
  if(library->cacheDocument){if(const auto* list=library->cacheDocument->document().find("state");list&&list->type==Node::Type::List){for(const auto& entry:list->children){const auto raw=text(entry.find("hash"));if(auto hash=core::TorrentHash::parse(raw)){cached.insert_or_assign(hash->hex(),&entry);}}}}
  libraryStamp=current;++revision;
 }
 std::optional<SwarmDiscoverySource> source(std::string_view bytes){
  open();inventory();if(!settings.enabled){return {};}const auto hash=core::TorrentHash::parse(bytes);if(!hash){return {};}const auto key=hash->hex();auto found=rows.find(key);if(found==rows.end()||duplicates.contains(key)||!found->second->saved.persistent){return {};}
  int active=::openat(lease->directoryHandle(),"active",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);need(active>=0,"Original active-state directory unavailable");std::string sourceStamp;try{sourceStamp=stamp(active,(key+".dat").c_str());}catch(...){::close(active);throw;}::close(active);if(sourceStamp.empty()){return {};}
  auto& memo=metadata[key];if(memo.stamp==sourceStamp&&memo.torrentNames==settings.useTorrentNames){return memo.value;}memo={sourceStamp,settings.useTorrentNames,{}};
  auto store=take(storage::TorrentStateStore::open(*lease));auto state=take(store.load(*lease,*hash));if(state.recoveredBackup()){return {};}auto doc=state.document();if(const auto c=cached.find(key);c!=cached.end()){need(unique(*c->second),"Ambiguous source cache record");for(auto [from,to]:{std::pair{"attributes","attributes"},{"azp","azureus_properties"},{"azpp","azureus_private_properties"}}){if(const auto* v=c->second->find(from)){need(unique(*v),"Ambiguous discovery metadata field");replace(doc,to,encoded(*v));}}}
  const auto* info=doc.find("info"),*attrs=doc.find("attributes"),*properties=doc.find("azureus_properties");need(unique(doc.root())&&info&&unique(*info)&&(!attrs||unique(*attrs))&&(!properties||unique(*properties)),"Ambiguous discovery source identity");
  const auto encoded=doc.serialize();libtorrent::error_code error;libtorrent::torrent_info torrent(libtorrent::span<const char>(encoded.data(),encoded.size()),error,libtorrent::from_span);if(error){return {};}const auto identity=storage::originalTorrentIdentityFromLibtorrent(doc,torrent);if(!identity||!storage::validateOriginalTorrentKey(*identity,*hash,true)||torrent.priv()||(identity->full.v1&&identity->full.v2&&identity->selected==storage::OriginalSelectedHash::V2)){return {};}
  for(const auto* field:{properties?properties->find("torrent_flags"):nullptr,attrs?attrs->find("flags"):nullptr,info->find("private")}){if(field&&field->type!=Node::Type::Integer){return {};}}
  if((number(properties?properties->find("torrent_flags"):nullptr)&4)||(number(attrs?attrs->find("flags"):nullptr)&0x210)){return {};}
  std::uint8_t networks=0;if(const auto* list=attrs?attrs->find("networks"):nullptr;list&&list->type==Node::Type::List){for(const auto& net:list->children){need(net.type==Node::Type::Bytes,"Malformed original source networks");if(net.bytes=="Public"){networks|=1;}else if(net.bytes=="I2P"){networks|=2;}else if(net.bytes=="Tor"){networks|=4;}}}if(!(networks&1)){return {};}
  SwarmDiscoverySource result;auto& item=result.item;const auto raw=hash->bytes();item.hash.assign(reinterpret_cast<const char*>(raw.data()),std::min<std::size_t>(20,raw.size()));item.key=rcm::base32(item.hash);item.title=torrent.name();if(!settings.useTorrentNames){if(const auto display=text(attrs?attrs->find("displayname"):nullptr);!display.empty()){item.title=display;}}
  item.size=torrent.total_size();item.networks=networks;item.trackerKeys=domains(doc);item.firstSeen=QDateTime::currentSecsSinceEpoch();item.lastSeen=item.firstSeen;
  const auto* params=attrs?attrs->find("parameters"):nullptr;const auto random=std::uint64_t(number(config->document().find("rcm.random.id")))^std::uint64_t(number(params?params->find("rand"):nullptr));item.random=std::bit_cast<std::int32_t>(std::uint32_t(random));
  const auto scrape=number(attrs?attrs->find("scrapecache"):nullptr,-1);if(scrape!=-1){item.packedPeers=std::bit_cast<std::int32_t>((std::uint32_t(std::uint64_t(scrape)>>32)<<16)|(std::uint32_t(scrape)&65535));}item.publishedHours=std::int32_t(number(doc.find("creation date"))/3600);
  for(int index=0;index<torrent.num_files();++index){const auto size=torrent.files().file_size(libtorrent::file_index_t(index));if(size>=50*1024*1024){result.fileSizes.push_back(size);}}
  memo.value=std::move(result);return memo.value;
 }
 void publish(){auto value=std::make_shared<SwarmDiscoverySnapshot>();value->revision=++revision;value->settings=settings;value->tableLayout=tableLayout;value->items=catalog->items();value->unread=catalog->unread();value->knownLibrary=owned.size();value->status=status;snapshot=std::move(value);}
 void write(std::string name,std::string bytes,std::optional<storage::ConfigFile>& file,std::string& current){
  auto doc=core::Document::parse(bytes,storage::compatibleConfigLimits());need(bool(doc),"Discovery save cannot be parsed");if(file){need(unique(doc->root()),"Ambiguous discovery save");for(std::size_t at=0;at<doc->root().children.size();at+=2){replace(file->document(),doc->root().children[at].bytes,encoded(doc->root().children[at+1]));}done(file->save(*lease));}else{file=take(storage::ConfigFile::createMissing(*lease,name,bytes));}current=resilientStamp(lease->directoryHandle(),name.c_str());
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
std::expected<std::shared_ptr<SwarmDiscoveryProfile>,Error> SwarmDiscoveryProfile::create(const storage::ProfileLease& lease){return guard([&]()->std::expected<std::shared_ptr<SwarmDiscoveryProfile>,Error>{auto out=std::make_unique<Impl>();out->lease=take(lease.retain());return std::shared_ptr<SwarmDiscoveryProfile>(new SwarmDiscoveryProfile(std::move(out)));});}
std::expected<rcm::Settings,Error> SwarmDiscoveryProfile::searchPermission(){return guard([&]()->std::expected<rcm::Settings,Error>{auto access=take(impl_->lease->sourceAccess());impl_->open();return impl_->settings;});}
std::expected<SwarmSearchSources,Error> SwarmDiscoveryProfile::searchItems(std::span<const std::string> values){return guard([&]()->std::expected<SwarmSearchSources,Error>{auto access=take(impl_->lease->sourceAccess());impl_->open();SwarmSearchSources out;out.settings=impl_->settings;if(!out.settings.enabled){return out;}need(values.size()<=8192,"DHT search snapshot exceeds its bound");impl_->inventory();auto allowed=[&](rcm::Item& item){if(item.hash.size()!=20||!(item.networks&1)){return false;}const auto hash=core::TorrentHash::parse(item.hash);if(hash&&impl_->rows.contains(hash->hex())&&!impl_->source(item.hash)){return false;}item.tracker.clear();return true;};for(auto item:impl_->catalog->items()){if(allowed(item)){out.local.push_back(std::move(item));}}std::size_t bytes=0;for(const auto& raw:values){bytes+=raw.size();need(raw.size()<=512&&bytes<=4*1024*1024,"DHT search payload exceeds its bound");auto item=rcm::decodeAssociation(raw,{},1,false,std::int32_t(QDateTime::currentSecsSinceEpoch()));if(item&&allowed(*item)){out.dht.push_back(std::move(*item));}}return out;});}
std::expected<std::shared_ptr<const SwarmDiscoverySnapshot>,Error> SwarmDiscoveryProfile::refresh(){return guard([&]()->std::expected<std::shared_ptr<const SwarmDiscoverySnapshot>,Error>{auto access=take(impl_->lease->sourceAccess());impl_->open();if(!impl_->snapshot||impl_->snapshot->revision!=impl_->revision){impl_->publish();}return impl_->snapshot;});}
std::expected<void,Error> SwarmDiscoveryProfile::change(const SwarmDiscoveryChanges& changes){return guard([&]()->std::expected<void,Error>{auto access=take(impl_->lease->sourceAccess(true));impl_->open();if(changes.settings){auto latest=take(storage::ConfigFile::load(*impl_->lease,"biglybt.config"));const auto& s=*changes.settings;need(s.maximum>=0&&std::size_t(s.maximum)<=rcm::MaximumItems&&s.depth>=1&&s.depth<=100&&s.sources.size()<=1024,"Discovery settings exceed supported bounds");for(auto [key,value]:{std::pair{"rcm.max_results",std::int64_t(s.maximum)},{"Plugin.aercm.rcm.config.max_results",s.maximum},{"rcm.max_search_level",s.depth},{"Plugin.aercm.rcm.config.max_level",s.depth},{"rcm.overall.enabled",s.enabled?1:0},{"Plugin.aercm.rcm.ui.enable",s.uiEnabled?1:0},{"rcm.global.filter.active_only",s.activeOnly?1:0},{"rcm.use.bigly.dht",s.useBigly?1:0},{"rcm.use.torrent.names",s.useTorrentNames?1:0}}){replace(latest.document(),key,n(value));}std::string sources="l";for(const auto& source:s.sources){need(source.size()<=1024,"Discovery source filter exceeds its bound");sources+=b(source);}sources+='e';replace(latest.document(),"Plugin.aercm.sources.setlist",std::move(sources));const auto defaults=model(rcm::settings(*core::Document::parse("de"))).sources;const bool isDefault=s.sources.size()==defaults.size()&&std::ranges::all_of(defaults,[&](const auto& value){return std::ranges::find(s.sources,value)!=s.sources.end();});replace(latest.document(),"Plugin.aercm.sources.isdefault",n(isDefault?1:0));done(latest.save(*impl_->lease));impl_->config=std::move(latest);impl_->configStamp=stamp(impl_->lease->directoryHandle(),"biglybt.config");impl_->settings=model(rcm::settings(impl_->config->document()));impl_->catalog->enforceMaximum(s.maximum);}
 if(changes.tableLayout){auto parsed=core::Document::parse(*changes.tableLayout,storage::compatibleConfigLimits());need(parsed&&unique(parsed->root())&&changes.tableLayout->size()<=1024*1024,"Invalid discoveries table layout");const auto stampValue=resilientStamp(impl_->lease->directoryHandle(),"tables.config");if(presentResilient(stampValue)){auto tables=take(storage::ConfigFile::load(*impl_->lease,"tables.config"));replace(tables.document(),"Table.RCM",*changes.tableLayout);done(tables.save(*impl_->lease));}else{auto file=take(storage::ConfigFile::createMissing(*impl_->lease,"tables.config","d9:Table.RCM"+*changes.tableLayout+"e"));}impl_->tableLayout=*changes.tableLayout;impl_->tableStamp=resilientStamp(impl_->lease->directoryHandle(),"tables.config");}
 impl_->catalog->read(changes.read);impl_->catalog->read(changes.unread,true);impl_->catalog->remove(changes.removed);if(changes.clearDeleted){impl_->catalog->resetDeleted();}impl_->flush();impl_->publish();return {};});}
std::expected<std::optional<SwarmDiscoverySource>,Error> SwarmDiscoveryProfile::source(std::string_view hash){return guard([&]()->std::expected<std::optional<SwarmDiscoverySource>,Error>{auto access=take(impl_->lease->sourceAccess());return impl_->source(hash);});}
std::expected<bool,Error> SwarmDiscoveryProfile::permitted(std::string_view hash,bool derived){return guard([&]()->std::expected<bool,Error>{auto access=take(impl_->lease->sourceAccess());impl_->open();impl_->inventory();if(!impl_->settings.enabled||hash.size()!=20){return false;}const auto parsed=core::TorrentHash::parse(hash);if(parsed&&impl_->rows.contains(parsed->hex())){return impl_->source(hash).has_value();}if(!derived){return false;}const auto rows=impl_->catalog->items();return std::ranges::any_of(rows,[&](const rcm::Item& i){return i.hash==hash&&(i.networks&1);});});}
std::expected<std::vector<SwarmDiscoverySource>,Error> SwarmDiscoveryProfile::candidates(std::size_t count){return guard([&]()->std::expected<std::vector<SwarmDiscoverySource>,Error>{auto access=take(impl_->lease->sourceAccess(true));impl_->open();impl_->inventory();std::vector<SwarmDiscoverySource> out;if(!impl_->settings.enabled||impl_->owned.empty()){return out;}if(number(impl_->config->document().find("rcm.random.id"),-1)==-1){std::uint64_t id=0;need(RAND_bytes(reinterpret_cast<unsigned char*>(&id),sizeof(id))==1,"Cannot generate original discovery identity");if(id==std::numeric_limits<std::uint64_t>::max()){id=0;}replace(impl_->config->document(),"rcm.random.id",n(std::bit_cast<std::int64_t>(id)));done(impl_->config->save(*impl_->lease));impl_->configStamp=stamp(impl_->lease->directoryHandle(),"biglybt.config");impl_->metadata.clear();}const auto max=std::min({count,std::size_t(16),impl_->owned.size()});for(std::size_t n=0;n<max;++n){const auto& hash=impl_->owned[(impl_->cursor++)%impl_->owned.size()];if(auto candidate=impl_->source(hash)){out.push_back(std::move(*candidate));}}return out;});}
std::expected<bool,Error> SwarmDiscoveryProfile::ingest(std::span<const std::string> values,std::string_view parent,int level,bool explicitLookup){return guard([&]()->std::expected<bool,Error>{auto access=take(impl_->lease->sourceAccess(true));impl_->open();impl_->inventory();if(!impl_->settings.enabled){return false;}need(values.size()<=64,"Discovery result bound exceeded");bool changed=false;const auto now=std::int32_t(QDateTime::currentSecsSinceEpoch());for(const auto& value:values){if(auto item=rcm::decodeAssociation(value,parent,level,explicitLookup,now)){changed|=impl_->catalog->ingest(std::move(*item),impl_->settings,impl_->owned);}}if(changed){impl_->publish();}return changed;});}
std::expected<void,Error> SwarmDiscoveryProfile::flush(){return guard([&]()->std::expected<void,Error>{auto access=take(impl_->lease->sourceAccess(true));impl_->open();impl_->flush();impl_->publish();return {};});}
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "swarm_discovery_profile.hpp"
#include <biglybtc/saved_download_tags.hpp>
#include "category_store.hpp"
#ifdef BIGLYBTC_TEST_TAG_DISCOVERY_STORE
#include "manual_tag_identity.hpp"
#endif
#include "original_torrent_identity_lt.hpp"
#include "torrent_state.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QCryptographicHash>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <iostream>
#include <sys/inotify.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cstring>
using namespace biglybtc;namespace rc=services::related_content;namespace lt=libtorrent;
namespace {
unsigned checks=0;void check(bool v,const char* why){++checks;if(!v){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,storage::Error> v){check(bool(v),v?"":v.error().message.c_str());return std::move(*v);}
template<class T>T value(rc::Result<T> v){check(bool(v),v?"":v.error().message.c_str());return std::move(*v);}
void done(std::expected<void,storage::Error> v){check(bool(v),v?"":v.error().message.c_str());}
std::string encoded(const lt::entry& e){std::string out;lt::bencode(std::back_inserter(out),e);return out;}
void write(const QString& name,std::string_view bytes){check(QDir().mkpath(QFileInfo(name).absolutePath()),"Generated directory");QFile f(name);check(f.open(QIODevice::WriteOnly)&&f.write(bytes.data(),bytes.size())==qint64(bytes.size()),"Generated write");}
std::string read(const QString& name){QFile f(name);check(f.open(QIODevice::ReadOnly),"Generated read");return f.readAll().toStdString();}
std::string hash(int start){std::string out(20,'\0');for(int i=0;i<20;++i){out[i]=char(start+i);}return out;}
rc::Item item(int start=1,int level=1,int now=1000){rc::Item i;i.hash=hash(start);i.key=rc::base32(i.hash);i.title="Generated кирилиця 🌍";i.size=1048579;i.version=1;i.random=start;i.packedPeers=(17<<16)|3;i.firstSeen=now;i.lastSeen=now;i.level=level;i.unread=true;i.relatedTo={hash(42)};i.tags={"Generated","Кирилиця"};return i;}
void oracle(const QString& mode,const QString& root){QProcess child;child.setProcessChannelMode(QProcess::MergedChannels);child.start(qEnvironmentVariable("BIGLYBTC_RCM_JAVA"),{"-Dazureus.config.path="+root,"-Duser.home="+root,"--add-opens=java.base/java.net=ALL-UNNAMED","-cp",qEnvironmentVariable("BIGLYBTC_RCM_CLASSPATH"),"RelatedContentOracle",mode,root});check(child.waitForStarted(),"Original oracle starts");if(!child.waitForFinished(15000)){child.kill();child.waitForFinished();throw std::runtime_error("Original oracle deadline");}const auto output=child.readAll();if(child.exitCode()!=0){std::cerr<<output.toStdString();}check(child.exitStatus()==QProcess::NormalExit&&child.exitCode()==0,"Original reference succeeded");}
void originalLargeMaximumSurvivesSavingOtherSettings(){QTemporaryDir root;check(root.isValid(),"Generated2M config root");lt::entry config(lt::entry::dictionary_type{});config["rcm.max_results"]=2000000;config["Plugin.aercm.rcm.config.max_results"]=2000000;config["unrelated"]="Generated opaque original setting";write(root.path()+"/biglybt.config",encoded(config));auto lease=take(storage::ProfileLease::acquire(root.path().toStdString()));auto profile=take(app::SwarmDiscoveryProfile::create(lease));auto settings=value(rc::settings(take(storage::ConfigFile::load(lease,"biglybt.config")).document()));settings.activeOnly=!settings.activeOnly;app::SwarmDiscoveryChanges changes;changes.settings=settings;done(profile->change(changes));auto saved=take(storage::ConfigFile::load(lease,"biglybt.config"));check(saved.document().find("rcm.max_results")->integer==2000000&&saved.document().find("Plugin.aercm.rcm.config.max_results")->integer==2000000,"Both original maximum keys retain2M");check(saved.document().find("unrelated")->bytes=="Generated opaque original setting","Unknown settings survive");}
void nullableSavedRecommendationsRemainLocal(){
 const auto key=std::string("Generated saved recommendation:null");lt::entry record(lt::entry::dictionary_type{});record["_i"]=7;record["_k"]=key;record["d"]="Generated saved recommendation";record["unrelated"]="Generated opaque recommendation field";lt::entry cache(lt::entry::dictionary_type{});cache["rc"]=lt::entry::list_type{record};cache["rcm"]=lt::entry::dictionary_type{};auto catalog=value(rc::Catalog::decode(encoded(cache),"Generated key"));auto rows=catalog.items();check(rows.size()==1&&rows[0].hash.empty()&&rows[0].tracker.empty(),"Original saved recommendations may omit hash and tracker");check(!rc::encodeAssociation(rows[0]),"Saved hashless recommendations gain no Public publication authority");auto saved=value(catalog.encode("Generated key"));auto copy=value(rc::Catalog::decode(saved,"Generated key"));check(copy.size()==1&&saved.find("Generated opaque recommendation field")!=std::string::npos,"Nullable recommendation and unknown fields survive");const std::array keys{key};check(copy.remove(keys),"Nullable recommendation can be removed");auto removed=take(core::Document::parse(value(copy.encodeDeleted()),storage::compatibleConfigLimits()).transform_error([](const auto&){return storage::Error{storage::ErrorCode::InvalidFormat,"Generated deletion map"};}));const auto* entries=removed.find("entries");check(entries&&entries->children.size()==1,"Nullable deletion tombstone recorded");const auto expected=QCryptographicHash::hash(QByteArray::fromStdString(key),QCryptographicHash::Sha1).left(8);check(entries->children[0].bytes==std::string_view(expected.constData(),std::size_t(expected.size())),"Original null tracker deletion key matches Java concatenation");
}
void originalTagDiscoverySuggestionCases(){
 const auto location=QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()+"/fixtures/tag-suggestion-original.bencode";
 const auto original=core::Document::parse(read(location));
 check(bool(original),"Actual original Tag Discovery suggestions fixture");
 const auto strings=[](const core::Node* list){
  check(list&&list->type==core::Node::Type::List,"Original Tag Discovery names list");
  std::vector<std::string> values;
  for(const auto& item:list->children){
   check(item.type==core::Node::Type::Bytes,"Original Tag Discovery exact name bytes");
   values.emplace_back(item.bytes);
  }
  return values;
 };
 const auto candidates=strings(original->find("candidates"));
 const auto* rows=original->find("rows");
 check(rows&&rows->type==core::Node::Type::List&&rows->children.size()==5,"Five original case-fold rows");
 for(const auto& row:rows->children){
  check(value(rc::tagDiscoverySuggestions(candidates,strings(row.find("assigned"))))==
      strings(row.find("suggestions")),"Tag Discovery follows the actual original filter");
 }
 const std::array malformed{std::string("\xff",1)};
 check(!rc::tagDiscoverySuggestions(malformed,{})&&!rc::tagDiscoverySuggestions(candidates,malformed),
     "Malformed native names do not bypass the assigned-name filter");
}
void tagDiscoveryUsesTheExactOriginalNameForAssignment(){
 const auto source=core::readSavedDownloadTags(
     "d1:3d1:1d1:n10:Mixed Casee1:2d1:n12:tag.type.mane1:3d1:n0:eee");
 check(bool(source),"Exact manual names including localized and empty rows");
 const std::optional<std::string> locale{"en"};
 check(take(app::findSavedManualTag(*source,locale,"Mixed Case"))==1,"Exact existing spelling");
 check(!take(app::findSavedManualTag(*source,locale,"mixed CASE")),"Case variant requires a new row");
 check(take(app::findSavedManualTag(*source,locale,"Manual"))==2,"Localized exact lookup");
 check(!take(app::findSavedManualTag(*source,locale,"tag.type.man")),"Original localized lookup ignores the raw alias");
 check(!app::findSavedManualTag(*source,std::nullopt,"Manual"),"Unknown locale cannot establish absence");
 const auto duplicate=core::readSavedDownloadTags("d1:3d1:1d1:n1:Ae1:2d1:n1:Aeee");
 check(duplicate&&!app::findSavedManualTag(*duplicate,locale,"A"),"Ambiguous original names require review");
}
#ifdef BIGLYBTC_TEST_TAG_DISCOVERY_STORE
void selectedTagDiscoveryRowsReuseTheCurrentExactTag(){
 QTemporaryDir root;
 check(root.isValid(),"Three generated Tag Discovery subjects");
 std::vector<core::TorrentHash> hashes;
 lt::entry inventory(lt::entry::dictionary_type{});
 inventory["downloads"]=lt::entry::list_type{};
 for(const auto* title:{"Generated A","Generated B","Generated C"}){
  lt::entry meta(lt::entry::dictionary_type{});
  auto& info=meta["info"];
  info["name"]=title;info["length"]=64;info["piece length"]=16384;info["pieces"]=std::string(20,'X');
  meta["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public")};
  const auto hash=QCryptographicHash::hash(QByteArray::fromStdString(encoded(info)),QCryptographicHash::Sha1);
  const auto identity=core::TorrentHash::parse(hash.toStdString());
  check(bool(identity),"Generated original identity");
  hashes.push_back(*identity);
  const auto state=root.path()+"/active/"+QString::fromStdString(identity->hex())+".dat";
  write(state,encoded(meta));
  lt::entry download(lt::entry::dictionary_type{});
  download["torrent_hash"]=hash.toStdString();download["torrent"]=state.toStdString();
  download["persistent"]=1;download["state"]=75;download["save_dir"]=(root.path()+"/payload").toStdString();
  inventory["downloads"].list().push_back(std::move(download));
 }
 const auto inventoryBytes=encoded(inventory);
 write(root.path()+"/downloads.config",inventoryBytes);
 lt::entry settings(lt::entry::dictionary_type{});
 auto& existing=settings["3"]["1"];
 existing["n"]="Queue matches";existing["c"]["gr"]="Existing group";
 existing["c"]["pub"]=0;existing["c"]["vis"]=0;
 const auto raw=hashes[0].bytes();
 existing["o"]=lt::entry::list_type{lt::entry(rc::base32({reinterpret_cast<const char*>(raw.data()),raw.size()}))};
 lt::entry added(lt::entry::dictionary_type{});added["a"]=100;
 existing["p"]=lt::entry::list_type{added};
 write(root.path()+"/tag.config",encoded(settings));
 auto lease=take(storage::ProfileLease::acquire(root.path().toStdString()));
 auto store=take(storage::ManualTagStore::open(lease));
 std::int64_t time=200;
 const std::array rows{std::pair{1,"Shared suggestion"},std::pair{2,"Queue matches"},
                       std::pair{0,"Shared suggestion"},std::pair{2,"shared suggestion"}};
 for(const auto& [subject,name]:rows){
  auto current=take(store->snapshot());
  const auto hash=hashes[subject];
  const auto bound=take(storage::bindManualTagIdentity(*store,lease,current,hash));
  const auto id=take(app::findSavedManualTag(*current.tags(),std::optional<std::string>{"en"},name));
  auto saved=id?take(store->apply(current,core::SavedTagAddMember{*id,hash,time})):
                take(store->createDiscoveryTag(current,bound,name,time));
  ++time;
  check(saved.changed&&saved.snapshot.generation()==current.generation()+1,"One current save per original selected row");
 }
 done(store->close());
 auto reopened=take(storage::ManualTagStore::open(lease));
 const auto result=take(reopened->snapshot());
 check(result.tags()->tags.size()==3,"Three exact original tag names after reopen");
 const auto found=[&](std::string_view name,const std::vector<core::TorrentHash>& members,bool pub){
  const auto& rows=result.tags()->tags;
  const auto tag=std::ranges::find_if(rows,[&](const auto& row){return row.rawName&&*row.rawName==name;});
  check(tag!=rows.end()&&std::ranges::is_permutation(tag->uniqueHashes,members),"Exact original tag membership");
  const auto* record=result.tags()->source->find("3")->find(tag->idKey);
  const auto* config=record->find("c");
  check(config&&config->find("pub")->integer==pub&&config->find("vis")->integer==pub&&
        config->find("gr")->bytes==(pub?"Discovery":"Existing group"),"Exact original tag visibility, sharing and group");
  check(record->find("p")->children.size()==members.size(),"Original time list stays aligned");
 };
 found("Queue matches",{hashes[0],hashes[2]},false);
 found("Shared suggestion",{hashes[0],hashes[1]},true);
 found("shared suggestion",{hashes[2]},true);
 check(read(root.path()+"/downloads.config")==inventoryBytes&&!QFileInfo::exists(root.path()+"/payload"),
       "Selection does not write the download inventory or payload");
 done(reopened->close());
}
#endif
void modelTests(){
 auto settings=value(rc::settings(*core::Document::parse("de")));check(settings.maximum==500&&settings.depth==3&&settings.enabled,"Original settings defaults");auto cfg=*core::Document::parse("de");check(bool(cfg.replaceValue("rcm.max_results","i17e")),"Setting");check(bool(cfg.replaceValue("Plugin.aercm.rcm.config.max_results","i99e")),"Plugin setting");check(value(rc::settings(cfg)).maximum==17,"Core limit remains authoritative over plugin mirror");
 const auto record=item();auto wire=value(rc::encodeAssociation(record));auto decoded=value(rc::decodeAssociation(wire,hash(42),1,false,1000));check(decoded.hash==record.hash&&decoded.title==record.title&&decoded.seeds()==17&&decoded.leechers()==3,"Original association fields");check(decoded.tags==record.tags,"UTF8 tag codec");check(wire.find("http")==std::string::npos&&wire.find("passkey")==std::string::npos,"No tracker URL in public values");auto privateValue=record;privateValue.tracker="tracker.invalid/private?passkey=generated-secret";check(!rc::encodeAssociation(privateValue),"Private descriptor cannot be published");check(!rc::decodeAssociation(std::string(513,'x'),hash(42),1,false,1000),"Wire value bound");check(!rc::decodeAssociation("d1:di1ee",hash(42),1,false,1000),"Malformed association ignored");
 const auto key=std::string("generated-obfuscation-id");const auto binary=std::string("\0\xff\0",3)+wire;check(rc::obfuscate(rc::obfuscate(binary,key),key)==binary,"RC4 with initial drop symmetric");check(value(rc::gunzip(value(rc::gzip(binary))))==binary,"Bounded gzip roundtrip");check(!rc::gunzip("bad gzip"),"Corrupt gzip refused");
 auto catalog=value(rc::Catalog::decode("de",key));settings.maximum=2;check(catalog.ingest(item(1,1,1000),settings,{}),"First discovery");check(catalog.ingest(item(2,3,1001),settings,{}),"Second discovery");check(catalog.ingest(item(3,2,1002),settings,{}),"Evict deeper lower-ranked finding");check(catalog.size()==2&&!catalog.contains(hash(2)),"Configured results cap");auto extra=item(4,3,1003);check(!catalog.ingest(extra,settings,{}),"Higher-level result cannot displace shallower findings");auto update=item(1,1,1004);update.random=57;check(catalog.ingest(update,settings,{}),"New independent rank vote");check(catalog.items()[0].randoms.size()==2,"Rank counts distinct votes");check(!catalog.ingest(update,settings,{}),"Exact same observation does not duplicate rank");
 const auto keys=std::array{rc::base32(hash(1))};check(catalog.read(keys)&&catalog.unread()==1,"Read state");auto saved=value(catalog.encode(key));auto loaded=value(rc::Catalog::decode(saved,key));check(loaded.size()==2&&loaded.unread()==1,"Native obfuscated cache restart");check(loaded.remove(keys),"Explicit deletion");const auto deleted=value(loaded.encodeDeleted());check(!loaded.ingest(item(1),settings,{}),"Deleted result cannot return");auto cold=value(rc::Catalog::decode(value(loaded.encode(key)),key,deleted));check(!cold.ingest(item(1),settings,{}),"Deletion survives restart");check(cold.resetDeleted()&&cold.ingest(item(1),settings,{}),"Explicit forget-deletion allows rediscovery");check(cold.enforceMaximum(1)==1&&cold.size()==1,"Immediate settings limit enforcement");check(cold.enforceMaximum(0)==1&&cold.size()==0,"Zero configured results");
 settings.maximum=20;settings.activeOnly=true;auto inactive=item();inactive.packedPeers=-1;check(!cold.ingest(inactive,settings,{}),"Original active-only filter");settings.activeOnly=false;const auto owned=std::array{inactive.hash};check(!cold.ingest(inactive,settings,owned),"Own-library hashes excluded");
 rc::Item source=item();source.trackerKeys=std::string("\x4d\x9d\xf6\x61",4);settings.sources={"*"};check(rc::sourceVisible(source,settings),"Wildcard source filter");settings.sources={"example.invalid"};check(!rc::sourceVisible(source,settings),"Configured source filter does not become all sources");
}
void javaTests(){if(!qEnvironmentVariableIsSet("BIGLYBTC_RCM_JAVA")){return;}QTemporaryDir generated;check(generated.isValid(),"Generated oracle root");oracle("emit",generated.path());const auto key=read(generated.path()+"/obs.key");auto catalog=value(rc::Catalog::decode(read(generated.path()+"/java-rcm.config"),key,read(generated.path()+"/java-rcmx.config")));check(catalog.size()==1&&catalog.unread()==1,"Actual original obfuscated cache decoded");const auto rows=catalog.items();check(rows[0].randoms==std::vector<std::int32_t>({11,12})&&rows[0].seeds()==17&&rows[0].relatedTo==std::vector<std::string>{hash(42)},"Original rank/link/peer fields");const auto expected=read(generated.path()+"/java-association.key");const auto actual=rc::associationKey(hash(42));check(std::string(reinterpret_cast<const char*>(actual.data()),actual.size())==expected,"Actual original DHT association key");const auto record=value(rc::decodeAssociation(read(generated.path()+"/java-wire.ben"),hash(42),1,true,1000));check(record.tags.size()>=2&&record.title==rows[0].title,"Original UTF8 wire decoded");const auto keys=std::array{rows[0].key};check(catalog.read(keys),"Native reads original finding");write(generated.path()+"/native-rcm.config",value(catalog.encode(key)));write(generated.path()+"/native-rcmx.config",value(catalog.encodeDeleted()));write(generated.path()+"/native-wire.ben",value(rc::encodeAssociation(record)));oracle("verify",generated.path());auto legacy=value(rc::Catalog::decode(read(generated.path()+"/java-legacy.config"),key));auto modern=value(rc::Catalog::decode(value(legacy.encode(key)),key));check(modern.size()==1&&modern.items()[0].title==record.title,"Original legacy map migrates without losing UTF8 identity");}
void profileTests(){QTemporaryDir root;check(root.isValid(),"Generated profile");QDir().mkpath(root.path()+"/active");lt::entry config(lt::entry::dictionary_type{});config["unknown-root"]="generated-marker";config["rcm.max_results"]=3;config["Plugin.aercm.sources.setlist"]=lt::entry::list_type{lt::entry("*")};write(root.path()+"/biglybt.config",encoded(config));lt::entry downloads(lt::entry::list_type{});std::array<std::string,3> hashes;
 for(int index=0;index<3;++index){lt::entry torrent(lt::entry::dictionary_type{});torrent["info"]["name"]="generated-"+std::to_string(index)+".bin";torrent["info"]["length"]=17;torrent["info"]["piece length"]=16384;torrent["info"]["pieces"]=QCryptographicHash::hash(QByteArray(17,'x'),QCryptographicHash::Sha1).toStdString();if(index==1){torrent["info"]["private"]=1;}torrent["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public"),lt::entry("I2P")};torrent["attributes"]["peersources"]=lt::entry::list_type{lt::entry("Tracker")};torrent["attributes"]["parameters"]["rand"]=7;torrent["azureus_properties"]["dht_backup_enable"]=0;if(index==2){torrent["azureus_properties"]["torrent_flags"]=4;}torrent["announce"]="udp://tracker.invalid/announce?passkey=generated-secret";const auto raw=encoded(torrent);auto doc=*core::Document::parse(raw);lt::error_code error;lt::torrent_info info(lt::span<const char>(raw.data(),raw.size()),error,lt::from_span);check(!error,"Generated metadata");auto identity=storage::originalTorrentIdentityFromLibtorrent(doc,info);check(bool(identity),"Original generated hash identity");const auto hash=identity->originalKey.bytes();hashes[index].assign(reinterpret_cast<const char*>(hash.data()),hash.size());const auto state=root.path()+"/active/"+QString::fromStdString(identity->originalKey.hex())+".dat";write(state,raw);lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=hashes[index];row["torrent"]=state.toStdString();row["save_dir"]=(root.path()+"/payload").toStdString();row["save_file"]="generated.bin";row["persistent"]=1;row["state"]=70;row["position"]=index+1;downloads.list().push_back(row);}
 lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=downloads;write(root.path()+"/downloads.config",encoded(inventory));const auto beforeInventory=read(root.path()+"/downloads.config");auto lease=take(storage::ProfileLease::acquire(root.path().toStdString()));auto profile=take(app::SwarmDiscoveryProfile::create(lease));check(take(profile->refresh())->settings.maximum==3,"Actual original config maximum");check(take(profile->source(hashes[0])).has_value(),"Stopped Public RCM source is independent of Mainline/peer backup");check(!take(profile->source(hashes[1]))&&!take(profile->source(hashes[2])),"Private and disable-RCM sources refused");const auto beforeCandidates=read(root.path()+"/biglybt.config");const auto discoveryConfig=core::Document::parse(beforeCandidates);check(discoveryConfig&&discoveryConfig->find("rcm.random.id"),"Opening discovery initializes the original identity");const auto revisionBeforeCandidates=lease.sourceRevision();auto candidates=take(profile->candidates(8));check(read(root.path()+"/biglybt.config")==beforeCandidates&&lease.sourceRevision()==revisionBeforeCandidates,"Background candidates are read-only");check(candidates.size()==1,"Only permitted original sources selected");auto output=value(rc::encodeAssociation(candidates[0].item));check(output.find("generated-secret")==std::string::npos&&output.find("passkey")==std::string::npos&&output.find("udp://")==std::string::npos,"Source path keeps tracker credentials out of DHT");const auto publicWire=value(rc::encodeAssociation(item(71)));const auto wires=std::array{publicWire};check(take(profile->ingest(wires,hashes[0],1,false)),"Actual profile ingests result");auto foreignPublic=item(79),foreignPrivate=item(80);foreignPrivate.hash=hashes[1];const std::array searchValues{value(rc::encodeAssociation(foreignPublic)),value(rc::encodeAssociation(foreignPrivate))};auto searchSources=take(profile->searchItems(searchValues));check(searchSources.local.size()==1&&searchSources.dht.size()==1&&searchSources.dht[0].hash==foreignPublic.hash,"Fresh search view excludes owned private hash from foreign DHT values");check(searchSources.local[0].tracker.empty()&&searchSources.dht[0].tracker.empty(),"Search source view contains no raw tracker URL");done(profile->flush());check(!QFileInfo::exists(root.path()+"/payload"),"Discovery never opens or creates payload");check(read(root.path()+"/downloads.config")==beforeInventory,"Queue/resume inventory untouched");lt::entry tables(lt::entry::dictionary_type{});tables["Table.MyTorrents"]["unknown"]="keep-other-table";tables["TableDefault.RCM"]["Column.rc_title"]=lt::entry::list_type{lt::entry(1),lt::entry(0),lt::entry(333),lt::entry(1),lt::entry(1),lt::entry(lt::entry::dictionary_type{})};tables["TableDefault.RCM"]["unknown-rcm"]="keep-rcm-field";write(root.path()+"/tables.config",encoded(tables));auto snapshot=take(profile->refresh());auto layout=*core::Document::parse(snapshot->tableLayout);check(layout.find("unknown-rcm")->bytes=="keep-rcm-field","Original TableDefault.RCM fallback");check(bool(layout.replaceValue("SortColumn","8:rc_title")),"Generated RCM sort");app::SwarmDiscoveryChanges tableChange;tableChange.tableLayout=layout.serialize();done(profile->change(tableChange));const auto savedTables=take(storage::ConfigFile::load(lease,"tables.config"));check(savedTables.document().find("Table.RCM")->find("unknown-rcm")->bytes=="keep-rcm-field"&&savedTables.document().find("Table.MyTorrents")->find("unknown")->bytes=="keep-other-table","Original RCM layout save preserves other tables and unknowns");snapshot=take(profile->refresh());check(snapshot->items.size()==1&&snapshot->unread==1,"Saved profile snapshot");app::SwarmDiscoveryChanges change;change.read={snapshot->items[0].key};done(profile->change(change));profile.reset();profile=take(app::SwarmDiscoveryProfile::create(lease));snapshot=take(profile->refresh());check(snapshot->items.size()==1&&snapshot->unread==0,"Native profile cold unread state");change={};change.removed={snapshot->items[0].key};done(profile->change(change));check(!take(profile->ingest(wires,hashes[0],1,false)),"Actual persistent removal");change={};change.settings=take(profile->refresh())->settings;change.settings->maximum=17;change.settings->depth=5;done(profile->change(change));auto cfg=take(storage::ConfigFile::load(lease,"biglybt.config"));check(cfg.document().find("rcm.max_results")->integer==17&&cfg.document().find("Plugin.aercm.rcm.config.max_results")->integer==17,"Core and original UI max mirrors saved");check(cfg.document().find("unknown-root")->bytes=="generated-marker","Unknown config preserved");check(take(profile->refresh())->settings.depth==5,"Depth setting saved");
}
}
void permissionChecksDoNotOpenTheDiscoveryCatalogue(){
 QTemporaryDir root;check(root.isValid(),"Generated permission root");
 write(root.path()+"/biglybt.config","de");
 // Permission is a configuration fact; malformed optional results/layout must
 // remain errors for consumers without blocking this lightweight network gate.
 write(root.path()+"/rcm.config","generated corrupt catalogue");
 write(root.path()+"/rcmx.config","generated corrupt deletions");
 write(root.path()+"/tables.config","generated corrupt layout");
 auto lease=take(storage::ProfileLease::acquire(root.path().toStdString()));
 auto profile=take(app::SwarmDiscoveryProfile::create(lease));
 const int events=::inotify_init1(IN_NONBLOCK|IN_CLOEXEC);check(events>=0,"Generated open observer");
 check(::inotify_add_watch(events,root.path().toUtf8().constData(),IN_OPEN)>=0,"Generated source watch");
 for(int n=0;n<500;++n){check(take(profile->searchPermission()).enabled,"Fresh enabled permission");}
 auto cfg=take(storage::ConfigFile::load(lease,"biglybt.config"));
 check(bool(cfg.document().replaceValue("rcm.overall.enabled","i0e")),"Generated disable");done(cfg.save(lease));
 check(!take(profile->searchPermission()).enabled,"Permission immediately follows current config");
 write(root.path()+"/biglybt.config","malformed generated config");
 check(!profile->searchPermission()&&!profile->searchPermission(),"Malformed permission source never reuses stale settings");
 alignas(inotify_event) std::array<char,65536> buffer{};bool openedCatalogue=false;
 for(;;){const auto bytes=::read(events,buffer.data(),buffer.size());if(bytes<=0){break;}
  for(std::size_t offset=0;offset<static_cast<std::size_t>(bytes);){
   const auto* e=reinterpret_cast<const inotify_event*>(buffer.data()+offset);
   if(e->len&&(std::strcmp(e->name,"rcm.config")==0||std::strcmp(e->name,"rcmx.config")==0||std::strcmp(e->name,"tables.config")==0||std::strcmp(e->name,"downloads.config")==0)){openedCatalogue=true;}
   offset+=sizeof(inotify_event)+e->len;
  }
 }
 ::close(events);check(!openedCatalogue,"Frequent permission checks never open result/layout/inventory files");
 write(root.path()+"/biglybt.config","de");check(!profile->refresh(),"Full result consumer still refuses malformed source files");
}
void originalPublicTorrentFormatTagsReachTheAssociation() {
 QTemporaryDir root;
 check(root.isValid(), "Generated torrent format profile");
 write(root.path()+"/biglybt.config","d13:rcm.random.idi-954ee");
 struct Scenario {
  const char* name;
  bool v1,v2,selectV2,publicSource,privateTorrent;
  const char* expectedTag;
 };
 const std::array scenarios{
  Scenario{"v1.bin",true,false,false,true,false,""},
  Scenario{"v2.bin",false,true,false,true,false,"_v2_"},
  Scenario{"hybrid-v1.bin",true,true,false,true,false,"_hybrid_"},
  Scenario{"hybrid-v2.bin",true,true,true,true,false,nullptr},
  Scenario{"i2p-only.bin",false,true,false,false,false,nullptr},
  Scenario{"private.bin",false,true,false,true,true,nullptr},
 };
 lt::entry inventory(lt::entry::dictionary_type{});
 inventory["downloads"]=lt::entry::list_type{};
 std::vector<std::string> hashes;
 const QByteArray payload(3,'x');
 for (const auto& scenario : scenarios) {
  lt::file_storage files;
  files.add_file(scenario.name,payload.size());
  lt::create_flags_t flags{};
  if (!scenario.v1) { flags |= lt::create_torrent::v2_only; }
  if (!scenario.v2) { flags |= lt::create_torrent::v1_only; }
  lt::create_torrent creator(files,16384,flags);
  if (scenario.v1) {
   const auto hash=QCryptographicHash::hash(payload,QCryptographicHash::Sha1);
   creator.set_hash(lt::piece_index_t{0},lt::sha1_hash(hash.constData()));
  }
  if (scenario.v2) {
   const auto hash=QCryptographicHash::hash(payload,QCryptographicHash::Sha256);
   creator.set_hash2(lt::file_index_t{0},lt::piece_index_t::diff_type{0},lt::sha256_hash(hash.constData()));
  }
  creator.set_priv(scenario.privateTorrent);
  auto torrent=creator.generate();
  if (scenario.selectV2) { torrent["azureus_private_properties"]["hybrid_hash_v2"]=1; }
  torrent["attributes"]["networks"]=lt::entry::list_type{
      lt::entry(scenario.publicSource ? "Public" : "I2P")};
  const auto raw=encoded(torrent);
  auto document=core::Document::parse(raw);
  lt::error_code error;
  lt::torrent_info info(lt::span<const char>(raw.data(),raw.size()),error,lt::from_span);
  check(document && !error,"Valid generated torrent format source");
  const auto identity=take(storage::originalTorrentIdentityFromLibtorrent(*document,info));
  check(bool(identity.full.v1)==scenario.v1 && bool(identity.full.v2)==scenario.v2,
      "Actual full infohash formats");
  const auto hash=identity.originalKey.bytes();
  hashes.emplace_back(reinterpret_cast<const char*>(hash.data()),hash.size());
  const auto path=root.path()+"/active/"+QString::fromStdString(identity.originalKey.hex())+".dat";
  write(path,raw);
  lt::entry row(lt::entry::dictionary_type{});
  row["torrent_hash"]=hashes.back();
  row["torrent"]=path.toStdString();
  row["save_dir"]=(root.path()+"/payload").toStdString();
  row["save_file"]=scenario.name;
  row["persistent"]=1;
  row["state"]=70;
  row["position"]=static_cast<std::int64_t>(hashes.size());
  inventory["downloads"].list().push_back(std::move(row));
 }
 write(root.path()+"/downloads.config",encoded(inventory));
 auto lease=take(storage::ProfileLease::acquire(root.path().toStdString()));
 auto profile=take(app::SwarmDiscoveryProfile::create(lease));
 for (std::size_t index=0;index<scenarios.size();++index) {
  const auto* expected=scenarios[index].expectedTag;
  const auto source=take(profile->source(hashes[index]));
  check(bool(source)==bool(expected),"Existing format and network publication permission");
  if (!source) { continue; }
  const auto wire=value(rc::encodeAssociation(source->item));
  const auto restored=value(rc::decodeAssociation(wire,hash(42),1,false,1000));
  const auto tags=*expected ? std::vector<std::string>{expected} : std::vector<std::string>{};
  check(source->item.tags==tags && restored.tags==tags,"Torrent format reaches the actual child association");
 }
 const auto parent=take(profile->source(hashes[1]));
 check(bool(parent),"Authenticated parent source");
 const auto wire=take(profile->publication(hashes[1],hashes[2],0));
 check(bool(wire),"Independent authenticated parent and child association");
 const auto encodedPublication=core::Document::parse(*wire);
 const auto* parentTags=encodedPublication ? encodedPublication->find("m") : nullptr;
 const auto* bucket=encodedPublication ? encodedPublication->find("b") : nullptr;
 check(parentTags && parentTags->type==core::Node::Type::Bytes
     && rc::decodeTags(parentTags->bytes)==std::vector<std::string>{"_v2_"},
     "Original parent attributes retain the parent's torrent format");
 check(bucket && bucket->type==core::Node::Type::Integer && bucket->integer==-54
     && bucket->integer==parent->item.random%100,
     "Original bucket uses the signed parent remainder");
 const auto child=value(rc::decodeAssociation(*wire,hashes[1],1,false,1000));
 check(child.hash==hashes[2] && child.tags==std::vector<std::string>{"_hybrid_"},
     "Original child g field stays separate from parent m");
 const auto withoutParentTags=take(profile->publication(hashes[0],hashes[2],0));
 check(bool(withoutParentTags),"Authenticated parent without tags");
 const auto noParent=core::Document::parse(*withoutParentTags);
 check(noParent && !noParent->find("m") && !noParent->find("b"),"Unlabelled parent has no attribute fields");
 check(!take(profile->publication(hashes[4],hashes[2],0))
     && !take(profile->publication(hashes[1],hashes[5],0)),
     "Both sides require current Public publication permission");
 const auto probe=take(profile->beginSwarmTagProbe(hashes[1]));
 check(bool(probe),"Current owned parent can start a Swarm Tag probe");
 lt::entry report(lt::entry::dictionary_type{});
 report["m"]=rc::encodeTags(std::array<std::string,3>{"new","_v2_","_internal_"});
 report["g"]=rc::encodeTags(std::array<std::string,1>{"child only"});
 lt::entry own(lt::entry::dictionary_type{});
 own["b"]=-154;
 own["m"]=rc::encodeTags(std::array<std::string,1>{"same bucket"});
 const std::array replies{encoded(report),encoded(own)};
 const auto observed=take(profile->completeSwarmTagProbe(*probe,replies));
 check(bool(observed) && std::ranges::equal(observed->tags(),std::array<std::string,1>{"new"}),
     "Only new Public parent tags pass the completed original attribute policy");
 auto stateStore=take(storage::TorrentStateStore::open(lease));
 const auto hash=core::TorrentHash::parse(hashes[1]);
 check(hash.has_value(),"Original selected parent identity");
 auto original=take(stateStore.loadAttributes(lease,*hash));
 check(observed->current(lease,original) && !original.document().find("attributes")->find("stag"),
     "The handoff uses the exact snapshot and has no previous write effect");
 auto catalogue=take(storage::ConfigFile::load(lease,"biglybt.config"));
 check(bool(catalogue.document().replaceValue("rcm.numunread.cache","i2e")),"Unrelated discovery bookkeeping");
 done(catalogue.save(lease));
 check(observed->current(lease,original),"Unrelated publication writes leave the probe current");
 const auto beforeInfo=core::encodeValue(*original.document().find("info"));
 const auto postimage=take(observed->encodeCurrentAttributes(lease,original));
 check(bool(postimage),"Current source produces its authorized attributes");
 done(stateStore.saveAttributes(lease,original,*postimage));
 const auto saved=take(stateStore.loadAttributes(lease,*hash));
 const auto* suggestions=saved.document().find("attributes")->find("stag");
 check(suggestions && suggestions->type==core::Node::Type::List && suggestions->children.size()==1
     && suggestions->children.front().bytes=="new","Commit uses the Native original attributes writer");
 const auto afterInfo=core::encodeValue(*saved.document().find("info"));
 check(beforeInfo && afterInfo && *beforeInfo==*afterInfo
     && !take(observed->encodeCurrentAttributes(lease,saved)),"The immutable info and newer source survive replay");
 check(!take(profile->beginSwarmTagProbe(hashes[4])),"Private-network parent has no active probe");
 const auto initialIdentity=take(profile->beginSwarmTagProbe(hashes[1]));
 check(bool(initialIdentity),"Fresh probe after the saved event");
 auto config=take(storage::ConfigFile::load(lease,"biglybt.config"));
 check(bool(config.document().replaceValue("rcm.random.id","i317e")),"Changed discovery identity");
 done(config.save(lease));
 check(!observed->current(lease,original) && !take(profile->completeSwarmTagProbe(*initialIdentity,replies)),
     "An old discovery identity or state cannot authorize the handoff");
 const auto changedWire=take(profile->publication(hashes[1],hashes[2],0));
 check(bool(changedWire) && *changedWire!=*wire,"Old publication bytes are invalidated by identity change");
 const auto replacement=core::Document::parse(*changedWire);
 check(replacement && replacement->find("b") && replacement->find("b")->integer==17,
     "Current parent bucket follows the saved profile identity");
 lt::entry tagRoot(lt::entry::dictionary_type{});
 int nextTag=0;
 const auto manual=[&](const char* name,std::size_t subject) -> lt::entry& {
  auto& tag=tagRoot["3"][std::to_string(++nextTag)];
  tag["n"]=name;
  tag["o"]=lt::entry::list_type{lt::entry(rc::base32(hashes[subject]))};
  return tag;
 };
 manual("Parent",1);
 manual("Chïld",2)["c"]["pub"]=1;
 manual("Never publish",2)["c"]["pub"]=0;
 manual("Trackers automatic",2)["c"]["pp.trackers"]=lt::entry::list_type{lt::entry("generated.invalid")};
 manual("New Only",2)["c"]["pp.constraint"]=lt::entry::list_type{lt::entry("true"),lt::entry("am=3;")};
 manual("Removing",2)["c"]["pp.constraint"]=lt::entry::list_type{lt::entry("true"),lt::entry("am=2;")};
 manual("Untagged",2)["c"]["pp.untagged"]=1;
 manual("Different subject",0);
 const auto tagPath=root.path()+"/tag.config";
 const auto tags=encoded(tagRoot);
 write(tagPath,tags);
 auto scanSubjects=take(profile->libraryTagScanSubjects());
 check(scanSubjects==hashes,"Library scan keeps every current held original subject in order");
 check(!take(profile->beginSwarmTagProbe(hashes[4],app::SwarmTagLookup::LibraryScan)) &&
     !take(profile->beginSwarmTagProbe(hashes[5],app::SwarmTagLookup::LibraryScan)),
     "Library scan refuses I2P-only and private subjects on the Public source");
 const auto scanned=take(profile->beginSwarmTagProbe(hashes[2],app::SwarmTagLookup::LibraryScan));
 const auto explicitProbe=take(profile->beginSwarmTagProbe(hashes[2],app::SwarmTagLookup::Direct));
 check(scanned&&explicitProbe,"Actual held sources allow both original attribute views");
 lt::entry scanReply(lt::entry::dictionary_type{});
 scanReply["m"]=rc::encodeTags(std::array<std::string,4>{"CHÏLD","Never publish","_internal_","Unassigned"});
 scanReply["g"]=rc::encodeTags(std::array<std::string,1>{"Child only"});
 const std::array scanReplies{encoded(scanReply)};
 const auto scanSuggestions=take(profile->completeDirectTagLookup(*scanned,scanReplies));
 const auto direct=take(profile->completeDirectTagLookup(*explicitProbe,scanReplies));
 check(scanSuggestions&&scanSuggestions->torrentTitle=="hybrid-v1.bin"
     &&scanSuggestions->found==std::vector<std::string>{"Unassigned"}&&direct
     &&direct->found==std::vector<std::string>({"CHÏLD","Never publish","_internal_","Unassigned"}),
     "Library suggestions exclude assigned private, case-matched and internal names without changing the direct event view");
 write(tagPath+".saving",tags);
 check(!take(profile->completeDirectTagLookup(*scanned,scanReplies)),"A pending tag source cannot authorize a stale scan result");
 check(QFile::remove(tagPath+".saving"),"Remove generated pending scan candidate");
 const auto labelled=take(profile->publication(hashes[1],hashes[2],0));
 check(bool(labelled),"Present saved tag source");
 const auto publication=core::Document::parse(*labelled);
 check(publication && publication->find("m") && publication->find("g"),"Separate manual label channels");
 check(rc::decodeTags(publication->find("m")->bytes)==std::vector<std::string>{"_v2_","parent"}
     && rc::decodeTags(publication->find("g")->bytes)==std::vector<std::string>{"_hybrid_","chïld","new only"},
     "Only original Public selected manual tags reach the right channels");
 check(bool(config.document().replaceValue("tag.manager.pub.default","i0e")),"Change original tag privacy default");
 done(config.save(lease));
 const auto narrowed=take(profile->publication(hashes[1],hashes[2],0));
 check(bool(narrowed) && *narrowed!=*labelled,"Default privacy change invalidates former wire");
 const auto remaining=value(rc::decodeAssociation(*narrowed,hashes[1],1,false,1000));
 check(remaining.tags==std::vector<std::string>{"_hybrid_","chïld"},"Only explicit public label remains");
 write(tagPath+".saving","de");
 const auto pending=take(profile->publication(hashes[1],hashes[2],0));
 check(bool(pending) && value(rc::decodeAssociation(*pending,hashes[1],1,false,1000)).tags==std::vector<std::string>{"_hybrid_"},
     "Unresolved tag configuration contributes no manual labels");
 check(read(tagPath)==tags && read(tagPath+".saving")=="de","Discovery reads leave tag sources intact");
 check(QFile::remove(tagPath+".saving"),"Resolve generated pending candidate");
 check(bool(config.document().replaceValue("tagmanager.enable","i0e")),"Disable tag manager for new session");
 done(config.save(lease));
 const auto warm=take(profile->publication(hashes[1],hashes[2],0));
 check(bool(warm) && *warm==*narrowed,"Current discovery keeps the original tag-manager startup mode");
 profile.reset();
 profile=take(app::SwarmDiscoveryProfile::create(lease));
 const auto reopened=take(profile->publication(hashes[1],hashes[2],0));
 check(bool(reopened) && value(rc::decodeAssociation(*reopened,hashes[1],1,false,1000)).tags==std::vector<std::string>{"_hybrid_"},
     "Disabled tag-manager startup contributes no saved manual labels");
 check(read(tagPath)==tags && QFile::remove(tagPath),"Clear generated tag sources");


 // The same generated downloads now exercise the two independent category
 // channels and the saved category roster held at profile startup.
 lt::entry definitions(lt::entry::dictionary_type{});
 auto& categories=definitions["categories"].list();
 for (const auto* name : {"HiddenCat","__all__","CaféClub","__uncategorised__","DefaultCat"}) {
  lt::entry row(lt::entry::dictionary_type{});
  row["name"]=name;
  categories.push_back(std::move(row));
 }
 const auto definitionPath=root.path()+"/categories.config";
 write(definitionPath,encoded(definitions));
 lt::entry categoryPrivacy(lt::entry::dictionary_type{});
 categoryPrivacy["1"]["3"]["c"]["pub"]=0;
 categoryPrivacy["1"]["4"]["c"]["pub"]=1;
 write(tagPath,encoded(categoryPrivacy));
 check(bool(config.document().replaceValue("tagmanager.enable","i1e"))
     && bool(config.document().replaceValue("tag.manager.pub.default","i1e"))
     && bool(config.document().replaceValue("locale","2:en")),"Original category sharing settings");
 done(config.save(lease));
 auto categorize=[&](std::size_t index,std::string_view name) {
  auto identity=core::TorrentHash::parse(hashes[index]);
  check(identity.has_value(),"Exact category test subject");
  auto current=take(stateStore.loadAttributes(lease,*identity));
  auto raw=core::encodeValue(*current.document().find("attributes"));
  check(raw.has_value(),"Original category assignment attributes");
  auto attributes=core::Document::parse(*raw);
  check(attributes && attributes->replaceValue("category",std::to_string(name.size())+':'+std::string(name)),
        "Prepare exact saved category assignment");
  done(stateStore.saveAttributes(lease,current,attributes->serialize()));
 };
 categorize(0,"HiddenCat");
 categorize(1,"CaféClub");
 categorize(2,"DefaultCat");
 auto categoryProfile=take(app::SwarmDiscoveryProfile::create(lease));
 const auto outgoing=take(categoryProfile->publication(hashes[1],hashes[2],0));
 check(bool(outgoing),"Category labels have an actual outgoing association");
 const auto channel=core::Document::parse(*outgoing);
 check(channel && channel->find("m") && channel->find("g")
     && rc::decodeTags(channel->find("m")->bytes)==std::vector<std::string>{"_v2_","caféclub"}
     && rc::decodeTags(channel->find("g")->bytes)==std::vector<std::string>{"_hybrid_","defaultcat"},
     "Saved explicit and default category privacy reach their original channels");
 const auto hidden=take(categoryProfile->publication(hashes[0],hashes[2],0));
 check(bool(hidden),"Original torrent route keeps unrelated association fields");
 const auto hiddenWire=core::Document::parse(*hidden);
 check(hiddenWire && !hiddenWire->find("m"),"Explicitly private category is withheld");
 const auto categoryProbe=take(categoryProfile->beginSwarmTagProbe(hashes[1]));
 check(bool(categoryProbe),"Current resolved category provides an exclusion source");
 lt::entry categoryReport(lt::entry::dictionary_type{});
 categoryReport["m"]=rc::encodeTags(std::array<std::string,2>{"caféclub","fresh"});
 const std::array categoryReplies{encoded(categoryReport)};
 const auto proposed=take(categoryProfile->completeSwarmTagProbe(*categoryProbe,categoryReplies));
 check(bool(proposed) && std::ranges::find(proposed->tags(),"caféclub")==proposed->tags().end()
     && std::ranges::find(proposed->tags(),"fresh")!=proposed->tags().end(),
     "Already public category is excluded from learned labels");
 const auto selectedBefore=take(stateStore.loadAttributes(lease,*hash));
 check(proposed->current(lease,selectedBefore),"Current category source keeps this observation usable");
 write(definitionPath+".new",encoded(definitions));
 const auto unsettled=take(categoryProfile->publication(hashes[1],hashes[2],0));
 check(bool(unsettled),"Category recovery leaves the association's basic permission intact");
 const auto candidateWire=core::Document::parse(*unsettled);
 check(candidateWire && candidateWire->find("m")
     && rc::decodeTags(candidateWire->find("m")->bytes)==std::vector<std::string>{"_v2_"}
     && !proposed->current(lease,selectedBefore)
     && !take(categoryProfile->beginSwarmTagProbe(hashes[1])),
     "The original category save candidate withholds category sharing and learning");
 check(QFile::remove(definitionPath+".new"),"Remove generated category save candidate");
 categoryPrivacy["1"]["4"]["c"]["pub"]=0;
 write(tagPath,encoded(categoryPrivacy));
 check(!proposed->current(lease,selectedBefore)
     && !take(categoryProfile->completeSwarmTagProbe(*categoryProbe,categoryReplies)),
     "Changed saved category privacy revokes the old observation");
 const auto privateWire=take(categoryProfile->publication(hashes[1],hashes[2],0));
 check(bool(privateWire),"Current original category route is still authorized");
 const auto privateChannel=core::Document::parse(*privateWire);
 check(privateChannel && privateChannel->find("m")
     && rc::decodeTags(privateChannel->find("m")->bytes)==std::vector<std::string>{"_v2_"},
     "Changed saved privacy suppresses the former public label");
 categoryPrivacy["1"]["4"]["c"]["pub"]=1;
 categoryPrivacy["1"]["5"]["c"]["pub"]=0;
 write(tagPath,encoded(categoryPrivacy));
 check(bool(config.document().replaceValue("tag.manager.pub.default","i0e")),"Set new category privacy default");
 done(config.save(lease));
 auto owner=take(storage::CategoryStore::open(lease));
 const auto initial=take(owner->snapshot());
 const auto removed=take(owner->remove(initial,"HiddenCat"));
 check(removed.changed,"Use the actual category deletion owner");
 const auto parentTagsNow=[&] {
  const auto bytes=take(categoryProfile->publication(hashes[1],hashes[2],0));
  check(bool(bytes),"Eligible Public association remains available");
  const auto wire=core::Document::parse(*bytes);
  check(wire && wire->find("m"),"Parent format stays available");
  return rc::decodeTags(wire->find("m")->bytes);
 };
 check(parentTagsNow()==std::vector<std::string>{"_v2_"},"Unobserved definition change withholds the category");
 QTemporaryDir foreign;
 check(foreign.isValid(),"Generated foreign category root");
 auto foreignLease=take(storage::ProfileLease::acquire(foreign.path().toStdString()));
 categoryProfile->observeCategoryDefinitionChange(foreignLease,initial,removed.snapshot,"HiddenCat",false);
 categoryProfile->observeCategoryDefinitionChange(lease,initial,removed.snapshot,"CaféClub",false);
 check(parentTagsNow()==std::vector<std::string>{"_v2_"},"Foreign and mismatched edit reports cannot authorize the category");
 categoryProfile->observeCategoryDefinitionChange(lease,initial,removed.snapshot,"HiddenCat",false);
 check(parentTagsNow()==std::vector<std::string>{"_v2_","caféclub"},
     "Surviving category retains its original ID after an earlier definition is removed");
 auto created=take(owner->create(removed.snapshot,"HiddenCat"));
 check(created.changed,"Actual recreation after deletion");
 categoryProfile->observeCategoryDefinitionChange(lease,removed.snapshot,created.snapshot,"HiddenCat",true);
 categorize(0,"HiddenCat");
 auto recreateWire=take(categoryProfile->publication(hashes[0],hashes[2],0));
 check(bool(recreateWire),"Fresh category still has an association");
 const auto privateRecreated=core::Document::parse(*recreateWire);
 check(privateRecreated && !privateRecreated->find("m"),"Fresh category uses the private default");
 auto savedCategoryTags=take(storage::ConfigFile::load(lease,"tag.config"));
 categoryPrivacy["1"]["3"]["c"]=lt::entry(lt::entry::dictionary_type{});
 categoryPrivacy["1"]["6"]["c"]["pub"]=1;
 check(bool(savedCategoryTags.document().replaceValue("1",encoded(categoryPrivacy["1"]))),"Write visibility for the new exact runtime ID");
 done(savedCategoryTags.save(lease));
 recreateWire=take(categoryProfile->publication(hashes[0],hashes[2],0));
 const auto recreatedDocument=core::Document::parse(*recreateWire);
 check(recreatedDocument && recreatedDocument->find("m")
       && rc::decodeTags(recreatedDocument->find("m")->bytes)==std::vector<std::string>{"hiddencat"},
       "The same name reappears only under the fresh original category ID");
 std::swap(categories[0],categories[2]);
 write(definitionPath,encoded(definitions));
 check(!take(categoryProfile->beginSwarmTagProbe(hashes[1])),"External category reorder remains unavailable to a warm roster");
 categoryProfile.reset();
 check(QFile::remove(tagPath) && QFile::remove(definitionPath),"Clear generated category sources");

 check(bool(config.document().replaceValue("rcm.random.id","i-1e")),"Missing discovery identity");
 done(config.save(lease));
 check(!take(profile->publication(hashes[1],hashes[2],0)),"No bucket from a missing identity");
 check(!QFileInfo::exists(root.path()+"/payload") && !QFileInfo::exists(root.path()+"/tag.config"),
     "Format projection leaves payload and registry untouched");
}
void discoveryIdentityIsSavedWhenEnabled() {
 QTemporaryDir root;
 check(root.isValid(), "Generated discovery identity root");
 const auto path = root.path() + "/biglybt.config";
 const auto before = std::string("d19:rcm.overall.enabledi0ee");
 write(path, before);
 auto lease = take(storage::ProfileLease::acquire(root.path().toStdString()));
 auto profile = take(app::SwarmDiscoveryProfile::create(lease));
 check(read(path) == before, "Disabled discovery leaves the configuration untouched");
 app::SwarmDiscoveryChanges change;
 change.settings = take(profile->searchPermission());
 change.settings->enabled = true;
 done(profile->change(change));
 const auto enabled = read(path);
 const auto document = core::Document::parse(enabled);
 const auto* id = document ? document->find("rcm.random.id") : nullptr;
 check(id && id->type == core::Node::Type::Integer && id->integer != -1,
       "Enabling discovery saves the original identity with its settings");
 profile.reset();
 const auto revision = lease.sourceRevision();
 profile = take(app::SwarmDiscoveryProfile::create(lease));
 check(read(path) == enabled && lease.sourceRevision() == revision,
       "Reopen preserves the saved identity without replacing the configuration");
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 try{
  originalLargeMaximumSurvivesSavingOtherSettings();
  nullableSavedRecommendationsRemainLocal();
  originalTagDiscoverySuggestionCases();
  tagDiscoveryUsesTheExactOriginalNameForAssignment();
#ifdef BIGLYBTC_TEST_TAG_DISCOVERY_STORE
  selectedTagDiscoveryRowsReuseTheCurrentExactTag();
#endif
  modelTests();
  javaTests();
  profileTests();
  originalPublicTorrentFormatTagsReachTheAssociation();
  permissionChecksDoNotOpenTheDiscoveryCatalogue();
  discoveryIdentityIsSavedWhenEnabled();
  std::cout<<"PASS related-content codec/cache/profile: "<<checks<<" assertions; original format/privacy/source policy\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL "<<e.what()<<'\n';
  return 1;
 }
}

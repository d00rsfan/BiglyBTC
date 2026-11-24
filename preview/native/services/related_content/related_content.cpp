// SPDX-License-Identifier: GPL-2.0-or-later
// RelatedContentManager/RelatedContent/CryptoManagerImpl, original cb8c6cb9.
#include "related_content.hpp"
#include "cache_slices.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QString>
#include <QUrl>
#include <algorithm>
#include <bit>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <zlib.h>
namespace biglybtc::services::related_content {
namespace {
using Node=core::Node;constexpr core::Limits limits{MaximumCacheBytes,MaximumCacheBytes,2000000,128};
std::unexpected<Error> fail(const char* why){return std::unexpected(Error{why});}
std::string b(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}std::string n(std::int64_t i){return "i"+std::to_string(i)+"e";}
std::string text(const Node* p,std::string fallback={}){return p&&p->type==Node::Type::Bytes?std::string(p->bytes):std::move(fallback);}
std::int64_t number(const Node* p,std::int64_t fallback=0){return p&&p->type==Node::Type::Integer?p->integer:fallback;}
std::int32_t narrow(std::int64_t x){return std::bit_cast<std::int32_t>(std::uint32_t(x));}
bool boolean(const Node* p,bool fallback=false){return p&&p->type==Node::Type::Integer?std::uint32_t(p->integer)!=0:fallback;}
bool unique(const Node& v){if(v.type!=Node::Type::Dictionary||v.children.size()%2){return false;}std::set<std::string_view> keys;for(std::size_t i=0;i<v.children.size();i+=2){if(v.children[i].type!=Node::Type::Bytes||!keys.insert(v.children[i].bytes).second){return false;}}return true;}
Result<core::Document> document(std::string bytes){auto d=core::Document::parse(std::move(bytes),limits);if(!d||!unique(d->root())){return fail("Ambiguous or unreadable related-content map");}return std::move(*d);}
std::string utf(std::string_view s){return QString::fromUtf8(s.data(),qsizetype(s.size())).toUtf8().toStdString();}
std::string title(std::string_view s){auto q=QString::fromUtf8(s.data(),qsizetype(s.size()));if(q.startsWith("urlencode(")){q=q.mid(10);if(q.endsWith(')')){q.chop(1);}q=q.trimmed();if(q.isEmpty()){q="download";}}return q.toUtf8().toStdString();}
std::string latin(std::string_view s){std::string out;for(auto cp:QString::fromUtf8(s.data(),qsizetype(s.size())).toUcs4()){out+=char(cp<=255?cp:'?');}return out;}
std::string sha(std::string_view s){return QCryptographicHash::hash(QByteArray(s.data(),qsizetype(s.size())),QCryptographicHash::Sha1).toStdString();}
std::string ints(std::span<const std::int32_t> values){std::string out;for(auto v:values){for(int n=24;n>=0;n-=8){out+=char(std::uint32_t(v)>>n);}}return out;}
Result<std::vector<std::int32_t>> unints(std::string_view bytes){if(bytes.size()%4){return fail("Invalid original related-content integer array");}std::vector<std::int32_t> out;for(std::size_t i=0;i<bytes.size();i+=4){std::uint32_t v=0;for(unsigned j=0;j<4;++j){v=(v<<8)|static_cast<unsigned char>(bytes[i+j]);}out.push_back(std::bit_cast<std::int32_t>(v));}return out;}
Item row(const core::Document& d){Item i;i.hash=text(d.find("h"));i.title=title(text(d.find("d")));i.tracker=utf(text(d.find("t")));i.version=narrow(number(d.find("v")));i.random=narrow(number(d.find("r")));i.size=number(d.find("z"));i.publishedHours=narrow(number(d.find("p")));i.packedPeers=narrow(number(d.find("q"),-1));i.firstSeen=narrow(number(d.find("f")));i.lastSeen=narrow(number(d.find("s")));i.level=narrow(number(d.find("e")));i.changedOn=number(d.find("cl"));i.unread=boolean(d.find("u"));i.networks=std::uint8_t(number(d.find("o"),1));i.trackerKeys=text(d.find("k"));i.webSeedKeys=text(d.find("w"));if(i.trackerKeys.size()%4){i.trackerKeys.clear();}if(i.webSeedKeys.size()%4){i.webSeedKeys.clear();}i.tags=decodeTags(text(d.find("g")));if(auto r=unints(text(d.find("l")))){i.randoms=std::move(*r);if(i.randoms.size()>100){i.randoms.resize(100);}}return i;}
void replace(core::Document& d,std::string_view key,std::string value){if(!d.replaceValue(key,std::move(value))){throw std::runtime_error("Related-content field exceeds its bound");}}
std::string savedRow(const Item& i,std::string_view original,std::int32_t id){auto d=document(std::string(original));if(!d){throw std::runtime_error(d.error().message);}for(auto [key,value]:{std::pair{"v",std::int64_t(i.version)},{"r",i.random},{"z",i.size},{"p",i.publishedHours},{"q",i.packedPeers},{"f",i.firstSeen},{"s",i.lastSeen},{"e",i.level},{"u",i.unread?1:0},{"o",i.networks},{"cl",i.changedOn},{"_i",id}}){replace(*d,key,n(value));}replace(*d,"d",b(i.title));replace(*d,"_k",b(i.key));if(!i.hash.empty()){replace(*d,"h",b(i.hash));}if(!i.tracker.empty()){replace(*d,"t",b(i.tracker));}replace(*d,"l",b(ints(i.randoms)));replace(*d,"g",b(encodeTags(i.tags)));if(!i.trackerKeys.empty()){replace(*d,"k",b(i.trackerKeys));}if(!i.webSeedKeys.empty()){replace(*d,"w",b(i.webSeedKeys));}return d->serialize();}
std::uint32_t javaHash(std::string_view s){std::uint32_t h=0;for(const auto c:QString::fromUtf8(s.data(),qsizetype(s.size()))){h=31*h+c.unicode();}return h;}
bool appendNewTag(std::vector<std::string>& names,std::string_view name) {
 if (std::ranges::find(names,name)!=names.end()) { return false; }
 names.emplace_back(name);
 return true;
}
bool javaTagSetOrder(std::vector<std::string>& names) {
 std::vector<std::size_t> counts(16);
 std::vector<std::uint32_t> hashes;
 const auto bucket=[&](std::uint32_t hash){return hash & (counts.size()-1);};
 const auto grow=[&] {
  counts.assign(counts.size()*2,0);
  for (const auto hash : hashes) { ++counts[bucket(hash)]; }
 };
 for (const auto& name : names) {
  const auto hash=javaHash(name);
  hashes.push_back(hash^(hash>>16));
  if (++counts[bucket(hashes.back())]>8) {
   // A tree bin has a different iteration order from the linked bins.
   if (counts.size()>=64) { return false; }
   grow();
  }
  if (hashes.size()>counts.size()*3/4) { grow(); }
 }
 std::vector<std::size_t> order(hashes.size());
 std::iota(order.begin(),order.end(),std::size_t{0});
 std::ranges::stable_sort(order,{},[&](auto index){return bucket(hashes[index]);});
 std::vector<std::string> sorted;
 sorted.reserve(names.size());
 for (const auto index : order) { sorted.push_back(std::move(names[index])); }
 names=std::move(sorted);
 return true;
}
}
Result<Settings> settings(const core::Document& d){if(!unique(d.root())){return fail("Ambiguous related-content settings");}Settings s;s.maximum=narrow(number(d.find("rcm.max_results"),500));s.depth=narrow(number(d.find("rcm.max_search_level"),3));s.enabled=boolean(d.find("rcm.overall.enabled"),true);s.uiEnabled=boolean(d.find("Plugin.aercm.rcm.ui.enable"),true);s.activeOnly=boolean(d.find("rcm.global.filter.active_only"));s.useBigly=boolean(d.find("rcm.use.bigly.dht"));s.useTorrentNames=boolean(d.find("rcm.use.torrent.names"));
 const std::vector<std::string> defaults={"vhdn.vuze.com","tracker.vodo.net","bt.archive.org","tracker.legaltorrents.com","tracker.mininova.org","www.legaltorrents.com","torrent.ubuntu.com","torrents.freebsd.org","torrent.fedoraproject.org","tracker.opensuse.org","torrents.linuxmint.com","tracker.johncave.co.nz","academictorrents.com"};
 const auto* sources=d.find("Plugin.aercm.sources.setlist");if(sources){if(sources->type!=Node::Type::List||sources->children.size()>4096){return fail("Invalid related-content source filter");}for(const auto& v:sources->children){if(v.type!=Node::Type::Bytes||v.bytes.size()>1024){return fail("Invalid related-content source filter");}s.sources.emplace_back(v.bytes);}}
 else{s.sources=defaults;}
 if(boolean(d.find("Plugin.aercm.sources.isdefault"))||std::ranges::all_of(s.sources,[&](const auto& value){return std::ranges::find(defaults,value)!=defaults.end();})){s.sources=defaults;}return s;}
std::string base32(std::string_view data){constexpr std::string_view letters="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::uint32_t acc=0;unsigned bits=0;std::string out;for(unsigned char c:data){acc=(acc<<8)|c;bits+=8;while(bits>=5){bits-=5;out+=letters[(acc>>bits)&31];}}if(bits){out+=letters[(acc<<(5-bits))&31];}return out;}
Result<std::string> unbase32(std::string_view text){std::string out;std::uint32_t acc=0;unsigned bits=0;for(char c:text){const auto at=std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567").find(c);if(at==std::string_view::npos){return fail("Invalid original Base32 related hash");}acc=(acc<<5)|std::uint32_t(at);bits+=5;if(bits>=8){bits-=8;out+=char(acc>>bits);}}if(bits&&(acc&((1U<<bits)-1))){return fail("Noncanonical original Base32 related hash");}return out;}
std::array<std::uint8_t,20> associationKey(std::string_view original){constexpr std::string_view hex="0123456789ABCDEF";std::string key="az:rcm:assoc:";for(unsigned char c:original){key+=hex[c>>4];key+=hex[c&15];}const auto digest=sha(key);std::array<std::uint8_t,20> out{};std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.data()),20,out.begin());return out;}
std::array<std::uint8_t,20> sizeAssociationKey(std::int64_t size){const auto digest=sha("az:rcm:size:assoc:"+std::to_string(size));std::array<std::uint8_t,20> out{};std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.data()),20,out.begin());return out;}
std::string obfuscate(std::string_view bytes,std::string_view id){const auto key=sha(id);std::array<unsigned char,256> state{};for(unsigned i=0;i<256;++i){state[i]=i;}unsigned j=0;for(unsigned i=0;i<256;++i){j=(j+state[i]+static_cast<unsigned char>(key[i%20]))&255;std::swap(state[i],state[j]);}unsigned i=0;j=0;const auto next=[&](){i=(i+1)&255;j=(j+state[i])&255;std::swap(state[i],state[j]);return state[(unsigned(state[i])+state[j])&255];};for(unsigned n=0;n<1024;++n){next();}std::string out(bytes);for(auto& v:out){v=char(static_cast<unsigned char>(v)^next());}return out;}
Result<std::string> gzip(std::string_view bytes){if(bytes.size()>MaximumCacheBytes){return fail("Related-content cache exceeds its bound");}z_stream stream{};if(deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)!=Z_OK){return fail("Cannot compress related-content cache");}std::string out;std::array<char,32768> buffer{};stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));stream.avail_in=uInt(bytes.size());int code;do{stream.next_out=reinterpret_cast<Bytef*>(buffer.data());stream.avail_out=buffer.size();code=deflate(&stream,Z_FINISH);out.append(buffer.data(),buffer.size()-stream.avail_out);}while(code==Z_OK&&out.size()<=MaximumCacheBytes);deflateEnd(&stream);if(code!=Z_STREAM_END||out.size()>MaximumCacheBytes){return fail("Cannot compress bounded related-content cache");}return out;}
Result<std::string> gunzip(std::string_view bytes){if(bytes.size()>MaximumCacheBytes){return fail("Related-content cache exceeds its bound");}z_stream stream{};if(inflateInit2(&stream,31)!=Z_OK){return fail("Cannot decode related-content cache");}stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));stream.avail_in=uInt(bytes.size());std::string out;std::array<char,32768> buffer{};int code;do{stream.next_out=reinterpret_cast<Bytef*>(buffer.data());stream.avail_out=buffer.size();code=inflate(&stream,Z_NO_FLUSH);out.append(buffer.data(),buffer.size()-stream.avail_out);}while(code==Z_OK&&out.size()<=MaximumCacheBytes);const auto remaining=stream.avail_in;inflateEnd(&stream);if(code!=Z_STREAM_END||remaining||out.size()>MaximumCacheBytes){return fail("Corrupt or oversized related-content cache; existing file retained");}return out;}
std::vector<std::string> decodeTags(std::string_view bytes){std::vector<std::string> out;std::size_t at=0;while(at<bytes.size()){const auto length=static_cast<unsigned char>(bytes[at++]);if(length>20||length>bytes.size()-at){break;}out.push_back(utf(bytes.substr(at,length)));at+=length;}return out;}
std::string encodeTags(std::span<const std::string> tags){std::string out;for(const auto& tag:tags){auto q=QString::fromUtf8(tag.data(),qsizetype(tag.size())).left(20);while(q.toUtf8().size()>20){q.chop(1);}const auto bytes=q.toUtf8().toStdString();if(out.size()+bytes.size()+1>64){break;}out+=char(bytes.size());out+=bytes;}return out;}
Result<Item> decodeAssociation(std::string bytes,std::string_view from,int level,bool explicitLookup,std::int32_t now){if(bytes.size()>512){return fail("Oversized related-content value");}auto d=document(std::move(bytes));if(!d){return std::unexpected(d.error());}if(!d->find("d")||d->find("d")->type!=Node::Type::Bytes||!d->find("r")||d->find("r")->type!=Node::Type::Integer){return fail("Malformed related-content value");}Item i;i.title=title(text(d->find("d")));i.hash=text(d->find("h"));i.tracker=i.hash.empty()?utf(text(d->find("t"))):std::string{};if((!i.hash.empty()&&i.hash.size()!=20)||(i.hash.empty()&&i.tracker.empty())){return fail("Invalid related-content identity");}i.key=i.hash.empty()?i.title+":"+i.tracker:base32(i.hash);i.random=narrow(number(d->find("r")));i.version=narrow(number(d->find("v")));i.size=number(d->find("s"));i.publishedHours=narrow(number(d->find("p")));const auto* seeds=d->find("z"),*leechers=d->find("l");if(seeds||leechers){i.packedPeers=std::bit_cast<std::int32_t>((std::uint32_t(number(seeds))<<16)|(std::uint32_t(number(leechers))&65535));}i.trackerKeys=text(d->find("k"));i.webSeedKeys=text(d->find("w"));if(i.trackerKeys.size()%4){i.trackerKeys.clear();}if(i.webSeedKeys.size()%4){i.webSeedKeys.clear();}i.tags=decodeTags(text(d->find("g")));i.networks=std::uint8_t(number(d->find("o"),1));i.firstSeen=now;i.lastSeen=now;i.level=level;i.unread=true;i.explicitLookup=explicitLookup;if(!from.empty()){i.relatedTo.emplace_back(from);}return i;}
Result<std::string> encodeAssociation(const Item& i,const Item* parent){if(i.hash.size()!=20||!i.tracker.empty()||!(i.networks&1)){return fail("Only authenticated public torrent identities may be published");}auto d=*core::Document::parse("de");replace(d,"d",b(QString::fromUtf8(i.title.data(),qsizetype(i.title.size())).left(80).toUtf8().toStdString()));replace(d,"h",b(i.hash));const auto r=i.random%1000;replace(d,"r",n(r<0?-r:r));if(i.version>0){replace(d,"v",n(i.version));}if(i.size){replace(d,"s",n(i.size));}if(i.publishedHours>0){replace(d,"p",n(i.publishedHours));}if(i.seeds()>0){replace(d,"z",n(i.seeds()));}if(i.leechers()>0){replace(d,"l",n(i.leechers()));}if(!i.trackerKeys.empty()){replace(d,"k",b(i.trackerKeys));}if(!i.webSeedKeys.empty()){replace(d,"w",b(i.webSeedKeys));}if(!i.tags.empty()){replace(d,"g",b(encodeTags(i.tags)));}if(i.networks!=1){replace(d,"o",n(i.networks));}
 if (parent && !parent->tags.empty()) {
  if (parent->hash.size()!=20 || !parent->tracker.empty() || !(parent->networks&1)) {
   return fail("Only authenticated public parent attributes may be published");
  }
  replace(d,"b",n(parent->random%100));
  replace(d,"m",b(encodeTags(parent->tags)));
 }
 auto bytes=d.serialize();if(bytes.size()>512){return fail("Original association value exceeds the DHT payload bound");}return bytes;}
namespace {
bool internalTag(std::string_view name) {
 return name.starts_with('_') && name.ends_with('_');
}
Result<std::optional<std::vector<std::string>>> mergeSavedTags(
    std::vector<std::string> learned,std::span<const std::string> current) {
 if (current.size()>=16 || learned.empty()) { return std::nullopt; }
 for (const auto& name : current) { appendNewTag(learned,name); }
 if (learned.size()<=current.size()) { return std::nullopt; }
 if (learned.size()>128 || !javaTagSetOrder(learned)) {
  return fail("Cannot order bounded merged Swarm Tags");
 }
 return learned;
}
}
Result<std::optional<std::vector<std::string>>> mergePublicationTags(
    std::span<const std::string> replies,std::span<const std::string> current,const Item& parent) {
 if (replies.size()>30) { return fail("Too many related-content attribute replies"); }
 if (current.size()>=16) { return std::nullopt; }
 std::vector<std::string> discovered;
 for (const auto& raw : replies) {
  if (raw.size()>512) { continue; }
  const auto value=document(raw);
  if (!value) { continue; }
  const auto* bucket=value->find("b");
  if (bucket && (bucket->type!=Node::Type::Integer || bucket->integer%100==parent.random%100)) { continue; }
  const auto* names=value->find("m");
  if (!names || names->type!=Node::Type::Bytes) { continue; }
  for (const auto& name : decodeTags(names->bytes)) {
   if (std::ranges::find(parent.tags,name)!=parent.tags.end()) { continue; }
   if (appendNewTag(discovered,name) && discovered.size()>128) {
    return fail("Observed Swarm Tag list exceeds its bound");
   }
  }
 }
 if (!javaTagSetOrder(discovered)) { return fail("Ambiguous Swarm Tag hash bucket order"); }
 std::erase_if(discovered,internalTag);
 return mergeSavedTags(std::move(discovered),current);
}
Result<ExplicitAttributeTags> explicitAttributeTags(
    std::span<const std::string> replies,std::span<const std::string> current,
    std::span<const std::string> known,std::span<const std::string> cached) {
 if (replies.size()>30 || cached.size()>128) {
  return fail("Too many related-content attribute replies or cached tags");
 }
 ExplicitAttributeTags result;
 for (const auto& name : cached) {
  if (name.size()>128) { return fail("Oversized cached related-content tag"); }
  appendNewTag(result.found,name);
 }
 std::vector<std::string> learned;
 for (const auto& raw : replies) {
  if (raw.size()>512) { continue; }
  const auto value=document(raw);
  if (!value) { continue; }
  const auto* names=value->find("m");
  if (!names || names->type!=Node::Type::Bytes) { continue; }
  for (const auto& name : decodeTags(names->bytes)) {
   appendNewTag(result.found,name);
   if (result.found.size()>128) { return fail("Discovered attribute names exceed the bound"); }
   if (internalTag(name) || std::ranges::find(known,name)!=known.end()) { continue; }
   appendNewTag(learned,name);
  }
 }
 auto saved=mergeSavedTags(std::move(learned),current);
 if (!saved) { return std::unexpected(saved.error()); }
 result.saved=std::move(*saved);
 return result;
}
namespace {
#include "original_tag_suggestion_case.inc"

std::uint32_t tagCasePoint(std::uint32_t point) {
 const auto range=std::ranges::upper_bound(TagCaseRanges,point,{},&TagCaseRange::first);
 if(range==TagCaseRanges.begin()){return point;}
 const auto& candidate=*(range-1);
 if(point>candidate.last||(point-candidate.first)%candidate.step){return point;}
 return static_cast<std::uint32_t>(static_cast<std::int64_t>(point)+candidate.delta);
}
using TagCaseKey=std::pair<qsizetype,std::u32string>;
std::optional<TagCaseKey> tagCaseKey(std::string_view raw) {
 const auto text=QString::fromUtf8(raw.data(),qsizetype(raw.size()));
 if(text.toUtf8().toStdString()!=raw){return std::nullopt;}
 TagCaseKey result{text.size(),{}};
 const auto points=text.toUcs4();
 result.second.reserve(static_cast<std::size_t>(points.size()));
 for(const auto point:points){result.second.push_back(tagCasePoint(point));}
 return result;
}
}
Result<std::vector<std::string>> tagDiscoverySuggestions(
    std::span<const std::string> candidates,std::span<const std::string> assigned) {
 if(candidates.size()>128||assigned.size()>4096){return fail("Tag Discovery names exceed their bound");}
 std::set<TagCaseKey> assignedNames;
 std::size_t bytes=0;
 for(const auto& name:assigned){
  if(name.size()>4096||name.size()>1024*1024-bytes){return fail("Assigned Tag Discovery names exceed their bound");}
  bytes+=name.size();
  auto key=tagCaseKey(name);
  if(!key){return fail("Assigned Tag Discovery name is invalid UTF-8");}
  assignedNames.insert(std::move(*key));
 }
 std::vector<std::string> result;
 for(const auto& name:candidates){
  if(name.size()>128){return fail("A Tag Discovery candidate exceeds its bound");}
  if(internalTag(name)){continue;}
  const auto key=tagCaseKey(name);
  if(!key){return fail("Tag Discovery candidate is invalid UTF-8");}
  if(!assignedNames.contains(*key)){appendNewTag(result,name);}
 }
 return result;
}
bool TagScanSnapshot::contains(const TagScanSelection& selection)const{
 return request==selection.request&&findings&&std::ranges::any_of(*findings,
     [&](const auto& finding){return selection.matches(finding);});
}
std::string deletionKey(const Item& i){return (i.hash.empty()?sha(latin(i.title+":"+i.tracker)):i.hash).substr(0,8);}
bool sourceVisible(const Item& i,const Settings& s){
 for(const auto& domain:s.sources){if(domain=="*"){return true;}const auto dot=domain.rfind('.');const auto previous=dot==std::string::npos||dot==0?std::string::npos:domain.rfind('.',dot-1);const auto compressed=previous==std::string::npos?domain:domain.substr(previous+1);const auto hash=javaHash(compressed);std::string key;for(int n=24;n>=0;n-=8){key+=char(hash>>n);}for(const auto* keys:{&i.trackerKeys,&i.webSeedKeys}){for(std::size_t at=0;at+4<=keys->size();at+=4){if(keys->compare(at,4,key)==0){return true;}}}}
 return false;
}
Result<Catalog> Catalog::decode(std::string bytes,std::string_view id,std::string deleted){try{
 Catalog c;std::string plain;const auto envelope=detail::CacheSlices(bytes).dictionary();const bool encrypted=std::ranges::any_of(envelope,[](const auto& field){return field.first=="d";});std::optional<core::Document> outer;if(encrypted){auto parsed=document(bytes);if(!parsed){return std::unexpected(parsed.error());}outer=std::move(*parsed);c.outer_=*outer;}
 if(const auto* payload=outer?outer->find("d"):nullptr){if(payload->type!=Node::Type::Bytes){return fail("Malformed original discoveries envelope");}auto unpacked=gunzip(obfuscate(payload->bytes,id));if(!unpacked){return std::unexpected(unpacked.error());}plain=std::move(*unpacked);}else{plain=std::move(bytes);c.outer_=*core::Document::parse("de");}
 const auto fields=detail::CacheSlices(plain).dictionary();std::string_view records,relations;std::string unknown="d";for(auto [key,value]:fields){if(key=="rc"){records=value;}else if(key=="rcm"){relations=value;}else{unknown+=b(key);unknown+=value;}}unknown+='e';auto kept=document(std::move(unknown));if(!kept){return std::unexpected(kept.error());}c.inner_=std::move(*kept);c.legacyPlain_=!encrypted&&(!records.empty()||!relations.empty());
 auto del=document(std::move(deleted));if(!del){return std::unexpected(del.error());}c.deleted_=std::move(*del);if(const auto* entries=c.deleted_.find("entries")){if(entries->type!=Node::Type::List||entries->children.size()>MaximumItems*10){return fail("Invalid persistent discovery deletions");}for(const auto& value:entries->children){if(value.type!=Node::Type::Bytes||value.bytes.size()!=8){return fail("Invalid original discovery deletion key");}c.deletedKeys_.emplace_back(value.bytes);}}
 if(records.empty()&&relations.empty()){return c;}if(records.empty()||relations.empty()){return fail("Incomplete original discoveries cache; retained without overwrite");}std::map<std::int32_t,std::size_t> ids;std::set<std::string> keys;
 const auto load=[&](std::string_view encoded,std::string key){if(c.rows_.size()>=MaximumItems){return false;}auto doc=document(std::string(encoded));if(!doc||!doc->find("_i")||doc->find("_i")->type!=Node::Type::Integer){return false;}auto item=row(*doc);if(!item.hash.empty()&&item.hash.size()!=20){return false;}if(const auto* own=doc->find("_k")){if(own->type!=Node::Type::Bytes){return false;}key=utf(own->bytes);}item.key=std::move(key);if(item.key.empty()||!keys.insert(item.key).second||!ids.emplace(narrow(number(doc->find("_i"))),c.rows_.size()).second){return false;}c.rows_.push_back({std::move(item),std::string(encoded)});return true;};
 if(records.front()=='l'){for(const auto value:detail::CacheSlices(records).list()){if(!load(value,{})){return fail("Ambiguous original discovery record; cache retained");}}}else{for(auto [key,value]:detail::CacheSlices(records).dictionary()){if(!load(value,utf(key))){return fail("Invalid legacy discovery record; cache retained");}}}
 for(auto [key,encoded]:detail::CacheSlices(relations).dictionary()){auto hash=unbase32(key);auto value=core::Document::parse(std::string(encoded),limits);if(!hash||hash->size()!=20||!value||value->root().type!=Node::Type::Bytes){return fail("Invalid original discovery relationship");}auto list=unints(value->root().bytes);if(!list){return std::unexpected(list.error());}for(auto id:*list){const auto found=ids.find(id);if(found==ids.end()){return fail("Dangling original discovery relationship; cache retained");}auto& parents=c.rows_[found->second].item.relatedTo;if(std::ranges::find(parents,*hash)==parents.end()){parents.push_back(*hash);}}}return c;
 }catch(const std::exception&){return fail("Invalid or oversized discoveries cache structure; existing file retained");}}
Result<std::string> Catalog::encode(std::string_view id)const{try{
 std::string rows="l";std::map<Hash,std::vector<std::int32_t>> links;std::int32_t index=0;for(const auto& record:rows_){rows+=savedRow(record.item,record.original,index);for(const auto& parent:record.item.relatedTo){links[parent].push_back(index);}++index;}rows+='e';std::string relations="d";for(const auto& [hash,indices]:links){relations+=b(base32(hash))+b(ints(indices));}relations+='e';std::map<std::string,std::string> fields;const auto retained=inner_.serialize();for(const auto& [key,value]:detail::CacheSlices(retained).dictionary()){fields.emplace(key,value);}fields.insert_or_assign("rc",std::move(rows));fields.insert_or_assign("rcm",std::move(relations));std::string plain="d";for(const auto& [key,value]:fields){plain+=b(key)+value;}plain+='e';if(legacyPlain_){return plain;}auto packed=gzip(plain);if(!packed){return std::unexpected(packed.error());}auto outer=outer_;replace(outer,"d",b(obfuscate(*packed,id)));return outer.serialize();
 }catch(const std::exception&){return fail("Discovery cache encoding failed within its bound");}}
Result<std::string> Catalog::encodeDeleted()const{try{auto out=deleted_;std::string list="l";for(const auto& k:deletedKeys_){list+=b(k);}list+='e';replace(out,"entries",std::move(list));return out.serialize();}catch(const std::exception& e){return std::unexpected(Error{e.what()});}}
std::vector<Item> Catalog::items()const{std::vector<Item> out;out.reserve(rows_.size());for(const auto& r:rows_){out.push_back(r.item);}return out;}
std::vector<std::string> Catalog::tagsForHash(std::string_view hash) const {
 const auto found=std::ranges::find_if(rows_,[&](const auto& row) { return row.item.hash==hash; });
 return found==rows_.end() ? std::vector<std::string>{} : found->item.tags;
}
bool Catalog::contains(std::string_view hash)const{return std::ranges::any_of(rows_,[&](const Row& r){return r.item.hash==hash;});}
std::size_t Catalog::unread()const noexcept{return std::ranges::count_if(rows_,[](const Row& r){return r.item.unread;});}
bool Catalog::ingest(Item i,const Settings& s,std::span<const Hash> owned){if(!s.enabled||(s.activeOnly&&i.seeds()<=0&&i.leechers()<=0)||std::ranges::find(owned,i.hash)!=owned.end()||std::ranges::find(deletedKeys_,deletionKey(i))!=deletedKeys_.end()){return false;}auto at=std::ranges::find_if(rows_,[&](const Row& r){return r.item.key==i.key||(!i.hash.empty()&&r.item.hash==i.hash);});if(at!=rows_.end()){auto& old=at->item;if(i.version<old.version){return false;}auto before=old;old.lastSeen=i.lastSeen;if(std::ranges::find(old.randoms,i.random)==old.randoms.end()&&old.randoms.size()<100){old.randoms.push_back(i.random);}old.version=std::max(old.version,i.version);old.level=std::min(old.level,i.level);if(i.packedPeers!=-1){old.packedPeers=i.packedPeers;}if(!old.publishedHours&&i.publishedHours>0){old.publishedHours=i.publishedHours;}for(const auto& tag:i.tags){if(std::ranges::find(old.tags,tag)==old.tags.end()){old.tags.push_back(tag);}}old.networks|=i.networks;before.lastSeen=old.lastSeen;const bool changed=before!=old;if(changed){old.changedOn=std::int64_t(i.lastSeen)*1000;dirty_=true;}return changed;}
 if(s.maximum<=0||rows_.size()>=MaximumItems){return false;}if(rows_.size()>=std::size_t(s.maximum)){auto worst=rows_.end();for(auto p=rows_.begin();p!=rows_.end();++p){const auto& x=p->item;if(x.explicitLookup||x.level<i.level){continue;}if(worst==rows_.end()||x.level>worst->item.level||(x.level==worst->item.level&&std::pair{x.randoms.size(),x.lastSeen}<std::pair{worst->item.randoms.size(),worst->item.lastSeen})){worst=p;}}if(worst==rows_.end()){return false;}rows_.erase(worst);}
 i.randoms={i.random};i.changedOn=std::int64_t(i.lastSeen)*1000;rows_.push_back({std::move(i),"de"});dirty_=true;return true;}
std::size_t Catalog::enforceMaximum(std::int32_t maximum){const auto cap=std::size_t(std::max<std::int32_t>(0,maximum));if(rows_.size()<=cap){return 0;}const auto removed=rows_.size()-cap;std::stable_sort(rows_.begin(),rows_.end(),[](const Row& a,const Row& b){if(a.item.level!=b.item.level){return a.item.level>b.item.level;}return std::pair{a.item.randoms.size(),a.item.lastSeen}<std::pair{b.item.randoms.size(),b.item.lastSeen};});rows_.erase(rows_.begin(),rows_.begin()+std::ptrdiff_t(removed));dirty_=true;return removed;}
bool Catalog::read(std::span<const std::string> keys,bool unread){bool changed=false;for(auto& r:rows_){if(std::ranges::find(keys,r.item.key)!=keys.end()&&r.item.unread!=unread){r.item.unread=unread;r.item.changedOn=QDateTime::currentMSecsSinceEpoch();changed=true;}}dirty_|=changed;return changed;}
bool Catalog::remove(std::span<const std::string> keys,bool persistent){const auto old=rows_.size();std::erase_if(rows_,[&](const Row& r){if(std::ranges::find(keys,r.item.key)==keys.end()){return false;}if(persistent){auto key=deletionKey(r.item);if(r.item.hash.empty()&&r.item.tracker.empty()){auto original=document(r.original);if(original){const auto title=original->find("d")?r.item.title:std::string("null");const auto tracker=original->find("t")?r.item.tracker:std::string("null");key=sha(latin(title+":"+tracker)).substr(0,8);}}if(std::ranges::find(deletedKeys_,key)==deletedKeys_.end()){deletedKeys_.push_back(key);deletedDirty_=true;}}return true;});dirty_|=old!=rows_.size();return old!=rows_.size();}
bool Catalog::resetDeleted(){if(deletedKeys_.empty()){return false;}deletedKeys_.clear();deletedDirty_=true;return true;}
}

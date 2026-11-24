// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include "cache_limits.hpp"
#include <array>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>
namespace biglybtc::services::related_content {
using Hash=std::string; // Original binary20-byte selected infohash; never a tracker URL.
struct Error {std::string message;};
template<class T>using Result=std::expected<T,Error>;
struct Settings {
 std::int32_t maximum=500,depth=3;bool enabled=true,uiEnabled=true,activeOnly=false,useBigly=false,useTorrentNames=false;
 std::vector<std::string> sources;
 bool operator==(const Settings&)const=default;
};
struct Item {
 std::string key,title,tracker,trackerKeys,webSeedKeys;Hash hash;
 std::vector<std::string> tags;std::vector<std::int32_t> randoms;
 std::int32_t version=0,random=0,publishedHours=0,packedPeers=-1,firstSeen=0,lastSeen=0,level=0;
 std::int64_t size=0,changedOn=0;std::uint8_t networks=1;bool unread=false,explicitLookup=false;
 std::vector<Hash> relatedTo;
 int seeds()const noexcept{return packedPeers==-1?-1:int((std::uint32_t(packedPeers)>>16)&65535);}
 int leechers()const noexcept{return packedPeers==-1?-1:int(std::uint32_t(packedPeers)&65535);}
 bool operator==(const Item&)const=default;
};
struct Snapshot {
 std::uint64_t revision=0;Settings settings;std::vector<Item> items;
 std::string status,tableLayout="de";std::size_t unread=0,knownLibrary=0;bool writable=true;
};
Result<Settings> settings(const core::Document&);
std::string base32(std::string_view);Result<std::string> unbase32(std::string_view);
std::array<std::uint8_t,20> associationKey(std::string_view originalHash);
std::array<std::uint8_t,20> sizeAssociationKey(std::int64_t size);
std::string obfuscate(std::string_view bytes,std::string_view originalObfuscationId);
Result<std::string> gzip(std::string_view);Result<std::string> gunzip(std::string_view);
std::vector<std::string> decodeTags(std::string_view);std::string encodeTags(std::span<const std::string>);
Result<Item> decodeAssociation(std::string bytes,std::string_view from,int level,bool explicitLookup,std::int32_t now);
Result<std::string> encodeAssociation(const Item& publicItem,const Item* publicParent=nullptr);
Result<std::optional<std::vector<std::string>>> mergePublicationTags(
    std::span<const std::string> replies,std::span<const std::string> current,
    const Item& parent);
struct ExplicitAttributeTags {
 std::vector<std::string> found;
 std::optional<std::vector<std::string>> saved;
};
Result<ExplicitAttributeTags> explicitAttributeTags(
    std::span<const std::string> replies,std::span<const std::string> current,
    std::span<const std::string> known,std::span<const std::string> cached);
// The Tag Discovery view keeps exact callback spelling and only checks
// case-insensitive equality against tags already attached to this download.
Result<std::vector<std::string>> tagDiscoverySuggestions(
    std::span<const std::string> candidates,std::span<const std::string> assigned);
struct TagScanFinding {
 std::string hash,torrentTitle,tag;
 std::int64_t foundOn=0;
};
struct TagScanSelection {
 std::uint64_t request=0;
 std::string hash,tag;
 std::int64_t foundOn=0;
 bool matches(const TagScanFinding& row)const noexcept {
  return row.hash==hash&&row.tag==tag&&row.foundOn==foundOn;
 }
};
using TagScanSelections=std::vector<TagScanSelection>;
inline constexpr std::size_t MaximumTagScanFindings=20000;
struct TagScanSnapshot {
 std::uint64_t request=0;
 std::size_t total=0,finished=0,unavailable=0;
 bool running=false,resultLimit=false;
 std::string status;
 std::shared_ptr<const std::vector<TagScanFinding>> findings;
 bool contains(const TagScanSelection&)const;
};
std::string deletionKey(const Item&);bool sourceVisible(const Item&,const Settings&);
class Catalog final {
public:
 static Result<Catalog> decode(std::string cache,std::string_view originalObfuscationId,std::string deleted="de");
 Result<std::string> encode(std::string_view originalObfuscationId)const;
 Result<std::string> encodeDeleted()const;
 std::vector<Item> items()const;
 std::vector<std::string> tagsForHash(std::string_view) const;
 bool ingest(Item,const Settings&,std::span<const Hash> owned);
 std::size_t enforceMaximum(std::int32_t maximum);
 bool read(std::span<const std::string> keys,bool unread=false);
 bool remove(std::span<const std::string> keys,bool persistent=true);
 bool resetDeleted();bool contains(std::string_view hash)const;
 std::size_t unread()const noexcept;std::size_t size()const noexcept{return rows_.size();}
 bool dirty()const noexcept{return dirty_;}bool deletedDirty()const noexcept{return deletedDirty_;}
 void saved()noexcept{dirty_=false;deletedDirty_=false;}
private:
 struct Row {Item item;std::string original;};
 core::Document outer_=*core::Document::parse("de"),inner_=*core::Document::parse("de"),deleted_=*core::Document::parse("de");
 std::vector<Row> rows_;std::vector<std::string> deletedKeys_;bool dirty_=false,deletedDirty_=false,legacyPlain_=false;
};
}

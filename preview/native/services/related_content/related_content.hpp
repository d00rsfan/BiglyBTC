// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
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
inline constexpr std::size_t MaximumCacheBytes=192U*1024U*1024U,MaximumItems=100000;
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
Result<std::string> encodeAssociation(const Item& publicItem);
std::string deletionKey(const Item&);bool sourceVisible(const Item&,const Settings&);
class Catalog final {
public:
 static Result<Catalog> decode(std::string cache,std::string_view originalObfuscationId,std::string deleted="de");
 Result<std::string> encode(std::string_view originalObfuscationId)const;
 Result<std::string> encodeDeleted()const;
 std::vector<Item> items()const;
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

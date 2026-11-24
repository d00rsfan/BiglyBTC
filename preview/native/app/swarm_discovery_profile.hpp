// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include <biglybtc/library.hpp>
#include <chrono>
#include "related_content.hpp"
#include <memory>
namespace biglybtc::core { struct SavedDownloadTags; }
namespace biglybtc::storage { class TorrentStateFile; class CategorySnapshot; }
namespace biglybtc::app {
namespace rcm=services::related_content;
using SwarmDiscoverySnapshot=rcm::Snapshot;
struct SwarmDiscoverySource {
 rcm::Item item;
 std::vector<std::int64_t> fileSizes;
 std::string savedCategory;
 bool operator==(const SwarmDiscoverySource&) const=default;
};
struct SwarmSearchSources {rcm::Settings settings;std::vector<rcm::Item> local,dht;};
struct SwarmDiscoveryChanges {std::optional<rcm::Settings> settings;std::vector<std::string> read,unread,removed;bool clearDeleted=false;std::optional<std::string> tableLayout;};
enum class SwarmTagLookup { Publication, Direct, LibraryScan };
// Affine to the existing profile IO broker. Never called on the socket or GUI
// thread; all reads/publications hold this SAME original profile's source gate.
class SwarmTagProbe final {
private:
 friend class SwarmDiscoveryProfile;
 std::shared_ptr<const storage::ProfileLease> lease;
 core::TorrentHash hash;
 rcm::Item parent;
 std::string stateDigest;
 std::vector<std::string> oldTags;
 std::vector<std::string> cachedTags;
 SwarmTagLookup lookup=SwarmTagLookup::Publication;
 bool savedLabels=false;
};
class SwarmTagUpdate final {
public:
 const core::TorrentHash& subject() const noexcept { return hash; }
 std::span<const std::string> tags() const noexcept { return proposed; }
 bool current(const storage::ProfileLease&,const storage::TorrentStateFile&) const;
 std::expected<std::optional<std::string>,storage::Error> encodeCurrentAttributes(
     const storage::ProfileLease&,const storage::TorrentStateFile&) const;
private:
 friend class SwarmDiscoveryProfile;
 std::shared_ptr<const storage::ProfileLease> lease;
 core::TorrentHash hash;
 std::string stateDigest;
 std::vector<std::string> proposed;
 bool savedLabels=false;
 std::chrono::steady_clock::time_point end;
};
std::expected<std::optional<std::int32_t>,storage::Error> findSavedManualTag(
    const core::SavedDownloadTags&,const std::optional<std::string>& locale,std::string_view exactName);
struct SwarmAttributeResult {
 std::vector<std::string> found;
 std::optional<SwarmTagUpdate> saved;
 std::string torrentTitle;
};
class SwarmDiscoveryProfile final {
public:
 static std::expected<std::shared_ptr<SwarmDiscoveryProfile>,storage::Error> create(const storage::ProfileLease&);
 ~SwarmDiscoveryProfile();
 std::expected<std::shared_ptr<const SwarmDiscoverySnapshot>,storage::Error> refresh();
 std::expected<void,storage::Error> change(const SwarmDiscoveryChanges&);
 std::expected<std::optional<SwarmDiscoverySource>,storage::Error> source(std::string_view hash);
 // Called after the category owner commits. Advances only its in-memory
 // original identities. Every later publication checks the actual file source.
 void observeCategoryDefinitionChange(const storage::ProfileLease&,
     const storage::CategorySnapshot& before,const storage::CategorySnapshot& after,
     std::string_view name,bool added) noexcept;
 // Encode both currently verified Public sources under one profile read gate.
 std::expected<std::optional<std::string>,storage::Error> publication(
     std::string_view parent,std::string_view child,std::int64_t fileSize);
 std::expected<bool,storage::Error> permitted(std::string_view hash,bool derived=false);
 std::expected<std::optional<SwarmTagProbe>,storage::Error> beginSwarmTagProbe(
     std::string_view parent,SwarmTagLookup lookup=SwarmTagLookup::Publication);
 std::expected<std::optional<SwarmTagUpdate>,storage::Error> completeSwarmTagProbe(
     const SwarmTagProbe&,std::span<const std::string> replies);
 std::expected<std::optional<SwarmAttributeResult>,storage::Error> completeDirectTagLookup(
     const SwarmTagProbe&,std::span<const std::string> replies);
 std::expected<std::vector<SwarmDiscoverySource>,storage::Error> candidates(std::size_t count=8);
 // Ordered held inventory subjects. Each actual lookup must still pass the
 // current source/network and owned manual-tag checks before use.
 std::expected<std::vector<std::string>,storage::Error> libraryTagScanSubjects();
 std::expected<bool,storage::Error> ingest(std::span<const std::string> values,std::string_view parent,int level,bool explicitLookup);
 // IO-broker-affine fresh source/permission views for Public keyword search.
 std::expected<rcm::Settings,storage::Error> searchPermission();
 std::expected<SwarmSearchSources,storage::Error> searchItems(std::span<const std::string> dhtValues={});
 std::expected<void,storage::Error> flush();
private:
 static SwarmTagUpdate savedTagUpdate(const SwarmTagProbe&,std::vector<std::string>);
 bool isCurrentTagProbe(const SwarmTagProbe&);
 struct Impl;explicit SwarmDiscoveryProfile(std::unique_ptr<Impl>);std::unique_ptr<Impl> impl_;
};
}

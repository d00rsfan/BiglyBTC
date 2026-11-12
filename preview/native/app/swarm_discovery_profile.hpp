// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "related_content.hpp"
#include <memory>
namespace biglybtc::app {
namespace rcm=services::related_content;
using SwarmDiscoverySnapshot=rcm::Snapshot;
struct SwarmDiscoverySource {rcm::Item item;std::vector<std::int64_t> fileSizes;bool operator==(const SwarmDiscoverySource&)const=default;};
struct SwarmDiscoveryChanges {std::optional<rcm::Settings> settings;std::vector<std::string> read,unread,removed;bool clearDeleted=false;std::optional<std::string> tableLayout;};
// Affine to the existing profile IO broker. Never called on the socket or GUI
// thread; all reads/publications hold this SAME original profile's source gate.
class SwarmDiscoveryProfile final {
public:
 static std::expected<std::shared_ptr<SwarmDiscoveryProfile>,storage::Error> create(const storage::ProfileLease&);
 ~SwarmDiscoveryProfile();
 std::expected<std::shared_ptr<const SwarmDiscoverySnapshot>,storage::Error> refresh();
 std::expected<void,storage::Error> change(const SwarmDiscoveryChanges&);
 std::expected<std::optional<SwarmDiscoverySource>,storage::Error> source(std::string_view hash);
 std::expected<bool,storage::Error> permitted(std::string_view hash,bool derived=false);
 std::expected<std::vector<SwarmDiscoverySource>,storage::Error> candidates(std::size_t count=8);
 std::expected<bool,storage::Error> ingest(std::span<const std::string> values,std::string_view parent,int level,bool explicitLookup);
 std::expected<void,storage::Error> flush();
private:
 struct Impl;explicit SwarmDiscoveryProfile(std::unique_ptr<Impl>);std::unique_ptr<Impl> impl_;
};
}

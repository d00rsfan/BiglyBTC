// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_profile.hpp"
#include "provider.hpp"
#include "chat_transport_factory.hpp"
#include "azureus_torrent.hpp"
#include "swarm_discovery_profile.hpp"
#include "search.hpp"
#include "../adapters/local_rating.hpp"
#include "owned_dht_statistics.hpp"
#include "read_schedule.hpp"
#include "router.hpp"
#include <array>
#include <optional>
namespace biglybtc::engine {class PersistentSession;}
namespace biglybtc::app {
class GeneralChats;
using ChatTransportFactory=engine::ChatTransportFactory;
struct GeneralChatRoom {
 ChatRoomSettings settings;std::string status;bool joined=false,canSend=false;
 std::uint32_t unread=0;std::size_t nodes=0,active=0;
};
struct SwarmNetworkObservation {
 std::uint64_t lookups=0,publications=0,completed=0;
 std::size_t active=0,pending=0;std::string status="Opening saved discoveries…",selected;
 bool operator==(const SwarmNetworkObservation&)const=default;
};
struct SharedRatingSnapshot {
 std::string hash,status;std::uint64_t request=0;
 plugins::rating::RatingResults ratings;
 std::size_t received=0,decodeErrors=0,storesAttempted=0,storesConfirmed=0;
 bool running=false,current=false,localFallback=false,cacheSaved=false,publishing=false,byHash=false;
};
struct RatingChatNoticeSnapshot {std::string hash,room,status;std::uint64_t request=0;bool running=false,created=false;};
struct GeneralChatSnapshot {
 std::uint64_t generation=0,sequence=0;
   std::string externalV4,externalV6;
  std::string availability,selected;bool closing=false,closed=false;
 std::vector<std::shared_ptr<const SharedRatingSnapshot>> ratings;
 std::vector<RatingChatNoticeSnapshot> ratingNotices;
 engine::AzureusTorrentObservation torrentDht;
 std::shared_ptr<const SwarmDiscoverySnapshot> discoveries;SwarmNetworkObservation discoveryNetwork;
 std::shared_ptr<const services::related_content::SearchProgress> discoverySearch;
 std::array<std::shared_ptr<const services::azureus_udp::OwnedDhtStatisticsSnapshot>,3> transports;
 std::array<std::shared_ptr<const services::azureus_router::RouterStatisticsSnapshot>,3> routers;
 std::vector<GeneralChatRoom> rooms;std::vector<services::msgsync::RoomMessage> messages;
};
struct GeneralChatResult {
 enum class Kind {Join,Leave,Select,Send,Options,NicknameMode,Nickname};
 std::uint64_t request=0;Kind kind=Kind::Join;std::string key,text;
 std::optional<storage::Error> failure;
};
// Facade callable by the GUI/Host. One Qt worker owns both IP families and all
// rooms. Its two bounded IO brokers retain the same profile OFD. The external
// lifetime must join outside the source gate before releasing the engine.
class GeneralChats final {
public:
 static std::expected<std::unique_ptr<GeneralChats>,storage::Error> create(ChatTransportFactory,std::uint64_t generation);
 ~GeneralChats();GeneralChats(const GeneralChats&)=delete;
 std::shared_ptr<const GeneralChatSnapshot> snapshot()const noexcept;
 std::vector<GeneralChatResult> takeResults();
 std::optional<std::uint64_t> joinRoom(std::string);
 std::optional<std::uint64_t> leaveRoom(std::string);
 std::optional<std::uint64_t> selectRoom(std::string);
 // Coalesced GUI visibility hint; empty means hidden or unfocused.
 bool setReadingRoom(std::string);
 std::optional<std::uint64_t> send(std::string key,std::string text);
 std::optional<std::uint64_t> options(std::string key,bool favourite,bool saveHistory);
 std::optional<std::uint64_t> nicknameMode(std::string key,bool shared);
 std::optional<std::uint64_t> nickname(std::string key,std::string value,bool expectedShared);
 bool refreshDiscoveries();
 bool discoverNow();
 bool lookupDiscoveries(std::string hash,std::int64_t fileSize=0);
 bool changeDiscoveries(SwarmDiscoveryChanges);
 bool searchDiscoveries(services::related_content::SearchRequest);
 bool cancelDiscoverySearch();
 bool updateRatingDownloads(std::vector<plugins::rating::publication::ReadHint>);
 bool readRatingsByHash(core::TorrentHash,std::uint64_t expectedGeneration);
 bool cancelRatingsByHash(core::TorrentHash,std::uint64_t expectedGeneration);
 bool readRatings(core::TorrentHash);
 bool publishRating(core::TorrentHash);
 bool cancelRatings(core::TorrentHash);
 bool updateTorrents(std::vector<engine::AzureusTorrentTarget>);
 std::vector<engine::AzureusPeerBatch> takeTorrentPeers();
 void recordDeliveredTorrentPeers(std::size_t)noexcept;
 void requestStop()noexcept;
 void join()noexcept;
private:
 struct Impl;explicit GeneralChats(std::unique_ptr<Impl>);
 std::unique_ptr<Impl> impl_;
};
}

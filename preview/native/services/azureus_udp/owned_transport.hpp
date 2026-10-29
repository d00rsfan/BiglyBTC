// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ping.hpp"
#include "transport_statistics.hpp"
#include "owned_dht_statistics.hpp"
#include "find_node.hpp"
#include "find_value.hpp"
#include "store.hpp"
#include "error.hpp"
#include "authority.hpp"
#include "store_policy.hpp"
#include "database.hpp"
#include "router.hpp"
#include <memory>
#include <QChronoTimer>
#include <QHostAddress>
#include <QPointer>
#include <QUdpSocket>
#include <chrono>
#include <deque>
#include <functional>
#include <unordered_map>
#include <utility>
namespace biglybtc::services::azureus_udp {
namespace packet=biglybtc::services::azureus_packets;
namespace find=biglybtc::services::azureus_find_node;
namespace lookup=biglybtc::services::azureus_find_value;
namespace storage=biglybtc::services::azureus_store;
namespace remote_error=biglybtc::services::azureus_error;
namespace auth=biglybtc::services::azureus_auth;
namespace policy=biglybtc::services::azureus_store_policy;
namespace database=biglybtc::services::azureus_db_runtime;
struct Peer { QHostAddress address;quint16 port=0;bool operator==(const Peer&) const=default; };
struct Ticket { std::uint64_t value=0;bool operator==(const Ticket&) const=default; };
enum class Failure { State,Thread,Peer,Capacity,Version,Deadline,Receiver,Random,Encode,Send,Timeout,Cancelled,Closed,Remote,Authority };
using Outcome=std::expected<packet::Decoded,Failure>;
using Callback=std::move_only_function<void(Outcome)>;
using FindNodeOutcome=std::expected<find::Decoded,Failure>;
using FindNodeCallback=std::move_only_function<void(FindNodeOutcome)>;
struct FindNodeReply {
  std::int32_t randomId=0,status=-1,estimatedSize=0;
  std::vector<find::nodes::Snapshot> contacts;
};
using FindValueOutcome=std::expected<lookup::Decoded,Failure>;
using FindValueCallback=std::move_only_function<void(FindValueOutcome)>;
template<class T> using DetailedOutcome=std::expected<std::variant<T,remote_error::Decoded>,Failure>;
using DetailedPingOutcome=DetailedOutcome<packet::Decoded>;
using DetailedPingCallback=std::move_only_function<void(DetailedPingOutcome)>;
using DetailedFindNodeOutcome=DetailedOutcome<find::Decoded>;
using DetailedFindNodeCallback=std::move_only_function<void(DetailedFindNodeOutcome)>;
using DetailedFindValueOutcome=DetailedOutcome<lookup::Decoded>;
using DetailedFindValueCallback=std::move_only_function<void(DetailedFindValueOutcome)>;
using StoreOutcome=DetailedOutcome<storage::Decoded>;
using StoreCallback=std::move_only_function<void(StoreOutcome)>;
struct StoreRequest {
  std::int32_t randomId=0;
  std::vector<std::string> keys;
  std::vector<std::vector<storage::values::Value>> valueSets;
};
struct ErrorReplySnapshot {
  std::int32_t type=0;
  std::optional<remote_error::Endpoint> originator;
  std::optional<std::string> request,signature;
};
struct FindValueSlice {
  bool continuation=false;
  std::int8_t diversification=1;
  std::optional<std::vector<lookup::values::Value>> values; // Owned bytes, null -> contacts.
  std::vector<lookup::nodes::Snapshot> contacts;
};
struct GeneratedStoreService {
  GeneratedStoreService();
  GeneratedStoreService(const GeneratedStoreService&);
  GeneratedStoreService& operator=(const GeneratedStoreService&);
  GeneratedStoreService(GeneratedStoreService&&) noexcept;
  GeneratedStoreService& operator=(GeneratedStoreService&&) noexcept;
  bool seeded=false;
  std::int32_t k=20;
  std::optional<std::int32_t> storageNetwork;
  std::optional<policy::NodeId> furthest;
  std::vector<policy::Key> keyStates;
  std::int32_t findNodeStatus=-1,estimatedSize=0;
  std::vector<find::nodes::Snapshot> findNodeContacts;
  std::size_t maximumReceipts=32;
  std::optional<database::Limits> volatileDatabase; // Explicit generated-only real DB; absent preserves old mode.
};
using ServiceRequest=std::variant<find::Decoded,storage::Decoded>;
enum class ServiceStatus { Replied,AddressRejected,AuthorityFailed,PolicyFailed,EncodeFailed,SendFailed,DatabaseFailed };
struct ServiceReceipt {
  Peer peer;
  ServiceRequest request;
  std::optional<find::nodes::Contact> sender;
  bool transportAlive=false,findNodeAlive=false,addressMismatch=false;
  std::optional<std::expected<policy::Reply,policy::Error>> admission;
  std::optional<auth::Error> authorityError;
  std::optional<find::nodes::ErrorCode> identityError;
  ServiceStatus status=ServiceStatus::EncodeFailed;
  std::int32_t replyAction=0;
  bool sent=false;
  std::optional<database::Error> databaseError;
  ServiceReceipt(Peer,ServiceRequest);
  ServiceReceipt(const ServiceReceipt&);
  ServiceReceipt& operator=(const ServiceReceipt&);
  ServiceReceipt(ServiceReceipt&&) noexcept=default;
  ServiceReceipt& operator=(ServiceReceipt&&) noexcept=default;
};
struct GeneratedServiceState {
  find::nodes::NodeId localId{};
  auth::Statistics authority{};
  policy::State admission{};
  std::size_t receiptCount=0;
};
struct Config {
  std::int32_t network=4,instance=0;
  std::int8_t version=55,flags=0,flags2=0;
  bool servePings=false;
  std::optional<FindNodeReply> findNodeReply; // Supplied inert snapshot, no nonce authorization/routing.
  std::optional<std::vector<FindValueSlice>> findValueReply; // Inert owned test pieces only.
  std::optional<std::string> storeReply; // Inert diversification, including an empty array.
  std::optional<ErrorReplySnapshot> storeErrorReply; // Mutually exclusive with storeReply, no admission/authority.
  std::optional<GeneratedStoreService> generatedStoreService; // Explicit generated-only authority/admission mode.
  std::size_t maximumPending=128,maximumPeers=128;
  std::move_only_function<std::uint64_t()> randomWord; // null -> Qt system generator.
  bool observeOperations=false,enableVivaldi=false; // Explicit owner policy; never enabled by a view.
};

// Development transport: fresh ephemeral numeric-loopback sockets and supplied
// owned peer allowlist only. Every API/destruction belongs to its Qt owner.
// No profile, DNS, proxy, discovery, router mapping or public-route switch.
class OwnedPingTransport final : public QObject {
public:
  explicit OwnedPingTransport(Config,QObject* parent=nullptr);
  ~OwnedPingTransport() override;
  std::expected<void,Failure> start(const QHostAddress& bindAddress=QHostAddress::LocalHost);
  std::expected<void,Failure> allowPeer(Peer);
  std::expected<Ticket,Failure> ping(Peer,std::int8_t remoteVersion,QObject* receiver,Callback,
      std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  std::expected<Ticket,Failure> findNode(Peer,std::int8_t remoteVersion,find::nodes::NodeId target,
      QObject* receiver,FindNodeCallback,std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  std::expected<Ticket,Failure> findValue(Peer,std::int8_t remoteVersion,lookup::nodes::NodeId target,
      std::int32_t flags,std::int32_t maximumValues,QObject* receiver,FindValueCallback,
      std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  std::expected<Ticket,Failure> pingDetailed(Peer,std::int8_t remoteVersion,QObject* receiver,DetailedPingCallback,
      std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  std::expected<Ticket,Failure> findNodeDetailed(Peer,std::int8_t remoteVersion,find::nodes::NodeId target,
      QObject* receiver,DetailedFindNodeCallback,std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  std::expected<Ticket,Failure> findValueDetailed(Peer,std::int8_t remoteVersion,lookup::nodes::NodeId target,
      std::int32_t flags,std::int32_t maximumValues,QObject* receiver,DetailedFindValueCallback,
      std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  // One owned physical datagram; no logical STORE batching or DB authorization.
  std::expected<Ticket,Failure> store(Peer,std::int8_t remoteVersion,StoreRequest,QObject* receiver,StoreCallback,
      std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  // Empty FIND_NODE target, without implicit contact/token persistence.
  std::expected<Ticket,Failure> requestStoreToken(Peer,std::int8_t remoteVersion,QObject* receiver,DetailedFindNodeCallback,
      std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  // Explicit normal receiver binding. Actual router/DB must already be bound
  // on THIS Qt thread to THIS socket identity. No caller-supplied closest,
  // seeded, token or value facts. Shared ownership lasts through close/join.
  // current checks the owner's held profile and stop state before each effect.
  std::expected<void,Failure> serveDatabase(std::shared_ptr<azureus_router::RouterOwner>,
      std::shared_ptr<database::Owner>,std::function<bool()> current);
  std::expected<GeneratedServiceState,Failure> generatedServiceState() const;
  std::expected<database::Snapshot,Failure> databaseState() const;
  // Owner-only pull. Moved receipts own the request referenced by policy indices.
  std::expected<std::optional<ServiceReceipt>,Failure> takeServiceReceipt();
  bool cancel(Ticket);
  void close();
  Peer endpoint() const { return {socket_.localAddress(),socket_.localPort()}; }
  std::size_t pendingCount() const { return pending_.size(); }
  std::size_t completionCount() const { return completions_.size(); }
  bool deadlineActive() const { return deadline_.isActive(); }
  Statistics statistics() const { return statistics_; }
  // Owner-only O(1) pull. No start/read/drain/timer or callbacks. Identity
  // exhaustion affects this optional diagnostic only; transport still operates.
  std::expected<TransportStatisticsSnapshot,Failure> statisticsSnapshot() const noexcept;
  std::expected<OwnedDhtStatisticsSnapshot,Failure> dhtStatisticsSnapshot() const noexcept;
private:
  using Clock=std::chrono::steady_clock;
  using Value=std::variant<packet::Decoded,find::Decoded,lookup::Decoded,storage::Decoded,remote_error::Decoded>;
  using Result=std::expected<Value,Failure>;
  using CompletionCallback=std::move_only_function<void(Result)>;
  using Encoder=std::move_only_function<std::expected<std::string,Failure>(packet::udp::EncodeFields)>;
  template<class T,class CallbackType> static CompletionCallback detailedCallback(CallbackType callback) {
    if (!callback) { return {}; }
    return [callback=std::move(callback)](Result result) mutable {
      if (!result) { callback(std::unexpected(result.error())); }
      else if (auto* decoded=std::get_if<T>(&*result)) {
        callback(DetailedOutcome<T>{std::in_place,std::in_place_type<T>,std::move(*decoded)});
      } else if (auto* error=std::get_if<remote_error::Decoded>(&*result)) {
        callback(DetailedOutcome<T>{std::in_place,std::in_place_type<remote_error::Decoded>,std::move(*error)});
      } else { callback(std::unexpected(Failure::State)); }
    };
  }
  template<class T,class CallbackType> static std::move_only_function<void(DetailedOutcome<T>)> ordinaryCallback(CallbackType callback) {
    if (!callback) { return {}; }
    return [callback=std::move(callback)](DetailedOutcome<T> result) mutable {
      if (!result) { callback(std::unexpected(result.error())); }
      else if (auto* decoded=std::get_if<T>(&*result)) { callback(std::expected<T,Failure>{std::move(*decoded)}); }
      else { callback(std::unexpected(Failure::Remote)); }
    };
  }
  enum class State { Idle,Bound,Closed };
  struct CallbackState { QPointer<QObject> receiver;CompletionCallback callback; };
  struct Pending {
    Ticket ticket;Peer peer;std::int64_t connection;std::int32_t expectedAction;Clock::time_point expires;
    std::shared_ptr<CallbackState> callback;QMetaObject::Connection death;Clock::time_point started;
  };
  void observePing(const Pending&,const Result&)noexcept;
  struct Completion { std::shared_ptr<CallbackState> callback;Result outcome; };
  std::expected<Ticket,Failure> submit(Peer,std::int8_t,QObject*,CompletionCallback,
      std::chrono::nanoseconds,std::int32_t action,Encoder);
  bool owner() const;
  static Peer normalize(Peer);
  bool allowed(const Peer&) const;
  std::uint64_t word();
  void armDeadline();
  void observeOperation(const Pending&,OperationEventKind) noexcept;
  void observeVivaldi(const Pending&,const packet::Decoded&) noexcept;
  void observePeerPosition(const Pending&,VivaldiValue,std::int64_t elapsed,std::int64_t observed,bool accepted)noexcept;
  void expire();
  void finish(std::int32_t,Result);
  void part(std::int32_t,Result);
  void abandon(Ticket);
  void scheduleDelivery();
  void deliver();
  void consume();
  void answer(const Peer&,const packet::Decoded&,std::int64_t receivedAt);
  void answer(const Peer&,const find::Decoded&,std::int64_t receivedAt);
  void answer(const Peer&,const lookup::Decoded&,std::int64_t receivedAt);
  void answer(const Peer&,const storage::Decoded&,std::int64_t receivedAt);
  void answerService(const Peer&,ServiceRequest,std::int64_t receivedAt);
  void answerDatabase(const Peer&,const lookup::Decoded&,std::int64_t receivedAt);
  void sendServiceReply(ServiceReceipt&,std::expected<std::string,Failure>);
  bool normalCurrent()noexcept;
  bool normalAlive(const find::nodes::Contact&)noexcept;
  std::expected<std::vector<find::nodes::Snapshot>,Failure> normalClosest(const find::nodes::NodeId&);
  database::Owner* activeDatabase()const noexcept{return normalDatabase_?normalDatabase_.get():database_.get();}
  std::shared_ptr<azureus_router::RouterOwner> normalRouter_;
  std::shared_ptr<database::Owner> normalDatabase_;
  std::function<bool()> normalCurrent_;
  std::vector<find::nodes::Contact> normalContacts_; // Only actually admitted request sources.
  Config config_;
  std::unique_ptr<find::nodes::NodeCache> nodeCache_;
  std::unique_ptr<auth::Authority> authority_;
  std::unique_ptr<policy::Admission> admission_;
  std::unique_ptr<database::Owner> database_;
  std::optional<find::nodes::NodeId> localId_;
  std::deque<ServiceReceipt> serviceReceipts_;
  QUdpSocket socket_;
  QChronoTimer deadline_;
  State state_=State::Idle;
  std::vector<Peer> peers_;
  std::unordered_map<std::int32_t,Pending> pending_;
  std::deque<Completion> completions_;
  std::uint64_t nextTicket_=0;
  bool draining_=false,drainScheduled_=false,delivering_=false,deliveryScheduled_=false;
  Statistics statistics_;
  const std::uint64_t statisticsGeneration_;
  mutable std::uint64_t statisticsPublication_=0;
  TransportStatisticsFamily statisticsFamily_=TransportStatisticsFamily::Unbound;
  std::array<OperationEvent,OwnedDhtStatisticsSnapshot::MaximumEvents> operationEvents_{};
  std::size_t eventBegin_=0,eventCount_=0;std::uint64_t eventSerial_=0,droppedEvents_=0;
  VivaldiPosition vivaldi_;
  std::optional<VivaldiValue> lastRemotePosition_;std::optional<std::int64_t> lastRtt_;
  std::optional<OwnedPingObservation> lastPing_;
  std::uint64_t pingSequence_=0;bool pingLost_=false;
  std::array<VivaldiPeerObservation,OwnedDhtStatisticsSnapshot::MaximumPeers> peerPositions_{};
  std::size_t peerPositionCount_=0;std::uint64_t peerPositionSequence_=0,droppedPeerPositions_=0;bool peerPositionsLost_=false;
};
}

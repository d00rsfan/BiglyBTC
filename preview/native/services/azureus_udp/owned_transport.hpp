// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ping.hpp"
#include "biglybtc/move_only_function.hpp"
#include "../azureus_transfer/call.hpp"
#include "biglybtc/shared_udp_witness.hpp"
#include "transport_statistics.hpp"
#include "reachability.hpp"
#include "owned_dht_statistics.hpp"
#include "find_node.hpp"
#include "find_value.hpp"
#include "store.hpp"
#include "query_storage.hpp"
#include "../azureus_key_block/key_block.hpp"
#include "error.hpp"
#include "authority.hpp"
#include "store_policy.hpp"
#include "database.hpp"
#include "router.hpp"
#include <memory>
#include <atomic>
#include <QChronoTimer>
#include <QHostAddress>
#include <QPointer>
#include <QUdpSocket>
#include <chrono>
#include <deque>
#include <functional>
#include <unordered_map>
#include <utility>
#include "../azureus_control/lookup.hpp"
namespace biglybtc::services::azureus_key_block {class State;}
namespace biglybtc::services::azureus_udp {
namespace packet=biglybtc::services::azureus_packets;
namespace find=biglybtc::services::azureus_find_node;
namespace lookup=biglybtc::services::azureus_find_value;
namespace storage=biglybtc::services::azureus_store;
namespace query=biglybtc::services::azureus_query_storage;
namespace keyblock=biglybtc::services::azureus_key_block;
namespace remote_error=biglybtc::services::azureus_error;
namespace auth=biglybtc::services::azureus_auth;
namespace policy=biglybtc::services::azureus_store_policy;
namespace database=biglybtc::services::azureus_db_runtime;
struct Peer { QHostAddress address;quint16 port=0;bool operator==(const Peer&) const=default; };
struct Ticket { std::uint64_t value=0;bool operator==(const Ticket&) const=default; };
// Correlated remote claim, not a confirmed local address or send authority.
// Construction is private: only a live actor can observe a pending RPC error1.
class AddressReport final {
public:
  AddressReport(const AddressReport&)=default;
  const Peer& reporter()const noexcept{return reporter_;}
  const packet::udp::Address& reported()const noexcept{return reported_;}
  std::size_t observedContacts()const noexcept{return observedContacts_;}
  std::uint64_t generation()const noexcept{return generation_;}
  std::uint64_t sequence()const noexcept{return sequence_;}
private:
  AddressReport()=default;
  Peer reporter_;packet::udp::Address reported_;
  std::size_t observedContacts_=0;
  std::uint64_t generation_=0,sequence_=0;
  friend class OwnedPingTransport;
};
enum class Failure { State,Thread,Peer,Capacity,Version,Deadline,Receiver,Random,Encode,Send,Timeout,Cancelled,Closed,Remote,Authority };
using Outcome=std::expected<packet::Decoded,Failure>;
using Callback=biglybtc::core::MoveOnlyFunction<void(Outcome)>;
using FindNodeOutcome=std::expected<find::Decoded,Failure>;
using FindNodeCallback=biglybtc::core::MoveOnlyFunction<void(FindNodeOutcome)>;
struct FindNodeReply {
  std::int32_t randomId=0,status=-1,estimatedSize=0;
  std::vector<find::nodes::Snapshot> contacts;
};
using FindValueOutcome=std::expected<lookup::Decoded,Failure>;
using FindValueCallback=biglybtc::core::MoveOnlyFunction<void(FindValueOutcome)>;
template<class T> using DetailedOutcome=std::expected<std::variant<T,remote_error::Decoded>,Failure>;
using DetailedPingOutcome=DetailedOutcome<packet::Decoded>;
using DetailedPingCallback=biglybtc::core::MoveOnlyFunction<void(DetailedPingOutcome)>;
using DetailedFindNodeOutcome=DetailedOutcome<find::Decoded>;
using DetailedFindNodeCallback=biglybtc::core::MoveOnlyFunction<void(DetailedFindNodeOutcome)>;
using DetailedFindValueOutcome=DetailedOutcome<lookup::Decoded>;
using DetailedFindValueCallback=biglybtc::core::MoveOnlyFunction<void(DetailedFindValueOutcome)>;
using StoreOutcome=DetailedOutcome<storage::Decoded>;
using StoreCallback=biglybtc::core::MoveOnlyFunction<void(StoreOutcome)>;
using QueryStoreOutcome=DetailedOutcome<query::Decoded>;
using QueryStoreCallback=biglybtc::core::MoveOnlyFunction<void(QueryStoreOutcome)>;
using KeyBlockOutcome=DetailedOutcome<keyblock::Decoded>;
using KeyBlockCallback=biglybtc::core::MoveOnlyFunction<void(KeyBlockOutcome)>;
struct KeyBlockMaintenanceFacts {std::uint64_t operation=0,considered=0,tokens=0,packets=0,acknowledged=0,failed=0;bool active=false;std::optional<Failure> failure;};
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
// A copyable receipt can only be minted by its actual actor. Retaining it is
// not authority: answerTransfer rechecks peer, generation, source and budget.
class TransferReceipt final {
public:
  TransferReceipt()=default;
  bool operator==(const TransferReceipt&)const=default;
private:
  std::uint64_t generation_=0,serial_=0,nonce_=0;
  friend class OwnedPingTransport;
};
// Source/handler/receiver-scoped DATA authority; it never admits a DHT peer.
class TransferPeerLease final {
public:
 TransferPeerLease()=default;
 bool operator==(const TransferPeerLease&)const=default;
private:
 std::uint64_t generation_=0,serial_=0;
 friend class OwnedPingTransport;
};
enum class TransferMode {Call,Read,Write};
// Explicit normal Call/Write handler opt-in; no new peer/routing authority.
enum class TransferOriginator {ExactEndpoint,SameIp};
struct TransferRequest {
  TransferReceipt receipt;Peer peer;std::int32_t network=0;
  std::string handler,request;
  std::uint64_t sourceRevision=0;
  std::chrono::steady_clock::time_point deadline;
  TransferMode mode=TransferMode::Call;
  azureus_nodes::Snapshot advertised; // External identity; peer remains actual UDP source.
};
using TransferOutcome=std::expected<std::string,Failure>;
using TransferCallback=biglybtc::core::MoveOnlyFunction<void(TransferOutcome)>;
using TransferRequestCallback=std::function<void(std::shared_ptr<const TransferRequest>)>;
// Physical channel supplied by the engine owner. No socket is opened by this
// actor in shared mode. advertised is the current DHT identity; local identifies
// the actual SDK listener. The engine must revoke/recreate on identity changes.
struct SharedChannel {
  Peer local,advertised;
  std::function<bool()> current;
  std::function<std::expected<qint64,Failure>(std::string_view,const Peer&,
      std::chrono::steady_clock::time_point)> send;
  std::shared_ptr<const core::SharedUdpWitness> witness{};
};
enum class RoutingScope {ExactPeers,OwnedLoopback,PublicBound};
inline constexpr bool dynamicRoutingScope(RoutingScope scope)noexcept{return scope==RoutingScope::OwnedLoopback||scope==RoutingScope::PublicBound;}
// Packet means this endpoint's matched packet; ContactRecord retains the
// existing cache/NODE supplied-version provenance. ProbeHint grants neither.
enum class RoutingProtocolSource {ProbeHint,ContactRecord,Packet};
struct RoutingContactSnapshot {find::nodes::Contact contact;RoutingProtocolSource protocolSource=RoutingProtocolSource::ProbeHint;std::int32_t nodeStatus=-1;};
struct RoutingSnapshot {
  std::uint64_t generation=0,operation=0,revision=0;
  bool finished=false;
  bool seeded=false; // Last successful self-ID integration on this actor, never authority by itself.
  std::shared_ptr<const azureus_control::LookupSnapshot> lookup;
  std::optional<Failure> failure;
  std::uint64_t learned=0,rejected=0,pings=0;
  // Passive actor attachments; existing raw actor cap256, normal service cap32.
  // Hints are labelled, not endpoint observations.
  std::array<RoutingContactSnapshot,azureus_router::MaximumRouterContacts> contacts{};std::size_t contactCount=0;
};
struct SavedRoutingContact {find::nodes::Snapshot contact;std::int64_t timeAlive=0;};
struct MaintenanceFacts {std::uint64_t ticks=0,refreshes=0;std::size_t pending=0;std::optional<Failure> failure;};
struct Config {

  std::int32_t network=4,instance=0;
  std::int8_t version=55,flags=0,flags2=0;
  bool servePings=false;bool initialReachable=true;
  bool bootstrapOnly=false; // Shared SDK only: NODE probes, never room/DB service.
  RoutingScope routingScope=RoutingScope::ExactPeers;
  std::optional<FindNodeReply> findNodeReply; // Supplied inert snapshot, no nonce authorization/routing.
  std::optional<std::vector<FindValueSlice>> findValueReply; // Inert owned test pieces only.
  std::optional<std::string> storeReply; // Inert diversification, including an empty array.
  std::optional<ErrorReplySnapshot> storeErrorReply; // Mutually exclusive with storeReply, no admission/authority.
  std::optional<GeneratedStoreService> generatedStoreService; // Explicit generated-only authority/admission mode.
  std::size_t maximumPending=128,maximumPeers=128;
  biglybtc::core::MoveOnlyFunction<std::uint64_t()> randomWord; // null -> Qt system generator.
  bool observeOperations=false,enableVivaldi=false; // Explicit owner policy; never enabled by a view.
  bool enableSurveys=false; // Startup policy supplied by the actual profile owner.
};

// Development transport: fresh numeric-loopback sockets, exact peers by default.
// Explicit normal OwnedLoopback scope can discover bounded same-family peers;
// every API/destruction still belongs to its Qt owner. No DNS, proxy or public
// route exists; profile validation is the bound owner's asynchronous preflight.
struct CacheMaintenanceFacts {std::uint64_t operation=0,considered=0,tokens=0,packets=0,values=0,purged=0,failed=0;bool active=false;std::optional<Failure> failure;};
struct SurveyMaintenanceFacts {std::uint64_t operation=0,considered=0,eligible=0,lookups=0,queries=0,replies=0,tokens=0,packets=0,values=0,failed=0;bool active=false;std::optional<Failure> failure;};

class OwnedPingTransport final : public QObject {
public:
  explicit OwnedPingTransport(Config,QObject* parent=nullptr);
  ~OwnedPingTransport() override;
  std::expected<void,Failure> start(const QHostAddress& bindAddress=QHostAddress::LocalHost,quint16 bindPort=0,std::function<bool()> publicPermit={});
  std::expected<void,Failure> startShared(SharedChannel,std::function<bool()> publicPermit={});
  // Owner-thread bounded ingress. Uses the same parser/correlation path as UDP.
  // Queueing a frame never admits its sender as a peer or grants source rights.
  bool receiveShared(Peer,std::string);
  std::int32_t network()const noexcept{return config_.network;}
  std::expected<void,Failure> updateBootstrapIdentity(Peer);
  std::expected<void,Failure> allowPeer(Peer);
  // Normal dynamic scope only, under the existing worker source permit.
  // A supplied version is an unobserved probe hint; this never grants Alive.
  std::expected<void,Failure> attachKnownPeer(Peer,std::int8_t protocolHint,std::chrono::steady_clock::time_point absoluteEnd);
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
  // One original QUERY_STORE packet, at most 384 rows and the ordinary MTU.
  // Observational only: no STORE token or value publication is granted.
  std::expected<Ticket,Failure> sendKeyBlock(Peer,std::int8_t remoteVersion,keyblock::RequestBody,QObject*,KeyBlockCallback,std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  std::expected<std::uint64_t,Failure> republishKeyBlocks(std::chrono::milliseconds maximumDuration=std::chrono::minutes(30));
  KeyBlockMaintenanceFacts keyBlockMaintenanceFacts()const noexcept;
  bool cancelKeyBlockForwarding(std::uint64_t operation);
  // Same-thread normal owner only. Saves complete inside a fresh source permit.
  // No volatile acknowledgement is sent for changed signed metadata.
  std::expected<void,Failure> serveKeyBlocks(std::shared_ptr<keyblock::State>,std::function<bool(std::uint64_t,biglybtc::core::MoveOnlyFunction<void(bool)>)>);
  std::expected<Ticket,Failure> queryStorage(Peer,std::int8_t remoteVersion,query::RequestBody,QObject* receiver,QueryStoreCallback,
      std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  std::expected<Ticket,Failure> store(Peer,std::int8_t remoteVersion,StoreRequest,QObject* receiver,StoreCallback,
      std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  // Empty FIND_NODE target, without implicit contact/token persistence.
  std::expected<Ticket,Failure> requestStoreToken(Peer,std::int8_t remoteVersion,QObject* receiver,DetailedFindNodeCallback,
      std::chrono::nanoseconds deadline=std::chrono::seconds(30));
  // Explicit normal receiver binding. Actual router/DB must already be bound
  // on THIS Qt thread to THIS socket identity. No caller-supplied closest,
  // seeded, token or value facts. Shared ownership lasts through close/join.
  // current checks the owner's held profile and stop state before each effect.
  // Async completion stays on this actor: true resumes inside the one-shot
  // worker-held permit until actor acknowledgement; false reports actual
  // source rejection only after that worker has released its source gate.
  std::expected<void,Failure> serveDatabase(std::shared_ptr<azureus_router::RouterOwner>,
      std::shared_ptr<database::Owner>,std::function<bool()> current,
      std::function<bool(biglybtc::core::MoveOnlyFunction<void(bool)>)> preflight={});
  // Normal Original-diversification receiver only. Completion must execute on
  // this actor under the current source permit AFTER the required checkpoint.
  // A delayed GET additionally reacquires its key permit and checks the pure
  // content receipt; it never repeats the DB read/counter callbacks.
  std::expected<void,Failure> setStorageCheckpoint(
      std::function<bool(std::uint64_t,biglybtc::core::MoveOnlyFunction<void(bool)>)>,
      std::chrono::milliseconds maximumWait=std::chrono::seconds(5));
  // Optional key-specific gate for local torrent-value withdrawal before GET.
  // Completion obeys the same worker-held permit/acknowledgement contract.
  std::expected<void,Failure> setLocalReadPreflight(
      std::function<bool(std::string,biglybtc::core::MoveOnlyFunction<void(bool)>)>);
  // Explicit normal-owner self-ID FIND_NODE. Completion and seeded lineage
  // arise only inside this actor from its own actual requests/replies.
  std::expected<std::uint64_t,Failure> integrateRouting(Peer,std::chrono::nanoseconds);
  bool cancelRouting(std::uint64_t);
  std::expected<void,Failure> restoreRoutingContacts(std::span<const SavedRoutingContact>);
  std::expected<std::vector<SavedRoutingContact>,Failure> exportRoutingContacts();
  std::expected<std::uint64_t,Failure> integrateKnownRouting(std::chrono::nanoseconds);
  std::expected<void,Failure> maintainRouting(std::int64_t wallMilliseconds);
  MaintenanceFacts maintenanceFacts()const noexcept;
  std::expected<std::uint64_t,Failure> republishCachedValues(std::int64_t now,std::int64_t period,std::chrono::milliseconds maximumDuration=std::chrono::minutes(30));
  CacheMaintenanceFacts cacheMaintenanceFacts()const noexcept;
  std::expected<std::uint64_t,Failure> surveyCachedValues(std::chrono::milliseconds maximumDuration=std::chrono::minutes(30));
  SurveyMaintenanceFacts surveyMaintenanceFacts()const noexcept;
  bool cancelSurveyValues(std::uint64_t operation);


  bool isRoutingPeer(const Peer&)const noexcept;
  ReachabilityFacts reachabilityFacts()const{return reachability_.facts();}
  std::expected<bool,Failure> updateReachability();
  std::expected<void,Failure> setReachability(std::optional<bool> knownReachable);
  std::shared_ptr<const RoutingSnapshot> routingSnapshot()const noexcept;
  std::expected<GeneratedServiceState,Failure> generatedServiceState() const;
  std::expected<database::Snapshot,Failure> databaseState() const;
  // Owner-only pull. Moved receipts own the request referenced by policy indices.
  std::expected<std::optional<ServiceReceipt>,Failure> takeServiceReceipt();
  // Opt-in normal service: <=32 handlers, <=6 concurrent calls per handler,
  // <=64 calls/requests globally, <=16KiB values, <=30s. No peer admission.
  std::expected<void,Failure> registerTransferHandler(std::string handler,bool disableCallAcks,
      QObject* receiver,TransferRequestCallback,std::function<std::optional<std::uint64_t>()> sourceRevision,TransferMode mode=TransferMode::Call,bool acceptEmptyWrites=false,TransferOriginator originator=TransferOriginator::ExactEndpoint);
  // Trusted owner, under its held source permit. At most64 leases /120s.
  // The expected external identity is separate from the actual socket tuple.
  std::expected<TransferPeerLease,Failure> leaseTransferPeer(Peer,std::string_view handler,
      azureus_nodes::Snapshot expectedExternal,QObject* registeredReceiver,
      std::uint64_t heldSourceRevision,std::chrono::steady_clock::time_point end);
  bool releaseTransferPeer(TransferPeerLease);
  // Same logical peer, new physical port: Call only, fresh matching lease and
  // receiver/source required. Overall deadline and request identity are kept.
  std::expected<std::size_t,Failure> rerouteTransferPeer(std::string_view handler,
      Peer oldPeer,Peer newPeer,QObject* registeredReceiver,std::uint64_t sourceRevision);

  bool isTransferPeer(const Peer&,std::string_view handler)const noexcept;
  std::expected<Ticket,Failure> callTransfer(Peer,std::int8_t remoteVersion,std::string handler,
      std::string request,QObject* receiver,TransferCallback,std::chrono::steady_clock::time_point deadline);
  // Original one-way NAT DATA write with an empty request key. Completion is
  // transport ACK only, not application acceptance or a call reply. Requires a
  // Write registration or explicit empty-write opt-in on the same Call handler.
  std::expected<Ticket,Failure> writeTransfer(Peer,std::int8_t remoteVersion,std::string handler,
      std::string request,QObject* receiver,TransferCallback,std::chrono::steady_clock::time_point deadline);
  // Original DDB read: request is the DATA key (<=255 bytes), response uses
  // the same connection's ReadReply, never the chat write/call protocol.
  std::expected<Ticket,Failure> readTransfer(Peer,std::int8_t remoteVersion,std::string handler,
      std::string request,QObject* receiver,TransferCallback,std::chrono::steady_clock::time_point deadline);
  // Transient original read: no incoming handler registration/room identity.
  // Every physical request/reply turn still needs this fresh source revision.
  std::expected<Ticket,Failure> readTransfer(Peer,std::int8_t,std::string handler,
      std::string request,QObject*,TransferCallback,std::chrono::steady_clock::time_point,
      std::function<std::optional<std::uint64_t>()> sourceRevision);
  std::expected<void,Failure> answerTransfer(const TransferReceipt&,std::string reply);
  bool unregisterTransferHandler(std::string_view handler);
  bool cancelTransfer(Ticket);
  bool cancel(Ticket);
  // Queued, bounded delivery, revoked by replacement/close. Owner thread only.
  std::expected<void,Failure> setAddressReportSink(std::function<void(const AddressReport&)>);
  void close();
  Peer endpoint() const { return shared_?shared_->advertised:Peer{socket_.localAddress(),socket_.localPort()}; }
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
  std::expected<Ticket,Failure> startTransfer(Peer,std::int8_t,std::string,std::string,QObject*,TransferCallback,std::chrono::steady_clock::time_point,TransferMode,std::function<std::optional<std::uint64_t>()> = {});
  struct TransferState;
  struct DataPeerLease {
    TransferPeerLease receipt;Peer peer;std::string handler;azureus_nodes::Snapshot external;
    QPointer<QObject> receiver;std::uint64_t revision=0;
    std::chrono::steady_clock::time_point end;
  };
  std::vector<DataPeerLease> dataPeers_;std::uint64_t nextDataPeerSerial_=0;
  const DataPeerLease* dataPeer(const Peer&,std::string_view)const noexcept;
  bool anyDataPeer(const Peer&)const noexcept;
  bool transferAuthorized(const std::shared_ptr<TransferState>&,const Peer&,std::uint64_t)const noexcept;

  std::vector<std::shared_ptr<TransferState>> handlers_,transfers_;
  std::uint64_t nextTransferSerial_=0;
  bool transferOwned(const std::shared_ptr<TransferState>&)const noexcept;
  bool transferOriginator(const Peer&,const azureus_transfer::Packet&)const;
  void reapTransfers();
  void transferPacket(const Peer&,azureus_transfer::Packet);
  void pumpTransfer();
  void transferTurn(const std::shared_ptr<TransferState>&);
  void finishTransfer(const std::shared_ptr<TransferState>&,TransferOutcome);
  void closeTransfer();
  Clock::time_point transferWakeup()const noexcept;
  bool transferCurrent(const std::shared_ptr<TransferState>&,std::uint64_t)noexcept;
  std::expected<void,Failure> sendTransfer(const std::shared_ptr<TransferState>&,const Peer&,std::int8_t,std::int64_t,const azureus_transfer::Data&,Clock::time_point);
  using Value=std::variant<packet::Decoded,find::Decoded,lookup::Decoded,storage::Decoded,query::Decoded,keyblock::Decoded,remote_error::Decoded>;
  using Result=std::expected<Value,Failure>;
  using CompletionCallback=biglybtc::core::MoveOnlyFunction<void(Result)>;
  using Encoder=biglybtc::core::MoveOnlyFunction<std::expected<std::string,Failure>(packet::udp::EncodeFields)>;
  // Every physical PublicBound write consumes the still-held source permit.
  std::expected<qint64,Failure> writePacket(std::string_view,const Peer&,Clock::time_point end=Clock::time_point::max(),const std::shared_ptr<TransferState>& dataState={},std::optional<std::uint64_t> dataRevision={});
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
  template<class T,class CallbackType> static biglybtc::core::MoveOnlyFunction<void(DetailedOutcome<T>)> ordinaryCallback(CallbackType callback) {
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

  struct SurveyBook;struct SurveyJob;
  std::shared_ptr<SurveyBook> surveyBook_;SurveyMaintenanceFacts surveyFacts_;
  bool surveyAlive(const std::shared_ptr<SurveyJob>&)const noexcept;
  bool surveyOwnsRouting()const noexcept;
  void cancelSurvey(std::optional<Failure> failure={});
  void tickSurvey();
  void surveyStep(const std::shared_ptr<SurveyJob>&,bool granted=false);
  void surveyPermit(const std::shared_ptr<SurveyJob>&,std::optional<std::size_t>,bool repair,std::optional<std::string>,biglybtc::core::MoveOnlyFunction<void()>);
  void surveyGather(const std::shared_ptr<SurveyJob>&);
  void surveyRoute(const std::shared_ptr<SurveyJob>&);
  void surveyBuildQueries(const std::shared_ptr<SurveyJob>&);
  void surveyQueryStep(const std::shared_ptr<SurveyJob>&,std::size_t);
  void surveyQueryReply(const std::shared_ptr<SurveyJob>&,std::size_t,std::uint64_t,QueryStoreOutcome);
  void surveyProjectReply(const std::shared_ptr<SurveyJob>&);
  void surveyBuildRepairs(const std::shared_ptr<SurveyJob>&);
  void surveyRepairStep(const std::shared_ptr<SurveyJob>&,std::size_t);
  void surveyTokenReply(const std::shared_ptr<SurveyJob>&,std::size_t,std::uint64_t,DetailedFindNodeOutcome);
  void surveyStoreReply(const std::shared_ptr<SurveyJob>&,std::size_t,std::uint64_t,StoreOutcome);
  void observeSurveyQuery(const find::nodes::Contact&,const std::vector<std::optional<std::string>>&);
  struct QueryReply {
    Peer peer;query::Decoded request;std::optional<find::nodes::Contact> sender;std::int64_t receivedAt=0;Clock::time_point end;
    std::vector<std::string> prefixes,keys;
    std::vector<std::optional<std::string>> matches;
    std::size_t nextKey=0;std::uint64_t phase=0;bool live=true,initialized=false;
    QueryReply(Peer p,query::Decoded r):peer(std::move(p)),request(std::move(r)){}
  };
  std::vector<std::shared_ptr<QueryReply>> queryReplies_;
  struct KeyBlockForward;
  bool keyBlockForwardAlive(const std::shared_ptr<KeyBlockForward>&)const noexcept;
  void finishKeyBlockForwarding(std::optional<Failure> = {});
  void tickKeyBlockForwarding();
  void pumpKeyBlockForwarding(const std::shared_ptr<KeyBlockForward>&);
  void keyBlockForwardStep(const std::shared_ptr<KeyBlockForward>&,std::size_t);
  void keyBlockForwardPermit(const std::shared_ptr<KeyBlockForward>&,std::size_t,biglybtc::core::MoveOnlyFunction<void()>);
  struct KeyBlockReply;
  bool sendKeyBlockedReply(const Peer&,const packet::udp::Header&,std::int64_t,std::string_view key);
  void observeKeyBlockError(const remote_error::Decoded&);
  void observeAddressReport(const Pending&,const remote_error::Decoded&);
  void answerKeyBlock(const Peer&,keyblock::Decoded,std::int64_t);
  void keyBlockReplyGate(const std::shared_ptr<KeyBlockReply>&,bool final=false);
  void keyBlockReplyReady(const std::shared_ptr<KeyBlockReply>&,bool);
  void finishKeyBlockReply(const std::shared_ptr<KeyBlockReply>&);
  void cancelKeyBlockReplies();void expireKeyBlockReplies();
  void answerQuery(const Peer&,query::Decoded,std::int64_t);
  void queryReplyGate(const std::shared_ptr<QueryReply>&);
  void queryReplyReady(const std::shared_ptr<QueryReply>&);
  void finishQueryReply(const std::shared_ptr<QueryReply>&);
  void expireQueryReplies();
  void cancelQueryReplies();

  void answerDatabase(const Peer&,const lookup::Decoded&,std::int64_t receivedAt);
  void sendServiceReply(ServiceReceipt&,std::expected<std::string,Failure>);
  bool normalCurrent()noexcept;
  bool normalAlive(const find::nodes::Contact&)noexcept;
  std::expected<std::vector<find::nodes::Snapshot>,Failure> normalClosest(const find::nodes::NodeId&);
  struct Routing;
  std::shared_ptr<Routing> routing_;
  std::shared_ptr<const RoutingSnapshot> routingSnapshot_;
  std::uint64_t nextRouting_=0,routingRevision_=0,learnedRouting_=0,rejectedRouting_=0,routingPings_=0;
  bool normalSeeded_=false;
  struct RoutingPing {Ticket ticket;find::nodes::NodeId id;};
  std::array<std::optional<RoutingPing>,8> routingPingsPending_{};
  std::deque<azureus_router::RouterIntent> routingIntents_;
  bool ownedRoutingPeer(const Peer&)const noexcept;
  std::optional<Peer> routingPeer(const find::nodes::Contact&)const;
  bool learnRoutingContact(const find::nodes::Contact&,bool alive,RoutingProtocolSource=RoutingProtocolSource::ContactRecord);
  void answerRoutingPing(const Peer&,packet::Decoded,std::int64_t);
  void routingReply(const std::shared_ptr<Routing>&,std::uint64_t,FindNodeOutcome);
  void pumpRouting(const std::shared_ptr<Routing>&);
  void finishRouting(const std::shared_ptr<Routing>&);
  void captureRouting()noexcept;
  void routingFailed(Failure)noexcept;
  void dispatchRoutingPings();
  std::expected<std::uint64_t,Failure> startRouting(find::nodes::NodeId,Clock::time_point,bool integration);
  void nextRoutingRefresh();
  std::deque<find::nodes::NodeId> routingRefreshes_;
  std::vector<find::nodes::NodeId> importedRouting_;
  MaintenanceFacts maintenance_;

  database::Owner* activeDatabase()const noexcept{return normalDatabase_?normalDatabase_.get():database_.get();}
  std::shared_ptr<azureus_router::RouterOwner> normalRouter_;
  std::shared_ptr<database::Owner> normalDatabase_;
  std::function<bool()> normalCurrent_;
  std::function<bool(biglybtc::core::MoveOnlyFunction<void(bool)>)> normalPreflight_;
  std::function<bool(std::string,biglybtc::core::MoveOnlyFunction<void(bool)>)> localReadPreflight_;
  struct CacheJob;std::shared_ptr<CacheJob> cacheJob_;CacheMaintenanceFacts cacheFacts_;std::string cacheResumeAfter_;
  bool cacheAlive(const std::shared_ptr<CacheJob>&)const noexcept;
  bool cachePeerCurrent(const std::shared_ptr<CacheJob>&,std::size_t,std::uint64_t)const noexcept;
  void cacheLoadPage(const std::shared_ptr<CacheJob>&,bool current=false);
  void pumpCache(const std::shared_ptr<CacheJob>&);void cacheStep(const std::shared_ptr<CacheJob>&,std::size_t);
  void cachePeerDone(const std::shared_ptr<CacheJob>&,std::size_t,bool,bool=false);
  void cachePermit(const std::shared_ptr<CacheJob>&,std::size_t,std::optional<std::string>,biglybtc::core::MoveOnlyFunction<void()>);
  void cachePurge(const std::shared_ptr<CacheJob>&);void expireCache();void cancelCacheRepublish(std::optional<Failure> reason={});
  struct StorageReply {
    std::uint64_t policy=0;std::optional<std::string> key;std::optional<std::uint64_t> content;
    Clock::time_point end;std::size_t bytes=0;bool live=true,registering=true,readPending=false;
    std::atomic<bool> checkpointCallback{false},readCallback{false};
    biglybtc::core::MoveOnlyFunction<void()> ready;
  };
  std::function<bool(std::uint64_t,biglybtc::core::MoveOnlyFunction<void(bool)>)> storageCheckpoint_;
  std::vector<std::shared_ptr<StorageReply>> storageReplies_;std::size_t storageReplyBytes_=0;
  std::chrono::milliseconds storageReplyWait_{5000};
  bool queueStorageReply(Peer,std::optional<std::string>,std::optional<std::uint64_t>,std::size_t,biglybtc::core::MoveOnlyFunction<void()>);
  void resumeStorageReply(const std::shared_ptr<StorageReply>&,bool);
  void finishStorageReply(const std::shared_ptr<StorageReply>&,bool);
  void retireStorageReply(const std::shared_ptr<StorageReply>&);
  void expireStorageReplies();void cancelStorageReplies();
  void sendCheckpointStore(ServiceReceipt&,std::int64_t);
  void sendDatabaseResult(const Peer&,const lookup::Decoded&,std::int64_t,const std::optional<database::Read>&,const find::nodes::Contact&);
  bool normalResuming_=false;
  struct NormalContact:find::nodes::Contact {
    RoutingProtocolSource protocolSource=RoutingProtocolSource::Packet;
    std::int32_t nodeStatus=-1;
    NormalContact(const find::nodes::Contact& value,RoutingProtocolSource source=RoutingProtocolSource::Packet)noexcept:find::nodes::Contact(value),protocolSource(source){}
  };
  Config config_;
  Reachability reachability_{config_.initialReachable,std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()};
  std::int32_t nodeStatus_=-1; // Original UNKNOWN until reachability is accurate.
  std::shared_ptr<keyblock::State> normalKeyBlocks_;
  std::function<bool(std::uint64_t,biglybtc::core::MoveOnlyFunction<void(bool)>)> keyBlockCheckpoint_;
  std::vector<std::shared_ptr<KeyBlockReply>> keyBlockReplies_;
  QChronoTimer keyBlockReplyDeadline_;
  std::shared_ptr<KeyBlockForward> keyBlockForward_;
  KeyBlockMaintenanceFacts keyBlockForwardFacts_;QChronoTimer keyBlockForwardDeadline_;
  std::optional<std::string> keyBlockForwardCursor_;

  std::size_t indirectKeyBlockPending_=0;std::uint64_t indirectKeyBlockEpoch_=0;
  std::vector<NormalContact> normalContacts_; // One existing combined peer envelope. No parallel hint map.
  std::optional<Clock::time_point> knownDeadline_; // Tightens only the executing Known command; never a grant.
  std::function<bool()> publicPermit_; // Existing owner one-shot source permit, PublicBound only.
  std::unique_ptr<find::nodes::NodeCache> nodeCache_;
  std::unique_ptr<auth::Authority> authority_;
  std::unique_ptr<policy::Admission> admission_;
  std::unique_ptr<database::Owner> database_;
  std::optional<find::nodes::NodeId> localId_;
  std::deque<ServiceReceipt> serviceReceipts_;
  std::optional<SharedChannel> shared_;
  struct SharedFrame {Peer sender;std::string bytes;};
  std::deque<SharedFrame> sharedFrames_; // At most64 physical datagrams.
  bool hasDatagrams()const{return shared_?!sharedFrames_.empty():socket_.hasPendingDatagrams();}
  QUdpSocket socket_;
  QChronoTimer deadline_;
  QChronoTimer routingDeadline_;
  QChronoTimer storageReplyDeadline_;
  QChronoTimer cacheDeadline_;
  QChronoTimer queryReplyDeadline_;
  QChronoTimer surveyDeadline_;
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
  std::function<void(const AddressReport&)> addressReportSink_;
  std::uint64_t addressReportEpoch_=0,addressReportSequence_=0;
  std::size_t addressReportPending_=0;
  std::array<VivaldiPeerObservation,OwnedDhtStatisticsSnapshot::MaximumPeers> peerPositions_{};
  std::size_t peerPositionCount_=0;std::uint64_t peerPositionSequence_=0,droppedPeerPositions_=0;bool peerPositionsLost_=false;
};
}

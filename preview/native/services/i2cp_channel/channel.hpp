// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "protocol.hpp"
#include "session.hpp"
#include "leaseset.hpp"
#include "messages.hpp"
#include "tracker.hpp"
#include "stream.hpp"
#include <QHostAddress>
#include <QObject>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace biglybtc::services::i2p::i2cp {
// Explicit external-router TCP registration, NOT a usable/tunnel I2P session.
// Every call/destruction belongs to this QObject's thread. Nothing auto-starts.
class InitialChannel final:public QObject {
  Q_OBJECT
public:
  enum class State {Idle,Connecting,WaitingDate,DateReceived,Closed,Failed,Creating,SessionCreated,Reconfiguring};
  Q_ENUM(State)
  enum class Failure {None,InvalidEndpoint,InvalidVersion,InvalidDeadline,UnsupportedAuthentication,
    ConnectFailed,WriteFailed,ReadFailed,Deadline,Framing,UnsupportedMessage,UnexpectedMessage,
    InvalidDate,RouterDisconnected,TransportClosed,Resources,SessionRejected,SessionDestroyed,InvalidSessionId,UnsupportedTransport,InvalidLeaseSet};
  Q_ENUM(Failure)
  struct Options {
    QHostAddress address; // Explicit numeric loopback only, no host resolution.
    quint16 port=0;       // Explicit caller-selected port; no discovery/default.
    std::string clientVersion; // Explicit nonempty ASCII, not a claimed core version.
    bool authenticationRequired=false; // true refuses before connecting.
    int deadlineMilliseconds=30000; // Absolute connect+date budget,1..30000.
    bool tlsRequired=false; // true refuses before TCP; no plaintext fallback.
  };
  explicit InitialChannel(QObject* parent=nullptr);
  ~InitialChannel() override;
  // Active begin refuses without changing the current attempt. Terminal/idle
  // attempts can explicitly restart. No retries or reconnects occur internally.
  bool begin(const Options&);
  void close();
  // No private key is retained. These synchronous signing calls require a
  // current date/registration, explicit date and no pending write/request.
  // Completion means only queued I/O; actual CREATED/UPDATED is separate.
  [[nodiscard]] ConfigResult<void> createSession(const ClientIdentity&,const SessionOptions& clientOptions,std::uint64_t creationMilliseconds);
  // Explicit LS2 opt-in consumes the capability only when the create request
  // is armed. It stays owned through renewals and is wiped on close/failure.
  // Existing borrowed registration never automatically signs LeaseSets.
  [[nodiscard]] ConfigResult<void> createLeaseSession(ClientIdentity&&,const SessionOptions& clientOptions,std::uint64_t creationMilliseconds);
  [[nodiscard]] ConfigResult<void> reconfigureLeaseSession(const SessionOptions& clientOptions,std::uint64_t creationMilliseconds);
  [[nodiscard]] ConfigResult<void> reconfigureSession(const ClientIdentity&,const SessionOptions& clientOptions,std::uint64_t creationMilliseconds);
  [[nodiscard]] std::optional<std::uint16_t> sessionId()const noexcept;
  [[nodiscard]] SessionOptions sessionOptions()const; // Last actual ack only.
  struct LeaseSubmission {
    std::uint64_t revision=0,publishedMilliseconds=0,expiresMilliseconds=0;
    std::size_t leases=0;bool unpublished=false;
  };
  // Historical signing+successful socket enqueue only. No router ack, OPEN,
  // tunnel health, delivery or right to submit application payload is implied.
  [[nodiscard]] std::optional<LeaseSubmission> leaseSetSubmission()const noexcept;
  enum class DatagramPhase {Pending,RouterAccepted,RouterStatus,LocalCancelled,LocalTimeout,LocalLeaseExpired};
  struct DatagramObservation {std::uint32_t nonce=0;DatagramPhase phase=DatagramPhase::Pending;std::optional<MessageStatus> routerStatus;};
  struct DatagramOptions {std::uint16_t fromPort=0,toPort=0;GzipMode gzip=GzipMode::Stored;std::optional<std::uint64_t> expirationMilliseconds;};
  // Explicit status-requesting datagrams only; initial reliability=none refuses.
  // One pending request. Nonces never repeat within this object's lifetime.
  // Enqueue/Accepted/probable/local success are not end-to-end delivery proof.
  [[nodiscard]] ConfigResult<std::uint32_t> submitDatagram2(View recipientDestination,View payload,const DatagramOptions&);
  [[nodiscard]] ConfigResult<std::uint32_t> submitClaimedDatagram3(View recipientDestination,View payload,const DatagramOptions&);
  // Local cancellation only: I2CP has no matching remote cancel message.
  bool cancelDatagram(std::uint32_t nonce);
  [[nodiscard]] bool datagramPending()const noexcept;
  [[nodiscard]] std::optional<DatagramObservation> datagramObservation()const noexcept;
  struct ReceivedDatagram {
    std::uint32_t messageId=0;std::uint16_t fromPort=0,toPort=0;
    // Datagram2 verifies signing-key possession/current recipient, not every
    // sender Destination byte or freshness. Datagram3 is an unsigned claim.
    std::variant<Datagram2,Datagram3> body;
  };
  enum class ReceiveDrop {Disabled,QueueFull,Malformed,Unsupported,InvalidDatagram,WrongSession,UnexpectedPayload,Duplicate,LeaseExpired,PendingTimeout,ControlBackpressure,Cancelled};
  struct ReceiverObservation {
    bool enabled=false,fast=false,pending=false,controlLost=false;
    std::size_t queued=0,decodedBytes=0,queuedControls=0;
    std::uint64_t dropped=0;std::optional<ReceiveDrop> lastDrop;
  };
  // Explicit opt-in after owned Create is armed is allowed; admission still
  // requires an actual current unexpired LS2 enqueue. No default activation.
  bool enableDatagramReceiver();
  void disableDatagramReceiver();
  [[nodiscard]] std::optional<ReceivedDatagram> takeReceivedDatagram();
  [[nodiscard]] ReceiverObservation receiverObservation()const noexcept;
  // Explicit hash-addressed UDP tracker only, no DNS/backend selection.
  // One operation; a raw reply never authenticates its purported sender.
  [[nodiscard]] ConfigResult<std::uint64_t> scrapeTracker(const TrackerRequest&);
  bool cancelTracker(std::uint64_t operation);
  [[nodiscard]] std::optional<TrackerObservation> trackerObservation()const;
  struct StreamOptions {std::uint16_t fromPort=0,toPort=0;int deadlineMilliseconds=120000;};
  // Explicit full resolved peer only; no DNS, default router or Host activation.
  // Requires retained owned signer/current LS2; one bounded outbound stream.
  [[nodiscard]] ConfigResult<std::uint64_t> connectStream(View destination,const StreamOptions&);
  [[nodiscard]] ConfigResult<void> writeStream(std::uint64_t operation,View);
  [[nodiscard]] ConfigResult<Bytes> takeStreamBytes(std::uint64_t operation,std::size_t maximum);
  bool finishStreamWrite(std::uint64_t operation);
  bool cancelStream(std::uint64_t operation); // Local withdrawal, no remote ACK claim.
  [[nodiscard]] std::optional<streaming::Observation> streamObservation()const noexcept;
  [[nodiscard]] State state()const noexcept;
  [[nodiscard]] Failure failure()const noexcept;
  [[nodiscard]] bool socketActive()const noexcept;
  [[nodiscard]] bool deadlineActive()const noexcept;
  // Passive observations only; never clock, session, identity or payload rights.
  [[nodiscard]] std::optional<SetDate> routerDate()const;
  [[nodiscard]] std::optional<Disconnect> routerDisconnect()const;
signals:
  // Coalesced queued CURRENT-state notifications. Intermediate states may be
  // omitted. A callback may stop/restart/delete this object synchronously.
  void stateChanged(State);
  void dateReceived();
  void sessionCreated();
  void sessionUpdated();
  void leaseSetSubmitted();
  void datagramChanged(); // Coalesced latest observation, not a result queue.
  void receiverChanged(); // Queued current bounded queue/drop observation.
  void trackerChanged(); // Queued latest same-generation operation observation.
  void streamChanged(); // Queued latest current operation; deletion/restart safe.
  void failed(Failure);
  void closed();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::services::i2p::i2cp

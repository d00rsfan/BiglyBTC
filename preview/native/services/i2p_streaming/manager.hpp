// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "stream.hpp"
#include <vector>

namespace biglybtc::services::i2p::streaming {
inline constexpr std::size_t MaximumStreams=4,MaximumPendingSyns=4,MaximumPendingSynBytes=262144;
enum class IdleAction {Send,Disconnect};
struct ManagerOptions {unsigned inactivityMilliseconds=90000;IdleAction idleAction=IdleAction::Send;};
struct Ports {std::uint16_t local=0,remote=0;};
struct ConnectOptions {Ports ports;unsigned handshakeMilliseconds=120000,maximumWindow=MaximumWindow;};
struct ListenOptions {Ports ports;std::vector<Destination> peers;unsigned maximumWindow=MaximumWindow;};
enum class WaitFor {Readable,Writable};
enum class WaitState {Pending,Ready,TimedOut,Cancelled};
struct WaitObservation {WaitFor kind=WaitFor::Readable;WaitState state=WaitState::Pending;std::size_t minimumBytes=1;};
struct ManagedObservation {Observation stream;Ports ports;bool inbound=false;unsigned idleJitterMilliseconds=0;std::optional<WaitObservation> wait;};
struct ManagerObservation {
  std::size_t streams=0,pendingSyns=0,pendingSynBytes=0,queuedTx=0,queuedRx=0;
  bool listening=false;std::uint64_t rejectedPackets=0,expiredSyns=0;
};
struct Outbound {std::uint64_t operation=0;Packet packet;Ports ports;};
// Pure finite manager. Identity/lease/socket authority stays with its owner.
// IDs and operation numbers come from the owner's persistent lifetime ledger.
class Manager {
public:
  Manager(Destination local,ManagerOptions,Connection::Entropy={});
  bool valid()const noexcept;
  bool receives()const noexcept;
  bool active()const noexcept;
  Result<void> connect(std::uint64_t operation,std::uint32_t id,Destination,const ConnectOptions&,std::uint64_t now,std::optional<std::uint64_t> absoluteHandshakeEnd={});
  Result<void> listen(const ListenOptions&,std::uint64_t now);
  void stopListening()noexcept;
  Result<void> accept(std::uint64_t operation,std::uint32_t id,std::uint64_t now);
  // Optional post-verification clock/current-source fence; neither grants IO.
  void receive(const Mux&,std::uint64_t now,std::function<std::uint64_t()> afterVerification={},std::function<bool()> current={});
  std::optional<Outbound> next(std::uint64_t now);
  void dispatched(const Outbound&,std::uint64_t now);
  const Destination* peer(std::uint64_t operation)const noexcept;
  std::optional<unsigned> transmitTimeout(const Outbound&)const noexcept;
  Result<void> write(std::uint64_t operation,View,std::uint64_t now);
  Result<Bytes> read(std::uint64_t operation,std::size_t maximum,std::uint64_t now);
  bool finishWrite(std::uint64_t operation,std::uint64_t now);
  bool cancel(std::uint64_t operation)noexcept;
  bool release(std::uint64_t operation)noexcept;
  bool wait(std::uint64_t operation,WaitFor,std::size_t minimumBytes,unsigned timeoutMilliseconds,std::uint64_t now);
  void tick(std::uint64_t now);
  void terminate(Phase)noexcept;
  // Finite authority/application deadlines remain live while output is blocked.
  std::optional<std::uint64_t> hardWakeup()const noexcept;
  std::optional<std::uint64_t> wakeup()const noexcept;
  std::optional<ManagedObservation> observation(std::uint64_t operation)const noexcept;
  ManagerObservation observation()const noexcept;
private:
  struct Wait {WaitObservation observed;std::uint64_t end=0;};
  struct Entry {
    Connection connection;Ports ports;bool inbound=false,idlePending=false;
    std::uint32_t id=0;unsigned idleJitter=0;std::uint64_t handshakeEnd=0,lastActivity=0;
    std::optional<Wait> wait;
  };
  struct Pending {Packet syn;Ports ports;std::uint64_t end=0;std::size_t bytes=0;};
  Entry* find(std::uint64_t)noexcept;
  const Entry* find(std::uint64_t)const noexcept;
  void settleWait(Entry&,std::uint64_t now)noexcept;
  bool available(std::uint64_t operation,std::uint32_t id)const noexcept;
  Destination local_;ManagerOptions options_;Connection::Entropy entropy_;
  std::array<std::optional<Entry>,MaximumStreams> streams_{};
  std::optional<ListenOptions> listener_;
  std::deque<Pending> pending_;
  std::size_t pendingBytes_=0,cursor_=0;
  std::uint64_t rejected_=0,expired_=0;
};
} // namespace biglybtc::services::i2p::streaming

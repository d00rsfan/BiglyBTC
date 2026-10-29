// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "identity.hpp"
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <utility>

namespace biglybtc::services::i2p::streaming {
inline constexpr Byte Protocol=6;
inline constexpr std::uint16_t Syn=1,Close=2,Reset=4,Signature=8,SignatureRequested=16,From=32,Delay=64,MaxSize=128,Interactive=256,NoAck=1024;
inline constexpr std::size_t MaximumPayload=32768,MaximumBuffered=65536,MaximumReorder=64;
inline constexpr std::uint16_t DefaultSegment=1730;
inline constexpr unsigned MaximumWindow=8;
struct Packet {
  std::uint32_t send=0,receive=0,sequence=0,ackThrough=0;
  std::vector<std::uint32_t> nacks;
  Byte resendDelay=0;
  std::uint16_t flags=0,delay=0,maxSize=0;
  std::optional<Destination> from;
  Bytes payload;
  bool signatureVerified=false;
};
// Exact Packet.java online signature preimage: signature bytes are zeroed.
// Offline signatures, ECHO and unknown flags/options are explicitly unsupported.
Result<Bytes> encode(const Packet&,const ClientIdentity& signer);
Result<Packet> decode(View,const Destination& expectedPeer);
struct Mux {std::uint16_t fromPort=0,toPort=0;Bytes packet;};
// Separate protocol6 entry points; existing datagram/RAW18 decoders stay strict.
Result<Bytes> encodeMux(const Mux&);
Result<Mux> decodeMux(View);

enum class Phase {Connecting,Established,ReadEof,Closed,Cancelled,Timeout,LeaseExpired,RemoteReset,Failed};
struct Observation {
  std::uint64_t operation=0;Phase phase=Phase::Connecting;
  std::size_t queuedTx=0,queuedRx=0;
  std::uint64_t rejectedPackets=0,transmittedPackets=0,retransmissions=0,acknowledgedBytes=0;
  unsigned sendWindow=3,maximumWindow=MaximumWindow,slowStartThreshold=64,rtoMilliseconds=9000;
  std::size_t inFlightPackets=0,maximumInFlightPackets=0,inFlightBytes=0;
  bool peerChoked=false;
};
// Source SimpleBandwidthEstimator: packet/ms Westwood+ with explicit time.
// No clock, thread or callback; float evaluation matches the Java filters.
class BandwidthEstimator {
public:
  explicit BandwidthEstimator(std::uint64_t now=0):at_(now){}
  void sample(unsigned packets,std::uint64_t now,unsigned rtt)noexcept;
  float estimate(std::uint64_t now,unsigned rtt)noexcept;
private:
  void decay()noexcept;
  float update(std::uint64_t now,unsigned rtt)noexcept;
  std::uint64_t at_=0;
  std::int64_t acked_=-1;
  float filtered_=0,stage_=0;
};
// Pure bounded single-stream sender over original wire/window semantics.
// Caller supplies monotonic milliseconds and actual dispatch acknowledgements.
// Entropy is an inert bounded integer draw for original additive growth; the
// actor supplies actual entropy. Without it, that probabilistic growth pauses.
// It owns no private keys, socket, clock or filesystem authority.
class Connection {
public:
  using Entropy=std::function<unsigned(unsigned)>;
  Connection(std::uint64_t operation,std::uint32_t localId,Destination local,Destination peer,
      unsigned maximumWindow=MaximumWindow,std::uint64_t startedMilliseconds=0,Entropy={});
  const Destination& peer()const noexcept{return peer_;}
  bool active()const noexcept;
  Observation observation()const noexcept;
  Result<void> write(View);
  void finishWrite()noexcept{finish_=true;}
  Bytes read(std::size_t maximum);
  void terminate(Phase)noexcept;
  void rejectPacket()noexcept{reject();}
  void receive(Packet,std::uint64_t now);
  std::optional<Packet> next(std::uint64_t now);
  void dispatched(const Packet&,std::uint64_t now);
  std::optional<unsigned> transmitTimeout(const Packet&)const noexcept;
  std::optional<std::uint64_t> wakeup()const noexcept;
private:
  struct Pending {Packet packet;unsigned sends=0,nacks=0;std::uint64_t sent=0,created=0;bool retry=false,fast=false,fastUsed=false;};
  Packet base()const;
  void acknowledgements(Packet&)const;
  void reject()noexcept;
  void sample(std::uint64_t);
  void adjustWindow(bool isNew,std::uint32_t sequence,unsigned acked);
  void congestion()noexcept;
  void recoveryThreshold(std::uint64_t now)noexcept;
  void retryTimeout(std::uint64_t now);
  Pending* oldest(bool unsent=false)noexcept;
  std::size_t flight()const noexcept;
  std::size_t activeResends()const noexcept;
  bool sendCredit()const noexcept;
  Destination local_,peer_;
  Observation observed_;
  std::uint32_t localId_=0,remoteId_=0,nextSequence_=0;
  std::int64_t contiguous_=-1;
  std::uint16_t segment_=DefaultSegment;
  bool finish_=false,localFinSent_=false,localFinAcked_=false,remoteFin_=false,ackWanted_=false,choked_=false,choking_=false;
  std::deque<Byte> tx_,rx_;
  std::map<std::uint32_t,Packet> reorder_;
  std::size_t reorderBytes_=0;
  std::array<std::optional<Pending>,MaximumWindow> sent_{};
  std::optional<std::uint64_t> retryAt_;
  std::int64_t highestSent_=-1,highestAcked_=-1,lastCongestionHighest_=-1;
  std::int64_t congestionWindowEnd_=2;
  unsigned maximumWindow_=MaximumWindow,window_=3,ssthresh_=64;
  bool ackSinceCongestion_=true;
  bool timeoutArmPending_=false;
  Entropy entropy_;
  BandwidthEstimator bandwidth_;
  unsigned samples_=0,rtt_=0,deviation_=0,rto_=9000,minimumRtt_=0x7fffffffU;
};
} // namespace biglybtc::services::i2p::streaming

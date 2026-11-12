// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace biglybtc::engine {
struct LivePeer {
 std::string hash,address,localAddress,client,peerId;
 std::uint64_t admission=0;
 std::uint16_t port=0,localPort=0;
 std::int64_t received=0,sent=0;
 int downRate=0,upRate=0,protocolDownRate=0,protocolUpRate=0;
 int state=0,progressPpm=-1,pieces=0,totalPieces=0,rtt=0,downloadRequests=0,uploadRequests=0,queuedBytes=0;
 unsigned sources=0;
 bool incoming=false,seed=false,utp=false,webSeed=false,encrypted=false,interesting=false,choked=false,remoteInterested=false,remoteChoked=false,snubbed=false,optimistic=false;
 std::array<unsigned char,128> availability{}; //0 absent,1 mixed,2 present; actual SDK bitfield bins.
};
struct PeerSnapshot {
 std::vector<LivePeer> peers;
 std::size_t admitted=0,downloading=0,seeding=0,queued=0,checking=0,paused=0,failed=0,unavailable=0;
 bool truncated=false;
};
}

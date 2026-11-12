// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
namespace biglybtc::engine {
// Owned passive samples. No SDK handles, borrowed block arrays or payload IO.
enum class InspectionSection { General, Files, Trackers, Peers, Pieces, Map, Blocks, Activity, Information, Privacy, Console, Transfers, Tags, Options, Swarm };
struct TrackerInspection {
 std::string url,local,protocol,message;int tier=0,state=0,seeds=-1,leechers=-1,completed=-1,failures=0,nextSeconds=-1,peers=-1;
};
struct PieceInspection {int index=0,blocks=0,finished=0,writing=0,requested=0,availability=-1,priority=-1;};
struct BlockInspection {int piece=0,index=0,state=0,received=0,size=0,requests=0;std::string peer;};
struct TorrentInspectionEvent {std::uint64_t sequence=0;std::int64_t wallMillis=0;std::string message;};
struct TorrentInspection {
 std::string hash,error,name,comment,creator,v1,v2,resumeDecision;
 std::uint64_t admission=0;bool retained=false,checked=false,privateTorrent=false,paused=false,forceStart=false,dht=false,pex=false,lsd=false,trackersAllowed=false,truncated=false,piecesKnown=false;
 int pieceLength=0,totalPieces=0,piecesFirst=0,connections=0,seeds=0,connectionLimit=0,uploadSlots=0,downloadLimit=0,uploadLimit=0;
 std::int64_t bytes=0,completed=0,received=0,sent=0,downloadRate=0,uploadRate=0,discarded=0,hashFailed=0,activeSeconds=0,seedingSeconds=0,created=0;
 std::optional<std::int64_t> cumulativeReceived,cumulativeSent;
 float availability=-1;
 std::vector<TrackerInspection> trackers;
 std::vector<TorrentInspectionEvent> events;
 std::vector<PieceInspection> activePieces;
 std::vector<BlockInspection> blocks;
 std::vector<std::uint8_t> pieces; // bounded range: 0 missing, 1 verified; unknown if !piecesKnown
 std::vector<int> pieceAvailability;
 std::vector<std::int64_t> fileCompleted; // original Java geometry, not SDK synthetic indices
};
}

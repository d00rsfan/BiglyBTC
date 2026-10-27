#pragma once
#include "biglybtc/torrent_layout.hpp"
#include "biglybtc/file_policy.hpp"
#include <algorithm>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace biglybtc::core {
// Saved native intent only; neither value is a hash-verification certificate.
// Historical callers and absent saved goal retain FullTorrent.
enum class ExistingVerificationGoal : std::uint8_t { FullTorrent=0, SelectedFiles=1 };
struct ExistingPieceGoalError { std::string_view message; };
// Call only after raw/full identity and every original/engine slot geometry are
// authenticated. Piece0/1 describes NEED, not observed or verified presence.
inline std::expected<std::vector<std::uint8_t>,ExistingPieceGoalError> existingPieceGoal(
    const compat::OriginalLayout& layout,std::span<const compat::FilePolicy> files,
    ExistingVerificationGoal goal,std::size_t enginePieces) {
  const auto bad=[](std::string_view message){return std::unexpected(ExistingPieceGoalError{message});};
  if((goal!=ExistingVerificationGoal::FullTorrent&&goal!=ExistingVerificationGoal::SelectedFiles)
      ||layout.pieceLength<=0||layout.pieceLength>compat::OriginalMaximumPieceLength||layout.bytes<=0||layout.files.empty()||layout.files.size()>100000
      ||files.size()!=layout.files.size()||!enginePieces||enginePieces>1000000){return bad("Invalid bounded existing verification domain");}
  const auto pieces=static_cast<std::uint64_t>(layout.bytes)/static_cast<std::uint64_t>(layout.pieceLength)
      +(layout.bytes%layout.pieceLength!=0);
  if(pieces!=enginePieces){return bad("Existing verification piece count differs from authenticated layout");}
  std::vector<std::uint8_t> required(enginePieces,goal==ExistingVerificationGoal::FullTorrent?1:0);
  std::int64_t next=0;bool selectedBytes=false;
  for(std::size_t n=0;n<layout.files.size();++n){const auto& file=layout.files[n];const auto& policy=files[n];
    if(file.offset!=next||file.bytes<0||file.bytes>layout.bytes-next
        ||(file.padding&&(!policy.skipped||policy.priority!=0))){return bad("Existing verification file geometry/policy differs");}
    next+=file.bytes;if(goal!=ExistingVerificationGoal::SelectedFiles||file.padding||policy.skipped||!file.bytes){continue;}
    selectedBytes=true;const auto first=file.offset/layout.pieceLength,last=(next-1)/layout.pieceLength;
    for(auto piece=first;piece<=last;++piece){required[static_cast<std::size_t>(piece)]=1;}
  }
  if(next!=layout.bytes||(goal==ExistingVerificationGoal::SelectedFiles&&!selectedBytes)){return bad("Selected verification requires a nonempty real-file goal");}
  return required;
}
// Every observed bit must be from one accepted full status/counter epoch. A
// missing/partial/malformed bitfield is never completed with inferred zeros.
inline bool satisfiesExistingPieceGoal(std::span<const std::uint8_t> required,
    std::span<const std::uint8_t> observed) noexcept {
  if(required.empty()||required.size()!=observed.size()){return false;}
  bool needed=false;
  for(std::size_t n=0;n<required.size();++n){if(required[n]>1||observed[n]>1||(required[n]&&!observed[n])){return false;}needed|=required[n]!=0;}
  return needed;
}
}

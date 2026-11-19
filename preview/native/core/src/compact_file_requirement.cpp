// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/compact_file_requirement.hpp>
#include <limits>
#include <algorithm>
#include <iterator>
namespace biglybtc::compat {
std::expected<OriginalFilePieceSpan,CompactFileRequirementError> originalFilePieceSpan(
    std::int64_t offset,std::int64_t bytes,std::int64_t pieceBytes,bool skipped){
  if(offset<0||bytes<0||pieceBytes<=0||bytes>std::numeric_limits<std::int64_t>::max()-offset){return std::unexpected(CompactFileRequirementError::Geometry);}
  const auto first=offset/pieceBytes,last=(offset+bytes-1)/pieceBytes;
  // Signed division intentionally matches Java,including a zero-length origin.
  if(last<0||first>std::numeric_limits<std::int32_t>::max()||last>std::numeric_limits<std::int32_t>::max()){
    return std::unexpected(CompactFileRequirementError::Geometry);
  }
  return OriginalFilePieceSpan{static_cast<std::int32_t>(first),static_cast<std::int32_t>(last),skipped};
}
std::expected<bool,CompactFileRequirementError> originalCompactFileMustExist(
    std::span<const OriginalFilePieceSpan> files,std::size_t target){
  if(target>=files.size()){return std::unexpected(CompactFileRequirementError::Target);}
  for(std::size_t i=0;i<files.size();++i){const auto& file=files[i];
    if(file.first<0||file.last<0||std::int64_t(file.last)+1<file.first){return std::unexpected(CompactFileRequirementError::Geometry);}
    if(i&&(file.first<files[i-1].first||file.last<files[i-1].last)){return std::unexpected(CompactFileRequirementError::Order);}
  }
  const auto& selected=files[target];
  for(const auto& file:files){
    if(file.last<selected.first){continue;}
    if(file.first>selected.last){break;}
    if(!file.skipped&&((file.first<=selected.first&&selected.first<=file.last)
        ||(file.first<=selected.last&&selected.last<=file.last))){return true;}
  }
  return false;
}

std::expected<std::vector<std::uint8_t>,CompactFileRequirementError> originalCompactFileRequirements(
    std::span<const OriginalFilePieceSpan> files){
  std::vector<OriginalFilePieceSpan> needed;
  for(std::size_t i=0;i<files.size();++i){const auto& file=files[i];
    if(file.first<0||file.last<0||std::int64_t(file.last)+1<file.first){return std::unexpected(CompactFileRequirementError::Geometry);}
    if(i&&(file.first<files[i-1].first||file.last<files[i-1].last)){return std::unexpected(CompactFileRequirementError::Order);}
    if(file.skipped||file.first>file.last){continue;}
    if(!needed.empty()&&file.first<=needed.back().last){needed.back().last=std::max(needed.back().last,file.last);}
    else{needed.push_back(file);}
  }
  const auto covered=[&](std::int32_t piece){
    const auto after=std::upper_bound(needed.begin(),needed.end(),piece,[](auto value,const auto& file){return value<file.first;});
    return after!=needed.begin()&&std::prev(after)->last>=piece;
  };
  std::vector<std::uint8_t> result;result.reserve(files.size());
  for(const auto& file:files){
    // In a complete ordered census,a reversed aligned-zero range cannot have
    // an overlapping file:earlier last<=its last,later first>=its first.
    result.push_back(file.first<=file.last&&(covered(file.first)||covered(file.last)));
  }
  return result;
}
}

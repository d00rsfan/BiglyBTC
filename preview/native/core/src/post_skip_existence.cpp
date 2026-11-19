// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/post_skip_existence.hpp"
#include <algorithm>
#include <new>

namespace biglybtc::compat {
std::expected<PostSkipExistencePlan,PostSkipExistenceError> originalPostSkipExistence(
    std::span<const OriginalFilePieceSpan> files,std::span<const std::int32_t> kinds,
    std::span<const std::uint8_t> changed,std::span<const std::uint8_t> exists,
    bool skipped,bool constructed,std::optional<std::span<const std::uint8_t>> resume){
  using Error=PostSkipExistenceError;
  const auto count=files.size();
  if(count>100000||(resume&&resume->size()>1000000)){return std::unexpected(Error::Limit);}
  if(kinds.size()!=count||changed.size()!=count||exists.size()!=count){return std::unexpected(Error::Shape);}
  for(std::size_t i=0;i<count;++i){if(kinds[i]<1||kinds[i]>4||changed[i]>1||exists[i]>1){return std::unexpected(Error::Shape);}}
  try{
    PostSkipExistencePlan result{std::vector<std::uint8_t>(count),std::vector<std::uint8_t>(count),std::vector<std::uint8_t>(count),std::vector<std::uint8_t>(count)};
    if(!constructed){return result;}
    const auto boundary=originalCompactFileRequirements(files);
    if(!boundary){return std::unexpected(boundary.error()==CompactFileRequirementError::Order?Error::Order:Error::Geometry);}
    std::vector<std::int32_t> first,last;first.reserve(count);last.reserve(count);
    for(const auto& file:files){first.push_back(file.first);last.push_back(file.last);}
    // Source expands only the changed targets' immediate contiguous matching
    // boundary runs. Expanding the resulting neighbours again would differ.
    // Range union avoids quadratic work for a long shared/zero-file census.
    std::vector<std::int32_t> difference(count+1);
    const auto mark=[&](std::size_t begin,std::size_t end){++difference[begin];--difference[end];};
    for(std::size_t i=0;i<count;++i){if(!changed[i]){continue;}mark(i,i+1);
      if(i&&last[i-1]==first[i]){const auto begin=std::lower_bound(last.begin(),last.begin()+static_cast<std::ptrdiff_t>(i),first[i]);mark(static_cast<std::size_t>(begin-last.begin()),i);}
      if(i+1<count&&first[i+1]==last[i]){const auto end=std::upper_bound(first.begin()+static_cast<std::ptrdiff_t>(i+1),first.end(),last[i]);mark(i+1,static_cast<std::size_t>(end-first.begin()));}
    }
    std::vector<std::uint32_t> nonzero;
    if(resume){nonzero.resize(resume->size()+1);for(std::size_t i=0;i<resume->size();++i){nonzero[i+1]=nonzero[i]+((*resume)[i]!=0);}}
    std::int32_t active=0;
    for(std::size_t i=0;i<count;++i){active+=difference[i];result.interesting[i]=active>0;if(!result.interesting[i]){continue;}
      bool required=(*boundary)[i];const bool compact=kinds[i]==2||kinds[i]==4;
      if(!compact&&resume&&first[i]<=last[i]){
        if(static_cast<std::size_t>(last[i])>=resume->size()){return std::unexpected(Error::Resume);}
        required=required||nonzero[static_cast<std::size_t>(last[i])+1]!=nonzero[static_cast<std::size_t>(first[i])];
      }
      if(required&&!skipped){result.existenceProbes[i]=1;result.allocationRequests[i]=!exists[i];}
      else if(!required&&skipped&&compact&&exists[i]){result.compactRemovals[i]=1;}
    }
    return result;
  }catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
}

#include "biglybtc/file_storage_selection.hpp"
#include <algorithm>

namespace biglybtc::compat {
std::expected<std::vector<OriginalFileStorageChange>,PolicyError> originalSkipStoragePlan(
    std::span<const std::int32_t> requested,std::span<const std::uint8_t> changed,bool skipped){
  if(requested.size()!=changed.size()||requested.size()>100000
      ||std::ranges::any_of(requested,[](auto kind){return kind<1||kind>4;})
      ||std::ranges::any_of(changed,[](auto value){return value>1;})){
    return std::unexpected(PolicyError{"Invalid or incomplete Original skip/storage selection census"});
  }
  std::vector<OriginalFileStorageChange> result;
  if(skipped){return result;}
  for(const auto compact:{2,4}){
    OriginalFileStorageChange step{compact==2?1:3,std::vector<std::uint8_t>(requested.size())};
    bool any=false;
    for(std::size_t i=0;i<requested.size();++i){step.files[i]=changed[i]&&requested[i]==compact;any=any||step.files[i];}
    if(any){result.push_back(std::move(step));}
  }
  return result;
}
}

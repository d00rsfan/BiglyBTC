#include "biglybtc/queue_positions.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <deque>
#include <limits>
#include <new>
#include <set>
#include <unordered_map>
namespace biglybtc::core {
std::expected<std::vector<std::uint32_t>,QueuePositionsError> normalizeQueuePositions(
    std::span<const QueuePositionInput> inputs,std::span<const QueuePositionTransition> transitions) {
  using E=QueuePositionsError;
  try {
    if(inputs.size()>100000||transitions.size()>inputs.size()){return std::unexpected(E::Limit);}
    std::unordered_map<std::uint64_t,std::size_t> byToken;
    for(std::size_t i=0;i<inputs.size();++i){
      if(!inputs[i].token||!inputs[i].persistent||!byToken.emplace(inputs[i].token,i).second){return std::unexpected(E::Identity);}
    }
    std::vector<bool> changed(inputs.size());
    for(const auto& event:transitions){
      const auto found=byToken.find(event.token);
      if(found==byToken.end()||changed[found->second]||inputs[found->second].complete!=event.complete){return std::unexpected(E::Group);}
      changed[found->second]=true;
    }
    std::vector<std::int64_t> positions;positions.reserve(inputs.size());
    std::array<std::size_t,2> loaded{};
    for(std::size_t i=0;i<inputs.size();++i){
      const auto& in=inputs[i];const bool oldGroup=changed[i]?!in.complete:in.complete;
      auto p=in.savedPosition?std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*in.savedPosition)):-1;
      if(p==-1){p=static_cast<std::int32_t>(loaded[oldGroup?1:0]+1);}
      positions.push_back(changed[i]?static_cast<std::int64_t>(inputs.size())+1:p);++loaded[oldGroup?1:0];
    }
    // Original comparator subtracts signed32 positions. Refuse its non-ordering
    // overflow domain rather than feeding a non-strict comparator to C++ sort.
    if(!positions.empty()){
      const auto range=std::ranges::minmax(positions);
      if(range.max-range.min>std::numeric_limits<std::int32_t>::max()){return std::unexpected(E::Position);}
    }
    std::array<std::deque<std::size_t>,2> groups;
    for(unsigned group=0;group<2;++group){
      std::vector<std::size_t> order;std::set<std::int64_t> distinct;
      for(std::size_t i=0;i<inputs.size();++i){
        if(!changed[i]&&inputs[i].complete==(group!=0)){
          if(!distinct.insert(positions[i]).second){return std::unexpected(E::Ambiguous);}order.push_back(i);
        }
      }
      std::ranges::sort(order,[&](std::size_t a,std::size_t b){return positions[a]<positions[b];});
      groups[group].assign(order.begin(),order.end());
    }
    // Source N+1 then moveTop/moveEnd and fixup preserves all other relative
    // order. Apply the actually accepted transition order, never guessed time.
    for(const auto& event:transitions){
      auto& group=groups[event.complete?1:0];const auto index=byToken.at(event.token);
      if(event.top){group.push_front(index);}else{group.push_back(index);}
    }
    std::vector<std::uint32_t> output(inputs.size());
    for(const auto& group:groups){std::uint32_t position=0;for(const auto index:group){output[index]=++position;}}
    return output;
  }catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
} // namespace biglybtc::core

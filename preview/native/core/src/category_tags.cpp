// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/category_tags.hpp>
#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
#include <set>
namespace biglybtc::core::tags::category {
namespace {
std::uint32_t bucketHash(std::u16string_view name) noexcept {
  std::uint32_t hash = 0;
  for (const char16_t unit : name) { hash = hash * 31 + unit; }
  return hash ^ (hash >> 16);
}

std::optional<std::vector<std::size_t>> javaStartupOrder(std::span<const Entry> entries,
    std::size_t& capacity) {
  std::vector<std::uint32_t> hashes;
  hashes.reserve(entries.size());
  for (const auto& entry : entries) { hashes.push_back(bucketHash(entry.rawName)); }
  std::vector<std::size_t> counts(16, 0);
  const auto bucket = [&](std::size_t idx) { return hashes[idx] & (counts.size() - 1); };
  const auto resize = [&](std::size_t inserted) {
    counts.assign(counts.size() * 2, 0);
    for (std::size_t i = 0; i < inserted; ++i) { ++counts[bucket(i)]; }
  };
  for (std::size_t i = 0; i < hashes.size(); ++i) {
    if (++counts[bucket(i)] > 8) {
      // OpenJDK grows a short table before converting an overfull chain.
      // Tree buckets require their own ordering model.
      if (counts.size() >= 64) { return {}; }
      resize(i + 1);
    }
    if (i + 1 > counts.size() * 3 / 4) { resize(i + 1); }
  }
  std::vector<std::size_t> order(entries.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  // HashMap's linked-bin split preserves the insertion order in each bucket.
  std::ranges::stable_sort(order, {}, bucket);
  capacity = counts.size();
  return order;
}
std::optional<std::vector<std::size_t>> javaChangedOrder(std::span<const Entry> before,
    std::span<const Entry> after, std::span<const std::size_t> oldOrder,
    DefinitionChange change, std::size_t& capacity) {
  using Operation = DefinitionChange::Operation;
  const bool added = change.operation == Operation::Added;
  if ((added ? before.size() + 1 != after.size() : after.size() + 1 != before.size())) { return {}; }

  std::map<std::int32_t, std::size_t> positions;
  for (std::size_t index = 0; index < after.size(); ++index) {
    positions.emplace(after[index].id, index);
  }
  std::vector<std::size_t> order;
  order.reserve(after.size());
  bool removed = false;
  for (const auto index : oldOrder) {
    const auto& entry = before[index];
    const auto found = positions.find(entry.id);
    if (found != positions.end()) {
      order.push_back(found->second);
      positions.erase(found);
    } else if (!added && !removed && entry.kind == Kind::User && entry.rawName == change.name) {
      removed = true;
    } else {
      return {};
    }
  }
  if (!added) {
    return removed && positions.empty() ? std::optional{std::move(order)} : std::nullopt;
  }
  if (positions.size() != 1) { return {}; }
  const auto index = positions.begin()->second;
  if (after[index].kind != Kind::User || after[index].rawName != change.name) { return {}; }

  const auto bucket = [&](std::size_t i) { return bucketHash(after[i].rawName) & (capacity - 1); };
  const auto bin = bucket(index);
  const auto colliding = std::ranges::count_if(order, [&](std::size_t i) { return bucket(i) == bin; });
  if (colliding >= 8) {
    if (capacity >= 64) { return {}; }
    capacity *= 2;
  }
  if (after.size() > capacity * 3 / 4) { capacity *= 2; }
  order.push_back(index);
  std::ranges::stable_sort(order, {}, bucket);
  return order;
}
} // namespace

bool member(Kind kind,bool download,bool userMember,std::optional<Kind> assigned)noexcept{
 if(!download){return false;}
 if(kind==Kind::User){return userMember;}
 if(kind==Kind::All){return true;}
 return !assigned||*assigned==Kind::Uncategorized;
}
Roster::Roster():entries_{{1,u"Categories.all",Kind::All},{2,u"Categories.uncategorized",Kind::Uncategorized}}{}
std::expected<void,runtime::Error> Roster::observe(std::span<const runtime::Text> names,
    std::optional<DefinitionChange> change){
 try{
  if(names.size()>4096){return std::unexpected(runtime::Error::Bounds);}
  std::map<runtime::Text,std::int32_t,std::less<>> old;
  for(const auto& entry:entries_){if(entry.kind==Kind::User){old.emplace(entry.rawName,entry.id);}}
  std::vector<Entry> next{entries_[0],entries_[1]};std::set<runtime::Text,std::less<>> unique;
  std::size_t units=0;auto allocator=next_;
  for(const auto& name:names){
   if(name.empty()||name==u"Categories.all"||name==u"Categories.uncategorized"||!unique.insert(name).second){return std::unexpected(runtime::Error::Invalid);}
   if(name.size()>4096||name.size()>262144-units){return std::unexpected(runtime::Error::Bounds);}units+=name.size();
   std::int32_t id;const auto found=old.find(name);
   if(found!=old.end()){id=found->second;}
   else{if(allocator>std::numeric_limits<std::int32_t>::max()){return std::unexpected(runtime::Error::Bounds);}id=static_cast<std::int32_t>(allocator++);}
   next.push_back({id,name,Kind::User});
  }
  if (!observed_) {
   order_ = javaStartupOrder(next, capacity_);
  } else if (order_ && change) {
   order_ = javaChangedOrder(entries_, next, *order_, *change, capacity_);
  } else if (!std::ranges::equal(entries_, next, {}, &Entry::rawName, &Entry::rawName)) {
   order_.reset();
  }
  entries_=std::move(next);next_=allocator;observed_=true;return {};
 }catch(...){return std::unexpected(runtime::Error::Memory);}
}
}

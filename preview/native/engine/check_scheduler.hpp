// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include <algorithm>
#include <cstdint>
#include <expected>
#include <set>
#include <span>
#include <vector>

namespace biglybtc::engine::detail {
// Original cb8c6cb9bb97916cbc7e80338a017200ffc3e71f:
// DiskManagerRecheckScheduler and DiskManagerOperationScheduler.
struct CheckPolicy {
  std::uint32_t maximumActive=1; // zero is unlimited
  bool onePerFilesystem=false,concurrentReads=false,smallestFirst=true;
};
inline std::expected<CheckPolicy,const char*> readCheckPolicy(const core::Node& root) {
  if(root.type!=core::Node::Type::Dictionary){return std::unexpected("Invalid checking configuration");}
  CheckPolicy result;
  const auto integer=[&](std::string_view key,std::int32_t fallback)->std::expected<std::int32_t,const char*>{
    const core::Node* found=nullptr;
    for(std::size_t i=0;i+1<root.children.size();i+=2){
      if(root.children[i].bytes==key){if(found){return std::unexpected("Duplicate checking configuration");}found=&root.children[i+1];}
    }
    if(!found){return fallback;}
    if(found->type!=core::Node::Type::Integer){return std::unexpected("Invalid checking configuration value");}
    const auto low=static_cast<std::uint32_t>(found->integer);
    return low<=INT32_MAX?static_cast<std::int32_t>(low):-1-static_cast<std::int32_t>(UINT32_MAX-low);
  };
  auto limit=integer("diskmanager.hashchecking.maxactive",1);
  auto filesystem=integer("diskmanager.one.op.per.fs",0);
  auto reads=integer("diskmanager.one.op.per.fs.conc.read",0);
  auto smallest=integer("diskmanager.hashchecking.smallestfirst",1);
  if(!limit||!filesystem||!reads||!smallest){return std::unexpected("Invalid checking configuration");}
  result.maximumActive=*limit<=0?0:static_cast<std::uint32_t>(*limit);
  result.onePerFilesystem=*filesystem!=0;result.concurrentReads=*reads!=0;result.smallestFirst=*smallest!=0;
  return result;
}
struct CheckRequest {
  std::uint64_t admission=0;
  std::vector<std::uint64_t> filesystems;
  std::int64_t remainingBytes=0;
  bool retainedPermit=false,eligible=true;
};
// A granted check keeps its slot through cancellation/pause drainage. Waiting
// checks are ordered by remaining bytes (or admission order), never by GUI row.
inline std::set<std::uint64_t> scheduleChecks(const CheckPolicy& policy,std::span<const CheckRequest> requests) {
  std::set<std::uint64_t> permitted,occupied;
  const bool perFilesystem=policy.onePerFilesystem&&!policy.concurrentReads;
  const auto reserve=[&](const CheckRequest& request){permitted.insert(request.admission);if(request.filesystems.empty()){occupied.insert(0);}else{occupied.insert(request.filesystems.begin(),request.filesystems.end());}};
  std::vector<const CheckRequest*> waiting;
  for(const auto& request:requests){
    if(request.retainedPermit){reserve(request);}
    else if(request.eligible){waiting.push_back(&request);}
  }
  std::ranges::sort(waiting,[&](const auto* a,const auto* b){
    if(policy.smallestFirst&&a->remainingBytes!=b->remainingBytes){return a->remainingBytes<b->remainingBytes;}
    return a->admission<b->admission;
  });
  for(const auto* request:waiting){
    if(policy.maximumActive&&permitted.size()>=policy.maximumActive){break;}
    if(perFilesystem){
      if(occupied.contains(0)||((request->filesystems.empty()||std::ranges::find(request->filesystems,0)!=request->filesystems.end())&&!occupied.empty())){continue;}
      if(std::ranges::any_of(request->filesystems,[&](auto id){return occupied.contains(id);})){continue;}
    }
    reserve(*request);
  }
  return permitted;
}
}

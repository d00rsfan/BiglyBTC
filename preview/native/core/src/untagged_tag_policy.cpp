// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/untagged_tag_policy.hpp>
namespace biglybtc::core::tags::untagged {
std::expected<runtime::Truth,Error> otherManualTags(std::span<const Entry> tags,bool complete)noexcept{
  if(tags.size()>4096){return std::unexpected(Error::Bounds);}bool unknown=!complete;
  for(const auto& tag:tags){
    if(tag.member==runtime::Membership::Absent||tag.isUntagged==true){continue;}
    if(tag.isUntagged==false&&tag.member==runtime::Membership::Present){return runtime::Truth::Yes;}
    unknown=true;
  }return unknown?runtime::Truth::Unknown:runtime::Truth::No;
}
Membership project(runtime::Truth other,bool persistent,bool member)noexcept{
  if(other==runtime::Truth::Yes&&member){return Membership::Remove;}
  if(other==runtime::Truth::No&&persistent&&!member){return Membership::Add;}
  return Membership::Keep;
}
}

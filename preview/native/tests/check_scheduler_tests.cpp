// SPDX-License-Identifier: GPL-2.0-or-later
#include "check_scheduler.hpp"
#include <iostream>
#include <stdexcept>
using namespace biglybtc;
using engine::detail::CheckPolicy;
using engine::detail::CheckRequest;
void require(bool value,const char* text){if(!value){throw std::runtime_error(text);}}
int main(){
  using engine::detail::scheduleChecks;
  const std::vector<CheckRequest> requests{{1,{11},100},{2,{11},10},{3,{22},50}};
  auto policy=CheckPolicy{};
  require(scheduleChecks(policy,requests)==std::set<std::uint64_t>{2},"Original default global limit one, smallest first");
  policy.maximumActive=0;policy.onePerFilesystem=true;
  require(scheduleChecks(policy,requests)==std::set<std::uint64_t>({2,3}),"Same filesystem serial, different filesystem parallel");
  policy.concurrentReads=true;
  require(scheduleChecks(policy,requests).size()==3,"Original concurrent read opt-out");
  policy.concurrentReads=false;policy.onePerFilesystem=false;
  require(scheduleChecks(policy,requests).size()==3,"Original per-filesystem opt-out");
  policy.maximumActive=2;policy.onePerFilesystem=true;policy.smallestFirst=false;
  require(scheduleChecks(policy,requests)==std::set<std::uint64_t>({1,3}),"Admission ordering when smallest-first is disabled");
  auto retained=requests;retained[0].retainedPermit=true;retained[0].eligible=false;
  policy.smallestFirst=true;
  require(scheduleChecks(policy,retained)==std::set<std::uint64_t>({1,3}),"Cancelling/pausing check retains slot until drained");
  retained[0].retainedPermit=false;
  require(scheduleChecks(policy,retained)==std::set<std::uint64_t>({2,3}),"Drained permit releases its filesystem");
  const std::vector<CheckRequest> linked{{1,{11,22},10},{2,{22},20},{3,{33},30}};
  require(scheduleChecks(policy,linked)==std::set<std::uint64_t>({1,3}),"Linked payload reserves every authenticated filesystem");
  const std::vector<CheckRequest> unknown{{1,{},10},{2,{22},20}};
  require(scheduleChecks(policy,unknown)==std::set<std::uint64_t>{1},"Unknown filesystem shares a conservative slot");
  const std::vector<CheckRequest> zero{{1,{22},10},{2,{0},20}};
  require(scheduleChecks(policy,zero)==std::set<std::uint64_t>{1},"Unknown device cannot bypass another filesystem");
  auto defaults=core::Document::parse("de");require(bool(defaults),"Default fixture parse");
  auto parsed=engine::detail::readCheckPolicy(defaults->root());require(parsed&&parsed->maximumActive==1&&!parsed->onePerFilesystem&&!parsed->concurrentReads&&parsed->smallestFirst,"Original defaults");
  auto user=core::Document::parse("d34:diskmanager.hashchecking.maxactivei0e25:diskmanager.one.op.per.fsi1ee");require(bool(user),"User fixture parse");
  parsed=engine::detail::readCheckPolicy(user->root());require(parsed&&scheduleChecks(*parsed,requests)==std::set<std::uint64_t>({2,3}),"Actual user settings");
  require(user->setInteger("diskmanager.hashchecking.maxactive",-1),"Negative unlimited fixture");
  parsed=engine::detail::readCheckPolicy(user->root());require(parsed&&parsed->maximumActive==0,"Original negative means unlimited");
  require(user->setInteger("diskmanager.hashchecking.maxactive",4294967297LL),"Java int narrowing fixture");
  parsed=engine::detail::readCheckPolicy(user->root());require(parsed&&parsed->maximumActive==1,"Java Number.intValue narrowing");
  auto malformed=core::Document::parse("d34:diskmanager.hashchecking.maxactive1:xe");
  require(bool(malformed),"Malformed-value fixture still uses valid bencoding");
  require(!engine::detail::readCheckPolicy(malformed->root()),"Malformed configuration refuses");
  std::cout<<"PASS original checking policy and filesystem scheduling cases\n";
}

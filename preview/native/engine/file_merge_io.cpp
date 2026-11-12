// SPDX-License-Identifier: GPL-2.0-or-later
#include "file_merge_io.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
namespace biglybtc::engine::detail {
struct FileMergeIo::Impl {
 struct Queued {MergeIoJob job;std::stop_source stop;};
 mutable std::mutex mutex;std::condition_variable ready;std::deque<Queued> queue;
 std::vector<MergeIoResult> results;std::map<std::uint64_t,std::size_t> reservations;
 std::vector<MergeIoOwner> activeOwners;std::optional<std::stop_source> activeStop;
 std::function<void()> wake;std::jthread worker;bool stopping=false;std::size_t reserved=0;std::uint64_t lastId=0;
 static bool matches(const std::vector<MergeIoOwner>& owners,const MergeIoOwner& owner){return std::ranges::find(owners,owner)!=owners.end();}
 void run(){
  for(;;){Queued next;{
   std::unique_lock lock(mutex);ready.wait(lock,[&]{return stopping||!queue.empty();});if(stopping&&queue.empty()){return;}next=std::move(queue.front());queue.pop_front();activeOwners=next.job.owners;activeStop=next.stop;
  }
   MergeIoData data=std::unexpected(MergeIoFailure{MergeIoFailure::Cancelled,"Local reuse cancelled"});
   try{if(!next.stop.stop_requested()){data=next.job.execute(next.stop.get_token());}}catch(...){data=std::unexpected(MergeIoFailure{MergeIoFailure::Read,"Local read worker failed"});}
   // Release every duplicated payload/profile/control descriptor BEFORE the
   // completion becomes visible to a retiring owner.
   next.job.execute={};std::function<void()> notify;
   {std::lock_guard lock(mutex);activeOwners.clear();activeStop.reset();results.push_back({next.job.id,std::move(data)});notify=wake;}
   if(notify){try{notify();}catch(...){}}
  }
 }
};
FileMergeIo::FileMergeIo():impl_(std::make_unique<Impl>()){impl_->results.reserve(32);impl_->activeOwners.reserve(16);}
FileMergeIo::~FileMergeIo(){stopAndJoin();}
bool FileMergeIo::submit(MergeIoJob job){
 if(!job.id||!job.execute||job.owners.empty()||job.owners.size()>16||!job.reservedBytes||job.reservedBytes>64*1024*1024){return false;}
 std::lock_guard lock(impl_->mutex);if(impl_->stopping||job.id<=impl_->lastId||impl_->reservations.size()>=32||job.reservedBytes>64*1024*1024-impl_->reserved){return false;}
 if(!impl_->worker.joinable()){impl_->worker=std::jthread([state=impl_.get()]{state->run();});}
 impl_->lastId=job.id;impl_->reserved+=job.reservedBytes;impl_->reservations.emplace(job.id,job.reservedBytes);impl_->queue.push_back({std::move(job),{}});impl_->ready.notify_one();return true;
}
std::vector<MergeIoResult> FileMergeIo::take(){
 std::lock_guard lock(impl_->mutex);std::vector<MergeIoResult> results;results.swap(impl_->results);
 for(const auto& result:results){const auto found=impl_->reservations.find(result.id);if(found!=impl_->reservations.end()){impl_->reserved-=found->second;impl_->reservations.erase(found);}}return results;
}
void FileMergeIo::setWake(std::function<void()> wake){std::lock_guard lock(impl_->mutex);impl_->wake=std::move(wake);}
void FileMergeIo::cancelOwner(const MergeIoOwner& owner){
 std::function<void()> notify;{
  std::lock_guard lock(impl_->mutex);if(Impl::matches(impl_->activeOwners,owner)&&impl_->activeStop){impl_->activeStop->request_stop();}
  for(auto it=impl_->queue.begin();it!=impl_->queue.end();){if(!Impl::matches(it->job.owners,owner)){++it;continue;}it->stop.request_stop();it->job.execute={};impl_->results.push_back({it->job.id,std::unexpected(MergeIoFailure{MergeIoFailure::Cancelled,"Local reuse owner retired"})});it=impl_->queue.erase(it);notify=impl_->wake;}
 }if(notify){try{notify();}catch(...){}}
}
bool FileMergeIo::ownerBusy(const MergeIoOwner& owner)const{std::lock_guard lock(impl_->mutex);return Impl::matches(impl_->activeOwners,owner)||std::ranges::any_of(impl_->queue,[&](const auto& q){return Impl::matches(q.job.owners,owner);});}
std::size_t FileMergeIo::reservedBytes()const{std::lock_guard lock(impl_->mutex);return impl_->reserved;}
void FileMergeIo::stopAndJoin()noexcept{
 try{
  {std::lock_guard lock(impl_->mutex);impl_->stopping=true;if(impl_->activeStop){impl_->activeStop->request_stop();}for(auto& queued:impl_->queue){queued.stop.request_stop();queued.job.execute={};}impl_->queue.clear();impl_->wake={};}
  impl_->ready.notify_all();if(impl_->worker.joinable()){impl_->worker.join();}
  std::lock_guard lock(impl_->mutex);impl_->results.clear();impl_->reservations.clear();impl_->reserved=0;
 }catch(...){std::terminate();} // A running reader must never escape its owner.
}
}

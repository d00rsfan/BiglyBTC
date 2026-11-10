// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_profile_io.hpp"
#include <limits>
namespace biglybtc::app {
namespace {
storage::Error cancelled(){return {storage::ErrorCode::Cancelled,"Room profile operation was cancelled or expired"};}
std::string roomKey(const ChatProfileRequest& request){return std::visit([](const auto& value)->std::string{using T=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<T,ChatProfileList>){return {};}else if constexpr(std::is_same_v<T,ChatProfileOptions>){return value.settings.key;}else{return value.key;}},request);}
std::expected<ChatProfileData,storage::Error> execute(ChatProfile& profile,ChatProfileRequest request){
 return std::visit([&](auto value)->std::expected<ChatProfileData,storage::Error>{
  using T=std::decay_t<decltype(value)>;ChatProfileData out;
  if constexpr(std::is_same_v<T,ChatProfileList>){auto result=profile.rooms();if(!result){return std::unexpected(result.error());}out.catalogue=std::move(*result);}
  else if constexpr(std::is_same_v<T,ChatProfileLoad>){auto identity=profile.identity(value.key,value.create);if(!identity){return std::unexpected(identity.error());}out.identity=std::make_shared<ChatIdentity>(std::move(*identity));auto history=profile.history(value.key);if(history){out.history=std::move(*history);}else{out.historyError=history.error();}}
  else if constexpr(std::is_same_v<T,ChatProfileOptions>){auto result=profile.saveOptions(value.settings);if(!result){return std::unexpected(result.error());}}
  else if constexpr(std::is_same_v<T,ChatProfileNicknameMode>||std::is_same_v<T,ChatProfileNickname>){
   auto result=[&](){if constexpr(std::is_same_v<T,ChatProfileNicknameMode>){return profile.setSharedNickname(value.key,value.shared);}else{return profile.setNickname(value.key,std::move(value.nickname));}}();
   if(!result){return std::unexpected(result.error());}auto catalogue=profile.rooms();if(!catalogue){return std::unexpected(catalogue.error());}out.catalogue=std::move(*catalogue);
  }
  else {auto result=profile.saveHistory(value.key,std::move(value.encoded));if(!result){return std::unexpected(result.error());}}
  return out;
 },std::move(request));
}
}
ChatProfileIo::ChatProfileIo(std::shared_ptr<const storage::ProfileLease> lease){
 if(!lease){throw std::runtime_error("Room IO needs the held profile");}auto opened=ChatProfile::open(*lease);if(!opened){throw std::runtime_error(opened.error().message);}profile_=std::move(*opened);io_=std::make_unique<Broker>(std::move(lease));
}
ChatProfileIo::~ChatProfileIo(){requestStop();join();}
void ChatProfileIo::bind(QObject* owner){if(io_){io_->bind(owner);}}
std::expected<std::uint64_t,storage::Error> ChatProfileIo::submit(ChatProfileRequest request,std::uint64_t generation,Clock::time_point end){
 if(stopping_||!io_){return std::unexpected(cancelled());}const auto now=Clock::now();if(generation==0||end<=now||end-now>std::chrono::seconds(30)){return std::unexpected(cancelled());}
 if(pending_.size()>=8){return std::unexpected(storage::Error{storage::ErrorCode::Busy,"Room profile queue is full"});}
 auto key=roomKey(request);if(key.size()>4096||(std::holds_alternative<ChatProfileHistory>(request)&&std::get<ChatProfileHistory>(request).encoded.size()>1024*1024)){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Room profile request exceeds its bound"});}
 if(next_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Room profile request identity exhausted"});}
 const auto id=next_+1;auto box=std::make_shared<Box>();const auto profile=profile_;pending_.push_back({id,generation,std::move(key),end,{},box,{}});
 std::expected<Broker::Handle,core::SubmitError> job=std::unexpected(core::SubmitError::Stopped);
 try{job=io_->submit([box,profile,request=std::move(request),end](std::stop_token stop)mutable->Broker::Result{
  if(stop.stop_requested()||Clock::now()>=end){return std::unexpected(cancelled());}
  box->outcome=execute(*profile,std::move(request));if(!*box->outcome){return std::unexpected(box->outcome->error());}return Broker::Data{};
 });}catch(...){
  // The broker may have reserved a slot before submission threw. Revoke the
  // complete broker; its lifetime caller joins outside the source gate.
  io_->requestStop();stopping_=true;pending_.pop_back();
  return std::unexpected(storage::Error{storage::ErrorCode::Io,"Room IO submission failed"});
 }
 if(!job){pending_.pop_back();return std::unexpected(storage::Error{storage::ErrorCode::Busy,"Room profile queue is full or stopping"});}pending_.back().job=std::move(*job);next_=id;return id;
}
bool ChatProfileIo::cancel(std::uint64_t id){for(auto& pending:pending_){if(pending.id==id){pending.outcome=std::unexpected(cancelled());Broker::revoke(pending.job);return true;}}return false;}
std::vector<ChatProfileCompletion> ChatProfileIo::advance(){
 std::vector<ChatProfileCompletion> out;if(!io_){return out;}io_->drainWake();
 for(auto i=pending_.begin();i!=pending_.end();){
  if(Clock::now()>=i->end&&!i->outcome){Broker::revoke(i->job);}
  if(!i->outcome){if(auto ready=Broker::take(i->job)){
    if(i->box->outcome){i->outcome=std::move(*i->box->outcome);i->box->outcome.reset();}else{i->outcome=std::unexpected(*ready?storage::Error{storage::ErrorCode::Io,"Room IO omitted its result"}:ready->error());}
    Broker::acknowledge(i->job);
  }}
  if(!Broker::released(i->job)){++i;continue;}
  // Released is published after destruction of the worker's source access.
  // No application crypto, UI or callback runs with that access still held.
  if(!i->outcome){i->outcome=std::unexpected(Broker::failure(i->job));}io_->release(i->job);out.push_back({i->id,i->generation,std::move(i->key),std::move(*i->outcome)});i=pending_.erase(i);
 }return out;
}
void ChatProfileIo::requestStop()noexcept{stopping_=true;if(io_){io_->unbind();io_->requestStop();}}
void ChatProfileIo::join()noexcept{if(io_){io_->join();io_.reset();}pending_.clear();profile_.reset();}
}

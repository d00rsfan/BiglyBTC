// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/internal_tag_owner.hpp>
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
namespace biglybtc::core::tags::internal {
struct Token::Data {std::int32_t id;runtime::Text name;bool registered=false,removed=false;};
struct Token::Life {};
namespace {
template<class T>void appendSpace(std::vector<T>& values,std::size_t maximum){if(values.size()==values.capacity()){values.reserve(std::min(maximum,std::max(values.size()+1,values.capacity()?values.capacity()*2:std::size_t{8})));}}
}

std::optional<std::int32_t> Token::id()const noexcept{return data_?std::optional{data_->id}:std::nullopt;}
Owner::Owner(Limits limit):limits_(limit),thread_(std::this_thread::get_id()),life_(std::make_shared<const Token::Life>()){}
Owner::~Owner()=default;
std::expected<std::unique_ptr<Owner>,Error> Owner::create(Limits limit){
 try{const Limits cap;limit.objects=std::min(limit.objects,cap.objects);limit.registered=std::min(limit.registered,cap.registered);limit.nameUnits=std::min(limit.nameUnits,cap.nameUnits);limit.totalUnits=std::min(limit.totalUnits,cap.totalUnits);limit.creations=std::min(limit.creations,cap.creations);return std::unique_ptr<Owner>(new Owner(limit));}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::optional<Error> Owner::check()const noexcept{if(thread_!=std::this_thread::get_id()){return Error::WrongThread;}if(closed_){return Error::Closed;}return {};}
std::optional<Error> Owner::check(const Token& token)const noexcept{if(auto error=check()){return error;}if(!token.data_||token.life_.lock()!=life_){return Error::Foreign;}if(token.data_->removed){return Error::Removed;}return {};}
std::size_t Owner::liveUnits()const noexcept{std::size_t total=0;for(const auto& weak:allocated_){if(auto value=weak.lock()){total+=value->name.size();}}return total;}
std::expected<Token,Error> Owner::createTag(std::u16string_view name,bool autoAdd){
 try{
  if(auto error=check()){return std::unexpected(*error);}std::erase_if(allocated_,[](const auto& value){return value.expired();});
  if(allocated_.size()>=limits_.objects||creations_>=limits_.creations||name.size()>limits_.nameUnits||name.size()>limits_.totalUnits-liveUnits()||(autoAdd&&registered_.size()>=limits_.registered)||revision_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Error::Bounds);}
  if(autoAdd){appendSpace(registered_,limits_.registered);}const auto next=next_+1U;
  auto data=std::make_shared<Token::Data>(std::bit_cast<std::int32_t>(next),runtime::Text(name),autoAdd,false);Token token(data,life_);
  allocated_.push_back(data);if(autoAdd){registered_.push_back(data);}next_=next;++creations_;++revision_;return token;
 }catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::expected<void,Error> Owner::attach(const Token& token){
 try{if(auto error=check(token)){return std::unexpected(*error);}if(token.data_->registered){return std::unexpected(Error::Registered);}if(registered_.size()>=limits_.registered||revision_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Error::Bounds);}appendSpace(registered_,limits_.registered);registered_.push_back(token.data_);token.data_->registered=true;++revision_;return {};}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::expected<void,Error> Owner::rename(const Token& token,std::u16string_view name){
 try{if(auto error=check(token)){return std::unexpected(*error);}if(token.data_->name==name){return {};}const auto other=liveUnits()-token.data_->name.size();if(name.size()>limits_.nameUnits||name.size()>limits_.totalUnits-other||revision_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Error::Bounds);}runtime::Text next(name);token.data_->name.swap(next);++revision_;return {};}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::expected<void,Error> Owner::remove(const Token& token){if(auto error=check(token)){return std::unexpected(*error);}if(revision_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Error::Bounds);}std::erase(registered_,token.data_);token.data_->registered=false;token.data_->removed=true;++revision_;return {};}
std::expected<Snapshot,Error> Owner::snapshot()const{
 try{if(auto error=check()){return std::unexpected(*error);}Snapshot out;out.revision=revision_;out.entries.reserve(registered_.size());for(const auto& entry:registered_){out.entries.push_back({entry->id,entry->name});}return out;}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::expected<void,Error> Owner::close()noexcept{if(thread_!=std::this_thread::get_id()){return std::unexpected(Error::WrongThread);}if(closed_){return {};}closed_=true;life_.reset();registered_.clear();allocated_.clear();return {};}
}

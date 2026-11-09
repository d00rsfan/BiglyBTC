// SPDX-License-Identifier: GPL-2.0-or-later
#include "call.hpp"
#include <algorithm>
namespace biglybtc::services::azureus_transfer {
std::expected<bool,Error> Reassembly::add(const Data& data){
  if(data.type!=Type::WriteRequest&&data.type!=Type::ReadReply){return std::unexpected(Error::Input);}if(!encodeBody(data)){return std::unexpected(Error::Range);}
  if(total_&&*total_!=data.total){return std::unexpected(Error::Range);}if(!total_){total_=data.total;}
  for(const auto& p:pieces_){if(data.start==p.start&&data.length==p.length&&data.payload==p.payload){return value().has_value();}if(data.start==p.start||(data.start<p.start+p.length&&p.start<data.start+data.length)){return std::unexpected(Error::Range);}}
  if(pieces_.size()==MaximumPieces){return std::unexpected(Error::Limit);}pieces_.push_back(data);std::ranges::sort(pieces_,{},&Data::start);return value().has_value();
}
std::vector<Range> Reassembly::missing()const{
  if(!total_){return {{0,0}};}std::vector<Range> out;std::int32_t p=0;for(const auto& part:pieces_){if(p<part.start){out.push_back({p,part.start-p});}p=part.start+part.length;}if(p<*total_){out.push_back({p,*total_-p});}return out;
}
std::optional<std::string> Reassembly::value()const{
  if(!total_||pieces_.empty()){return {};}std::int32_t p=0;for(const auto& part:pieces_){if(part.start!=p){return {};}p+=part.length;}if(p!=*total_){return {};}std::string out;out.reserve(static_cast<std::size_t>(*total_));for(const auto& part:pieces_){out+=part.payload;}return out;
}
std::expected<std::vector<Data>,Error> fragments(Type type,std::string_view handler,std::string_view key,std::string_view whole,std::int32_t start,std::int32_t length,bool firstOnly){
  if((type!=Type::WriteRequest&&type!=Type::ReadReply)||whole.size()>MaximumValue||handler.size()!=20||key.size()!=20){return std::unexpected(Error::Limit);}if(start<0){start=0;}if(length<=0){length=static_cast<std::int32_t>(whole.size());}
  if(whole.empty()){return std::vector<Data>{{type,std::string(handler),std::string(key),0,0,0,{}}};}
  if(static_cast<std::size_t>(start)>=whole.size()){return std::vector<Data>{};}if(length<0||static_cast<std::size_t>(length)>whole.size()-static_cast<std::size_t>(start)){return std::unexpected(Error::Range);}
  std::vector<Data> out;const auto end=start+length;for(auto p=start;p<end;){const auto n=std::min<std::int32_t>(end-p,static_cast<std::int32_t>(MaximumData));out.push_back({type,std::string(handler),std::string(key),p,n,static_cast<std::int32_t>(whole.size()),std::string(whole.substr(static_cast<std::size_t>(p),static_cast<std::size_t>(n)))});p+=n;if(firstOnly){break;}}return out;
}
bool WriteProgress::acknowledge(const Data& data,std::size_t total)noexcept{
  if(data.type!=Type::WriteReply||data.start<0||data.length<0||static_cast<std::size_t>(data.start)>total||static_cast<std::size_t>(data.length)>total-static_cast<std::size_t>(data.start)){return false;}receivedAck=true;if(data.start==0&&static_cast<std::size_t>(data.length)==total){complete=true;}return true;
}
}

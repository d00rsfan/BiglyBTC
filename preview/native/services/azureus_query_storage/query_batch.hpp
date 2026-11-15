// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "query_storage.hpp"
namespace biglybtc::services::azureus_query_storage {
// Pure owning bodies only. Caller must obtain current source/endpoint authority
// for each physical send; the supplied header is used only for exact sizing.
inline std::expected<std::vector<RequestBody>,Error> splitRequest(const RequestBody& input,const udp::EncodeFields& sizingHeader,std::size_t maximumBytes=1400){
 if(input.headerLength!=6||maximumBytes>1400||maximumBytes<64){return std::unexpected(Error{ErrorCode::Input,0,{}});}
 if(input.groups.size()>maximumEntries){return std::unexpected(Error{ErrorCode::Limit,0,{}});}
 std::size_t total=0;
 for(const auto& group:input.groups){
  if(group.prefix.size()>6||group.suffixes.size()>maximumEntries-total){
   return std::unexpected(Error{ErrorCode::Limit,0,{}});
  }
  total+=group.suffixes.size();
  for(const auto& suffix:group.suffixes){
   if(suffix.size()!=6-group.prefix.size()){
    return std::unexpected(Error{ErrorCode::Input,0,{}});
   }
  }
 }
 std::vector<RequestBody> output;RequestBody current;std::size_t rows=0;
 const auto flush=[&]{output.push_back(std::move(current));current=RequestBody{};rows=0;};
 for(const auto& group:input.groups){
  if(group.suffixes.empty()){
   current.groups.push_back(group);auto wire=encodeRequest({sizingHeader,current});if(!wire||wire->size()>maximumBytes){current.groups.pop_back();if(current.groups.empty()){return std::unexpected(Error{wire?ErrorCode::Limit:wire.error().code,0,{}});}flush();current.groups.push_back(group);wire=encodeRequest({sizingHeader,current});if(!wire||wire->size()>maximumBytes){return std::unexpected(Error{wire?ErrorCode::Limit:wire.error().code,0,{}});}}continue;
  }
  bool start=true;
  for(const auto& suffix:group.suffixes){
   if(rows==384){flush();start=true;}if(start){current.groups.push_back({group.prefix,{}});start=false;}current.groups.back().suffixes.push_back(suffix);auto wire=encodeRequest({sizingHeader,current});
   if(!wire||wire->size()>maximumBytes){current.groups.back().suffixes.pop_back();if(current.groups.back().suffixes.empty()){current.groups.pop_back();}if(current.groups.empty()){return std::unexpected(Error{wire?ErrorCode::Limit:wire.error().code,0,{}});}flush();current.groups.push_back({group.prefix,{suffix}});wire=encodeRequest({sizingHeader,current});if(!wire||wire->size()>maximumBytes){return std::unexpected(Error{wire?ErrorCode::Limit:wire.error().code,0,{}});}}
   ++rows;
  }
 }
 if(!current.groups.empty()||output.empty()){auto wire=encodeRequest({sizingHeader,current});if(!wire||wire->size()>maximumBytes){return std::unexpected(Error{wire?ErrorCode::Limit:wire.error().code,0,{}});}flush();}return output;
}
}

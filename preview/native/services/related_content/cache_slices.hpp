// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cache_limits.hpp"
#include <charconv>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>
namespace biglybtc::services::related_content::detail {
// Validate a large bencoded cache without allocating a recursive Node tree.
// Each returned view borrows the caller's buffer. Per-record Document parsing
// still supplies the ordinary original-field/type/duplicate validation.
class CacheSlices final {
 std::string_view input_;std::size_t position_=0,nodes_=0;
 [[noreturn]] static void bad(){throw std::runtime_error("Invalid or oversized discoveries cache structure");}
 char peek()const{if(position_>=input_.size()){bad();}return input_[position_];}
 std::string_view string(){const auto begin=position_;while(position_<input_.size()&&input_[position_]>='0'&&input_[position_]<='9'){++position_;}if(position_==begin||peek()!=':'){bad();}const auto digits=input_.substr(begin,position_-begin);if(digits.size()>1&&digits[0]=='0'){bad();}std::size_t size=0;const auto parsed=std::from_chars(digits.data(),digits.data()+digits.size(),size);if(parsed.ec!=std::errc{}||parsed.ptr!=digits.data()+digits.size()){bad();}++position_;if(size>input_.size()-position_){bad();}auto result=input_.substr(position_,size);position_+=size;return result;}
 void skip(std::size_t depth){if(depth>128||++nodes_>MaximumCacheNodes){bad();}const auto marker=peek();if(marker>='0'&&marker<='9'){string();return;}if(marker=='i'){const auto start=++position_;const auto end=input_.find('e',start);if(end==std::string_view::npos){bad();}const auto text=input_.substr(start,end-start);std::int64_t value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);if(text.empty()||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||(text.size()>1&&text[0]=='0')||text=="-0"||(text.size()>2&&text[0]=='-'&&text[1]=='0')){bad();}position_=end+1;return;}if(marker!='l'&&marker!='d'){bad();}++position_;std::set<std::string_view> keys;while(peek()!='e'){if(marker=='d'){if(++nodes_>MaximumCacheNodes){bad();}if(!keys.insert(string()).second){bad();}}skip(depth+1);}++position_;}
public:
 explicit CacheSlices(std::string_view input):input_(input){if(input.size()>MaximumCacheBytes){bad();}}
 std::vector<std::pair<std::string_view,std::string_view>> dictionary(){if(peek()!='d'){bad();}++position_;std::vector<std::pair<std::string_view,std::string_view>> result;std::set<std::string_view> keys;while(peek()!='e'){const auto key=string();if(!keys.insert(key).second){bad();}const auto start=position_;skip(1);result.emplace_back(key,input_.substr(start,position_-start));}++position_;if(position_!=input_.size()){bad();}return result;}
 std::vector<std::string_view> list(){if(peek()!='l'){bad();}++position_;std::vector<std::string_view> result;while(peek()!='e'){const auto start=position_;skip(1);result.push_back(input_.substr(start,position_-start));if(result.size()>MaximumItems){bad();}}++position_;if(position_!=input_.size()){bad();}return result;}
};
}

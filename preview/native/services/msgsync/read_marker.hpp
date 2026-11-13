// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace biglybtc::services::msgsync {
struct ChatReadMarker {std::int64_t writtenSeconds=0,messageSeconds=0;std::string id;};
struct ChatReadMessage {std::string id;std::int64_t timestamp=0;};
namespace read_marker_detail {
// Character.digit(char,10) on the JDK17 original reference. Supplementary
// digits are surrogate pairs and are not accepted by Long.parseLong(String).
inline int digit(char16_t c)noexcept{
 constexpr std::array starts{0x0030,0x0660,0x06f0,0x07c0,0x0966,0x09e6,0x0a66,0x0ae6,0x0b66,0x0be6,0x0c66,0x0ce6,0x0d66,0x0de6,0x0e50,0x0ed0,0x0f20,0x1040,0x1090,0x17e0,0x1810,0x1946,0x19d0,0x1a80,0x1a90,0x1b50,0x1bb0,0x1c40,0x1c50,0xa620,0xa8d0,0xa900,0xa9d0,0xa9f0,0xaa50,0xabf0,0xff10};for(auto start:starts){if(c>=start&&c<start+10){return c-start;}}return -1;
}
inline std::optional<std::int64_t> number(std::u16string_view text)noexcept{
 if(text.empty()){return {};}const bool negative=text.front()==u'-';if(negative||text.front()==u'+'){text.remove_prefix(1);}if(text.empty()){return {};}const std::uint64_t limit=negative?(UINT64_C(1)<<63):INT64_MAX;std::uint64_t value=0;for(auto c:text){const auto part=digit(c);if(part<0||value>(limit-std::uint64_t(part))/10){return {};}value=value*10+std::uint64_t(part);}return std::bit_cast<std::int64_t>(negative?std::uint64_t(0)-value:value);
}
inline std::string decode(std::u16string_view text){
 std::string result(text.size()*5/8,'\0');if(result.empty()){return result;}unsigned index=0;std::size_t offset=0;for(auto c:text){if(c>=u'a'&&c<=u'z'){c-=u'a'-u'A';}int part=-1;if(c>=u'A'&&c<=u'Z'){part=c-u'A';}else if(c>=u'2'&&c<=u'7'){part=c-u'2'+26;}if(part<0){continue;}if(index<=3){index=(index+5)%8;if(index==0){result[offset]|=char(part);if(++offset>=result.size()){break;}}else{result[offset]|=char(unsigned(part)<<(8-index));}}else{index=(index+5)%8;result[offset]|=char(unsigned(part)>>index);if(++offset>=result.size()){break;}result[offset]|=char(unsigned(part)<<(8-index));}}return result;
}
inline std::int64_t cutoff(const ChatReadMarker& marker)noexcept{return std::max(std::bit_cast<std::int64_t>(std::uint64_t(marker.writtenSeconds)-300),marker.messageSeconds);}
}
inline std::optional<ChatReadMarker> parseChatReadMarker(std::u16string_view text){
 if(text.size()>4096){return {};}while(!text.empty()&&text.back()==u'/'){text.remove_suffix(1);}const auto first=text.find(u'/');if(first==text.npos){return {};}const auto second=text.find(u'/',first+1);if(second==text.npos){return {};}const auto third=text.find(u'/',second+1);auto written=read_marker_detail::number(text.substr(0,first)),message=read_marker_detail::number(text.substr(first+1,second-first-1));if(!written||!message){return {};}return ChatReadMarker{*written,*message,read_marker_detail::decode(text.substr(second+1,third==text.npos?text.size()-second-1:third-second-1))};
}
inline bool oldOutstandingMessage(const std::optional<ChatReadMarker>& marker,std::span<const ChatReadMessage> ledger,const ChatReadMessage& message){
 if(!marker){return false;}if(message.id==marker->id||message.timestamp/1000<=read_marker_detail::cutoff(*marker)){return true;}bool hasOld=false,hasTarget=false;std::int64_t targetIndex=-1,oldIndex=-1;for(std::size_t i=0;i<ledger.size();++i){const auto& value=ledger[i];hasOld|=value.id==marker->id;hasTarget|=value.id==message.id;if(&value==&message){targetIndex=std::int64_t(i);}else if(value.id==marker->id){oldIndex=std::int64_t(i);}}return hasOld&&hasTarget&&targetIndex<=oldIndex;
}
inline std::vector<bool> fixupChatSeen(const std::optional<ChatReadMarker>& marker,std::span<const ChatReadMessage> ledger,std::span<const ChatReadMessage* const> targets){
 std::vector<bool> seen(targets.size(),false);if(!marker){return seen;}std::optional<std::size_t> oldIndex;for(std::size_t i=0;i<ledger.size();++i){if(ledger[i].id==marker->id){oldIndex=i;}}
 for(std::size_t n=0;n<targets.size();++n){const auto& message=*targets[n];seen[n]=message.id==marker->id||message.timestamp/1000<=read_marker_detail::cutoff(*marker);if(oldIndex){for(std::size_t i=0;i<=*oldIndex;++i){if(&ledger[i]==&message){seen[n]=true;break;}}}}return seen;
}
inline std::string encodeChatReadMarker(std::int64_t now,std::int64_t timestamp,std::string_view id){
 constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out=std::to_string(now/1000)+"/"+std::to_string(timestamp/1000)+"/";unsigned buffer=0,bits=0;for(unsigned char c:id){buffer=(buffer<<8)|c;bits+=8;while(bits>=5){bits-=5;out+=alphabet[(buffer>>bits)&31];}}if(bits){out+=alphabet[(buffer<<(5-bits))&31];}return out;
}
}

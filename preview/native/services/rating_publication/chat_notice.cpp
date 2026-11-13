// SPDX-License-Identifier: GPL-2.0-only
#include "chat_notice.hpp"
#include <algorithm>
namespace biglybtc::plugins::rating::publication {
std::u16string noticeMessage(std::int32_t score,std::u16string_view comment){
 // java.lang.String.trim removes UTF-16 units <=U+0020, not Unicode whitespace.
 while(!comment.empty()&&comment.front()<=u' '){comment.remove_prefix(1);}
 while(!comment.empty()&&comment.back()<=u' '){comment.remove_suffix(1);}
 std::u16string result=u"Rating: ";for(char c:std::to_string(score)){result+=char16_t(c);}
 if(!comment.empty()){result+=u", ";result+=comment;}return result;
}
bool shouldSendNotice(bool written,std::span<const NoticeMessage> messages,std::u16string_view text)noexcept{
 if(!written){return true;}if(messages.size()>50){return false;}
 return std::ranges::none_of(messages,[text](const auto& message){return message.own&&message.text==text;});
}
NoticeWait::Stage NoticeWait::poll(std::int32_t sync,bool destroyed)noexcept{
 if(stage_!=Stage::Sync){return stage_;}++polls_;if(destroyed){stage_=Stage::Cancelled;}
 else if(sync==0||polls_>=10){stage_=Stage::Delay;}
 return stage_;
}
NoticeWait::Stage NoticeWait::afterDelay(bool destroyed)noexcept{
 if(stage_==Stage::Delay){stage_=destroyed?Stage::Cancelled:Stage::Ready;}return stage_;
}
}

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "transcript_policy.hpp"
namespace biglybtc::services::msgsync {
// BuddyPluginBeta.getName(true)/getShortKey/addChatActivity and the SWT renderer.
inline QString activityChannel(QString key,QString displayName={}){
 QString name=std::move(displayName);if(name.isEmpty()){const auto pos=key.lastIndexOf('[');if(pos>=0&&key.endsWith(']')){const auto options=key.mid(pos+1,key.size()-pos-2);key=key.left(pos);if(options.contains("pk=")){key+=options.contains("ro=1")?"[R]":"[M]";}}name=QStringLiteral("Pub - '")+key+"'";}const auto pos=name.lastIndexOf('[');if(pos>=0&&name.endsWith(']')){name=name.left(pos);}return name;
}
inline QString activityMessage(const QString& text,std::int32_t origin){QString out;for(const auto& span:transcriptSpans(text,origin)){if(span.style==TranscriptStyle::Link){out+="<A HREF=\""+span.target+"\">"+span.text+"</A>";}else{out+=span.text;}}return out;}
}

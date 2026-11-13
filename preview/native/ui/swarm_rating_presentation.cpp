// SPDX-License-Identifier: GPL-2.0-or-later
#include "swarm_rating_presentation.hpp"
#include <QObject>
#include <algorithm>
#include <cmath>
namespace biglybtc::ui {
std::optional<SwarmRatingCell> presentSwarmRatings(std::span<const plugins::rating::RatingData> ratings){
 if(ratings.size()>1024){return {};}if(ratings.empty()){return SwarmRatingCell{QObject::tr("None"),{},false};}
 float total=0;QString comments;
 for(const auto& rating:ratings){if(!rating.comment){return {};}const auto score=std::clamp(rating.score,1,5);total+=static_cast<float>(score);const auto& raw=*rating.comment;std::size_t first=0,last=raw.size();while(first<last&&raw[first]<=u' '){++first;}while(last>first&&raw[last-1]<=u' '){--last;}auto comment=QStringView(raw.data()+first,qsizetype(last-first)).toString();if(comment.isEmpty()){comment=QObject::tr("<no comment>");}comments+=QStringLiteral("  ")+QString::number(score)+QStringLiteral(": ")+comment+QStringLiteral("\r\n");}
 // Java divides as float, then widens to double before its floor operation.
 const float quotient=total/static_cast<float>(ratings.size());const double average=std::floor(static_cast<double>(quotient)*10.0)/10.0;auto text=QString::number(average,'f',1);if(text.endsWith(QStringLiteral(".0"))){text.chop(2);}return SwarmRatingCell{text,QObject::tr("%1 out of 5 average score").arg(text)+QStringLiteral("\r\n")+comments,false};
}
}

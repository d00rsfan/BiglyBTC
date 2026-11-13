// SPDX-License-Identifier: GPL-2.0-only
#include "rating_stars.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <limits>
#include <QCoreApplication>
// Global scope is required for the generated static resource entry point.
static void initialiseRatingIcons(){Q_INIT_RESOURCE(rating_icons);}
namespace biglybtc::ui {
int ratingPixels(float value)noexcept{
 if(!(value>=0)){value=0;}else if(value>5){value=5;}
 return static_cast<int>(static_cast<double>(value)/5.0*RatingStarsWidth);
}
std::optional<std::int32_t> ratingScoreAt(int width,int x)noexcept{
 if(width<=0){return {};}const int padding=width>=RatingStarsWidth?(width-RatingStarsWidth)/2:0;const int star=width>=RatingStarsWidth?RatingStarWidth:width/5;if(!star){return {};}
 const auto raw=(std::int64_t(x)-padding)/star+1;return static_cast<std::int32_t>(std::min<std::int64_t>(5,raw));
}
float ratingGlobalSort(float average,std::uint32_t count)noexcept{return average+static_cast<float>(count)/10000.0f;}
QVariantMap presentRatingCell(const RatingCellFacts& facts,bool personalSort){
 QVariantMap result;const float average=facts.average.value_or(facts.personal>0?float(facts.personal):0.0f);const float global=ratingGlobalSort(average,facts.count);
 // RatingResults.getAverageScore uses a Java float multiply then int narrowing.
 const float scaled=average*10.0f;std::int32_t digits=0;if(!std::isnan(scaled)){if(scaled>=float(INT32_MAX)){digits=INT32_MAX;}else if(scaled<=float(INT32_MIN)){digits=INT32_MIN;}else{digits=static_cast<std::int32_t>(scaled);}}
 const auto text=QString::number(digits/10)+'.'+QString::number(digits%10);
 QString detail=facts.count?QCoreApplication::translate("RatingColumn","%1 (based on %2 ratings)").arg(text).arg(facts.count):QCoreApplication::translate("RatingColumn","%1 (no ratings yet)").arg(text);
 if(facts.comments){detail+='\n'+QCoreApplication::translate("RatingColumn","%1 comment(s)").arg(facts.comments);}if(facts.personal>0){detail+='\n'+QCoreApplication::translate("RatingColumn","Your rating is %1").arg(QString::number(float(facts.personal),'f',1));}
 result[QStringLiteral("enabled")]=facts.enabled&&!facts.loading;result[QStringLiteral("stars")]=global;result[QStringLiteral("personal")]=facts.personal;result[QStringLiteral("sort")]=personalSort?float(facts.personal):global;
 result[QStringLiteral("text")]=facts.enabled&&!facts.loading?text+QStringLiteral("/5.0"):QString{};result[QStringLiteral("detail")]=facts.loading?QCoreApplication::translate("RatingColumn","Loading rating…"):facts.enabled?detail:QString{};return result;
}
QImage ratingStars(float average,float personal,bool dark){
 static const bool resources=[](){initialiseRatingIcons();return true;}();(void)resources;
 static const QImage rated=QImage(QStringLiteral(":/biglybtc/rating/rated.png")).convertToFormat(QImage::Format_ARGB32);
 static const QImage unrated=QImage(QStringLiteral(":/biglybtc/rating/unrated.png")).convertToFormat(QImage::Format_ARGB32);
 if(rated.size()!=QSize(RatingStarsWidth,RatingStarsHeight)||unrated.size()!=rated.size()){return {};}
 QImage image=unrated.copy();const auto star=ratingPixels(average),line=ratingPixels(personal);
 for(int y=0;y<image.height()&&star>0;++y){std::memcpy(image.scanLine(y),rated.constScanLine(y),std::size_t(star)*sizeof(QRgb));}
 if(line>0){auto* upper=reinterpret_cast<QRgb*>(image.scanLine(image.height()-2));auto* lower=reinterpret_cast<QRgb*>(image.scanLine(image.height()-1));for(int x=0;x<line;++x){upper[x]=dark?qRgba(255,32,255,0x90):qRgba(0,32,255,0x90);lower[x]=dark?qRgba(255,16,127,0x40):qRgba(0,16,127,0x40);}}
 return image;
}
QImage ratingStarsForCell(float average,float personal,bool dark,int width){
 if(width<=0){return {};}auto image=ratingStars(average,personal,dark);if(width<image.width()){
  // SWT ImageData.scaledTo uses endpoint-aligned16.16 nearest sampling;
  // Qt FastTransformation uses a different mapping for narrow cells.
  QImage scaled(width,image.height(),QImage::Format_ARGB32);const std::int64_t step=width>1?((std::int64_t(image.width())<<16)-1)/(width-1):0;
  for(int y=0;y<image.height();++y){const auto* source=reinterpret_cast<const QRgb*>(image.constScanLine(y));auto* target=reinterpret_cast<QRgb*>(scaled.scanLine(y));std::int64_t position=0;for(int x=0;x<width;++x,position+=step){target[x]=source[position>>16];}}image=std::move(scaled);
 }return image;
}
}

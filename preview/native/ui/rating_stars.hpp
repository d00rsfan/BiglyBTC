// SPDX-License-Identifier: GPL-2.0-only
// Original pixel composition: azrating RatingImageUtil, Aelitis SARL.
#pragma once
#include <QImage>
#include <cstdint>
#include <optional>
#include <QVariantMap>
namespace biglybtc::ui {
inline constexpr int RatingGraphicRole=Qt::UserRole+18;
inline constexpr int RatingStarWidth=13,RatingStarsWidth=65,RatingStarsHeight=15;
struct RatingCellFacts {bool enabled=true,loading=false;std::optional<float> average;std::int32_t personal=0;std::uint32_t count=0,comments=0;};
QVariantMap presentRatingCell(const RatingCellFacts&,bool personalSort);
int ratingPixels(float)noexcept;
std::optional<std::int32_t> ratingScoreAt(int cellWidth,int x)noexcept;
float ratingGlobalSort(float average,std::uint32_t count)noexcept;
// Raw pixel replacement, including transparency. This is not alpha blending.
QImage ratingStars(float average,float personal,bool dark);
QImage ratingStarsForCell(float average,float personal,bool dark,int width);
}

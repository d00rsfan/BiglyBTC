// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/auto_speed_v2.hpp"
#include "biglybtc/bencode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace biglybtc::core {
namespace {
constexpr std::int64_t UploadStart=30720, DownloadStart=61440;
constexpr std::int64_t UploadMinimum=5120, DownloadMinimum=20480;
constexpr std::int64_t SearchMillis=30000, SeedingMillis=60000;
constexpr std::int64_t MaximumRate=std::numeric_limits<std::int32_t>::max();

bool validRate(std::int64_t rate) { return rate>=0&&rate<=MaximumRate; }

std::int64_t rate(double value) {
  return static_cast<std::int64_t>(std::clamp(value, 0.0, static_cast<double>(MaximumRate)));
}

std::int64_t rounded(double value) { return rate(std::floor(value+0.5)); }

double fraction(double current,double amount) { return std::clamp(current+amount,0.0,1.0); }

std::int64_t increase(std::int64_t current) {
  const auto step=current<102400?1024:current<409600?5120:10240;
  return std::min(MaximumRate,current+step);
}

float pingSignal(float rating) {
  if(rating>=1) { return 1; }
  if(rating<=-1) { return -1; }
  if(rating>-0.5f&&rating<0.5f) { return 0; }
  return 2.0f*(rating>0?rating-0.5f:rating+0.5f);
}

AdaptiveConfidence confidence(std::string_view value) {
  constexpr std::array names{"none","low","med","high","absolute"};
  for(std::size_t index=0;index<names.size();++index) {
    if(std::ranges::equal(value,std::string_view(names[index]),[](char a,char b) {
      return (a>='A'&&a<='Z'?a+('a'-'A'):a)==b;
    })) { return static_cast<AdaptiveConfidence>(index); }
  }
  return AdaptiveConfidence::None;
}

AdaptiveConfidence confidence(float type) {
  if(type<=-0.1f) { return AdaptiveConfidence::None; }
  if(type<=0.f) { return AdaptiveConfidence::Low; }
  if(type<=0.5f) { return AdaptiveConfidence::Medium; }
  if(type<=0.8f) { return AdaptiveConfidence::High; }
  return AdaptiveConfidence::Fixed;
}

bool unlimitedDownload(const AdaptiveCapacity& value) {
  return value.bytesPerSecond==0&&(value.type==1.f||value.type==-0.1f);
}
} // namespace

std::optional<AdaptivePing> adaptivePing(const std::array<std::int32_t,3>& rtt) {
  std::int64_t total=0;
  std::int32_t minimum=std::numeric_limits<std::int32_t>::max(), maximum=0;
  unsigned count=0, successful=0;
  for(const auto value:rtt) {
    if(value<0) { continue; }
    minimum=std::min(minimum,value);
    maximum=std::max(maximum,value);
    total+=value;
    ++count;
    if(value>0) { ++successful; }
  }
  if(!count) { return {}; }
  if(count>1) { total-=maximum; --count; }
  return AdaptivePing{(total/count+minimum)/2,successful};
}

std::expected<std::optional<AutoSpeedV2Preferences>, std::string_view> readAutoSpeedV2Preferences(const Document* document) {
  if(!document) { return std::optional<AutoSpeedV2Preferences>{}; }
  const auto integer=[&](std::string_view key,std::int64_t initial) {
    const auto* node=document->find(key);
    if(!node) { return initial; }
    if(node->type!=Node::Type::Integer||!validRate(node->integer)) { throw key; }
    return node->integer;
  };
  const auto savedConfidence=[&](std::string_view key) {
    const auto* node=document->find(key);
    if(!node) { return AdaptiveConfidence::None; }
    if(node->type!=Node::Type::Bytes) { throw key; }
    return confidence(node->bytes);
  };
  try {
    AutoSpeedV2Preferences result;
    result.enabled=integer("Auto Upload Speed Enabled",0)!=0;
    result.seedingEnabled=integer("Auto Upload Speed Seeding Enabled",0)!=0;
    const auto version=integer("Auto Upload Speed Version",2);
    if((!result.enabled&&!result.seedingEnabled)||(version!=2&&version!=3)) {
      return std::optional<AutoSpeedV2Preferences>{};
    }
    auto& settings=result.settings;
    settings.skipAfterAdjustment=integer("SpeedManagerAlgorithmProviderV2.setting.wait.after.adjust",1)!=0;
    settings.ratingInterval=static_cast<unsigned>(integer("SpeedManagerAlgorithmProviderV2.intervals.between.adjust",2));
    settings.uploadChokes=static_cast<unsigned>(std::min<std::int64_t>(30,integer("SpeedLimitMonitor.setting.choke.ping.count",1)));
    settings.uploadCapacity=std::max(UploadStart,integer("SpeedManagerAlgorithmProviderV2.setting.upload.max.limit",UploadStart));
    settings.downloadCapacity=std::max(DownloadStart,integer("SpeedManagerAlgorithmProviderV2.setting.download.max.limit",DownloadStart));
    settings.uploadConfidence=savedConfidence("SpeedLimitMonitor.setting.upload.limit.conf");
    settings.downloadConfidence=savedConfidence("SpeedLimitMonitor.setting.download.limit.conf");
    const auto percent=integer("SpeedLimitMonitor.setting.upload.used.download.mode",60);
    if(percent>10&&percent<100) { settings.downloadingUploadFraction=static_cast<float>(percent)/100.0f; }
    return std::optional{result};
  } catch(std::string_view key) { return std::unexpected(key); }
}

AutoSpeedV2::AutoSpeedV2(AutoSpeedV2Settings settings,std::int64_t now,
    std::optional<AdaptiveCapacity> savedUpload,std::optional<AdaptiveCapacity> savedDownload)
    : settings_(settings),upload_{UploadStart,UploadMinimum,settings.uploadConfidence},
      download_{DownloadStart,DownloadMinimum,settings.downloadConfidence},
      lastDownload_(std::max<std::int64_t>(0,now)),lastObservation_(lastDownload_) {
  if(!(settings_.downloadingUploadFraction>0.1f&&settings_.downloadingUploadFraction<1.0f)) {
    settings_.downloadingUploadFraction=0.6f;
  }
  if(savedUpload&&validRate(savedUpload->bytesPerSecond)&&std::isfinite(savedUpload->type)) {
    settings.uploadCapacity=std::max(UploadStart,savedUpload->bytesPerSecond);
    upload_.confidence=confidence(savedUpload->type);
  }
  if(savedDownload&&validRate(savedDownload->bytesPerSecond)&&std::isfinite(savedDownload->type)) {
    settings.downloadCapacity=std::max(DownloadStart,savedDownload->bytesPerSecond);
    download_.confidence=confidence(savedDownload->type);
    unlimitedDownload_=unlimitedDownload(*savedDownload);
  }
  updateCaps(settings.uploadCapacity,settings.downloadCapacity);
  resetPins(lastDownload_);
}

AutoSpeedV2::Saturation AutoSpeedV2::saturation(std::int64_t value,std::int64_t limit) {
  const float ratio=static_cast<float>(value)/static_cast<float>(limit==0?DownloadStart:limit);
  if(ratio>0.95f) { return Saturation::AtLimit; }
  if(ratio>0.75f) { return Saturation::High; }
  if(ratio>0.25f) { return Saturation::Medium; }
  if(ratio>0.03f) { return Saturation::Low; }
  return Saturation::None;
}

void AutoSpeedV2::updateCaps(std::int64_t upload,std::int64_t download) {
  upload_.maximum=std::clamp(upload,UploadMinimum,MaximumRate);
  download_.maximum=std::clamp(download,std::max(DownloadMinimum,upload_.maximum),MaximumRate);
  upload_.minimum=std::max(UploadMinimum,upload_.maximum/10);
  download_.minimum=std::max(DownloadMinimum,download_.maximum/10);
}

void AutoSpeedV2::resetPins(std::int64_t now) {
  upload_.saturatedSince=download_.saturatedSince=now;
  upload_.pinned=download_.pinned=true;
}

std::int64_t AutoSpeedV2::chooseCapacity(const Axis& axis,const AdaptiveCapacity& value) {
  if(!validRate(value.bytesPerSecond)||!std::isfinite(value.type)||!std::isfinite(value.metric)
      ||(value.bytesPerSecond<axis.maximum&&value.bytesPerSecond<DownloadMinimum)) { return axis.maximum; }
  if(value.type==-0.1f) { return std::max(axis.maximum,value.bytesPerSecond); }
  if(value.type==0.0f&&value.metric>=0.0f) { return axis.maximum; }
  return value.bytesPerSecond;
}

void AutoSpeedV2::observeCapacity(std::optional<AdaptiveCapacity> upload,std::optional<AdaptiveCapacity> download,std::int64_t now) {
  if(now<lastObservation_||testing()) { return; }
  const auto newUpload=upload?chooseCapacity(upload_,*upload):upload_.maximum;
  const auto newDownload=download?chooseCapacity(download_,*download):download_.maximum;
  if(upload&&newUpload!=upload_.maximum) {
    if(upload->type>=0.5f) { settings_.uploadChokes=std::min(settings_.uploadChokes+1,30u); }
    resetPins(now);
  }
  if(upload&&upload->type==1.0f) { upload_.confidence=AdaptiveConfidence::Fixed; }
  if(download&&validRate(download->bytesPerSecond)&&std::isfinite(download->type)) {
    unlimitedDownload_=unlimitedDownload(*download);
    if(download->type==1.0f) { download_.confidence=AdaptiveConfidence::Fixed; }
    if(unlimitedDownload_) { downloadFraction_=1.0; }
  }
  updateCaps(newUpload,newDownload);
}

void AutoSpeedV2::observeRates(AdaptiveRates transfer,AdaptiveRates limits,std::int64_t now) {
  ready_=now>=lastObservation_&&validRate(transfer.upload)&&validRate(transfer.download)
      &&validRate(limits.upload)&&validRate(limits.download);
  if(!ready_) { return; }
  lastObservation_=now;
  limits_=limits;
  upload_.bandwidth=saturation(transfer.upload,limits.upload);
  download_.bandwidth=saturation(transfer.download,limits.download);
  upload_.cap=saturation(limits.upload,upload_.maximum);
  download_.cap=saturation(limits.download,download_.maximum);
  if(mode_==AdaptiveMode::DownloadLimitSearch||(!testing()&&download_.bandwidth>Saturation::Low)) {
    lastDownload_=now;
    if(!testing()) { mode_=AdaptiveMode::Downloading; }
  } else if(!testing()&&now-lastDownload_>SeedingMillis) { mode_=AdaptiveMode::Seeding; }
}

bool AutoSpeedV2::testing() const { return search_.has_value(); }

float AutoSpeedV2::adjustmentMultiplier(float signal,std::int64_t now) {
  if(signal>0) {
    consecutiveUp_=std::min(consecutiveUp_+1,10u);consecutiveDown_=0;
    if(upload_.bandwidth<=Saturation::Low&&download_.bandwidth<=Saturation::Low) { consecutiveUp_=0; }
    constexpr std::array steps{0.25f,0.25f,0.5f,1.0f,1.25f,1.5f,1.75f,2.0f,2.25f,2.5f,3.0f};
    const bool medium=upload_.bandwidth<=Saturation::Medium&&download_.bandwidth<=Saturation::Medium;
    return medium?steps[consecutiveUp_]/2.0f:steps[consecutiveUp_];
  }
  consecutiveUp_=0;consecutiveDown_=std::min(consecutiveDown_+1,10u);
  if(!upload_.pinned) { settings_.uploadChokes=std::min(settings_.uploadChokes+1,30u); }
  resetPins(now);
  constexpr std::array steps{0.25f,0.25f,0.5f,1.0f,2.0f,3.0f,4.0f,6.0f,9.0f,15.0f,20.0f};
  return steps[consecutiveDown_];
}

bool AutoSpeedV2::updatePins(std::int64_t now) {
  const auto saturated=[](const Axis& axis) { return axis.bandwidth==Saturation::AtLimit&&axis.cap==Saturation::AtLimit; };
  if(!saturated(upload_)) { upload_.saturatedSince=now; }
  else if(now-upload_.saturatedSince>SearchMillis*settings_.uploadChokes
      &&upload_.confidence!=AdaptiveConfidence::Fixed
      &&(upload_.confidence>=AdaptiveConfidence::Medium||mode_!=AdaptiveMode::Downloading)) { upload_.pinned=false; }
  if(!saturated(download_)) { download_.saturatedSince=now; }
  else if(now-download_.saturatedSince>SearchMillis) {
    if(download_.confidence<AdaptiveConfidence::Medium&&mode_==AdaptiveMode::Downloading) { return true; }
    if(download_.confidence>=AdaptiveConfidence::Medium&&download_.confidence!=AdaptiveConfidence::Fixed) { download_.pinned=false; }
  }
  return false;
}

AdaptiveRates AutoSpeedV2::startSearch(std::int64_t now) {
  mode_=AdaptiveMode::DownloadLimitSearch;
  search_=Search{now,{},limits_.upload,download_.maximum};
  return {upload_.minimum,rounded(static_cast<double>(download_.maximum)*1.2)};
}

AdaptiveRates AutoSpeedV2::finishSearch(const AdaptiveTrialEstimate& estimate) {
  const auto capacity=estimate.download?chooseCapacity(download_,*estimate.download):download_.maximum;
  const float difference=static_cast<float>(std::abs(capacity-search_->originalCapacity))
      /static_cast<float>(std::max(capacity,search_->originalCapacity));
  download_.confidence=difference<0.15f&&estimate.choked?AdaptiveConfidence::Medium:AdaptiveConfidence::Low;
  updateCaps(std::max(upload_.maximum,capacity/40),capacity);
  const auto upload=search_->originalUploadLimit;
  mode_=AdaptiveMode::Downloading;
  search_.reset();resetPins(lastObservation_);
  return {upload,download_.maximum};
}

AdaptiveRates AutoSpeedV2::adjustUnpinned(float signal) {
  if(signal<0) { upload_.pinned=download_.pinned=true; }
  const auto saturated=[](const Axis& axis) { return axis.bandwidth==Saturation::AtLimit&&axis.cap==Saturation::AtLimit; };
  auto upload=upload_.maximum;
  auto download=download_.maximum;
  if(saturated(upload_)&&mode_!=AdaptiveMode::Downloading) {
    const auto cadence=static_cast<unsigned>(std::ceil(std::sqrt(settings_.uploadChokes)));
    if(cadence&&++uploadPinCount_%cadence==0) { upload=increase(upload); }
  }
  if(saturated(download_)&&!unlimitedDownload_) { download=increase(download); }
  updateCaps(upload,download);
  return {upload_.maximum,unlimitedDownload_?0:download_.maximum};
}

AdaptiveRates AutoSpeedV2::adjustPinned(float amount) {
  const auto upload=std::max(UploadStart,upload_.maximum);
  const auto download=std::max({DownloadStart,download_.maximum,upload});
  const auto uploadMin=std::max(UploadMinimum,upload/10);
  const auto downloadMin=std::max(DownloadMinimum,download/10);
  const auto used=mode_==AdaptiveMode::Downloading?rounded(static_cast<float>(upload)*settings_.downloadingUploadFraction):upload;
  const auto factor=static_cast<double>(amount)/10.0;
  const auto ratio=static_cast<double>(used)/static_cast<double>(download);
  if(amount>=0) {
    if(downloadFraction_<0.99f) { downloadFraction_=fraction(downloadFraction_,factor); }
    else if(upload_.bandwidth==Saturation::AtLimit) { uploadFraction_=fraction(uploadFraction_,ratio*0.5*factor); }
  } else if(uploadFraction_>0.01f) { uploadFraction_=fraction(uploadFraction_,ratio*factor); }
  else { downloadFraction_=fraction(downloadFraction_,factor); }
  const auto up=rounded(static_cast<double>(used-uploadMin)*uploadFraction_+static_cast<double>(uploadMin));
  const auto down=unlimitedDownload_||downloadFraction_==1.0?0:
      rounded(static_cast<double>(download-downloadMin)*downloadFraction_+static_cast<double>(downloadMin));
  return {up>upload?used:up,down};
}

std::optional<AdaptiveRates> AutoSpeedV2::constrain() const {
  if(limits_.upload<=upload_.maximum&&(limits_.download<=download_.maximum||!unlimitedDownload_)) { return {}; }
  return AdaptiveRates{std::clamp(limits_.upload,upload_.minimum,upload_.maximum),
      unlimitedDownload_?std::max(limits_.download,download_.minimum):std::clamp(limits_.download,download_.minimum,download_.maximum)};
}

std::optional<AdaptiveRates> AutoSpeedV2::adjust(float rating,unsigned pings,std::int64_t now,const AdaptiveTrialEstimate& trial) {
  if(!ready_||!pings||!std::isfinite(rating)||now<lastObservation_) { return {}; }
  std::optional<AdaptiveRates> ramp;
  if(testing()) {
    if(rating<-0.3f&&!search_->lastBadPing) { search_->lastBadPing=now; }
    if(now-search_->lastIncrease>SearchMillis||(search_->lastBadPing&&now-*search_->lastBadPing>SearchMillis)) {
      return finishSearch(trial);
    }
    if(download_.bandwidth>Saturation::Medium) {
      auto increase=limits_;
      increase.download=rate(static_cast<float>(increase.download)*1.1f);
      if(increase!=limits_) { search_->lastIncrease=now;ramp=increase; }
    }
  }
  if(settings_.skipAfterAdjustment&&lastAdjusted_) { lastAdjusted_=false;intervals_=0;return ramp; }
  intervals_+=std::min(pings,settings_.ratingInterval-intervals_);
  if(intervals_<settings_.ratingInterval) { return ramp; }
  intervals_=0;
  const auto signal=pingSignal(rating);
  lastAdjusted_=signal!=0&&!testing();
  if(testing()) { return ramp; }
  if(!signal) { return constrain(); }
  const auto multiple=adjustmentMultiplier(signal,now);
  if(updatePins(now)) { return startSearch(now); }
  if(!upload_.pinned||!download_.pinned) { return adjustUnpinned(signal); }
  return adjustPinned(signal*multiple);
}
} // namespace biglybtc::core

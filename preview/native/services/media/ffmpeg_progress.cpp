// SPDX-License-Identifier: GPL-2.0-or-later
#include "ffmpeg_progress.hpp"
#include <QString>
#include <array>
#include <cmath>
#include <utility>

namespace biglybtc::media {
namespace {
struct IntegerField {
  QStringView name;
  std::optional<std::int64_t> FfmpegProgressSample::*slot;
  bool allowNegative=false;
};
constexpr std::array integerFields {
  IntegerField{u"frame",&FfmpegProgressSample::frames},
  IntegerField{u"dup_frames",&FfmpegProgressSample::duplicateFrames},
  IntegerField{u"drop_frames",&FfmpegProgressSample::droppedFrames},
  IntegerField{u"out_time_us",&FfmpegProgressSample::elapsedMicroseconds,true},
  IntegerField{u"total_size",&FfmpegProgressSample::outputBytes},
};
constexpr auto fpsField=1U<<integerFields.size();

bool validKey(QStringView key) {
  if(key.isEmpty()){return false;}
  for(const auto character:key){
    if(!(character>=u'a'&&character<=u'z')&&!(character>=u'A'&&character<=u'Z')&&
        !(character>=u'0'&&character<=u'9')&&character!=u'_'){return false;}
  }
  return true;
}
}

FfmpegProgressLine FfmpegProgress::consume(QStringView line) {
  const auto invalid=[this]{
    failed_=true;
    return FfmpegProgressLine::Malformed;
  };
  if(failed_){return FfmpegProgressLine::Malformed;}
  if(line.isEmpty()){return FfmpegProgressLine::Ignored;}
  if(ended_||line.size()>1024||line.contains(u'\r')||line.contains(u'\n')){return invalid();}
  const auto equals=line.indexOf(u'=');
  if(equals<=0){return invalid();}
  const auto key=line.first(equals);
  const auto value=line.sliced(equals+1);
  if(key==u"progress"){
    if(!fields_||(value!=u"continue"&&value!=u"end")){return invalid();}
    if(value==u"end"&&!pending_.frames&&!pending_.elapsedMicroseconds&&!pending_.outputBytes){return invalid();}
    latest_=std::move(pending_);
    pending_={};
    fields_=0;
    ended_=value==u"end";
    return ended_?FfmpegProgressLine::End:FfmpegProgressLine::Sample;
  }
  if(key==u"fps"){
    if(fields_&fpsField){return invalid();}
    if(value!=u"N/A"){
      bool valid=false;
      const auto rate=value.toDouble(&valid);
      if(!valid||!std::isfinite(rate)||rate<0){return invalid();}
      pending_.framesPerSecond=rate;
    }
    fields_|=fpsField;
    return FfmpegProgressLine::Ignored;
  }
  for(std::size_t index=0;index<integerFields.size();++index){
    const auto& field=integerFields[index];
    if(field.name!=key){continue;}
    const auto bit=1U<<index;
    if(fields_&bit){return invalid();}
    if(value!=u"N/A"){
      bool valid=false;
      const auto number=value.toLongLong(&valid);
      if(!valid||(!field.allowNegative&&number<0)){return invalid();}
      pending_.*field.slot=number;
    }
    fields_|=bit;
    return FfmpegProgressLine::Ignored;
  }
  return validKey(key)?FfmpegProgressLine::Ignored:invalid();
}

bool FfmpegProgress::complete(int exitCode,bool cancelled) const noexcept {
  return !cancelled&&!exitCode&&!failed_&&ended_;
}

} // namespace biglybtc::media

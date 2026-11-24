// SPDX-License-Identifier: GPL-2.0-or-later
#include "transcode_profile.hpp"
#include "java_properties.hpp"
#include <QStringList>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>

namespace biglybtc::media {
namespace {
using Properties=services::JavaProperties;
constexpr int MaxBytes=1024*1024;
constexpr int MaxEntries=4096;

QStringView javaTrim(QStringView text) {
  while(!text.isEmpty()&&text.front().unicode()<=0x20){text=text.sliced(1);}
  while(!text.isEmpty()&&text.back().unicode()<=0x20){text=text.first(text.size()-1);}
  return text;
}
std::optional<QString> get(const Properties& values,const char* key) {
  const auto found=values.constFind(QString::fromLatin1(key));
  return found==values.cend()?std::nullopt:std::optional<QString>(*found);
}
QString value(const Properties& values,const char* key) {
  return values.value(QString::fromLatin1(key));
}
template<class Integer> std::optional<Integer> originalInteger(QStringView raw) {
  static_assert(std::is_signed_v<Integer>);
  if(raw.isEmpty()){return {};}
  const bool negative=raw[0]==u'-';
  if(negative||raw[0]==u'+'){raw=raw.sliced(1);}
  if(raw.isEmpty()){return {};}
  using Unsigned=std::make_unsigned_t<Integer>;
  const Unsigned limit=static_cast<Unsigned>(std::numeric_limits<Integer>::max())+negative;
  Unsigned number=0;
  for(const auto unit:raw){
    // Java Integer.parseInt/Long.parseLong uses Character.digit in radix 10.
    const auto digit=unit.digitValue();
    if(unit.category()!=QChar::Number_DecimalDigit||digit<0||digit>9||
        number>(limit-static_cast<Unsigned>(digit))/10){return {};}
    number=number*10+static_cast<Unsigned>(digit);
  }
  if(negative&&number==limit){return std::numeric_limits<Integer>::min();}
  return negative?-static_cast<Integer>(number):static_cast<Integer>(number);
}
bool floatingOverflow(QByteArrayView digits,bool hex) {
  auto marker=digits.indexOf(hex?'p':'e');
  if(marker<0){marker=digits.indexOf(hex?'P':'E');}
  const auto mantissa=marker<0?digits:digits.first(marker);
  std::int64_t exponent=0;
  if(marker>=0){
    auto suffix=digits.sliced(marker+1);
    const bool minus=suffix.front()=='-';
    if(minus||suffix.front()=='+'){suffix=suffix.sliced(1);}
    // The original input is byte-bounded. Saturation still exceeds any
    // mantissa adjustment, even in the longest admitted hexadecimal string.
    constexpr std::int64_t exponentLimit=10000000;
    for(const char digit:suffix){exponent=std::min(exponentLimit,exponent*10+digit-'0');}
    if(minus){exponent=-exponent;}
  }
  auto point=mantissa.indexOf('.');
  if(point<0){point=mantissa.size();}
  for(qsizetype index=0;index<mantissa.size();++index){
    if(mantissa[index]=='0'||mantissa[index]=='.'){continue;}
    const auto power=point-index-(index<point);
    return exponent+power*(hex?4:1)>=0;
  }
  return false;
}
double originalDouble(QStringView text) {
  auto characters=javaTrim(text).toLatin1();
  const bool negative=characters.startsWith('-');
  if(negative||characters.startsWith('+')){characters.remove(0,1);}
  if(characters=="NaN"){return std::numeric_limits<double>::quiet_NaN();}
  if(characters=="Infinity"){
    const auto infinity=std::numeric_limits<double>::infinity();
    return negative?-infinity:infinity;
  }
  if(characters.endsWith('f')||characters.endsWith('F')||
      characters.endsWith('d')||characters.endsWith('D')){characters.chop(1);}
  auto digits=QByteArrayView(characters);
  if(digits.empty()){return 0;}
  const bool hex=digits.startsWith("0x")||digits.startsWith("0X");
  if(hex){
    digits=digits.sliced(2);
    if(!digits.contains('p')&&!digits.contains('P')){return 0;}
  }
  if(digits.empty()||(!(digits.front()>='0'&&digits.front()<='9')&&
      digits.front()!='.'&&!(hex&&((digits.front()>='a'&&digits.front()<='f')||
                                  (digits.front()>='A'&&digits.front()<='F'))))){return 0;}
  double result=0;
  const auto parsed=std::from_chars(digits.begin(),digits.end(),result,
      hex?std::chars_format::hex:std::chars_format::general);
  if(parsed.ptr!=digits.end()){return 0;}
  if(parsed.ec==std::errc::result_out_of_range){
    result=floatingOverflow(digits,hex)?std::numeric_limits<double>::infinity():0;
  }else if(parsed.ec!=std::errc{}){
    return 0;
  }
  return negative?-result:result;
}
QStringList commaList(const QString& text) {
  auto fields=text.split(u',');
  if(!text.isEmpty()){
    while(!fields.empty()&&fields.back().isEmpty()){fields.removeLast();}
  }
  return fields;
}
std::optional<std::vector<int>> numbers(const Properties& values,const char* key) {
  const auto raw=get(values,key);
  if(!raw){return {};}
  std::vector<int> result;
  for(const auto& entry:commaList(*raw)){result.push_back(originalInteger<int>(entry).value_or(0));}
  return result;
}
std::vector<AspectRatio> aspectRatios(const QString& value) {
  std::vector<AspectRatio> out;
  for(const auto& token:commaList(value)){
    const auto parts=token.split(u':',Qt::SkipEmptyParts);
    if(parts.size()!=2){continue;}
    const auto num=originalInteger<int>(parts[0]);
    const auto den=originalInteger<int>(parts[1]);
    if(num&&den){out.push_back({*num,*den});}
  }
  const auto valueOf=[](const AspectRatio& ratio){
    if(!ratio.denominator){
      if(!ratio.numerator){return std::numeric_limits<float>::quiet_NaN();}
      return ratio.numerator>0?std::numeric_limits<float>::infinity():-std::numeric_limits<float>::infinity();
    }
    return static_cast<float>(ratio.numerator)/static_cast<float>(ratio.denominator);
  };
  std::stable_sort(out.begin(),out.end(),[&](const auto& a,const auto& b){
    const auto left=valueOf(a),right=valueOf(b);
    return std::isnan(left)?!std::isnan(right):left<right;
  });
  return out;
}
}

std::expected<TranscodeProfile,ProfileError> readTranscodeProfile(QByteArrayView bytes,bool v2Enabled) {
  if(bytes.size()>MaxBytes){return std::unexpected(ProfileError::Bounds);}
  auto text=QString::fromLatin1(bytes);
  if(!v2Enabled){
    const auto utf8=QString::fromUtf8(bytes);
    auto lines=QStringView(utf8);
    QString beforeV2;
    while(!lines.isEmpty()){
      const auto line=services::nextJavaPropertyLine(lines);
      const auto lower=javaTrim(line).toString().toLower();
      if(!lower.startsWith(u'#')&&lower.contains(QStringLiteral("v2-supported=true"))){break;}
      beforeV2+=line;
      beforeV2+=QStringLiteral("\r\n");
    }
    text=QString::fromLatin1(beforeV2.toUtf8());
  }
  auto parsed=services::readJavaProperties(text,MaxEntries,services::JavaPropertiesDuplicates::LastWins);
  if(!parsed){return std::unexpected(parsed.error()==services::JavaPropertiesError::TooManyEntries?
      ProfileError::Bounds:ProfileError::InvalidProperties);}
  const auto& fields=*parsed;
  const auto name=get(fields,"name");
  if(!name){return std::unexpected(ProfileError::MissingName);}
  const auto number=[&](const char* key,int fallback=0){
    return originalInteger<int>(value(fields,key)).value_or(fallback);
  };
  const auto longNumber=[&](const char* key){
    return originalInteger<std::int64_t>(value(fields,key)).value_or(0);
  };
  const auto flag=[&](const char* key){
    return value(fields,key).compare(QStringLiteral("true"),Qt::CaseInsensitive)==0;
  };
  TranscodeProfile profile;
  profile.name=*name;
  profile.description=get(fields,"description");
  profile.iconUrl=get(fields,"icon_url");
  profile.iconIndex=number("icon_index");
  profile.extension=get(fields,"file-extension");
  profile.container=get(fields,"container-format");
  profile.videoTarget=get(fields,"video-target");
  profile.videoOptions=get(fields,"video-args");
  profile.audioOptions=get(fields,"audio-args");
  profile.device=get(fields,"device").value_or(QStringLiteral("generic"));
  profile.videoWidth=number("video-width");profile.videoHeight=number("video-height");
  profile.maxWidth=number("video-max-width");profile.maxHeight=number("video-max-height");
  profile.pixelMod=number("video-pixel-mod",8);profile.paddingMod=number("padding-mod",1);
  profile.overallBitrateHint=number("overall-bitrate-hint");
  profile.audioMinBitrate=number("audio-min-bitrate");profile.audioMaxBitrate=number("audio-max-bitrate");
  profile.audioMaxSampleRate=number("audio-max-sample-rate");profile.audioMaxChannels=number("audio-max-channels");
  profile.videoTargetBitrate=longNumber("video-base-bitrate");
  profile.audioBitrate=longNumber("video-audio-bitrate");profile.maxSize=longNumber("maxsize");
  profile.maxFrameRate=originalDouble(value(fields,"max-frame-rate"));
  profile.autoThreads=flag("auto-threads");profile.autoPadding=flag("auto-padding");
  profile.limitVideoBitrate=flag("video-limit-bitrate");profile.disableFrameRate=flag("video-disable-frame-rate");
  profile.streamable=value(fields,"streamable").compare(QStringLiteral("yes"),Qt::CaseInsensitive)==0;
  profile.v2Supported=flag("v2-supported");
  profile.aspectRatios=aspectRatios(value(fields,"aspect-ratios"));
  profile.audioAllowedChannels=numbers(fields,"audio-allowed-channels");
  profile.audioAllowedSampleRates=numbers(fields,"audio-allowed-sample-rates");
  profile.audioAllowedBitrates=numbers(fields,"audio-allowed-bitrates");
  for(auto item=fields.cbegin();item!=fields.cend();++item){
    if(javaTrim(item.key()).startsWith(QStringLiteral("raw"))){
      profile.rawOptions.push_back(javaTrim(item.value()).toString());
    }
  }
  return profile;
}
std::int64_t TranscodeProfile::estimatedSize(int durationSeconds) const noexcept {
  const auto size=static_cast<std::int64_t>(durationSeconds)*overallBitrateHint/8;
  return maxSize&&size>maxSize?maxSize:size;
}
} // namespace biglybtc::media

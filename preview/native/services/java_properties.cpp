// SPDX-License-Identifier: GPL-2.0-or-later
#include "java_properties.hpp"

#include <optional>

namespace biglybtc::services {

QStringView nextJavaPropertyLine(QStringView& input) {
  qsizetype end=0;
  while(end<input.size()&&input[end]!=u'\r'&&input[end]!=u'\n'){++end;}
  auto line=input.first(end);
  auto consumed=end;
  if(consumed<input.size()&&input[consumed++]==u'\r'&&
      consumed<input.size()&&input[consumed]==u'\n'){++consumed;}
  input=input.sliced(consumed);
  return line;
}
namespace {
bool keySpace(QChar c) {
  return c==u' '||c==u'\t'||c==u'\f';
}
QStringView noLeadingSpace(QStringView text) {
  qsizetype first=0;
  while(first<text.size()&&keySpace(text[first])){++first;}
  return text.sliced(first);
}
bool continued(QStringView text) {
  qsizetype marks=0;
  for(auto i=text.size();i>0&&text[i-1]==u'\\';--i){++marks;}
  return marks%2!=0;
}
std::optional<QString> decoded(QStringView text) {
  QString result;
  result.reserve(text.size());
  for(qsizetype i=0;i<text.size();++i){
    auto c=text[i];
    if(c!=u'\\'){result+=c;continue;}
    if(++i==text.size()){break;}
    switch(text[i].unicode()){
    case u't':result+=u'\t';break;
    case u'n':result+=u'\n';break;
    case u'r':result+=u'\r';break;
    case u'f':result+=u'\f';break;
    case u'u':{
      if(i+4>=text.size()){return {};}
      char16_t value=0;
      for(qsizetype n=0;n<4;++n){
        const auto digit=text[++i].unicode();
        if(digit>=u'0'&&digit<=u'9'){value=char16_t(value*16+digit-u'0');}
        else if(digit>=u'A'&&digit<=u'F'){value=char16_t(value*16+digit-u'A'+10);}
        else if(digit>=u'a'&&digit<=u'f'){value=char16_t(value*16+digit-u'a'+10);}
        else{return {};}
      }
      result+=QChar(value);
      break;
    }
    default:result+=text[i];break;
    }
  }
  return result;
}
} // namespace

std::expected<JavaProperties, JavaPropertiesError> readJavaProperties(
    QStringView input, qsizetype maximumEntries, JavaPropertiesDuplicates duplicates) {
  JavaProperties result;
  while(!input.isEmpty()){
    auto physical=noLeadingSpace(nextJavaPropertyLine(input));
    if(physical.isEmpty()||physical[0]==u'#'||physical[0]==u'!'){continue;}
    QString line;
    for(;;){
      const auto more=continued(physical);
      line+=more?physical.first(physical.size()-1):physical;
      if(!more||input.isEmpty()){break;}
      physical=noLeadingSpace(nextJavaPropertyLine(input));
    }
    qsizetype split=0;
    bool escape=false;
    for(;split<line.size();++split){
      const auto c=line[split];
      if(!escape&&(c==u'='||c==u':'||keySpace(c))){break;}
      escape=!escape&&c==u'\\';
    }
    auto value=noLeadingSpace(QStringView(line).sliced(split));
    if(!value.isEmpty()&&(value[0]==u'='||value[0]==u':')){value=noLeadingSpace(value.sliced(1));}
    auto key=decoded(QStringView(line).first(split));
    auto decodedValue=decoded(value);
    if(!key||!decodedValue){return std::unexpected(JavaPropertiesError::InvalidEscape);}
    const auto present = result.contains(*key);
    if (present && duplicates == JavaPropertiesDuplicates::Reject) {
      return std::unexpected(JavaPropertiesError::Duplicate);
    }
    if (!present && result.size() >= maximumEntries) {
      return std::unexpected(JavaPropertiesError::TooManyEntries);
    }
    result.insert(std::move(*key),std::move(*decodedValue));
  }
  return result;
}

} // namespace biglybtc::services

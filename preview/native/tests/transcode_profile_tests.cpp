// SPDX-License-Identifier: GPL-2.0-or-later
#include "transcode_profile.hpp"
#include "biglybtc/bencode.hpp"
#include <QFile>
#include <cmath>
#include <iostream>
#include <stdexcept>

using biglybtc::core::Node;
using namespace biglybtc::media;

QByteArray read(const QString& path) {
  QFile file(path);
  if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("Cannot open fixture");}
  return file.readAll();
}
void need(bool value,const char* field) {
  if(!value){throw std::runtime_error(field);}
}
QString text(const Node& field) {
  return QString::fromUtf8(field.bytes.data(),static_cast<qsizetype>(field.bytes.size()));
}
void matchStrings(const Node& row,const char* name,const std::optional<QString>& actual) {
  const auto* field=row.find(name);
  need(bool(field)==actual.has_value(),name);
  if(field){need(text(*field)==*actual,name);}
}
void matchNumbers(const Node& row,const char* name,const auto& value) {
  const auto* field=row.find(name);
  need(field&&field->children.size()==value.size(),name);
  std::size_t index=0;
  for(const auto& item:value){need(item==field->children[index++].integer,name);}
}
void matchOptionalNumbers(const Node& row,const char* name,const std::optional<std::vector<int>>& values) {
  need(bool(row.find(name))==values.has_value(),name);
  if(values){matchNumbers(row,name,*values);}
}
void compare(const Node& row,const TranscodeProfile& p) {
  matchStrings(row,"name",p.name);matchStrings(row,"description",p.description);
  matchStrings(row,"icon_url",p.iconUrl);matchStrings(row,"extension",p.extension);
  matchStrings(row,"container",p.container);matchStrings(row,"device",p.device);
  matchStrings(row,"video_args",p.videoOptions);matchStrings(row,"audio_args",p.audioOptions);
  matchNumbers(row,"dimensions",std::array{p.videoWidth,p.videoHeight,p.maxWidth,p.maxHeight,p.pixelMod,p.paddingMod});
  matchNumbers(row,"bitrate",std::array<std::int64_t,5>{
        p.audioBitrate,p.videoTargetBitrate,p.overallBitrateHint,p.audioMinBitrate,p.audioMaxBitrate});
  matchNumbers(row,"flags",std::array{p.autoThreads,p.autoPadding,p.limitVideoBitrate,p.streamable,p.v2Supported,p.disableFrameRate});
  need(p.estimatedSize(120)==row.find("estimated_120_s")->integer,"estimated size");
  need(p.maxFrameRate==std::stod(std::string(row.find("rate_text")->bytes)),"frame rate");
  const auto* options=row.find("options");
  need(p.rawOptions.size()==(options?options->children.size():0),"raw option count");
  for(std::size_t i=0;i<p.rawOptions.size();++i){need(p.rawOptions[i]==text(options->children[i]),"raw option ordering");}
  const auto* aspects=row.find("aspects");
  need(aspects&&aspects->children.size()==p.aspectRatios.size(),"aspect ratio count");
  for(std::size_t i=0;i<p.aspectRatios.size();++i){
    need(p.aspectRatios[i].numerator==aspects->children[i].children[0].integer&&
        p.aspectRatios[i].denominator==aspects->children[i].children[1].integer,"aspect ratio values");
  }
  matchOptionalNumbers(row,"channels",p.audioAllowedChannels);
  matchOptionalNumbers(row,"sample_rates",p.audioAllowedSampleRates);
  matchOptionalNumbers(row,"allowed_audio_bitrates",p.audioAllowedBitrates);
}
void checkGeneratedProfile() {
  const QByteArray source="name=First\nname=Final \\u00e9\nvideo-width=bad\nraw10=ten\nraw02=two\n"
      "audio-allowed-channels=1, bad,2,\nstreamable=YES\n# v2-supported=true\n"
      "overall-bitrate-hint=8000\nmaxsize=1100\naspect-ratios=16:9,4:3,broken\n"
      "v2-supported=true\nvideo-width=42\nraw03=three\n";
  const auto classic=readTranscodeProfile(source,false);
  const auto modern=readTranscodeProfile(source,true);
  need(classic&&modern&&classic->name==QStringLiteral("Final é"),"inherited properties");
  need(classic->videoWidth==0&&!classic->v2Supported&&modern->videoWidth==42&&modern->v2Supported,"v2 suffix");
  need(classic->streamable&&classic->estimatedSize(120)==1100,"streamable and capped size");
  const auto& ratios=classic->aspectRatios;
  need(ratios.size()==2&&ratios[0].numerator==4&&ratios[0].denominator==3,"aspect ratio order");
  need(classic->audioAllowedChannels==std::vector<int>{1,0,2},"integer list");
  need(modern->rawOptions==std::vector{QStringLiteral("two"),QStringLiteral("three"),QStringLiteral("ten")},
      "raw property order");
  const auto continued=readTranscodeProfile("name=\\\n  continued\n",true);
  need(continued&&continued->name==QStringLiteral("continued"),"continuation");
  const auto invalid=readTranscodeProfile("name=\\u0q00\n",true);
  need(!invalid&&invalid.error()==ProfileError::InvalidProperties,"malformed escape");
  const auto unnamed=readTranscodeProfile("video-width=42\n",true);
  need(!unnamed&&unnamed.error()==ProfileError::MissingName,"missing name");
  need(bool(readTranscodeProfile("name=Default\n",true)),"default profile");
  const auto frameRate=[](const char* field){
    const QByteArray input=QByteArray("name=Rate\nmax-frame-rate=")+field+'\n';
    const auto parsed=readTranscodeProfile(input,true);
    need(parsed.has_value(),"frame rate profile");
    return parsed->maxFrameRate;
  };
  need(std::isinf(frameRate("+1e9999999999999999999")),"positive overflow");
  const auto underflow=frameRate("-1e-9999999999999999999");
  need(underflow==0&&std::signbit(underflow),"negative underflow");
  need(frameRate("+-12")==0,"invalid doubled sign");
  need(frameRate("0x1.8p3")==12,"hexadecimal frame rate");
}

int compareOriginalProfiles(int argc,char** argv){
  if(argc!=3){return 1;}
  const auto oracle=biglybtc::core::Document::parse(read(QString::fromLocal8Bit(argv[2])).toStdString());
  if(!oracle){return 2;}
  unsigned good=0,bad=0;
  for(const auto& row:oracle->find("profiles")->children){
    const auto source=text(*row.find("fixture"));
    try{
      const auto parsed=readTranscodeProfile(read(QString::fromLocal8Bit(argv[1])+u'/'+source),
          row.find("v2_mode")->integer!=0);
      if(row.find("error")){need(!parsed&&parsed.error()==ProfileError::MissingName,"missing name");++bad;}
      else{need(parsed.has_value(),"property file rejected");compare(row,*parsed);++good;}
    }catch(const std::exception& error){
      std::cerr<<source.toStdString()<<" v2="<<row.find("v2_mode")->integer<<" differs in "<<error.what()<<'\n';
      return 3;
    }
  }
  need(good==100&&bad==4,"fixture coverage");
  const QByteArray edge="name=Latin \\u00e9\nname=Final \\u00e9\nvideo-width=bad\nraw10=ten\nraw02=two\n"
      "audio-allowed-channels=1, bad,2,\nstreamable=YES\n# v2-supported=true\n"
      "overall-bitrate-hint=8000\nmaxsize=1100\naspect-ratios=16:9,4:3,broken\n"
      "v2-supported=true\nvideo-width=42\nraw03=three\n";
  for(bool modern:{false,true}){
    const auto actual=readTranscodeProfile(edge,modern);
    need(actual.has_value(),"generated profile rejected");
    compare(*oracle->find(modern?"modern_edge":"classic_edge"),*actual);
  }
  const auto continued=readTranscodeProfile("name=\\\n  continued\n",true);
  need(continued&&continued->name==QStringLiteral("continued"),"continuation");
  const auto invalid=readTranscodeProfile("name=\\u0q00\n",true);
  need(!invalid&&invalid.error()==ProfileError::InvalidProperties,"malformed escape");
  std::cout<<"PASS 104 exact original vuzexcode profile-mode rows and generated codec controls\n";
  return 0;
}

int main(int argc,char** argv){
  try{
    checkGeneratedProfile();
    if(argc==1){std::cout<<"PASS generated original property and profile controls\n";return 0;}
    return compareOriginalProfiles(argc,argv);
  }catch(const std::exception& error){
    std::cerr<<error.what()<<'\n';
    return 1;
  }
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "transcode_progress.hpp"
#include "ffmpeg_progress.hpp"
#include "biglybtc/bencode.hpp"
#include <QFile>
#include <iostream>
#include <stdexcept>

using namespace biglybtc::media;
using biglybtc::core::Node;

QByteArray read(const QString& path) {
  QFile file(path);
  if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("Cannot open fixture");}
  return file.readAll();
}
void need(bool value,const char* detail) {
  if(!value){throw std::runtime_error(detail);}
}
QString text(const Node& value) {
  return QString::fromUtf8(value.bytes.data(),qsizetype(value.bytes.size()));
}
void legacy(const QString& path,TranscodeOutputMode mode) {
  const auto source=biglybtc::core::Document::parse(read(path).toStdString());
  need(source.has_value(),"legacy oracle");
  const auto* rows=source->find("rows");
  need(rows&&rows->children.size()==13,"13 legacy rows");
  QString trailer;
  for(const auto& row:rows->children){
    TranscodeProgress actual(mode);
    const auto input=text(*row.find("input"));
    const auto kind=actual.consume(input);
    const auto counts=actual.current();
    need((kind==TranscodeLine::Progress)==bool(row.find("match")->integer),"legacy progress type");
    need((kind==TranscodeLine::Malformed)==bool(row.find("number_failure")->integer),"legacy rejected scalar");
    need(actual.trailerSeen()==bool(row.find("terminal")->integer),"legacy completion marker");
    need(counts.frame==row.find("frame")->integer&&counts.framesPerSecond==row.find("fps")->integer,"legacy counters");
    need(actual.diagnostics().size()==row.find("diagnostic_bytes")->integer,"legacy captured output");
    if(actual.trailerSeen()){trailer=input;}
  }
  TranscodeProgress bounded(mode);
  auto enormous=QString(48*1024,u'x')+trailer;
  need(!trailer.isEmpty(),"legacy oracle trailer");
  need(bounded.consume(enormous)==TranscodeLine::Diagnostic&&!bounded.trailerSeen(),"overflow trailer not parsed");
  need(bounded.diagnostics().size()==source->find("bounded_diagnostics_length")->integer,"legacy cap");
  need(bounded.consume(trailer)==TranscodeLine::Diagnostic&&bounded.trailerSeen(),"trailer after saturated output");
  need(bounded.finish(0,false)==TranscodeEnd::MissingProgress,"no inferred success");
  need(bounded.consume(u"frame=21 fps=37 q=7")==TranscodeLine::Progress,"complete progress");
  need(bounded.finish(0,false)==TranscodeEnd::Complete,"exact legacy completion");
  need(bounded.finish(0,true)==TranscodeEnd::Cancelled,"cancelled progress");
  need(bounded.finish(7,false)==TranscodeEnd::ProcessFailed,"failed exit");
}
void generated() {
  TranscodeProgress old(TranscodeOutputMode::Classic);
  need(old.consume(u"frame= 12 fps= 25 q=1 dup= 3 drop=0")==TranscodeLine::Progress,"duplicate frames");
  need(old.current().frame==9,"adjusted original frame");
  old.consume(u"video:23kB audio:14kB global headers:0kB muxing overhead 1.05%");
  need(old.trailerSeen()&&old.finish(0,false)==TranscodeEnd::Complete,"classic trailer");
  old.consume(u"partial file");
  need(old.finish(0,false)==TranscodeEnd::PartialInput,"original partial input");
  TranscodeProgress legacyError(TranscodeOutputMode::V2);
  need(legacyError.consume(u"frame= 1 fps= 0.6 q=1")==TranscodeLine::Malformed,"original frame-rate failure");
  need(legacyError.finish(0,false)==TranscodeEnd::MalformedProgress,"malformed legacy completion");
  FfmpegProgress modern;
  need(modern.consume(u"frame=14")==FfmpegProgressLine::Ignored,"unframed sample");
  need(modern.consume(u"fps=23.50")==FfmpegProgressLine::Ignored,"decimal frame rate");
  need(!modern.latest()&&!modern.complete(0,false),"sample published before delimiter");
  need(modern.consume(u"progress=continue")==FfmpegProgressLine::Sample,"complete sample");
  need(modern.latest()&&modern.latest()->frames==14&&modern.latest()->framesPerSecond==23.5,"sample values");
  modern.consume(u"out_time_us=3500000");
  modern.consume(u"total_size=N/A");
  need(modern.consume(u"progress=end")==FfmpegProgressLine::End&&modern.complete(0,false),"machine end");
  need(modern.latest()->elapsedMicroseconds==3500000&&!modern.latest()->outputBytes,"audio only pipe record");
  need(!modern.complete(1,false)&&!modern.complete(0,true),"cancelled or failed child");
  FfmpegProgress q;
  q.consume(u"fps=0");
  need(q.consume(u"progress=end")==FfmpegProgressLine::Malformed,"terminal without typed media evidence");
}
int main(int argc,char** argv){
  if(argc!=1&&argc!=4){return 1;}
  try{
    generated();
    if(argc==1){std::cout<<"PASS generated legacy and FFmpeg output framing controls\n";return 0;}
    legacy(QString::fromLocal8Bit(argv[1]),TranscodeOutputMode::Classic);
    legacy(QString::fromLocal8Bit(argv[2]),TranscodeOutputMode::V2);
    FfmpegProgress machine;
    const auto output=QString::fromUtf8(read(QString::fromLocal8Bit(argv[3])));
    int reported=0;
    for(const auto& line:output.split(u'\n',Qt::SkipEmptyParts)){
      const auto step=machine.consume(line);
      need(step!=FfmpegProgressLine::Malformed,"real FFmpeg machine line");
      if(step==FfmpegProgressLine::Sample||step==FfmpegProgressLine::End){++reported;}
    }
    const auto& final=machine.latest();
    need(reported>=2&&final&&final->frames==24&&final->duplicateFrames==0&&final->droppedFrames==0&&
        final->elapsedMicroseconds==2000000&&final->outputBytes&&*final->outputBytes>0,"real FFmpeg final values");
    need(machine.complete(0,false)&&!machine.complete(42,false)&&!machine.complete(0,true),"real FFmpeg completion gating");
    need(machine.consume(u"frame=24")==FfmpegProgressLine::Malformed&&!machine.complete(0,false),"data after end");
    FfmpegProgress duplicate;
    duplicate.consume(u"frame=2");
    need(duplicate.consume(u"frame=3")==FfmpegProgressLine::Malformed,"duplicated machine field");
    FfmpegProgress partial;
    need(partial.consume(u"progress=end")==FfmpegProgressLine::Malformed,"unqualified machine end");
    std::cout<<"PASS 26 original progress rows, bounded legacy diagnostics, and real FFmpeg generated machine progress\n";
    return 0;
  }catch(const std::exception& problem){
    std::cerr<<problem.what()<<'\n';
    return 1;
  }
}

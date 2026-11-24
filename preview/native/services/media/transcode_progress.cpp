// SPDX-License-Identifier: GPL-2.0-or-later
#include "transcode_progress.hpp"
#include <QRegularExpression>

namespace biglybtc::media {
namespace {
constexpr qsizetype diagnosticLimit=48*1024;
constexpr qsizetype lineLimit=64*1024;
const auto progression=QRegularExpression(QRegularExpression::anchoredPattern(
    QStringLiteral(R"(frame=\s*([0-9]*) fps=\s*([0-9]*) .*)")));
const auto duplicates=QRegularExpression(QRegularExpression::anchoredPattern(
    QStringLiteral(R"(frame=\s*([0-9]*) fps=\s*([0-9]*) .*?dup=\s*([0-9]*) .*)")));
const QRegularExpression classicTrailer(
    QStringLiteral(R"(video:.*?B audio:.*?B global headers:.*?B muxing overhead .*?%)"));
const QRegularExpression v2Trailer(
    QStringLiteral(R"(video:.*?B audio:.*?B .*? global headers:.*?B muxing overhead: .*?%)"));
}

TranscodeLine TranscodeProgress::consume(QStringView line) {
  if(line.size()>lineLimit||line.contains(u'\r')||line.contains(u'\n')){
    malformed_=true;
    return TranscodeLine::Malformed;
  }
  auto match=duplicates.matchView(line);
  const bool subtract=match.hasMatch();
  if(!subtract){match=progression.matchView(line);}
  if(match.hasMatch()){
    bool validFrame=false,validDuplicates=true,validRate=false;
    const auto frame=match.capturedView(1).toInt(&validFrame);
    const auto removed=subtract?match.capturedView(3).toInt(&validDuplicates):0;
    if(!validFrame||!validDuplicates){
      malformed_=true;
      return TranscodeLine::Malformed;
    }
    current_.frame=frame-removed;
    const auto rate=match.capturedView(2).toInt(&validRate);
    if(!validRate){
      malformed_=true;
      return TranscodeLine::Malformed;
    }
    current_.framesPerSecond=rate;
    progressSeen_=true;
    return TranscodeLine::Progress;
  }
  const auto room=diagnosticLimit-diagnostics_.size();
  if(room>0){
    if(line.size()>room){line=line.first(room);}
    diagnostics_+=line;
    diagnostics_+=u'\n';
    if(diagnostics_.size()>=diagnosticLimit){diagnostics_+=QStringLiteral("...[truncated]\n");}
  }
  const auto& trailer=mode_==TranscodeOutputMode::Classic?classicTrailer:v2Trailer;
  trailerSeen_=trailerSeen_||trailer.matchView(line).hasMatch();
  return TranscodeLine::Diagnostic;
}

TranscodeEnd TranscodeProgress::finish(int exitCode,bool cancelled) const noexcept {
  if(cancelled){return TranscodeEnd::Cancelled;}
  if(malformed_){return TranscodeEnd::MalformedProgress;}
  if(exitCode){return TranscodeEnd::ProcessFailed;}
  if(diagnostics_.contains(QStringLiteral("partial file\n"))){return TranscodeEnd::PartialInput;}
  if(!progressSeen_){return TranscodeEnd::MissingProgress;}
  if(!trailerSeen_){return TranscodeEnd::MissingTrailer;}
  return TranscodeEnd::Complete;
}

} // namespace biglybtc::media

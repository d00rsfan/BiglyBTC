// SPDX-License-Identifier: GPL-2.0-or-later
#include "ffmpeg_process.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <iostream>
#include <algorithm>
#include <stdexcept>

using namespace biglybtc::media;
using namespace std::chrono_literals;

void require(bool accepted,const char* reason) {
  if(!accepted){throw std::runtime_error(reason);}
}
bool waitFor(const auto& predicate,int limit=5000) {
  QElapsedTimer timer;
  timer.start();
  while(!predicate()&&timer.elapsed()<limit){
    QCoreApplication::processEvents();
    QThread::msleep(3);
  }
  return predicate();
}
bool terminal(FfmpegJobState value) {
  return value==FfmpegJobState::Produced||value==FfmpegJobState::Failed||
      value==FfmpegJobState::Cancelled||value==FfmpegJobState::TimedOut;
}
int fixtureChild(int argc, char** argv) {
  bool earlyExit = false;
  for(int i = 1; i + 1 < argc; ++i){
    if(QByteArrayView(argv[i]) == "-f"){
      earlyExit = QByteArrayView(argv[i+1]) == "mpegts";
      break;
    }
  }
  std::string scenario;
  if(!earlyExit){std::getline(std::cin, scenario);}
  QFile output(QString::fromLocal8Bit(argv[argc-1]));
  if(!output.open(QIODevice::WriteOnly) || output.write("media", 5) != 5){return 2;}
  output.close();
  std::cout << "frame=1\ntotal_size=" << (scenario == "wrong-size" ? 4 : 5) << '\n';
  if(scenario != "missing-end"){std::cout << "progress=end\n";}
  return scenario == "failed-exit" ? 1 : 0;
}

void fixtureCases() {
  const QString executable = QCoreApplication::applicationFilePath();
  QFile source(executable);
  require(source.open(QIODevice::ReadOnly), "self fixture path");
  QCryptographicHash digest(QCryptographicHash::Sha256);
  require(digest.addData(&source), "self fixture digest");
  FfmpegJob job;
  job.program = {executable, digest.result()};
  {
    FfmpegProcess process;
    auto malformed = job;
    malformed.audioBounds = FfmpegAudioBounds{8000, 6};
    require(!process.start(malformed) && process.childPid() == 0, "invalid channel bounds started a child");
    malformed.audioBounds = FfmpegAudioBounds{0, 2};
    require(!process.start(malformed) && process.childPid() == 0, "invalid sample rate bounds started a child");
  }
  for(const QByteArray scenario : {"produced", "wrong-size", "missing-end", "failed-exit"}){
    FfmpegProcess process;
    require(bool(process.start(job)), "fixture launch");
    require(waitFor([&]{return process.state() == FfmpegJobState::Running;}), "fixture running");
    require(process.push(scenario) == scenario.size() && process.finishInput(), "fixture input");
    require(waitFor([&]{return terminal(process.state());}) && process.childPid() == 0, "fixture child joined");
    const auto expected = scenario == "produced" ? FfmpegJobState::Produced : FfmpegJobState::Failed;
    require(process.state() == expected && process.resultFile().isEmpty() == (expected != FfmpegJobState::Produced),
        "fixture publication did not match exit, progress and file size");
  }
  FfmpegProcess early;
  job.input = FfmpegContainer::MpegTs;
  require(bool(early.start(job)), "early fixture launch");
  require(waitFor([&]{return terminal(early.state());}) && early.state() == FfmpegJobState::Failed &&
      early.childPid() == 0 && early.resultFile().isEmpty(), "early exit published an unfinished input");
  std::cout << "PASS joined output, byte count, final progress, child exit and input closure controls\n";
}

int progressiveCase(char** argv) {
  QFile input(QString::fromLocal8Bit(argv[4]));
  require(input.open(QIODevice::ReadOnly) && input.size() > 32768 && input.size() < 16 * 1024 * 1024,
      "Own bounded generated media input for fragmented output");
  const QByteArray contents = input.readAll();
  FfmpegJob job;
  job.program = {QString::fromLocal8Bit(argv[2]), QByteArray::fromHex(argv[3])};
  job.output = FfmpegContainer::Mp4;
  job.geometry = TranscodeVideoGeometry{96, 64, 0, 0, std::nullopt};
  job.fragmentMp4 = true;
  job.deadline = 12s;
  FfmpegProcess process;
  require(bool(process.start(job)), "Start the pinned fragmented MP4 worker");
  require(waitFor([&] { return process.state() == FfmpegJobState::Running; }), "Fragmented media worker is running");
  qsizetype sent = 0;
  const auto sendThrough = [&](qsizetype end) {
    return waitFor([&] {
      if (sent == end || terminal(process.state())) { return true; }
      const auto next = std::min<qsizetype>(32768, end - sent);
      const auto accepted = process.push(QByteArrayView(contents).sliced(sent, next));
      require(accepted >= 0, "The worker accepts bounded verified input");
      sent += accepted;
      return sent == end;
    }) && sent == end;
  };
  require(sendThrough(contents.size() * 3 / 4), "Supply the first verified input prefix");
  std::shared_ptr<QFile> first;
  QByteArray prefix;
  require(waitFor([&] {
    first = process.progressiveHandle();
    if (!first || !first->seek(0)) { return false; }
    prefix = first->read(256 * 1024);
    return prefix.mid(4, 4) == "ftyp" && prefix.contains("moov") &&
        prefix.contains("moof") && prefix.contains("mdat");
  }, 5000), "Read the initialization and first media fragment before sending the remaining input");
  require(process.state() == FfmpegJobState::Running && process.childPid() > 0 && !process.resultHandle(),
      "Distinguish a growing fragment from a joined complete file");
  require(sendThrough(contents.size()) && process.finishInput(), "Finish sending the generated media");
  require(waitFor([&] { return terminal(process.state()); }) && process.state() == FfmpegJobState::Produced &&
      process.childPid() == 0, "Join and retain the complete fragmented MP4");
  const auto joined = process.resultHandle();
  require(joined && joined->size() > prefix.size() && joined->seek(0) && joined->read(prefix.size()) == prefix,
      "Keep the already published prefix unchanged through final output");
  require(QFile::copy(process.resultFile(), QString::fromLocal8Bit(argv[5])), "Export the joined fixture for FFprobe");
  std::cout << "PASS real fragmented MP4 prefix before EOF and unchanged prefix after process join\n";
  return 0;
}

int main(int argc,char** argv){
  QCoreApplication application(argc,argv);
  if(argc > 1 && QByteArrayView(argv[1]) == "-nostdin"){return fixtureChild(argc, argv);}
  if(argc != 1 && argc != 5 && !(argc == 6 && QByteArrayView(argv[1]) == "--fragments")){return 2;}
  try{
    if(argc == 6){return progressiveCase(argv);}
    if(argc == 1){fixtureCases();return 0;}
    FfmpegJob job;
    job.program={QString::fromLocal8Bit(argv[1]),QByteArray::fromHex(argv[2])};
    job.geometry=TranscodeVideoGeometry{80,48,0,0,std::nullopt};
    job.output=FfmpegContainer::Mp4;
    job.deadline=8s;
    auto wrong=job;
    wrong.program.sha256[0]^=1;
    QFile input(QString::fromLocal8Bit(argv[3]));
    require(input.open(QIODevice::ReadOnly)&&input.size()<64*1024,"generated input");
    const auto all=input.readAll();
    QString internalPath;
    {
      FfmpegProcess process;
      require(!process.start(wrong)&&process.childPid()==0,"foreign program was started");
      require(bool(process.start(job)),"actual FFmpeg start request");
      require(waitFor([&]{return process.state()==FfmpegJobState::Running||terminal(process.state());})&&
          process.state()==FfmpegJobState::Running&&process.childPid()>0,"actual FFmpeg child");
      require(process.push(QByteArray(65537,'x'))<0,"input chunk bound");
      require(process.push(all)==all.size()&&process.finishInput(),"valid media input");
      require(waitFor([&]{return terminal(process.state());},10000),"actual transcode terminal timeout");
      if(process.state()!=FfmpegJobState::Produced){
        std::cerr<<process.diagnostics().toStdString()<<'\n';
        throw std::runtime_error("actual process failed to produce typed output");
      }
      require(process.childPid()==0&&process.progress()&&process.progress()->frames==24,"joined media output values");
      internalPath=process.resultFile();
      require(!internalPath.isEmpty()&&QFile::copy(internalPath,QString::fromLocal8Bit(argv[4])),"generated output export");
    }
    require(!QFile::exists(internalPath),"private job output directory survived ownership");
    {
      FfmpegProcess process;
      require(bool(process.start(job)),"cancellable child start request");
      require(waitFor([&]{return process.state()==FfmpegJobState::Running;})&&process.childPid()>0,"live child absent");
      require(process.cancel(),"actual cancellation rejected");
      require(waitFor([&]{return terminal(process.state());})&&process.state()==FfmpegJobState::Cancelled&&
          process.childPid()==0&&process.resultFile().isEmpty(),"unjoined or published cancellation");
    }
    {
      FfmpegProcess process;
      auto shortJob=job;shortJob.deadline=1000ms;
      require(bool(process.start(shortJob)),"timeout child start request");
      require(waitFor([&]{return process.state()==FfmpegJobState::Running;})&&process.childPid()>0,"live timed child absent");
      require(waitFor([&]{return terminal(process.state());})&&process.state()==FfmpegJobState::TimedOut&&
          process.childPid()==0&&process.resultFile().isEmpty(),"live timed child survived");
    }
    {
      FfmpegProcess process;
      require(bool(process.start(job)),"invalid input child start request");
      require(waitFor([&]{return process.state()==FfmpegJobState::Running;})&&process.childPid()>0,"invalid input child");
      require(process.push("not media")==9&&process.finishInput(),"invalid input fixture");
      require(waitFor([&]{return terminal(process.state());})&&process.state()==FfmpegJobState::Failed&&
          process.childPid()==0&&process.resultFile().isEmpty()&&!process.diagnostics().isEmpty(),"invalid input accepted");
    }
    std::cout<<"PASS digest-pinned actual FFmpeg process, media progress, output cleanup, failure, cancellation and timeout\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "recheck_fixture.hpp"
#include <QElapsedTimer>
#include <QtTest>
#include <sys/stat.h>
#include <fcntl.h>
#include <chrono>
#include <algorithm>
namespace {
using namespace biglybtc;
namespace f=test::recheck;
constexpr qsizetype Piece=65536;
struct Library {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-check-owned-XXXXXX")};
  QTemporaryDir other;
  QString profile;
  std::vector<core::TorrentHash> hashes;
  std::vector<QString> files;
  std::vector<qint64> sizes;
  std::optional<storage::ProfileLease> lease;
  Library(bool different,bool concurrent,int maximum):other(different?QStringLiteral("/dev/shm/biglybtc-check-owned-XXXXXX"):QStringLiteral("/tmp/biglybtc-check-secondary-XXXXXX")) {
    f::require(root.isValid()&&other.isValid());profile=root.path()+"/profile";
    f::require(QDir().mkpath(profile+"/active"));QByteArray rows="l";
    const QByteArray piece(Piece,'s');const auto digest=QCryptographicHash::hash(piece,QCryptographicHash::Sha1);
    for(int index=0;index<2;++index){
      const qint64 size=index==0?128*1024*1024:16*1024*1024;sizes.push_back(size);
      const auto directory=index==0||!different?root.path()+"/payload":other.path();
      f::require(QDir().mkpath(directory));const auto name=QStringLiteral("checked-%1.bin").arg(index);const auto file=directory+'/'+name;files.push_back(file);
      QFile output(file);f::require(output.open(QIODevice::WriteOnly));const auto chunk=piece.repeated(16);
      for(qint64 offset=0;offset<size;offset+=chunk.size()){f::require(output.write(chunk)==chunk.size());}f::require(output.flush());output.close();
      const auto info=f::dict({{"length","i"+QByteArray::number(size)+"e"},{"name",f::bytes(name.toUtf8())},{"piece length","i65536e"},{"pieces",f::bytes(digest.repeated(static_cast<qsizetype>(size/Piece)))}});
      auto hash=core::TorrentHash::parse(QCryptographicHash::hash(info,QCryptographicHash::Sha1).toStdString());f::require(bool(hash));hashes.push_back(*hash);
      const auto state=profile+"/active/"+QString::fromStdString(hash->hex())+".dat";
      const auto attrs=f::dict({{"networks","l6:Publice"},{"storetypes","l1:Le"}});
      f::write(state,f::dict({{"info",info},{"attributes",attrs},{"resume",f::dict({{"data",f::dict({{"valid","i1e"},{"blocks","de"},{"resume data",f::bytes(QByteArray(static_cast<qsizetype>(size/Piece),'\1'))}})}})}}));
      rows+=f::dict({{"torrent_hash",f::bytes(QByteArray(reinterpret_cast<const char*>(hash->bytes().data()),20))},{"torrent",f::bytes(state.toUtf8())},{"save_dir",f::bytes(directory.toUtf8())},{"save_file",f::bytes(name.toUtf8())},{"state","i70e"},{"position","i"+QByteArray::number(index+1)+"e"},{"persistent","i1e"},{"forceStart","i0e"},{"downloaded","i0e"},{"uploaded","i0e"},{"file_priorities","li1ee"}});
    }
    rows+='e';f::write(profile+"/downloads.config",f::dict({{"downloads",rows}}));
    f::write(profile+"/biglybt.config",f::dict({{"diskmanager.hashchecking.maxactive","i"+QByteArray::number(maximum)+"e"},{"diskmanager.one.op.per.fs","i1e"},{"diskmanager.one.op.per.fs.conc.read",concurrent?"i1e":"i0e"},{"diskmanager.hashchecking.smallestfirst","i0e"}}));
    auto held=storage::ProfileLease::acquire(profile.toStdString());f::require(bool(held));lease.emplace(std::move(*held));
  }
};
}
class CheckSchedulerOwnerTests final:public QObject {
  Q_OBJECT
private slots:
  void originalResumeAvoidsHashReadsAndKeepsCleanPartialMaps_data(){
    QTest::addColumn<QString>("mode");QTest::addColumn<bool>("reads");QTest::addColumn<bool>("complete");
    QTest::newRow("original-complete")<<QStringLiteral("complete")<<false<<true;
    QTest::newRow("original-partial")<<QStringLiteral("partial")<<false<<false;
    QTest::newRow("invalid-resume-falls-back")<<QStringLiteral("invalid")<<true<<true;
    QTest::newRow("changed-data-falls-back")<<QStringLiteral("changed")<<true<<false;
  }
  void originalResumeAvoidsHashReadsAndKeepsCleanPartialMaps(){
    QFETCH(QString,mode);QFETCH(bool,reads);QFETCH(bool,complete);Library library(false,false,0);
    const auto path=library.profile+"/active/"+QString::fromStdString(library.hashes[0].hex())+".dat";
    auto metadata=core::Document::parse(f::read(path).toStdString(),storage::compatibleConfigLimits());QVERIFY(metadata);
    QByteArray bitmap(static_cast<qsizetype>(library.sizes[0]/Piece),'\1');if(mode=="partial"){for(qsizetype i=bitmap.size()/2;i<bitmap.size();++i){bitmap[i]=0;}}
    const auto resume=f::dict({{"data",f::dict({{"valid",mode=="invalid"?"i0e":"i1e"},{"resume data",f::bytes(bitmap)},{"blocks","de"}})}});
    QVERIFY(metadata->replaceValue("resume",resume.toStdString()));f::write(path,QByteArray::fromStdString(metadata->serialize()));
    if(mode=="changed"){QTest::qWait(2);QFile data(library.files[0]);QVERIFY(data.open(QIODevice::ReadWrite));QCOMPARE(data.write("x",1),qint64{1});QVERIFY(data.flush());data.close();}
    auto created=engine::PersistentSession::create({3201,3202,3203});QVERIFY(created);auto owner=std::move(*created);
    engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;policy.enforceBind=true;
    QVERIFY(owner->configureNetwork(policy));QVERIFY(owner->configureMainlineBootstrap({}));QVERIFY(owner->startIdle(*library.lease));
    const auto started=owner->startManaged(*library.lease,engine::ManagedDownloadOptions{.hash=library.hashes[0]});QVERIFY2(started.has_value(),started?"":started.error().message.c_str());
    bool checked=false;std::uint64_t sequence=0;QElapsedTimer elapsed;elapsed.start();
    while(elapsed.elapsed()<30000){
      QVERIFY(owner->poll());const auto statuses=owner->managedDownloads();QCOMPARE(statuses.size(),std::size_t{1});QVERIFY(!statuses[0].failure);
      if(statuses[0].checked){QCOMPARE(statuses[0].complete,complete);if(mode=="partial"){QCOMPARE(statuses[0].completed,library.sizes[0]/2);}checked=true;const auto metrics=owner->diagnostics();sequence=metrics?metrics->sequence:0;break;}
      QTest::qWait(1);
    }
    QVERIFY(checked);std::optional<std::int64_t> operations;
    while(elapsed.elapsed()<30000){
      QVERIFY(owner->poll());const auto metrics=owner->diagnostics();
      if(metrics&&metrics->sequence>=sequence+2){for(const auto& metric:metrics->metrics){if(metric.name=="disk.num_read_ops"){operations=metric.value;}}break;}
      QTest::qWait(2);
    }
    QVERIFY(operations);QCOMPARE(*operations>0,reads);
    QVERIFY(owner->stopManaged(library.hashes[0]));
    while(elapsed.elapsed()<30000){QVERIFY(owner->poll());if(!owner->managedDownloads().front().retained){break;}QTest::qWait(2);}
    QVERIFY(!owner->managedDownloads().front().retained);
    const auto saved=core::Document::parse(f::read(path).toStdString(),storage::compatibleConfigLimits());QVERIFY(saved);
    const auto* outer=saved->find("resume");const auto* data=outer?outer->find("data"):nullptr;QVERIFY(data&&data->find("valid")&&data->find("resume data"));QCOMPARE(data->find("valid")->integer,std::int64_t{1});
    if(mode=="partial"){QCOMPARE(QByteArray(data->find("resume data")->bytes.data(),static_cast<qsizetype>(data->find("resume data")->bytes.size())),bitmap);}
    const auto closed=owner->close(std::chrono::steady_clock::now()+std::chrono::seconds{10});QVERIFY(closed.sessionJoined&&!closed.checkpointFailure);
  }
  void restoredMappedAndHybridLibrariesDoNotHashAgain_data(){
    QTest::addColumn<QString>("mode");QTest::addColumn<bool>("valid");
    QTest::newRow("linked-v1-clean")<<QStringLiteral("linked")<<true;
    QTest::newRow("hybrid-clean")<<QStringLiteral("hybrid")<<true;
    QTest::newRow("hybrid-multifile-clean")<<QStringLiteral("hybrid-multifile")<<true;
    QTest::newRow("linked-v1-invalid-must-check")<<QStringLiteral("linked")<<false;
    QTest::newRow("linked-v1-changed-must-check")<<QStringLiteral("linked-changed")<<true;
  }
  void restoredMappedAndHybridLibrariesDoNotHashAgain(){
    QFETCH(QString,mode);QFETCH(bool,valid);f::Fixture fixture(mode.startsWith("linked")?QStringLiteral("linked"):mode);
    if(mode=="hybrid-multifile"){
      const QByteArray first(17001,'a'),last(50003,'b');f::write(fixture.payload+"/bundle/first.bin",first);f::write(fixture.payload+"/bundle/last.bin",last);
      libtorrent::file_storage files;files.add_file("bundle/first.bin",first.size());files.add_file("bundle/last.bin",last.size());libtorrent::create_torrent creator(files,16384);libtorrent::error_code ec;libtorrent::set_piece_hashes(creator,fixture.payload.toStdString(),ec);QVERIFY(!ec);
      std::string encoded;libtorrent::bencode(std::back_inserter(encoded),creator.generate());auto doc=core::Document::parse(encoded);QVERIFY(doc);const auto* info=doc->find("info");QVERIFY(info&&info->find("pieces")&&info->find("files"));
      const auto infoBytes=QByteArray::fromStdString(doc->originalBytes().substr(info->begin,info->end-info->begin));const auto hash=core::TorrentHash::parse(QCryptographicHash::hash(infoBytes,QCryptographicHash::Sha1).toStdString());QVERIFY(hash);
      const auto pieces=info->find("pieces")->bytes.size()/20;QByteArray types="l";for(std::size_t i=0;i<info->find("files")->children.size();++i){types+="1:L";}types+='e';
      QVERIFY(doc->replaceValue("attributes",f::dict({{"networks","l6:Publice"},{"storetypes",types}}).toStdString()));QVERIFY(doc->replaceValue("resume",f::dict({{"data",f::dict({{"valid","i1e"},{"blocks","de"},{"resume data",f::bytes(QByteArray(static_cast<qsizetype>(pieces),'\1'))}})}}).toStdString()));
      QVERIFY(QFile::remove(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));fixture.hash=*hash;fixture.file=fixture.payload+"/bundle/first.bin";fixture.originalPayload=first;
      const auto row=f::dict({{"torrent_hash",f::bytes(QByteArray(reinterpret_cast<const char*>(fixture.hash.bytes().data()),20))},{"torrent",f::bytes((fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat").toUtf8())},{"save_dir",f::bytes(fixture.payload.toUtf8())},{"save_file",f::bytes("bundle")},{"state","i70e"}});
      f::write(fixture.profile+"/downloads.config",f::dict({{"downloads","l"+row+"e"}}));f::write(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat",QByteArray::fromStdString(doc->serialize()));
    }
    const auto path=fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat";
    if(mode=="linked-changed"){
      auto corrupt=fixture.originalPayload;corrupt[0]^=0x55;f::write(fixture.file,corrupt);fixture.originalPayload=corrupt;
      struct stat payload{};QVERIFY(::stat(fixture.file.toUtf8().constData(),&payload)==0);const timespec times[2]{payload.st_atim,{payload.st_mtim.tv_sec-1,payload.st_mtim.tv_nsec}};
      QVERIFY(::utimensat(AT_FDCWD,path.toUtf8().constData(),times,0)==0);
    }
    if(!valid){auto doc=core::Document::parse(f::read(path).toStdString());QVERIFY(doc);
      const auto resume=f::dict({{"data",f::dict({{"valid","i0e"},{"resume data",f::bytes(QByteArray(3,'\2'))},{"blocks","de"}})}});
      QVERIFY(doc->replaceValue("resume",resume.toStdString()));f::write(path,QByteArray::fromStdString(doc->serialize()));}
    for(int reopen=0;reopen<2;++reopen){
      auto made=engine::PersistentSession::create({4101+static_cast<std::uint64_t>(reopen),4201,4301});QVERIFY(made);auto owner=std::move(*made);
      engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;
      QVERIFY(owner->configureNetwork(policy));QVERIFY(owner->configureMainlineBootstrap({}));QVERIFY(owner->startIdle(*fixture.lease));
      const auto start=owner->startManaged(*fixture.lease,{fixture.hash});QVERIFY2(start,start?"":start.error().message.c_str());
      QElapsedTimer time;time.start();engine::ManagedDownloadStatus status{fixture.hash};
      while(time.elapsed()<10000){QVERIFY(owner->poll());status=owner->managedDownloads().front();QVERIFY2(!status.failure,status.failure?status.failure->message.c_str():"");if(status.checked){break;}QTest::qWait(2);}
      QVERIFY(status.checked);QCOMPARE(status.complete,mode!=QStringLiteral("linked-changed"));
      if(mode.startsWith("linked")){QCOMPARE(status.storageHashOperations>0,(!valid||mode=="linked-changed")&&reopen==0);}
      else {bool observed=false;const auto initial=owner->diagnostics();const auto sequence=initial?initial->sequence:0;
        while(time.elapsed()<10000){QVERIFY(owner->poll());const auto metrics=owner->diagnostics();if(metrics&&metrics->sequence>=sequence+2){for(const auto& metric:metrics->metrics){if(metric.name=="disk.num_read_ops"){QCOMPARE(metric.value,std::int64_t{0});observed=true;}}if(observed){break;}}QTest::qWait(5);}QVERIFY(observed);
      }
      const auto closed=owner->close(std::chrono::steady_clock::now()+std::chrono::seconds{10});QVERIFY(closed.sessionJoined&&!closed.checkpointFailure);
      auto saved=core::Document::parse(f::read(path).toStdString());QVERIFY(saved);QCOMPARE(saved->find("resume")->find("data")->find("valid")->integer,std::int64_t{1});
    }
    QCOMPARE(f::read(fixture.file),fixture.originalPayload);
  }
  void actualChecksUseOriginalFilesystemAndGlobalLimits_data(){
    QTest::addColumn<bool>("different");QTest::addColumn<bool>("concurrent");QTest::addColumn<int>("maximum");QTest::addColumn<bool>("parallel");QTest::addColumn<bool>("cancel");
    QTest::newRow("same-filesystem-serial")<<false<<false<<0<<false<<false;
    QTest::newRow("different-filesystems-parallel")<<true<<false<<0<<true<<false;
    QTest::newRow("concurrent-read-opt-out")<<false<<true<<0<<true<<false;
    QTest::newRow("global-one-overrides-different-filesystems")<<true<<false<<1<<false<<false;
    QTest::newRow("cancel-drains-before-next-check")<<false<<false<<0<<false<<true;
  }
  void actualChecksUseOriginalFilesystemAndGlobalLimits(){
    QFETCH(bool,different);QFETCH(bool,concurrent);QFETCH(int,maximum);QFETCH(bool,parallel);QFETCH(bool,cancel);
    Library library(different,concurrent,maximum);struct stat before[2]{};for(int i=0;i<2;++i){QVERIFY(::stat(library.files[i].toUtf8().constData(),&before[i])==0);}
    QCOMPARE(before[0].st_dev!=before[1].st_dev,different);
    auto created=engine::PersistentSession::create({3101,3102,3103});QVERIFY(created);auto owner=std::move(*created);
    engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;policy.enforceBind=true;
    QVERIFY(owner->configureNetwork(policy));QVERIFY(owner->configureMainlineBootstrap({}));QVERIFY(owner->startIdle(*library.lease));
    for(const auto& hash:library.hashes){const auto started=owner->startManagedRecheck(*library.lease,hash);QVERIFY2(started.has_value(),started?"":started.error().message.c_str());}
    bool sawFirstInProgress=false,sawParallel=false,finished=false,cancelled=false;QElapsedTimer elapsed;elapsed.start();
    while(elapsed.elapsed()<30000){
      QVERIFY(owner->poll());const auto statuses=owner->managedDownloads();QCOMPARE(statuses.size(),std::size_t{2});
      const auto find=[&](int index)->const engine::ManagedDownloadStatus&{return *std::ranges::find(statuses,library.hashes[index],&engine::ManagedDownloadStatus::hash);};
      const auto& a=find(0);const auto& b=find(1);QVERIFY(a.recheck&&b.recheck);QVERIFY(!a.failure&&!b.failure);
      if(a.retained&&a.recheck->scannedBytes>0&&a.recheck->scannedBytes<library.sizes[0]){
        sawFirstInProgress=true;if(b.recheck->scannedBytes>0){sawParallel=true;}
        if(!parallel){QCOMPARE(b.recheck->scannedBytes,qint64{0});}
        if(cancel&&!cancelled){QVERIFY(owner->cancelManagedRecheck(library.hashes[0]));cancelled=true;}
      }
      if(!a.retained&&!b.retained){
        QCOMPARE(a.recheck->phase,cancel?engine::ManagedRecheckPhase::Cancelled:engine::ManagedRecheckPhase::Finished);QCOMPARE(b.recheck->phase,engine::ManagedRecheckPhase::Finished);
        if(!cancel){QCOMPARE(a.recheck->goodPieces,static_cast<int>(library.sizes[0]/Piece));}QCOMPARE(b.recheck->goodPieces,static_cast<int>(library.sizes[1]/Piece));
        QCOMPARE(a.recheck->pieceMapSaved,!cancel);QVERIFY(b.recheck->pieceMapSaved);finished=true;break;
      }
      QTest::qWait(1);
    }
    QVERIFY(finished);QVERIFY(sawFirstInProgress);QCOMPARE(sawParallel,parallel);
    for(int i=0;i<2;++i){struct stat after{};QVERIFY(::stat(library.files[i].toUtf8().constData(),&after)==0);QCOMPARE(after.st_size,before[i].st_size);QCOMPARE(after.st_mtim.tv_sec,before[i].st_mtim.tv_sec);QCOMPARE(after.st_mtim.tv_nsec,before[i].st_mtim.tv_nsec);}
    const auto closed=owner->close(std::chrono::steady_clock::now()+std::chrono::seconds{10});QVERIFY(closed.sessionJoined&&!closed.checkpointFailure);
  }
};
QTEST_GUILESS_MAIN(CheckSchedulerOwnerTests)
#include "check_scheduler_owner_tests.moc"

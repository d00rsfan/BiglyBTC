// SPDX-License-Identifier: GPL-2.0-or-later
#include "persistent_session.hpp"
#include "managed_storage.hpp"
#include "managed_payload_counters.hpp"
#include "file_merge_io.hpp"
#include "file_channel_piece_window.hpp"
#include "torrent_creator.hpp"
#include "original_torrent_identity_lt.hpp"
#include "biglybtc/torrent_layout.hpp"
#include "owned_engine_fixture.hpp"
#include <QtTest>
#include <atomic>
#include <future>
#include <thread>
using namespace biglybtc;
using namespace std::chrono_literals;
namespace {
core::Document parse(const QByteArray& raw){auto doc=core::Document::parse(raw.toStdString(),storage::compatibleConfigLimits());test::require(bool(doc),"Parse generated merge document");return std::move(*doc);}
std::string b(std::string_view v){return std::to_string(v.size())+":"+std::string(v);}
struct Fixture {
 QTemporaryDir root;QString profile;std::vector<core::TorrentHash> hashes;std::vector<QString> states,paths;QByteArray content;std::vector<int> counts;std::optional<storage::ProfileLease> lease;bool v2Hybrid=false;
 Fixture(engine::CreateFormat format,bool mismatch=false,bool completeSource=false,bool skew=false,bool selectedV2=false):profile(root.path()+"/profile"),content(8*32768+37,'\0'){
  v2Hybrid=selectedV2;test::require(root.isValid()&&QDir().mkpath(profile+"/active"),"Own merge profile");for(qsizetype n=0;n<content.size();++n){content[n]=char((n*31+n/32768*17+7)%251);}std::string rows;
  for(int owner=0;owner<2;++owner){const auto save=root.path()+QStringLiteral("/payload%1").arg(owner),path=save+(skew?"/bundle/b-shared.bin":"/sample.bin"),metadata=root.path()+QStringLiteral("/source%1.torrent").arg(owner);auto all=content;if(mismatch&&owner==0){for(auto& ch:all){ch^=0x55;}}test::write(path,all);const int prefixBytes=owner?33003:17001;if(skew){test::write(save+"/bundle/a-prefix.bin",QByteArray(prefixBytes,owner?'Q':'P'));}
   engine::CreateOptions options;options.source=(skew?save+"/bundle":path).toStdString();options.output=metadata.toStdString();options.format=format;options.sourceTag=owner?"merge-target":"merge-source";options.pieceSize=32768;options.privateTorrent=true;
   const auto created=engine::createTorrent(options);test::require(bool(created),created?"":created.error().message.c_str());auto doc=parse(test::read(metadata));if(selectedV2){test::require(bool(doc.replaceValue("azureus_private_properties","d14:hybrid_hash_v2i1ee")),"Original hybrid-v2 selector");}libtorrent::error_code ec;libtorrent::torrent_info ti(metadata.toStdString(),ec);test::require(!ec,"Generated metadata parses");auto identity=storage::originalTorrentIdentityFromLibtorrent(doc,ti);test::require(bool(identity),"Original merge identity");hashes.push_back(identity->originalKey);counts.push_back(ti.num_pieces());
   auto layout=compat::originalLayout(doc);test::require(bool(layout),"Merge fixture geometry");std::string stores="l";for(const auto& file:layout->files){(void)file;stores+="1:L";}stores+='e';
   const auto attrs="d"+b("mergedata")+"i7e"+b("networks")+"l6:Publice"+b("opaque.merge")+"3:raw"+b("resumecomplete")+(completeSource&&owner==0?"i2e":"i1e")+b("storetypes")+stores+"e";
   test::require(bool(doc.replaceValue("attributes",attrs)),"Merge attributes");std::string bitmap(std::size_t(ti.num_pieces()),'\0');QByteArray physical(all.size(),'\0');for(int piece=0;piece<ti.num_pieces();++piece){const bool have=skew?(owner==0||piece<(format==engine::CreateFormat::V1?prefixBytes/32768:(prefixBytes+32767)/32768)):((completeSource&&owner==0)||(owner==0?piece<4:piece>=4));if(have){bitmap[std::size_t(piece)]='\1';const int offset=piece*32768,length=std::min<int>(32768,int(all.size())-offset);if(length>0){std::copy_n(all.data()+offset,length,physical.data()+offset);}}}
   test::require(bool(doc.replaceValue("resume","d4:datad6:blocksde11:resume data"+b(bitmap)+"5:validi1eee")),"Merge resume");const auto state=profile+"/active/"+QString::fromStdString(hashes.back().hex())+".dat";test::write(state,QByteArray::fromStdString(doc.serialize()));states.push_back(state);paths.push_back(path);if(skew){physical=owner?QByteArray(all.size(),'\0'):all;}test::write(path,physical);test::write(state,QByteArray::fromStdString(doc.serialize()));
   const auto raw=hashes.back().bytes();const std::string hash(reinterpret_cast<const char*>(raw.data()),raw.size());rows+="d"+b("torrent_hash")+b(hash)+b("torrent")+b(state.toStdString())+b("save_dir")+b(save.toStdString())+b("save_file")+b(skew?"bundle":"sample.bin")+b("persistent")+"i1e"+b("state")+"i75e"+b("position")+"i"+std::to_string(owner+1)+"e"+b("downloaded")+"i0e"+b("uploaded")+"i0e"+"e";
  }test::write(profile+"/downloads.config",QByteArray::fromStdString("d9:downloadsl"+rows+"ee"));test::write(profile+"/tables.config","de");auto held=storage::ProfileLease::acquire(std::filesystem::path(profile.toStdString()));test::require(bool(held),"Hold merge profile");lease.emplace(std::move(*held));
 }
};
std::unique_ptr<engine::PersistentSession> owner(Fixture& f,bool enabled=true,bool extended=false){auto result=engine::PersistentSession::create({701,702,703});test::require(bool(result),"Qualified merge owner");auto service=std::move(*result);engine::NetworkPolicy policy;policy.mode=f.v2Hybrid?engine::NetworkMode::IsolatedLoopback:engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;test::require(bool(service->configureNetwork(policy)),"Loopback-bound existing-library policy without external discovery");core::FileMergeSettings settings;settings.minPieces=1;settings.enabled=enabled;settings.extended=extended;test::require(bool(service->configureFileMerge(settings))&&bool(service->startIdle(*f.lease)),"Merge owner startup");for(const auto& hash:f.hashes){auto started=service->startManaged(*f.lease,{hash,0,true,false});test::require(bool(started),started?"":started.error().message.c_str());}return service;}
std::string diagnostic(engine::PersistentSession& s){const auto m=s.fileMergeStatus();std::string out=m.state+" candidates="+std::to_string(m.candidates)+" pending="+std::to_string(m.pending)+" supplied="+std::to_string(m.submittedBytes)+" verified="+std::to_string(m.verifiedPieces);for(const auto& r:m.rows){out+=" ["+r.state+" failures="+std::to_string(r.failures)+"]";}for(const auto& r:s.managedDownloads()){out+=" {phase="+std::to_string(int(r.phase))+" pieces="+std::to_string(r.pieces)+"/"+std::to_string(r.totalPieces)+" failure="+(r.failure?r.failure->message:"none")+"}";}return out;}
bool complete(engine::PersistentSession& s){if(!s.poll()){return false;}auto rows=s.managedDownloads();return rows.size()==2&&std::ranges::all_of(rows,[](const auto& row){return row.complete&&!row.failure;});}
}
class FileMergeTests final:public QObject {
 Q_OBJECT
private slots:
 void actualLocalReuse_data(){QTest::addColumn<int>("format");QTest::addColumn<bool>("selectedV2");QTest::newRow("v1")<<int(engine::CreateFormat::V1)<<false;QTest::newRow("pure-v2")<<int(engine::CreateFormat::V2)<<false;QTest::newRow("hybrid")<<int(engine::CreateFormat::Hybrid)<<false;QTest::newRow("hybrid-v2")<<int(engine::CreateFormat::Hybrid)<<true;}
 void actualLocalReuse(){QFETCH(int,format);QFETCH(bool,selectedV2);Fixture f{engine::CreateFormat(format),false,false,false,selectedV2};auto service=owner(f);QTRY_VERIFY2_WITH_TIMEOUT(complete(*service),diagnostic(*service).c_str(),15000);QCOMPARE(test::read(f.paths[0]),f.content);QCOMPARE(test::read(f.paths[1]),f.content);const auto actual=service->fileMergeStatus();QCOMPARE(actual.submittedBytes,std::int64_t(f.content.size()));QVERIFY(actual.verifiedPieces>0);for(const auto& row:service->managedDownloads()){QCOMPARE(row.received,std::int64_t{0});QCOMPARE(row.sent,std::int64_t{0});QVERIFY(row.mergedBytes&&*row.mergedBytes>7);}
  const auto closed=service->close(std::chrono::steady_clock::now()+10s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(closed.retirement.peerCounters);QCOMPARE(closed.retirement.peerCounters->counters().payloadReceived,std::int64_t{0});QCOMPARE(closed.retirement.peerCounters->counters().payloadSent,std::int64_t{0});service.reset();for(int n=0;n<2;++n){const auto saved=parse(test::read(f.states[n]));QCOMPARE(saved.find("attributes")->find("opaque.merge")->bytes,std::string("raw"));QVERIFY(saved.find("attributes")->find("mergedata")->integer>7);const auto* data=saved.find("resume")->find("data");QCOMPARE(data->find("valid")->integer,std::int64_t{1});QVERIFY(std::ranges::all_of(data->find("resume data")->bytes,[](char b){return b==1;}));}
  auto reopened=owner(f);QTRY_VERIFY_WITH_TIMEOUT(complete(*reopened),10000);QCOMPARE(reopened->fileMergeStatus().submittedBytes,std::int64_t{0});QVERIFY(reopened->close(std::chrono::steady_clock::now()+10s).sessionJoined);
 }
 void skewedFileBoundaries_data(){actualLocalReuse_data();}
 void skewedFileBoundaries(){QFETCH(int,format);QFETCH(bool,selectedV2);Fixture f{engine::CreateFormat(format),false,true,true,selectedV2};auto service=owner(f,true,true);QTRY_VERIFY2_WITH_TIMEOUT(complete(*service),diagnostic(*service).c_str(),15000);QCOMPARE(test::read(f.paths[1]),f.content);QCOMPARE(test::read(f.paths[0]),f.content);QVERIFY(service->fileMergeStatus().submittedBytes>0);QVERIFY(service->close(std::chrono::steady_clock::now()+10s).sessionJoined);}
 void completedSourcesRequireExtended(){Fixture f{engine::CreateFormat::V1,false,true};auto service=owner(f);const auto checked=[&]{if(!service->poll()){return false;}auto rows=service->managedDownloads();return rows.size()==2&&std::ranges::all_of(rows,[](const auto& r){return r.checked;});};QTRY_VERIFY_WITH_TIMEOUT(checked(),10000);QCOMPARE(service->fileMergeStatus().submittedBytes,std::int64_t{0});core::FileMergeSettings settings;settings.minPieces=1;settings.extended=true;QVERIFY(service->configureFileMerge(settings));QTRY_VERIFY2_WITH_TIMEOUT(complete(*service),diagnostic(*service).c_str(),15000);QVERIFY(service->close(std::chrono::steady_clock::now()+10s).sessionJoined);}
 void mismatchNeverWrites(){Fixture f{engine::CreateFormat::V1,true};const auto before0=test::read(f.paths[0]),before1=test::read(f.paths[1]);auto service=owner(f);const auto rejected=[&]{if(!service->poll()){return false;}return service->fileMergeStatus().rejectedPieces>=3;};QTRY_VERIFY_WITH_TIMEOUT(rejected(),10000);QCOMPARE(service->fileMergeStatus().submittedBytes,std::int64_t{0});QCOMPARE(test::read(f.paths[0]),before0);QCOMPARE(test::read(f.paths[1]),before1);QVERIFY(service->close(std::chrono::steady_clock::now()+10s).sessionJoined);}
 void disabledAndPause(){Fixture f{engine::CreateFormat::V1};auto service=owner(f,false);const auto checked=[&]{if(!service->poll()){return false;}auto rows=service->managedDownloads();return rows.size()==2&&std::ranges::all_of(rows,[](const auto& r){return r.checked;});};QTRY_VERIFY_WITH_TIMEOUT(checked(),10000);QCOMPARE(service->fileMergeStatus().submittedBytes,std::int64_t{0});QVERIFY(service->pauseManaged(f.hashes[1],true));core::FileMergeSettings settings;settings.minPieces=1;QVERIFY(service->configureFileMerge(settings));for(int n=0;n<5;++n){QVERIFY(service->poll());QTest::qWait(30);}QCOMPARE(service->fileMergeStatus().submittedBytes,std::int64_t{0});QVERIFY(service->pauseManaged(f.hashes[1],false));QTRY_VERIFY2_WITH_TIMEOUT(complete(*service),diagnostic(*service).c_str(),15000);QVERIFY(service->close(std::chrono::steady_clock::now()+10s).sessionJoined);}
 void stopRetiresPendingReuse(){Fixture f{engine::CreateFormat::V1};auto service=owner(f);const auto pending=[&]{if(!service->poll()){return false;}return service->fileMergeStatus().pending>0;};QTRY_VERIFY_WITH_TIMEOUT(pending(),10000);QVERIFY(service->stopManaged(f.hashes[1]));core::FileMergeSettings settings;settings.enabled=false;QVERIFY(service->configureFileMerge(settings));const auto retired=[&]{if(!service->poll()){return false;}for(const auto& r:service->managedDownloads()){if(r.hash==f.hashes[1]){return !r.retained&&r.phase==engine::ManagedPhase::Stopped&&!r.failure;}}return false;};QTRY_VERIFY_WITH_TIMEOUT(retired(),10000);QVERIFY(service->forgetManaged(f.hashes[1]));const auto payload=test::read(f.paths[1]);for(int n=0;n<5;++n){QVERIFY(service->poll());QTest::qWait(20);}QCOMPARE(test::read(f.paths[1]),payload);QVERIFY(service->startManagedRecheck(*f.lease,f.hashes[1]));const auto checked=[&]{if(!service->poll()){return false;}for(const auto& r:service->managedDownloads()){if(r.hash==f.hashes[1]){return r.recheck&&r.recheck->phase==engine::ManagedRecheckPhase::Finished;}}return false;};QTRY_VERIFY_WITH_TIMEOUT(checked(),10000);QCOMPARE(test::read(f.paths[1]),payload);QVERIFY(service->close(std::chrono::steady_clock::now()+10s).sessionJoined);}
 void readScopesAndVerifier(){Fixture f{engine::CreateFormat::V1};auto storage=engine::detail::ManagedStorage::prepare(*f.lease,f.hashes[0],{});QVERIFY2(storage,storage?"":storage.error().message.c_str());auto read=storage->prepareMergeRead(*f.lease,0,32768);QVERIFY2(read,read?"":read.error().message.c_str());auto verify=storage->prepareMergeVerifier(0);QVERIFY(verify);auto data=(*read)({});QVERIFY(data);QVERIFY((*verify)(*data));(*data)[0]^=1;QVERIFY(!(*verify)(*data));std::stop_source cancelled;cancelled.request_stop();QVERIFY(!(*read)(cancelled.get_token()));QVERIFY(QFile::rename(f.paths[0],f.paths[0]+".replaced"));test::write(f.paths[0],f.content);QVERIFY(!(*read)({}));}
 void verifiedFileChunk_data(){actualLocalReuse_data();}
 void verifiedFileChunk(){
  QFETCH(int,format);QFETCH(bool,selectedV2);
  Fixture f{engine::CreateFormat(format),false,true,true,selectedV2};
  auto provider=selectedV2?engine::detail::managedPayloadCounterBridge():nullptr;
  QVERIFY(!selectedV2||provider);
  auto source=engine::detail::ManagedStorage::prepare(
      *f.lease,f.hashes[0],{},{},{},std::move(provider));
  QVERIFY2(source,source?"":source.error().message.c_str());
  const auto& files=source->capturedFileLayout().files;
  const auto found=std::ranges::find(files,std::string("b-shared.bin"),&compat::OriginalFile::path);
  QVERIFY(found!=files.end());
  const std::size_t index=static_cast<std::size_t>(found-files.begin());
  const auto read=[&](std::uint64_t offset,std::uint32_t maximum){
    return source->prepareVerifiedFileRead(*f.lease,index,offset,maximum);
  };
  const auto matches=[&](const std::vector<char>& bytes,std::uint64_t offset,std::size_t size){
    return bytes.size()==size&&std::equal(bytes.begin(),bytes.end(),f.content.constData()+offset);
  };
  const auto boundary=static_cast<std::uint64_t>(32768-found->offset%32768);
  auto head=read(boundary-3,64);QVERIFY(head);
  auto headBytes=(*head)({});QVERIFY(headBytes&&matches(*headBytes,boundary-3,3));
  auto next=read(boundary,64);QVERIFY(next);
  auto nextBytes=(*next)({});QVERIFY(nextBytes&&matches(*nextBytes,boundary,64));
  const auto total=static_cast<std::uint64_t>(found->bytes);
  auto tail=read(total-7,64);QVERIFY(tail);
  auto tailBytes=(*tail)({});QVERIFY(tailBytes&&matches(*tailBytes,total-7,7));
  auto end=read(total,64);QVERIFY(end);
  auto eof=(*end)({});QVERIFY(eof&&eof->empty());
  QVERIFY(!read(total+1,64));QVERIFY(!read(0,0));QVERIFY(!read(0,65537));
  QVERIFY(!source->prepareVerifiedFileRead(*f.lease,files.size(),0,64));
  std::stop_source stopped;stopped.request_stop();
  auto cancelled=(*next)(stopped.get_token());
  QVERIFY(!cancelled&&cancelled.error().code==storage::ErrorCode::Cancelled);
  QFile payload(f.paths[0]);QVERIFY(payload.open(QIODevice::ReadWrite));
  QVERIFY(payload.seek(boundary+65));
  const char changed=static_cast<char>(f.content[static_cast<qsizetype>(boundary+65)]^1);
  QCOMPARE(payload.write(&changed,1),qint64{1});QVERIFY(payload.flush());payload.close();
  auto mismatch=(*next)({});
  QVERIFY(!mismatch&&mismatch.error().code==storage::ErrorCode::Missing);
  QVERIFY(QFile::rename(f.paths[0],f.paths[0]+".replaced"));
  test::write(f.paths[0],f.content);
  const auto foreign=(*next)({});
  QVERIFY(!foreign&&foreign.error().code==storage::ErrorCode::Conflict);
 }
 void verifiedFileReadOwner(){
  Fixture f{engine::CreateFormat::V1,false,true};
  auto service=owner(f,false);
  std::uint64_t admission=0;
  const auto current=[&]{
    if(!service->poll()){return false;}
    for(const auto& row:service->managedDownloads()){
      if(row.hash==f.hashes[0]&&row.checked&&row.retained&&!row.failure){
        admission=row.admission;
        return admission!=0;
      }
    }
    return false;
  };
  QTRY_VERIFY_WITH_TIMEOUT(current(),10000);
  const auto& hash=f.hashes[0];
  QVERIFY(!service->requestVerifiedFileRead(hash,admission+1,0,0,64));
  auto foreignThread=std::async(std::launch::async,[&]{
    return bool(service->requestVerifiedFileRead(hash,admission,0,0,64));
  });
  QVERIFY(!foreignThread.get());
  auto first=service->requestVerifiedFileRead(hash,admission,0,32766,64);
  QVERIFY2(first,first?"":first.error().message.c_str());
  const auto backpressure=service->requestVerifiedFileRead(hash,admission,0,0,64);
  QVERIFY(!backpressure&&backpressure.error().code==storage::ErrorCode::Busy);
  QVERIFY(!service->takeVerifiedFileRead(*first,f.hashes[1],admission));
  auto oldOwner=*first;
  ++oldOwner.engine.engineEpoch;
  QVERIFY(!service->takeVerifiedFileRead(oldOwner,hash,admission));
  QVERIFY(!service->cancelVerifiedFileRead(oldOwner,hash,admission));
  QVERIFY(!service->cancelVerifiedFileRead(*first,hash,admission+1));
  std::optional<std::vector<char>> bytes;
  std::optional<storage::Error> failure;
  const auto completed=[&]{
    if(bytes||failure){return true;}
    auto result=service->takeVerifiedFileRead(*first,hash,admission);
    if(!result){failure=result.error();return true;}
    bytes=std::move(*result);
    return bytes.has_value();
  };
  QTRY_VERIFY_WITH_TIMEOUT(completed(),3000);
  QVERIFY2(!failure,failure?failure->message.c_str():"");
  QVERIFY(bytes&&bytes->size()==2&&std::equal(bytes->begin(),bytes->end(),f.content.constData()+32766));
  QVERIFY(!service->takeVerifiedFileRead(*first,hash,admission));
  auto toCancel=service->requestVerifiedFileRead(hash,admission,0,0,64);
  QVERIFY(toCancel&&service->cancelVerifiedFileRead(*toCancel,hash,admission));
  QVERIFY(!service->takeVerifiedFileRead(*toCancel,hash,admission));
  std::optional<engine::PersistentSession::VerifiedFileReadTicket> beforeStop;
  const auto queued=[&]{
    if(beforeStop){return true;}
    const auto job=service->requestVerifiedFileRead(hash,admission,0,4096,64);
    if(!job){return false;}
    beforeStop=*job;
    return true;
  };
  QTRY_VERIFY_WITH_TIMEOUT(queued(),3000);
  QVERIFY(service->stopManaged(hash));
  QVERIFY(!service->takeVerifiedFileRead(*beforeStop,hash,admission));
  const auto closed=service->close(std::chrono::steady_clock::now()+10s);
  QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
 }
 void originalFileChannelPieceWindow(){
  const auto checked=[](auto plan,std::uint32_t first,std::uint32_t count,
      std::int32_t delay,std::uint32_t spacing){
    return plan&&plan->first==first&&plan->count==count&&plan->delay==delay&&plan->spacing==spacing;
  };
  using core::fileChannelPieceWindow;
  QVERIFY(checked(fileChannelPieceWindow(17001,0,16384,14,0,1200,5,250),1,5,250,1200));
  QVERIFY(checked(fileChannelPieceWindow(17001,49000,16384,14,100000,1000,5,40),4,6,40,166));
  QVERIFY(checked(fileChannelPieceWindow(17001,196608,16384,14,100000,1000,5,5),13,1,5,166));
  QVERIFY(checked(fileChannelPieceWindow(0,32768,16384,14,0,0),2,5,0,60000));
  QVERIFY(checked(fileChannelPieceWindow(0,229376,16384,14,1),14,0,0,0));
  QVERIFY(!fileChannelPieceWindow(UINT64_MAX,1,16384,14,1));
  QVERIFY(!fileChannelPieceWindow(0,0,16384,14,0,UINT32_MAX,5,INT32_MAX));
 }
 void currentFileChannelSeek(){
  Fixture fixture{engine::CreateFormat::V1};
  auto service=owner(fixture,false);
  std::uint64_t admission=0;
  const auto active=[&]{
    if(!service->poll()){return false;}
    for(const auto& row:service->managedDownloads()){
      if(row.hash==fixture.hashes[1]&&row.checked&&row.retained&&!row.failure&&
          row.phase==engine::ManagedPhase::Downloading){
        admission=row.admission;
        return admission!=0;
      }
    }
    return false;
  };
  QTRY_VERIFY_WITH_TIMEOUT(active(),10000);
  const auto& hash=fixture.hashes[1];
  const engine::AccountingIdentity identity{701,702,703};
  engine::PersistentSession::FileChannelSeek request{identity,hash,admission,0,0,0,1200,5,250};
  auto old=request;
  ++old.engine.engineEpoch;
  QVERIFY(!service->seekFileChannel(old));
  QVERIFY(!service->closeFileChannel(old.engine,hash,admission));
  const auto open=service->seekFileChannel(request);
  QVERIFY2(open,open?"":open.error().message.c_str());
  QCOMPARE(open->plannedPieces,std::uint32_t{5});
  QCOMPARE(open->scheduledPieces,std::uint32_t{4});
  const auto previous=service->requestVerifiedFileRead(hash,admission,0,0,64);
  QVERIFY(previous);
  request.offset=3*32768+2;
  const auto seek=service->seekFileChannel(request);
  QVERIFY2(seek,seek?"":seek.error().message.c_str());
  QCOMPARE(seek->firstPiece,std::uint32_t{3});
  QCOMPARE(seek->scheduledPieces,std::uint32_t{1});
  QVERIFY(!service->takeVerifiedFileRead(*previous,hash,admission));
  QVERIFY(service->closeFileChannel(identity,hash,admission));
  request.offset=0;
  const auto reopened=service->seekFileChannel(request);
  QVERIFY(reopened&&reopened->scheduledPieces==open->scheduledPieces);
  QVERIFY(service->pauseManaged(hash,true));
  QVERIFY(!service->seekFileChannel(request));
  const auto closed=service->close(std::chrono::steady_clock::now()+10s);
  QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
 }
 void cancelledIoReleasesBeforeRetirement(){using namespace engine::detail;FileMergeIo worker;std::atomic<bool> started=false,released=false;auto scope=std::shared_ptr<int>(new int(1),[&](int* p){delete p;released=true;});MergeIoJob job{1,{{"generated",1}},4096,[scope,&started](std::stop_token stop)->MergeIoData{started=true;while(!stop.stop_requested()){std::this_thread::sleep_for(1ms);}return std::unexpected(MergeIoFailure{MergeIoFailure::Cancelled,"cancelled"});}};scope.reset();QVERIFY(worker.submit(std::move(job)));QTRY_VERIFY_WITH_TIMEOUT(started.load(),1000);QVERIFY(worker.ownerBusy({"generated",1}));worker.cancelOwner({"generated",1});QTRY_VERIFY_WITH_TIMEOUT(!worker.ownerBusy({"generated",1}),1000);QVERIFY(released.load());auto results=worker.take();QCOMPARE(results.size(),std::size_t{1});QVERIFY(!results[0].data);QCOMPARE(worker.reservedBytes(),std::size_t{0});worker.stopAndJoin();}
};
QTEST_GUILESS_MAIN(FileMergeTests)
#include "file_merge_tests.moc"

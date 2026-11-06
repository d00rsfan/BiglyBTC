// SPDX-License-Identifier: GPL-2.0-or-later
#include "persistent_session.hpp"
#include "network_policy.hpp"
#include "download_history_store.hpp"
#include "durable_completion_receipt.hpp"
#include "encoded_document.hpp"
#include "owned_engine_fixture.hpp"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <future>
#include <thread>
#include <type_traits>
#include <cerrno>
#include <unistd.h>
using namespace biglybtc;using namespace std::chrono_literals;
namespace {
template<class T,class E>T need(std::expected<T,E> v){if(!v){if constexpr(requires{v.error().code;v.error().message;}){throw std::runtime_error("Completion storage "+std::to_string(static_cast<int>(v.error().code))+": "+v.error().message);}else{throw std::runtime_error("Actual completion workflow refused");}}return std::move(*v);}
template<class E>void need(std::expected<void,E> v){if(!v){if constexpr(requires{v.error().code;v.error().message;}){throw std::runtime_error("Completion storage "+std::to_string(static_cast<int>(v.error().code))+": "+v.error().message);}else{throw std::runtime_error("Actual completion workflow refused");}}}
std::string read(const QString& p){return test::read(p).toStdString();}
std::string encode(const core::Node& n){return need(core::encodeValue(n));}
core::Document state(const test::OwnedEngineFixture& f){return need(storage::EncodedDocument::parse(read(f.stateFile))).document();}
core::Document parse(std::string raw){return need(core::Document::parse(std::move(raw)));}
void write(const QString& p,std::string_view s){test::write(p,QByteArray(s.data(),static_cast<qsizetype>(s.size())));}
std::int64_t number(const core::Node* n,std::string_view key){const auto* v=n?n->find(key):nullptr;test::require(v&&v->type==core::Node::Type::Integer,"Actual original completion field absent/wrong type");return v->integer;}
std::int64_t wall(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::int64_t uid(const test::OwnedEngineFixture& f){return core::originalDownloadHistoryUid(std::string_view(reinterpret_cast<const char*>(f.hash->bytes().data()),f.hash->bytes().size()),1900);}
class PublicSeedTracker final:public QTcpServer {
public:
  explicit PublicSeedTracker(unsigned short seed){test::require(listen(QHostAddress::LocalHost,0),"Actual owned tracker listen");
    connect(this,&QTcpServer::newConnection,this,[this,seed,count=replies_]{while(hasPendingConnections()){
      auto* socket=nextPendingConnection();auto input=std::make_shared<QByteArray>();auto answered=std::make_shared<bool>(false);
      connect(socket,&QTcpSocket::readyRead,socket,[socket,input,answered,seed,count]{
        if(*answered){return;}*input+=socket->readAll();if(input->size()>16384){*answered=true;socket->abort();return;}if(!input->contains("\r\n\r\n")){return;}*answered=true;
        if(!input->startsWith("GET /announce?")||!input->contains("info_hash=")){socket->abort();return;}
        ++*count;
        QByteArray peers;peers.append(char(127));peers.append(char(0));peers.append(char(0));peers.append(char(1));peers.append(static_cast<char>(seed>>8));peers.append(static_cast<char>(seed&255));
        const QByteArray body=QByteArray("d8:intervali60e5:peers6:")+peers+'e';const QByteArray reply=QByteArray("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: ")+QByteArray::number(body.size())+"\r\n\r\n"+body;socket->write(reply);socket->disconnectFromHost();
      });connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    }});
  }
  std::string url()const{return "http://127.0.0.1:"+std::to_string(serverPort())+"/announce";}
  unsigned replies()const noexcept{return *replies_;}
private:
  std::shared_ptr<unsigned> replies_=std::make_shared<unsigned>(0);
};
void inputs(test::OwnedEngineFixture& f,bool history=true,bool enabled=true,bool mixed=false,std::string_view tracker={}){
  auto d=state(f);auto a=parse(encode(*d.find("attributes")));need(a.replaceValue("stats.download.added.time","i1900e"));need(a.replaceValue("complt","i-1e"));need(a.replaceValue("parameters","d25:stats.download.added.timei123e29:stats.download.completed.timei9876ee"));
  if(mixed){need(a.replaceValue("networks","l6:Public3:I2Pe"));}need(d.replaceValue("attributes",a.serialize()));if(!tracker.empty()){need(d.replaceValue("announce",std::to_string(tracker.size())+':'+std::string(tracker)));}write(f.stateFile,d.serialize());
  libtorrent::entry settings(libtorrent::entry::dictionary_type{});settings["Download History Enabled"]=enabled?1:0;write(f.profile+"/biglybt.config",test::encoded(settings));
  if(history){libtorrent::entry h(libtorrent::entry::dictionary_type{}),r(libtorrent::entry::dictionary_type{});r["u"]=uid(f);r["h"]=std::string(reinterpret_cast<const char*>(f.hash->bytes().data()),f.hash->bytes().size());r["n"]="Original name";r["s"]="/original/history-only-location";r["z"]=test::OwnedEngineFixture::PayloadBytes;r["a"]=123;r["c"]=0;r["r"]=0;r["t"]=libtorrent::entry::list_type{libtorrent::entry("Original manual tag"),libtorrent::entry("Original manual tag")};r["future-row"]=std::string("a\0b",3);h["records"]=libtorrent::entry::list_type{r};h["future-root"]="keep";write(f.profile+"/dlhistorya.config",test::encoded(h));}
}
struct Workflow {
  test::OwnedEngineFixture& f;storage::ProfileLease lease;std::unique_ptr<engine::PersistentSession> service;
  explicit Workflow(test::OwnedEngineFixture& fixture,bool mixed=false,bool history=true):f(fixture),lease(need(storage::ProfileLease::acquire(f.native()))),service(need(engine::PersistentSession::create({91,92,93}))){engine::NetworkPolicy policy;if(mixed){policy.mode=engine::NetworkMode::Normal;policy.incomingTcp=false;policy.httpTrackers=true;policy.enforceBind=true;}need(service->configureNetwork(policy));if(mixed){need(service->configureMixedNetworks());}if(history){need(service->configureHistoryCompletions(lease));}}
  std::optional<engine::ManagedDownloadStatus> status(){for(const auto& v:service->managedDownloads()){if(v.hash==*f.hash){return v;}}return {};}
  template<class F>void until(F done){const auto end=std::chrono::steady_clock::now()+25s;while(std::chrono::steady_clock::now()<end){need(service->poll());if(auto v=status();v&&v->failure){throw std::runtime_error(v->failure->message);}if(done()){return;}std::this_thread::sleep_for(20ms);}throw std::runtime_error("Actual completion/retirement deadline");}
  void download(bool mixed=false,unsigned short peer=0){need(service->startManaged(lease,{.hash=*f.hash,.ownedLoopbackPeerPort=mixed?static_cast<unsigned short>(0):peer?peer:f.port(),.forceStart=true,.mixedNetworks=mixed}));until([&]{auto v=status();return v&&v->checked&&!v->complete;});test::require(!need(service->takeHistoryCompletion()),"Incomplete observation minted receipt");until([&]{auto v=status();return v&&v->checked&&v->complete;});test::require(!need(service->takeHistoryCompletion()),"Live complete row minted checkpoint receipt");}
  void retire(){need(service->stopManaged(*f.hash));until([&]{auto v=status();return v&&!v->retained;});auto v=status();test::require(v&&!v->failure&&v->phase==engine::ManagedPhase::Stopped,"Actual flush/removal/checkpoint failed");}
  void close(){const auto done=service->close(std::chrono::steady_clock::now()+5s);test::require(done.sessionJoined&&done.retirement.storageReleased&&!done.checkpointFailure,"Actual joined close failed");}
  storage::CompletionHistoryOutcome event(){auto v=need(service->takeHistoryCompletion());test::require(v&&v->receipt&&!v->warning,"Actual durable completion receipt absent");return std::move(*v);}
};
thread_local bool failHistorySync=false,historySyncFailed=false;
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){const auto rc=__real_fsync(fd);if(rc==0&&failHistorySync){std::array<char,4096> path{};const auto n=::readlink(("/proc/self/fd/"+std::to_string(fd)).c_str(),path.data(),path.size());if(n>0&&static_cast<std::size_t>(n)<path.size()&&std::string_view(path.data(),static_cast<std::size_t>(n)).ends_with("/dlhistorya.config.saving")){failHistorySync=false;historySyncFailed=true;errno=EIO;return -1;}}return rc;}
class DownloadHistoryCompletionTests final:public QObject {
  Q_OBJECT
private slots:
  void defaultEngineNeedsNoHistoryDrainAndLateInterestCannotBackfill(){
    test::OwnedEngineFixture f(false);inputs(f);QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);auto work=std::async(std::launch::async,[&]{Workflow w(f,false,false);const auto history=read(f.profile+"/dlhistorya.config");w.download();w.retire();test::require(!need(w.service->takeHistoryCompletion()),"Unsubscribed engine retained optional receipt");auto late=w.service->configureHistoryCompletions(w.lease);test::require(!late&&late.error().code==storage::ErrorCode::Conflict,"Late interest invented past events");
      need(w.service->startManaged(w.lease,{.hash=*f.hash,.forceStart=true}));w.retire();need(w.service->forgetManaged(*f.hash));test::require(!need(w.service->takeHistoryCompletion())&&read(f.profile+"/dlhistorya.config")==history,"Default repeat/forget required auxiliary history or changed it");w.close();});QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(0s)==std::future_status::ready,60000);try{work.get();}catch(const std::exception& e){QFAIL(e.what());}
  }
  void actualObservedTimeSurvivesDelayedRetirement_data(){QTest::addColumn<bool>("mixed");QTest::newRow("Public")<<false;QTest::newRow("Mixed-handle-Public-leg")<<true;}
  void actualObservedTimeSurvivesDelayedRetirement(){
    QFETCH(bool,mixed);if(mixed&&!engine::PersistentSession::mixedNetworksAvailable()){QSKIP("Qualified mixed image unavailable");}static_assert(!std::is_default_constructible_v<storage::DurableCompletionReceipt>&&!std::is_copy_constructible_v<storage::DurableCompletionReceipt>);
    test::OwnedEngineFixture f(false);QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);std::unique_ptr<PublicSeedTracker> tracker;if(mixed){tracker=std::make_unique<PublicSeedTracker>(f.port());}inputs(f,true,true,mixed,tracker?tracker->url():std::string{});
    auto work=std::async(std::launch::async,[&]{Workflow w(f,mixed);auto store=need(storage::DownloadHistoryStore::open(w.lease));const auto before=read(f.profile+"/dlhistorya.config");const auto start=wall();w.download(mixed);const auto completed=wall();std::this_thread::sleep_for(1100ms);w.retire();
      auto original=state(f);const auto* a=original.find("attributes");const auto time=number(a,"complt");test::require(time>=start&&time<=completed&&time<wall()-1000,"Checkpoint replaced original event time");test::require(number(a?a->find("parameters"):nullptr,"stats.download.completed.time")==9876,"First-completed parameter replaced");
      auto out=w.event();auto result=need(store->complete(need(store->reload()),std::move(*out.receipt)));test::require(result.changed,"Completion did not update active history");auto expected=need(core::editSavedDownloadHistory(before,core::DownloadHistorySetCompleteTime{uid(f),time}));test::require(read(f.profile+"/dlhistorya.config")==expected.value.source->originalBytes(),"Completion changed unknown fields/UID/tags/name/location");
      test::require(!need(w.service->takeHistoryCompletion()),"Owner replayed event");test::require(!store->complete(need(store->snapshot()),std::move(*out.receipt)),"Moved receipt replayed");w.close();need(store->close());auto reopened=need(storage::DownloadHistoryStore::open(w.lease));test::require(read(f.profile+"/dlhistorya.config")==expected.value.source->originalBytes()&&f.intact(),"Reopen rewrote history/payload");need(reopened->close());});
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(0s)==std::future_status::ready,60000);try{work.get();}catch(const std::exception& e){QFAIL(e.what());}QVERIFY(!mixed||(tracker&&tracker->replies()>0));
  }
  void cancelStaleForeignMissingDisabledAndNoop_data(){QTest::addColumn<int>("kind");for(int n=0;n<7;++n){QTest::newRow(qPrintable(QString::number(n)))<<n;}}
  void cancelStaleForeignMissingDisabledAndNoop(){
    QFETCH(int,kind);test::OwnedEngineFixture f(false);inputs(f,kind!=4,kind!=5);QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    auto work=std::async(std::launch::async,[&]{Workflow w(f);w.download();w.retire();auto store=need(storage::DownloadHistoryStore::open(w.lease));if(kind==6){const auto time=number(state(f).find("attributes"),"complt");need(store->apply(need(store->reload()),storage::DownloadHistorySide::Active,core::DownloadHistorySetCompleteTime{uid(f),time}));}
      auto out=w.event();auto snapshot=need(store->reload());const bool present=QFile::exists(f.profile+"/dlhistorya.config");const auto before=present?read(f.profile+"/dlhistorya.config"):std::string{};std::stop_source stop;if(kind==0){stop.request_stop();}if(kind==1){auto changed=need(w.lease.sourceAccess(true));}if(kind==2){(void)need(store->reload());}
      if(kind==3){QTemporaryDir foreign;test::require(foreign.isValid(),"Foreign fixture");auto lease=need(storage::ProfileLease::acquire(foreign.path().toStdString()));auto other=need(storage::DownloadHistoryStore::open(lease));test::require(!other->complete(need(other->snapshot()),std::move(*out.receipt)),"Foreign OFD accepted event");need(other->close());}
      else{auto r=store->complete(snapshot,std::move(*out.receipt),stop.get_token());if(kind<=2){test::require(!r,"Cancelled/stale event wrote history");}else{test::require(r&&!r->changed,"Missing/disabled/exact no-op appended or rewrote");}}
      test::require(QFile::exists(f.profile+"/dlhistorya.config")==present&&(!present||read(f.profile+"/dlhistorya.config")==before)&&!QFile::exists(f.profile+"/BiglyBTC.download-history.config")&&!need(w.service->takeHistoryCompletion()),"Rejected/no-op event wrote files or resurrected");w.close();need(store->close());test::require(f.intact(),"History rejection rolled back payload");});
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(0s)==std::future_status::ready,60000);try{work.get();}catch(const std::exception& e){QFAIL(e.what());}
  }
  void genuineCheckedIncompleteProducesOriginalMarkerBeforeFullCheckpoint_data(){QTest::addColumn<int>("marker");QTest::newRow("absent")<<0;QTest::newRow("zero")<<1;QTest::newRow("positive")<<2;QTest::newRow("malformed-preserved")<<3;}
  void genuineCheckedIncompleteProducesOriginalMarkerBeforeFullCheckpoint(){
    QFETCH(int,marker);test::OwnedEngineFixture f(false);inputs(f);auto original=state(f);auto attributes=parse(encode(*original.find("attributes")));
    if(marker==0){std::string absent="d";const auto& values=attributes.root().children;for(std::size_t n=0;n+1<values.size();n+=2){if(values[n].bytes!="complt"){absent+=encode(values[n])+encode(values[n+1]);}}attributes=parse(absent+'e');}else{need(attributes.replaceValue("complt",marker==1?"i0e":marker==2?"i4321e":"3:raw"));}
    need(original.replaceValue("attributes",attributes.serialize()));write(f.stateFile,original.serialize());QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    auto work=std::async(std::launch::async,[&]{Workflow w(f);const auto history=read(f.profile+"/dlhistorya.config");const auto start=wall();w.download();const auto completed=wall();w.retire();auto after=state(f);const auto* a=after.find("attributes");
      test::require(number(a->find("parameters"),"stats.download.completed.time")==9876,"First-completed parameter changed");
      if(marker<3){const auto time=number(a,"complt");test::require(time>=start&&time<=completed,"Actual false->true callback sequence was not preserved");auto out=w.event();auto store=need(storage::DownloadHistoryStore::open(w.lease));test::require(need(store->complete(need(store->snapshot()),std::move(*out.receipt))).changed,"Actual producer did not update existing active history");need(store->close());}
      else{const auto* value=a->find("complt");auto outcome=need(w.service->takeHistoryCompletion());test::require(value&&value->type==core::Node::Type::Bytes&&value->bytes=="raw"&&outcome&&!outcome->receipt&&outcome->warning&&outcome->warning->code==storage::ErrorCode::InvalidFormat&&read(f.profile+"/dlhistorya.config")==history,"Malformed producer changed or blocked the saved payload checkpoint");}
      test::require(!need(w.service->takeHistoryCompletion()),"Actual producer replayed");w.close();test::require(f.intact(),"History producer changed actual completed payload");});
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(0s)==std::future_status::ready,60000);try{work.get();}catch(const std::exception& e){QFAIL(e.what());}
  }
  void actualPartialCheckpointClearsHistoryThenLaterFullCompletionKeepsFirstTime(){
    test::OwnedEngineFixture f(false,true),full(false);inputs(f);auto original=state(f);auto attributes=parse(encode(*original.find("attributes")));need(attributes.replaceValue("complt","i0e"));need(original.replaceValue("attributes",attributes.serialize()));write(f.stateFile,original.serialize());QCOMPARE(*f.hash,*full.hash);QTRY_VERIFY_WITH_TIMEOUT(f.ready()&&full.ready(),30000);
    auto work=std::async(std::launch::async,[&]{Workflow w(f);auto store=need(storage::DownloadHistoryStore::open(w.lease));need(store->apply(need(store->snapshot()),storage::DownloadHistorySide::Active,core::DownloadHistorySetCompleteTime{uid(f),123456}));
      need(w.service->startManaged(w.lease,{.hash=*f.hash,.ownedLoopbackPeerPort=f.port(),.forceStart=true}));w.until([&]{auto row=w.status();return row&&row->checked&&!row->complete&&row->received>0;});w.retire();auto partial=state(f);test::require(number(partial.find("attributes"),"complt")==-1&&number(partial.find("attributes")->find("parameters"),"stats.download.completed.time")==9876,"Partial checkpoint did not persist original negative producer/first time");
      auto negative=w.event();test::require(need(store->complete(need(store->reload()),std::move(*negative.receipt))).changed,"Actual false callback did not clear existing active history");test::require(need(store->snapshot()).history(storage::DownloadHistorySide::Active)->records.front().completeTime==0&&!need(w.service->takeHistoryCompletion()),"Negative event did not clear exactly once");
      const auto start=wall();w.download(false,full.port());const auto completed=wall();w.retire();auto complete=state(f);const auto time=number(complete.find("attributes"),"complt");test::require(time>=start&&time<=completed&&number(complete.find("attributes")->find("parameters"),"stats.download.completed.time")==9876,"Later complete callback lost its actual time or replaced first time");
      auto positive=w.event();test::require(need(store->complete(need(store->reload()),std::move(*positive.receipt))).changed&&need(store->snapshot()).history(storage::DownloadHistorySide::Active)->records.front().completeTime==time,"Later actual full completion did not replace zero history time");w.close();need(store->close());test::require(f.intact(),"Producer/checkpoint changed real payload");});
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(0s)==std::future_status::ready,90000);try{work.get();}catch(const std::exception& e){QFAIL(e.what());}
  }
  void replacementAndForgetCannotDropUnconsumedActualEvent(){
    test::OwnedEngineFixture f(false);inputs(f);QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    auto work=std::async(std::launch::async,[&]{Workflow w(f);w.download();w.retire();const auto checkpoint=read(f.stateFile);const auto inventory=read(f.profile+"/downloads.config");const auto previous=w.status()->admission;
      auto restarted=w.service->startManaged(w.lease,{.hash=*f.hash,.forceStart=true});test::require(!restarted&&restarted.error().code==storage::ErrorCode::Conflict&&!w.service->forgetManaged(*f.hash)&&w.status()->admission==previous&&read(f.stateFile)==checkpoint&&read(f.profile+"/downloads.config")==inventory,"Unconsumed completion was replaced/forgotten or source prepared");
      auto event=w.event();auto store=need(storage::DownloadHistoryStore::open(w.lease));need(store->complete(need(store->reload()),std::move(*event.receipt)));need(w.service->startManaged(w.lease,{.hash=*f.hash,.forceStart=true}));test::require(w.status()->admission!=previous,"Consumed event prevented real new admission");w.retire();w.close();need(store->close());});
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(0s)==std::future_status::ready,60000);try{work.get();}catch(const std::exception& e){QFAIL(e.what());}
  }
  void pausedHistoryAbandonmentHasNoProfileEffectsOrReplay(){
    test::OwnedEngineFixture f(false);inputs(f);QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    auto work=std::async(std::launch::async,[&]{Workflow w(f);w.download();w.retire();const auto checkpoint=read(f.stateFile),inventory=read(f.profile+"/downloads.config"),history=read(f.profile+"/dlhistorya.config");w.service->discardHistoryCompletions();
      test::require(!need(w.service->takeHistoryCompletion())&&read(f.stateFile)==checkpoint&&read(f.profile+"/downloads.config")==inventory&&read(f.profile+"/dlhistorya.config")==history,"Auxiliary discard wrote or replayed");need(w.service->forgetManaged(*f.hash));w.close();});
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(0s)==std::future_status::ready,60000);try{work.get();}catch(const std::exception& e){QFAIL(e.what());}
  }
  void actualSyncFailureRetainsCompletedActionAndReviewPrefix(){
    test::OwnedEngineFixture f(false);inputs(f);QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    auto work=std::async(std::launch::async,[&]{Workflow w(f);w.download();w.retire();auto out=w.event();auto store=need(storage::DownloadHistoryStore::open(w.lease));const auto before=read(f.profile+"/dlhistorya.config");struct Reset {~Reset(){failHistorySync=false;}} reset;failHistorySync=true;historySyncFailed=false;auto r=store->complete(need(store->snapshot()),std::move(*out.receipt));failHistorySync=false;
      test::require(historySyncFailed&&!r&&store->recoveryRequired()&&QFile::exists(f.profile+"/BiglyBTC.download-history.config")&&read(f.profile+"/dlhistorya.config")==before&&!need(w.service->takeHistoryCompletion()),"Actual history sync failure lost its review prefix or replayed");w.close();need(store->close());auto reopened=need(storage::DownloadHistoryStore::open(w.lease));test::require(!need(reopened->snapshot()).writable()&&f.intact(),"Uncertain history replayed or altered payload");need(reopened->close());});
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(0s)==std::future_status::ready,60000);try{work.get();}catch(const std::exception& e){QFAIL(e.what());}
  }
  void normalCloseConsumesAndNoCheckpointCloseSuppresses_data(){QTest::addColumn<bool>("omit");QTest::newRow("normal")<<false;QTest::newRow("NoCP")<<true;}
  void normalCloseConsumesAndNoCheckpointCloseSuppresses(){
    QFETCH(bool,omit);test::OwnedEngineFixture f(false);inputs(f);QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    auto work=std::async(std::launch::async,[&]{Workflow w(f);w.download();if(omit){w.retire();}const auto history=read(f.profile+"/dlhistorya.config"),oldState=read(f.stateFile),inventory=read(f.profile+"/downloads.config");if(omit){const auto end=w.service->closeWithoutCheckpoint(std::chrono::steady_clock::now()+5s);test::require(end.sessionJoined&&end.storageReleased&&!need(w.service->takeHistoryCompletion())&&read(f.stateFile)==oldState&&read(f.profile+"/downloads.config")==inventory&&read(f.profile+"/dlhistorya.config")==history,"NoCP exposed receipt or wrote profile/history");}else{w.close();auto out=w.event();auto store=need(storage::DownloadHistoryStore::open(w.lease));test::require(need(store->complete(need(store->snapshot()),std::move(*out.receipt))).changed,"Normal close lost durable event");need(store->close());}});
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(0s)==std::future_status::ready,60000);try{work.get();}catch(const std::exception& e){QFAIL(e.what());}
  }
  void startupCompleteAndPartialRetirementNeverBackfill_data(){QTest::addColumn<bool>("already");QTest::newRow("initial-complete")<<true;QTest::newRow("incomplete")<<false;}
  void startupCompleteAndPartialRetirementNeverBackfill(){
    QFETCH(bool,already);test::OwnedEngineFixture f(false,!already);inputs(f);if(already){for(const auto& file:f.files){test::write(f.payload+'/'+file.relative,file.expected);}}QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
    auto work=std::async(std::launch::async,[&]{Workflow w(f);const auto before=read(f.profile+"/dlhistorya.config");need(w.service->startManaged(w.lease,{.hash=*f.hash,.ownedLoopbackPeerPort=already?static_cast<unsigned short>(0):f.port(),.forceStart=true}));w.until([&]{auto v=w.status();return v&&v->checked&&(already?v->complete:v->received>0);});w.retire();if(already){test::require(!need(w.service->takeHistoryCompletion()),"Initially complete row invented a history event");}else{auto negative=w.event();auto store=need(storage::DownloadHistoryStore::open(w.lease));test::require(!need(store->complete(need(store->snapshot()),std::move(*negative.receipt))).changed,"Partial zero-history callback appended/backfilled");need(store->close());}test::require(read(f.profile+"/dlhistorya.config")==before,"Startup/partial row backfilled history");w.close();});
    QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(0s)==std::future_status::ready,60000);try{work.get();}catch(const std::exception& e){QFAIL(e.what());}
  }
};
QTEST_GUILESS_MAIN(DownloadHistoryCompletionTests)
#include "download_history_completion_tests.moc"

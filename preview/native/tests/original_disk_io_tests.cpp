#include "metadata_disk_router.hpp"
#include "original_disk_io.hpp"
#include <QtTest>
#include <boost/asio/executor_work_guard.hpp>
#include <QCryptographicHash>
#include <QDir>
#include <libtorrent/bencode.hpp>
#include <libtorrent/disk_buffer_holder.hpp>
#include <libtorrent/disk_interface.hpp>
#include <libtorrent/disk_observer.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/peer_request.hpp>
#include <libtorrent/performance_counters.hpp>
#include <libtorrent/settings_pack.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace {
namespace lt=libtorrent;
namespace st=biglybtc::storage;
namespace ed=biglybtc::engine::detail;
namespace fs=std::filesystem;
using Kind=st::OriginalStorageKind;
void require(bool yes,const char* why){if(!yes){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,st::Error>&& result){if(!result){throw std::runtime_error(result.error().message);}return std::move(*result);}
std::string encode(const lt::entry& e){std::string out;lt::bencode(std::back_inserter(out),e);return out;}
QByteArray sha1(std::string_view bytes){return QCryptographicHash::hash(QByteArray(bytes.data(),static_cast<qsizetype>(bytes.size())),QCryptographicHash::Sha1);}
QByteArray sha256(std::string_view bytes){return QCryptographicHash::hash(QByteArray(bytes.data(),static_cast<qsizetype>(bytes.size())),QCryptographicHash::Sha256);}
struct File {std::string name,data;bool pad=false;Kind kind=Kind::Linear;};
std::shared_ptr<lt::torrent_info> parse(const lt::entry& info){lt::entry torrent;torrent["info"]=info;const auto raw=encode(torrent);lt::error_code error;auto metadata=std::make_shared<lt::torrent_info>(lt::span<const char>(raw.data(),static_cast<std::ptrdiff_t>(raw.size())),error,lt::from_span);require(!error,"Generated disk metadata rejected");return metadata;}
std::shared_ptr<lt::torrent_info> v1(const std::vector<File>& files){lt::entry info;info["name"]=files.size()==1?files.front().name:"bundle";info["piece length"]=16384;std::string body;for(const auto& f:files){body+=f.pad?std::string(f.data.size(),'\0'):f.data;}std::string pieces;for(std::size_t p=0;p<body.size();p+=16384){pieces+=sha1(std::string_view(body).substr(p,std::min<std::size_t>(16384,body.size()-p))).toStdString();}info["pieces"]=pieces;
  if(files.size()==1){info["length"]=static_cast<std::int64_t>(files.front().data.size());}else{lt::entry::list_type rows;for(const auto& f:files){lt::entry row;row["length"]=static_cast<std::int64_t>(f.data.size());row["path"]=lt::entry::list_type{lt::entry(f.name)};if(f.pad){row["attr"]="p";}rows.push_back(std::move(row));}info["files"]=std::move(rows);}return parse(info);}
std::shared_ptr<lt::torrent_info> v2(std::string_view data){lt::entry info;info["name"]="tail.bin";info["piece length"]=32768;info["meta version"]=2;const auto first=sha256(data.substr(0,16384)),tail=sha256(data.substr(16384));const auto root=QCryptographicHash::hash(first+tail,QCryptographicHash::Sha256);auto& file=info["file tree"]["tail.bin"][""];file["length"]=static_cast<std::int64_t>(data.size());file["pieces root"]=root.toStdString();return parse(info);}
struct Gate {
  std::mutex mutex;std::condition_variable condition;bool entered=false,released=false;
  void hold(){std::unique_lock lock(mutex);entered=true;condition.notify_all();require(condition.wait_for(lock,std::chrono::seconds(15),[&]{return released;}),"Test publisher gate timed out");}
  bool wait(){std::unique_lock lock(mutex);return condition.wait_for(lock,std::chrono::seconds(5),[&]{return entered;});}
  void release(){const std::lock_guard lock(mutex);released=true;condition.notify_all();}
};
struct ReleaseOnExit{std::shared_ptr<Gate> gate;~ReleaseOnExit(){if(gate){gate->release();}}};
// A failed assertion against the old backend must still release the real disk
// gate and destroy its callbacks while their context and captured state live.
struct DrainDiskOnExit {
  std::shared_ptr<Gate> gate;lt::disk_interface& backend;lt::io_context& context;bool settled=false;
  ~DrainDiskOnExit(){if(!settled){gate->release();backend.abort(true);context.restart();context.poll();}}
};
struct ConcurrentReleaseState {
  std::mutex mutex;std::condition_variable condition;
  const std::thread::id testThread=std::this_thread::get_id();
  std::thread::id networkThread,callbackThread,releasedThread;
  bool started=false,armed=false,called=false,networkCaptureReleased=false,released=false,waitTimedOut=false;
  unsigned wrongThreadCaptureDestructions=0;
};
struct ConcurrentOwner {
  std::shared_ptr<ConcurrentReleaseState> state;
  ~ConcurrentOwner(){const std::lock_guard lock(state->mutex);state->releasedThread=std::this_thread::get_id();state->released=true;state->condition.notify_all();}
};
// Force old worker-side copies to outlive the primary network callback copy.
// This observes real std::function destruction, not an invented disk callback.
struct EarlyCompletion {
  std::shared_ptr<ConcurrentOwner> owner;std::shared_ptr<ConcurrentReleaseState> state;
  void operator()()const{const std::lock_guard lock(state->mutex);state->callbackThread=std::this_thread::get_id();state->called=true;state->condition.notify_all();}
  ~EarlyCompletion(){
    if(!owner){return;}bool network=false;
    {
      std::unique_lock lock(state->mutex);const auto thread=std::this_thread::get_id();network=state->armed&&thread==state->networkThread;
      if(state->armed&&thread!=state->testThread&&thread!=state->networkThread){
        ++state->wrongThreadCaptureDestructions;
        if(!state->condition.wait_for(lock,std::chrono::seconds(5),[&]{return state->networkCaptureReleased;})){state->waitTimedOut=true;}
      }
    }
    owner.reset(); // The signal below certifies that this actual copy released.
    if(network){const std::lock_guard lock(state->mutex);state->networkCaptureReleased=true;state->condition.notify_all();}
  }
};
struct DrainRunningDiskOnExit {
  std::shared_ptr<Gate> gate;lt::disk_interface& backend;
  boost::asio::executor_work_guard<lt::io_context::executor_type>& runnerWork;std::thread& runner;
  ~DrainRunningDiskOnExit(){gate->release();backend.abort(true);runnerWork.reset();if(runner.joinable()){runner.join();}}
};
// Explicit test-only publisher policy faults/gates. Payload and all asynchronous
// disk jobs are real; this does not qualify profile-file CAS durability.
struct Store {
  std::optional<std::string> current;std::shared_ptr<Gate> gate;std::atomic<bool> fail{false};unsigned calls=0;
  st::OriginalControlPublisher publisher(){return [this](const auto& expected,std::string_view replacement)->std::expected<void,st::Error>{require(expected==current,"Publisher expected original bytes");++calls;if(gate&&calls==1){gate->hold();}if(fail.load()){return std::unexpected(st::Error{st::ErrorCode::Io,"Test publisher unavailable"});}current=std::string(replacement);return {};};}
};
struct Bundle {
  std::shared_ptr<lt::torrent_info> metadata;std::shared_ptr<ed::OriginalDiskFiles> files;std::string route;
  lt::aux::vector<lt::download_priority_t,lt::file_index_t> priorities;
  std::vector<std::shared_ptr<Store>> stores;std::vector<QString> physical;
  Bundle(const QString& root,std::shared_ptr<lt::torrent_info> info,const std::vector<Kind>& kinds,std::shared_ptr<Gate> gate={}):metadata(std::move(info)),route((root+"/reserved-never-created").toStdString()){
    const auto& geometry=metadata->files();require(kinds.size()==static_cast<std::size_t>(geometry.num_files()),"Fixture kind count");std::vector<std::optional<ed::OriginalDiskFile>> entries;entries.reserve(kinds.size());
    for(int n=0;n<geometry.num_files();++n){const auto index=lt::file_index_t{n};priorities.push_back(lt::download_priority_t{4});if(geometry.pad_file_at(index)){entries.emplace_back(std::nullopt);stores.push_back({});physical.emplace_back();continue;}
      const auto path=root+"/physical-"+QString::number(n);st::detail::Descriptor fd(::open(path.toStdString().c_str(),O_CREAT|O_EXCL|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600));require(fd.get()>=0,"Cannot open owned disk fixture");auto store=std::make_shared<Store>();store->gate=gate;auto io=take(st::OriginalFileIo::open(fd.get(),{static_cast<std::uint64_t>(geometry.file_offset(index)),static_cast<std::uint64_t>(geometry.file_size(index)),static_cast<std::uint32_t>(geometry.piece_length())},kinds[static_cast<std::size_t>(n)],{},store->publisher()));
      entries.emplace_back(ed::OriginalDiskFile{std::move(fd),std::move(io)});stores.push_back(std::move(store));physical.push_back(path);
    }files=std::make_shared<ed::OriginalDiskFiles>(metadata,std::move(entries));
  }
  lt::storage_params parameters()const{return {metadata->files(),nullptr,route,lt::storage_mode_sparse,priorities,metadata->info_hash()};}
};
struct Disk {
  lt::io_context context;lt::settings_pack settings;lt::counters counters;std::unique_ptr<lt::disk_interface> backend;
  explicit Disk(const std::shared_ptr<ed::MetadataDiskPolicy>& policy):backend(ed::metadataDiskConstructor(policy)(context,settings,counters)){}
  ~Disk(){backend->abort(true);} // Discard queued callbacks on failure unwind; never invoke dead test locals.
  void pump(){context.restart();context.poll();}
};
struct Observer final:lt::disk_observer{unsigned calls=0;void on_disk()override{++calls;}};
QByteArray actual(const QString& path){QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot inspect actual disk bytes");return file.readAll();}
}
class OriginalDiskIoTests final:public QObject {
  Q_OBJECT
private slots:

  void selectedMissingRangesCarryRealFileAndCannotVerifyEvenAdvertisedZeroHash(){
    QTemporaryDir area;QVERIFY(area.isValid());lt::entry info;info["name"]="missing.bin";info["length"]=16384;info["piece length"]=16384;info["pieces"]=std::string(20,'\0');auto metadata=parse(info);
    std::vector<std::optional<ed::OriginalDiskFile>> entries(static_cast<std::size_t>(metadata->num_files()));auto files=std::make_shared<ed::OriginalDiskFiles>(metadata,std::move(entries),ed::OriginalDiskAccess::SelectedReadOnly);
    const auto route=(area.path()+"/never-created").toStdString();auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(route,files));Disk disk(policy);lt::aux::vector<lt::download_priority_t,lt::file_index_t> priorities;priorities.push_back(lt::download_priority_t{0});const lt::storage_params params{metadata->files(),nullptr,route,lt::storage_mode_sparse,priorities,metadata->info_hash()};auto holder=disk.backend->new_torrent(params,{});
    bool called=false;lt::sha1_hash hash;lt::storage_error error;disk.backend->async_hash(holder,lt::piece_index_t{0},{},lt::disk_interface::v1_hash,[&](lt::piece_index_t,const lt::sha1_hash& actual,const lt::storage_error& failure){hash=actual;error=failure;called=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),called),5000);
    QCOMPARE(error.ec.value(),ENOENT);QCOMPARE(static_cast<int>(error.file()),0);QVERIFY(hash!=metadata->hash_for_piece(lt::piece_index_t{0}));QVERIFY(!files->failure());QVERIFY(!QFileInfo::exists(QString::fromStdString(route)));holder.reset();
  }

  void selectedCompactEdgesHashAuthenticBoundaryPiecesAndKeepInteriorUnverified(){
    QTemporaryDir area;QVERIFY(area.isValid());const std::string left(8192,'A'),dnd(49152,'d'),right(8192,'B'),body=left+dnd+right;
    auto metadata=v1({{"left.bin",left},{"dnd.bin",dnd,false,Kind::Compact},{"right.bin",right}});Bundle generated(area.path(),metadata,{Kind::Linear,Kind::Compact,Kind::Linear});QVERIFY(generated.files->write(0,std::span<const char>(body.data(),body.size())));QVERIFY(generated.files->flush());
    const auto before0=actual(generated.physical[0]),before1=actual(generated.physical[1]),before2=actual(generated.physical[2]);QCOMPARE(before1.size(),16384);const auto controls=generated.stores[1]->current;QVERIFY(controls);generated.files.reset();
    std::vector<std::optional<ed::OriginalDiskFile>> entries;
    for(int n=0;n<metadata->num_files();++n){const auto index=lt::file_index_t{n};st::detail::Descriptor fd(::open(generated.physical[static_cast<std::size_t>(n)].toStdString().c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC));QVERIFY(fd.get()>=0);struct stat before{};QVERIFY(::fstat(fd.get(),&before)==0);const int borrowed=fd.get();const auto path=generated.physical[static_cast<std::size_t>(n)].toStdString();
      auto io=take(st::OriginalFileIo::open(fd.get(),{static_cast<std::uint64_t>(metadata->files().file_offset(index)),static_cast<std::uint64_t>(metadata->files().file_size(index)),16384},n==1?Kind::Compact:Kind::Linear,n==1?controls:std::nullopt));
      entries.emplace_back(ed::OriginalDiskFile{std::move(fd),std::move(io),[borrowed,path,before]() -> std::expected<void,st::Error>{struct stat held{},named{};if(::fstat(borrowed,&held)!=0||::lstat(path.c_str(),&named)!=0||held.st_dev!=before.st_dev||held.st_ino!=before.st_ino||named.st_dev!=before.st_dev||named.st_ino!=before.st_ino||held.st_size!=before.st_size||named.st_size!=before.st_size||held.st_ctim.tv_sec!=before.st_ctim.tv_sec||held.st_ctim.tv_nsec!=before.st_ctim.tv_nsec){return std::unexpected(st::Error{st::ErrorCode::Conflict,"Actual test input changed"});}return {};}});
    }
    auto files=std::make_shared<ed::OriginalDiskFiles>(metadata,std::move(entries),ed::OriginalDiskAccess::SelectedReadOnly);auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(generated.route,files));Disk disk(policy);auto holder=disk.backend->new_torrent(generated.parameters(),{});
    std::array<lt::storage_error,4> errors;std::array<lt::sha1_hash,4> hashes;int called=0;
    for(int n=0;n<4;++n){disk.backend->async_hash(holder,lt::piece_index_t{n},{},lt::disk_interface::v1_hash|lt::disk_interface::flush_piece,[&,n](lt::piece_index_t,const lt::sha1_hash& hash,const lt::storage_error& error){hashes[static_cast<std::size_t>(n)]=hash;errors[static_cast<std::size_t>(n)]=error;++called;});}QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),called==4),5000);
    for(int n:{0,3}){QVERIFY(!errors[static_cast<std::size_t>(n)].ec);QCOMPARE(hashes[static_cast<std::size_t>(n)].to_string(),metadata->hash_for_piece(lt::piece_index_t{n}).to_string());}
    for(int n:{1,2}){QCOMPARE(errors[static_cast<std::size_t>(n)].ec.value(),ENOENT);QCOMPARE(static_cast<int>(errors[static_cast<std::size_t>(n)].file()),1);QVERIFY(hashes[static_cast<std::size_t>(n)]!=metadata->hash_for_piece(lt::piece_index_t{n}));}
    QVERIFY(files->flush());const std::array<std::uint8_t,3> complete{1,0,1};QVERIFY(files->finishComplete(complete));QVERIFY(!files->failure());QCOMPARE(actual(generated.physical[0]),before0);QCOMPARE(actual(generated.physical[1]),before1);QCOMPARE(actual(generated.physical[2]),before2);QVERIFY(generated.stores[1]->current.has_value());QCOMPARE(*generated.stores[1]->current,*controls);holder.reset();
  }

  void abortWithoutLazyOriginalWorkerReleasesItsContextWork(){
    lt::io_context context;auto backend=ed::originalDiskConstructor(context,[](const std::string&){return std::shared_ptr<ed::OriginalDiskFiles>{};});
    backend->abort(false);QCOMPARE(context.poll(),std::size_t{0});QVERIFY(context.stopped());backend->abort(true);
  }
  void delayedReadOnlyBarrierKeepsNetworkContextAliveThroughAbortAndCaptureRelease(){
    QTemporaryDir area;QVERIFY(area.isValid());const std::string body(16384,'G');auto metadata=v1({{"existing.bin",body}});
    const auto path=area.path()+"/existing.bin";QFile payload(path);QVERIFY(payload.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(payload.write(body.data(),static_cast<qint64>(body.size())),static_cast<qint64>(body.size()));payload.close();
    auto gate=std::make_shared<Gate>();st::detail::Descriptor fd(::open(path.toStdString().c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC));QVERIFY(fd.get()>=0);
    auto io=take(st::OriginalFileIo::open(fd.get(),{0,16384,16384},Kind::Linear,{}));std::vector<std::optional<ed::OriginalDiskFile>> entries;
    entries.emplace_back(ed::OriginalDiskFile{std::move(fd),std::move(io),[gate]() -> std::expected<void,st::Error> {gate->hold();return {};}});
    auto files=std::make_shared<ed::OriginalDiskFiles>(metadata,std::move(entries),ed::OriginalDiskAccess::ReadOnly);std::weak_ptr<ed::OriginalDiskFiles> weakFiles=files;
    const auto networkThread=std::this_thread::get_id();std::thread::id callbackThread,releasedThread;bool called=false,released=false;
    struct CompletionOwner {bool& released;std::thread::id& thread;CompletionOwner(bool& value,std::thread::id& id):released(value),thread(id){}~CompletionOwner(){thread=std::this_thread::get_id();released=true;}};
    auto owner=std::make_shared<CompletionOwner>(released,releasedThread);std::weak_ptr<CompletionOwner> weakOwner=owner;
    lt::io_context context;auto backend=ed::originalDiskConstructor(context,[weakFiles](const std::string&){return weakFiles.lock();});
    lt::aux::vector<lt::download_priority_t,lt::file_index_t> priorities;priorities.push_back(lt::download_priority_t{4});
    const auto route=(area.path()+"/virtual-never-created").toStdString();const lt::storage_params params{metadata->files(),nullptr,route,lt::storage_mode_sparse,priorities,metadata->info_hash()};auto holder=backend->new_torrent(params,owner);
    DrainDiskOnExit cleanup{gate,*backend,context};
    backend->async_stop_torrent(holder,[capture=owner,&called,&callbackThread]{static_cast<void>(capture);callbackThread=std::this_thread::get_id();called=true;});
    QVERIFY(gate->wait());holder.reset();owner.reset();files.reset();backend->abort(false);
    // No stock backend/timer/Qt pump/restart can mask missing context work.
    // With the old code this exact empty-queue poll sets stopped() to true.
    QCOMPARE(context.poll(),std::size_t{0});QVERIFY(!context.stopped());QVERIFY(!called&&!weakOwner.expired()&&!weakFiles.expired());
    gate->release();backend->abort(true);QVERIFY(!context.stopped());QVERIFY(!called&&!weakOwner.expired()&&!weakFiles.expired());
    QCOMPARE(context.run(),std::size_t{2});QVERIFY(called&&released&&weakOwner.expired()&&weakFiles.expired());QVERIFY(callbackThread==networkThread);QVERIFY(releasedThread==networkThread);
    QCOMPARE(actual(path),QByteArray::fromStdString(body));QVERIFY(!QFileInfo::exists(QString::fromStdString(route)));cleanup.settled=true;
  }

  void runningContextRetiresConsumedBarrierCopiesAndLastOwnerOnNetworkThread(){
    QTemporaryDir area;QVERIFY(area.isValid());const std::string body(16384,'N');auto metadata=v1({{"existing.bin",body}});
    const auto path=area.path()+"/existing.bin";QFile payload(path);QVERIFY(payload.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(payload.write(body.data(),static_cast<qint64>(body.size())),static_cast<qint64>(body.size()));payload.close();
    auto gate=std::make_shared<Gate>();st::detail::Descriptor fd(::open(path.toStdString().c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC));QVERIFY(fd.get()>=0);
    auto io=take(st::OriginalFileIo::open(fd.get(),{0,16384,16384},Kind::Linear,{}));std::vector<std::optional<ed::OriginalDiskFile>> entries;
    entries.emplace_back(ed::OriginalDiskFile{std::move(fd),std::move(io),[gate]() -> std::expected<void,st::Error> {gate->hold();return {};}});
    auto files=std::make_shared<ed::OriginalDiskFiles>(metadata,std::move(entries),ed::OriginalDiskAccess::ReadOnly);std::weak_ptr<ed::OriginalDiskFiles> weakFiles=files;
    auto state=std::make_shared<ConcurrentReleaseState>();auto owner=std::make_shared<ConcurrentOwner>();owner->state=state;std::weak_ptr<ConcurrentOwner> weakOwner=owner;
    lt::io_context context;auto backend=ed::originalDiskConstructor(context,[weakFiles](const std::string&){return weakFiles.lock();});
    // Isolate capture-thread ownership from the separate no-work-guard test.
    // This runner really executes completions concurrently with the disk job.
    auto runnerWork=boost::asio::make_work_guard(context);
    std::thread runner([&]{
      {const std::lock_guard lock(state->mutex);state->networkThread=std::this_thread::get_id();state->started=true;state->condition.notify_all();}
      context.run();
    });
    DrainRunningDiskOnExit cleanup{gate,*backend,runnerWork,runner};
    {std::unique_lock lock(state->mutex);QVERIFY(state->condition.wait_for(lock,std::chrono::seconds(5),[&]{return state->started;}));}
    lt::aux::vector<lt::download_priority_t,lt::file_index_t> priorities;priorities.push_back(lt::download_priority_t{4});
    const auto route=(area.path()+"/virtual-never-created").toStdString();const lt::storage_params params{metadata->files(),nullptr,route,lt::storage_mode_sparse,priorities,metadata->info_hash()};auto holder=backend->new_torrent(params,owner);
    backend->async_stop_torrent(holder,EarlyCompletion{owner,state});
    QVERIFY(gate->wait());holder.reset();owner.reset();files.reset();
    {const std::lock_guard lock(state->mutex);state->armed=true;}
    backend->abort(false);gate->release();
    {
      std::unique_lock lock(state->mutex);QVERIFY(state->condition.wait_for(lock,std::chrono::seconds(5),[&]{return state->released;}));
      QVERIFY(state->called&&state->networkCaptureReleased&&!state->waitTimedOut);
      QCOMPARE(state->wrongThreadCaptureDestructions,0U);QVERIFY(state->callbackThread==state->networkThread);QVERIFY(state->releasedThread==state->networkThread);
    }
    backend->abort(true);runnerWork.reset();runner.join();
    QVERIFY(weakOwner.expired()&&weakFiles.expired());QVERIFY(context.stopped());QCOMPARE(actual(path),QByteArray::fromStdString(body));QVERIFY(!QFileInfo::exists(QString::fromStdString(route)));
  }

  void existingReadOnlyPaddingHashesZeroWithoutOpeningAbsentOrPoisonedPaths_data(){
    QTest::addColumn<bool>("poison");QTest::newRow("absent-pad")<<false;QTest::newRow("pad-symlink-to-independent-owned-sentinel")<<true;
  }
  void existingReadOnlyPaddingHashesZeroWithoutOpeningAbsentOrPoisonedPaths(){
    QFETCH(bool,poison);QTemporaryDir area,outsider;QVERIFY(area.isValid()&&outsider.isValid());
    const std::vector<File> specs{{"a.bin",std::string(8192,'A')},{"padding",std::string(8192,'\0'),true},{"b.bin",std::string(16384,'B')}};
    auto metadata=v1(specs);QVERIFY(metadata->files().pad_file_at(lt::file_index_t{1}));QVERIFY(QDir().mkpath(area.path()+"/bundle"));
    const auto put=[](const QString& path,const QByteArray& bytes){QFile file(path);require(file.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Create owned real/pad sentinel fixture");require(file.write(bytes)==bytes.size()&&file.flush(),"Write owned real/pad sentinel bytes");};
    const auto sentinel=outsider.path()+"/must-not-be-used";const auto pad=area.path()+"/bundle/padding";
    const QByteArray poisoned(8192,'P');struct stat before{};
    if(poison){put(sentinel,poisoned);QVERIFY(::lstat(sentinel.toStdString().c_str(),&before)==0);QVERIFY(::symlink(sentinel.toStdString().c_str(),pad.toStdString().c_str())==0);}
    std::vector<std::optional<ed::OriginalDiskFile>> entries;lt::aux::vector<lt::download_priority_t,lt::file_index_t> priorities;
    for(int n=0;n<metadata->num_files();++n){const auto index=lt::file_index_t{n};const auto& geometry=metadata->files();
      priorities.push_back(n==1?lt::download_priority_t{0}:lt::download_priority_t{4});if(n==1){entries.emplace_back(std::nullopt);continue;}
      const auto path=area.path()+'/'+QString::fromStdString(geometry.file_path(index));put(path,QByteArray::fromStdString(specs[static_cast<std::size_t>(n)].data));
      st::detail::Descriptor fd(::open(path.toStdString().c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC));QVERIFY(fd.get()>=0);
      auto io=take(st::OriginalFileIo::open(fd.get(),{static_cast<std::uint64_t>(geometry.file_offset(index)),static_cast<std::uint64_t>(geometry.file_size(index)),16384},Kind::Linear,{}));
      entries.emplace_back(ed::OriginalDiskFile{std::move(fd),std::move(io)});
    }
    auto files=std::make_shared<ed::OriginalDiskFiles>(metadata,std::move(entries),ed::OriginalDiskAccess::ReadOnly);
    std::array<char,8192> virtualBytes{};std::ranges::fill(virtualBytes,'X');QVERIFY(files->read(8192,virtualBytes));QVERIFY(std::ranges::all_of(virtualBytes,[](char byte){return byte=='\0';}));
    const auto route=(area.path()+"/never-created-route").toStdString();auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(route,files));Disk disk(policy);
    const lt::storage_params params{metadata->files(),nullptr,route,lt::storage_mode_sparse,priorities,metadata->info_hash()};auto holder=disk.backend->new_torrent(params,{});
    bool hashed=false;lt::sha1_hash hash;lt::storage_error failure;
    disk.backend->async_hash(holder,lt::piece_index_t{0},{},lt::disk_interface::v1_hash|lt::disk_interface::flush_piece,[&](lt::piece_index_t,const lt::sha1_hash& observed,const lt::storage_error& error){hash=observed;failure=error;hashed=true;});
    QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),hashed),5000);QVERIFY(!failure.ec);QCOMPARE(hash.to_string(),metadata->hash_for_piece(lt::piece_index_t{0}).to_string());
    QVERIFY(hash.to_string()!=sha1(std::string(8192,'A')+poisoned.toStdString()).toStdString());
    QVERIFY(files->flush());const std::array<std::uint8_t,3> completed{1,0,1};QVERIFY(files->finishComplete(completed));QVERIFY(!files->failure());
    if(poison){struct stat after{};QVERIFY(::lstat(sentinel.toStdString().c_str(),&after)==0);QCOMPARE(after.st_dev,before.st_dev);QCOMPARE(after.st_ino,before.st_ino);QCOMPARE(after.st_size,before.st_size);QCOMPARE(after.st_mtim.tv_sec,before.st_mtim.tv_sec);QCOMPARE(after.st_mtim.tv_nsec,before.st_mtim.tv_nsec);QCOMPARE(after.st_ctim.tv_sec,before.st_ctim.tv_sec);QCOMPARE(after.st_ctim.tv_nsec,before.st_ctim.tv_nsec);QCOMPARE(actual(sentinel),poisoned);QVERIFY(QFileInfo(pad).isSymLink());}
    else{QVERIFY(!QFileInfo::exists(pad)&&!QFileInfo(pad).isSymLink());}
    QVERIFY(!QFileInfo::exists(QString::fromStdString(route)));
  }

  void existingReadOnlyDescriptorsHashActualBytesAndRejectRepairThroughDiskCallbacks_data(){QTest::addColumn<bool>("corrupt");QTest::newRow("correct-size-correct-content")<<false;QTest::newRow("correct-size-corrupt-content")<<true;}
  void existingReadOnlyDescriptorsHashActualBytesAndRejectRepairThroughDiskCallbacks(){
    QFETCH(bool,corrupt);
    QTemporaryDir area;QVERIFY(area.isValid());const std::string body(16384,'E'),stored(16384,corrupt?'C':'E');auto metadata=v1({{"existing.bin",body}});const auto path=area.path()+QStringLiteral("/existing.bin");QFile payload(path);QVERIFY(payload.open(QIODevice::WriteOnly));QCOMPARE(payload.write(stored.data(),static_cast<qint64>(stored.size())),static_cast<qint64>(stored.size()));payload.close();
    st::detail::Descriptor fd(::open(path.toStdString().c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC));QVERIFY(fd.get()>=0);auto io=take(st::OriginalFileIo::open(fd.get(),{0,16384,16384},Kind::Linear,{}));std::vector<std::optional<ed::OriginalDiskFile>> entries;entries.emplace_back(ed::OriginalDiskFile{std::move(fd),std::move(io)});auto files=std::make_shared<ed::OriginalDiskFiles>(metadata,std::move(entries),ed::OriginalDiskAccess::ReadOnly);QCOMPARE(files->access(),ed::OriginalDiskAccess::ReadOnly);
    const auto route=(area.path()+QStringLiteral("/reserved-existing-never-created")).toStdString();auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(route,files));Disk disk(policy);lt::aux::vector<lt::download_priority_t,lt::file_index_t> priorities;priorities.push_back(lt::download_priority_t{4});const lt::storage_params params{metadata->files(),nullptr,route,lt::storage_mode_sparse,priorities,metadata->info_hash()};auto holder=disk.backend->new_torrent(params,{});
    bool hashed=false;lt::sha1_hash hash;lt::storage_error hashError;disk.backend->async_hash(holder,lt::piece_index_t{0},{},lt::disk_interface::v1_hash|lt::disk_interface::flush_piece,[&](lt::piece_index_t,const lt::sha1_hash& actual,const lt::storage_error& error){hash=actual;hashError=error;hashed=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),hashed),5000);QVERIFY(!hashError.ec);QCOMPARE(hash.to_string(),sha1(stored).toStdString());QCOMPARE(hash==metadata->hash_for_piece(lt::piece_index_t{0}),!corrupt);QVERIFY(!QFileInfo::exists(QString::fromStdString(route)));QVERIFY(!files->failure());
    bool wrote=false;lt::storage_error writeError;const std::string repair(16384,'X');disk.backend->async_write(holder,{lt::piece_index_t{0},0,16384},repair.data(),{},[&](const lt::storage_error& error){writeError=error;wrote=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),wrote),5000);QVERIFY(writeError.ec);QVERIFY(files->failure());QCOMPARE(files->failure()->code,st::ErrorCode::Unsupported);QCOMPARE(actual(path),QByteArray::fromStdString(stored));bool stopped=false;disk.backend->async_stop_torrent(holder,[&]{stopped=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),stopped),5000);QCOMPARE(actual(path),QByteArray::fromStdString(stored));QVERIFY(!QFileInfo::exists(QString::fromStdString(route)));
    std::weak_ptr<ed::OriginalDiskFiles> weak=files;holder.reset();files.reset();QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),weak.expired()),5000);
  }
  void measuredAdmissionLatencyIncludesActualExecutorQueueAndKeepsOwnersSeparate(){
    QTemporaryDir area,otherArea;QVERIFY(area.isValid()&&otherArea.isValid());
    auto gate=std::make_shared<Gate>();const std::string block(16384,'L');
    Bundle bundle(area.path(),v1({{"latency.bin",std::string(65536,'L')}}),{Kind::Reordered},gate);
    Bundle other(otherArea.path(),v1({{"other.bin",std::string(16384,'O')}}),{Kind::Linear});
    auto policy=std::make_shared<ed::MetadataDiskPolicy>();
    QVERIFY(policy->registerOriginalPath(bundle.route,bundle.files));QVERIFY(policy->registerOriginalPath(other.route,other.files));
    Disk disk(policy);auto holder=disk.backend->new_torrent(bundle.parameters(),{});
    auto otherHolder=disk.backend->new_torrent(other.parameters(),{});ReleaseOnExit release{gate};
    const auto fresh=bundle.files->diskLatency().snapshot(ed::DiskLatency::nowMillis());
    QCOMPARE(fresh.readMillis,std::optional<std::int64_t>{0});QCOMPARE(fresh.writeMillis,std::optional<std::int64_t>{0});
    bool wrote=false,read=false,failed=false;QByteArray returned;
    disk.backend->async_write(holder,{lt::piece_index_t{0},0,16384},block.data(),{},[&](const auto& error){failed|=bool(error.ec);wrote=true;});
    QVERIFY(gate->wait()); // Real executor is inside the actual control publisher.
    disk.backend->async_read(holder,{lt::piece_index_t{0},0,16384},[&](lt::disk_buffer_holder data,const lt::storage_error& error){
      failed|=bool(error.ec)||!data;if(data){returned=QByteArray(data.data(),16384);}read=true;
    });
    const auto held=bundle.files->diskLatency().snapshot(ed::DiskLatency::nowMillis());
    QCOMPARE(held.pendingReads,std::size_t{1});QCOMPARE(held.pendingWrites,std::size_t{1});
    QCOMPARE(held.completedReads,std::uint64_t{0});QCOMPARE(held.completedWrites,std::uint64_t{0});
    QVERIFY(held.readMillis&&held.writeMillis&&!held.measurementLost);QVERIFY(!wrote&&!read);
    const auto independent=other.files->diskLatency().snapshot(ed::DiskLatency::nowMillis());
    QCOMPARE(independent.pendingReads,std::size_t{0});QCOMPARE(independent.pendingWrites,std::size_t{0});
    QCOMPARE(independent.readMillis,std::optional<std::int64_t>{0});QCOMPARE(independent.writeMillis,std::optional<std::int64_t>{0});
    gate->release();QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),wrote&&read),5000);QVERIFY(!failed);
    QCOMPARE(returned,QByteArray::fromStdString(block));
    const auto completed=bundle.files->diskLatency().snapshot(ed::DiskLatency::nowMillis());
    QCOMPARE(completed.pendingReads,std::size_t{0});QCOMPARE(completed.pendingWrites,std::size_t{0});
    QCOMPARE(completed.completedReads,std::uint64_t{1});QCOMPARE(completed.completedWrites,std::uint64_t{1});
    QVERIFY(completed.readMillis&&*completed.readMillis>=*held.readMillis);
    QVERIFY(completed.writeMillis&&*completed.writeMillis>=*held.writeMillis);
    // A failed request removes its pending age but does not replace the last
    // successful latency, exactly as the original reader callback does.
    bool rejected=false;lt::storage_error readError;
    disk.backend->async_read(holder,{lt::piece_index_t{0},0,0},[&](lt::disk_buffer_holder,const lt::storage_error& error){readError=error;rejected=true;});
    QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),rejected),5000);QVERIFY(readError.ec);
    const auto afterFailure=bundle.files->diskLatency().snapshot(ed::DiskLatency::nowMillis());
    QCOMPARE(afterFailure.completedReads,completed.completedReads);QCOMPARE(afterFailure.readMillis,completed.readMillis);
    QCOMPARE(afterFailure.pendingReads,std::size_t{0});QVERIFY(!afterFailure.measurementLost);
  }
  void latencyLossIsBoundedStickyAndNewAdmissionStartsFresh(){
    // Explicit ledger-bound policy fixture; this does not issue 65k disk jobs.
    ed::DiskLatency ledger;std::vector<std::uint64_t> tokens;tokens.reserve(ed::DiskLatency::MaximumPending);
    for(std::size_t i=0;i<ed::DiskLatency::MaximumPending;++i){const auto token=ledger.begin(ed::DiskIoAxis::Read);QVERIFY(token);tokens.push_back(token);}
    QCOMPARE(ledger.begin(ed::DiskIoAxis::Write),std::uint64_t{0});
    auto lost=ledger.snapshot(ed::DiskLatency::nowMillis());QVERIFY(lost.measurementLost);QVERIFY(!lost.readMillis&&!lost.writeMillis);
    QCOMPARE(lost.pendingReads,ed::DiskLatency::MaximumPending);QCOMPARE(lost.pendingWrites,std::size_t{0});
    for(const auto token:tokens){ledger.finish(token,false);}
    lost=ledger.snapshot(ed::DiskLatency::nowMillis());QVERIFY(lost.measurementLost&&!lost.readMillis&&!lost.writeMillis);
    QCOMPARE(lost.pendingReads,std::size_t{0});QCOMPARE(lost.completedReads,std::uint64_t{0});
    ed::DiskLatency fresh;const auto known=fresh.snapshot(ed::DiskLatency::nowMillis());
    QVERIFY(!known.measurementLost);QCOMPARE(known.readMillis,std::optional<std::int64_t>{0});QCOMPARE(known.writeMillis,std::optional<std::int64_t>{0});
    const auto token=fresh.begin(ed::DiskIoAxis::Write);QVERIFY(token);
    const auto past=fresh.snapshot(-1);QVERIFY(!past.readMillis&&!past.writeMillis&&!past.measurementLost);
    fresh.finish(token,true);const auto completed=fresh.snapshot(ed::DiskLatency::nowMillis());
    QCOMPARE(completed.completedWrites,std::uint64_t{1});QVERIFY(completed.writeMillis&&!completed.measurementLost);
    fresh.finish(token,true);const auto duplicate=fresh.snapshot(ed::DiskLatency::nowMillis());
    QVERIFY(duplicate.measurementLost&&!duplicate.readMillis&&!duplicate.writeMillis);
  }
  void actualCrossFilePaddingReadsWritesAndSha1(){
    QTemporaryDir area;QVERIFY(area.isValid());const std::vector<File> specs{{"a.bin",std::string(8192,'A')},{"padding",std::string(4096,'\0'),true},{"b.bin",std::string(8192,'B')}};auto metadata=v1(specs);Bundle bundle(area.path(),metadata,{Kind::Linear,Kind::Linear,Kind::Linear});QVERIFY(metadata->files().pad_file_at(lt::file_index_t{1}));auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(bundle.route,bundle.files));Disk disk(policy);auto holder=disk.backend->new_torrent(bundle.parameters(),{});
    const std::string expected=std::string(8192,'A')+std::string(4096,'\0')+std::string(8192,'B');auto first=expected.substr(0,16384);std::fill(first.begin()+8192,first.begin()+12288,'Q');const auto tail=expected.substr(16384);int written=0;bool failed=false;
    disk.backend->async_write(holder,{lt::piece_index_t{0},0,16384},first.data(),{},[&](const auto& error){failed|=bool(error.ec);++written;});disk.backend->async_write(holder,{lt::piece_index_t{1},0,4096},tail.data(),{},[&](const auto& error){failed|=bool(error.ec);++written;});disk.backend->submit_jobs();QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),written==2),5000);QVERIFY(!failed);
    int reads=0;QByteArray firstRead,tailRead;disk.backend->async_read(holder,{lt::piece_index_t{0},0,16384},[&](lt::disk_buffer_holder data,const lt::storage_error& error){failed|=bool(error.ec)||!data;if(data){firstRead=QByteArray(data.data(),16384);}++reads;});disk.backend->async_read(holder,{lt::piece_index_t{1},0,4096},[&](lt::disk_buffer_holder data,const lt::storage_error& error){failed|=bool(error.ec)||!data;if(data){tailRead=QByteArray(data.data(),4096);}++reads;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),reads==2),5000);QVERIFY(!failed);QCOMPARE(firstRead+tailRead,QByteArray::fromStdString(expected));QCOMPARE(actual(bundle.physical[0]),QByteArray(8192,'A'));QCOMPARE(actual(bundle.physical[2]),QByteArray(8192,'B'));QVERIFY(!QFileInfo::exists(QString::fromStdString(bundle.route)));
    int hashes=0;std::array<QByteArray,2> hash;for(int n=0;n<2;++n){disk.backend->async_hash(holder,lt::piece_index_t{n},{},lt::disk_interface::v1_hash,[&,n](lt::piece_index_t piece,const lt::sha1_hash& result,const lt::storage_error& error){failed|=bool(error.ec)||static_cast<int>(piece)!=n;hash[static_cast<std::size_t>(n)]=QByteArray::fromStdString(result.to_string());++hashes;});}QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),hashes==2),5000);QVERIFY(!failed);QCOMPARE(hash[0],sha1(std::string_view(expected).substr(0,16384)));QCOMPARE(hash[1],sha1(std::string_view(expected).substr(16384)));
  }
  void actualPureV2ShortTailHashesOnlyActualBytes(){
    QTemporaryDir area;QVERIFY(area.isValid());const auto data=std::string(16384,'V')+std::string(3617,'T');auto metadata=v2(data);QVERIFY(metadata->info_hashes().has_v2()&&!metadata->info_hashes().has_v1());QCOMPARE(metadata->files().piece_size2(lt::piece_index_t{0}),20001);Bundle bundle(area.path(),metadata,std::vector<Kind>(static_cast<std::size_t>(metadata->num_files()),Kind::Linear));auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(bundle.route,bundle.files));Disk disk(policy);auto holder=disk.backend->new_torrent(bundle.parameters(),{});int writes=0;bool failed=false;
    for(int offset:{0,16384}){const int length=std::min(16384,20001-offset);disk.backend->async_write(holder,{lt::piece_index_t{0},offset,length},data.data()+offset,{},[&](const auto& error){failed|=bool(error.ec);++writes;});}QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),writes==2),5000);QVERIFY(!failed);
    std::array<lt::sha256_hash,2> leaves;bool combined=false;disk.backend->async_hash(holder,lt::piece_index_t{0},lt::span<lt::sha256_hash>(leaves.data(),2),{},[&](lt::piece_index_t,const lt::sha1_hash&,const lt::storage_error& error){failed|=bool(error.ec);combined=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),combined),5000);QVERIFY(!failed);QCOMPARE(QByteArray::fromStdString(leaves[0].to_string()),sha256(std::string_view(data).substr(0,16384)));QCOMPARE(QByteArray::fromStdString(leaves[1].to_string()),sha256(std::string_view(data).substr(16384)));
    bool single=false;QByteArray tail;disk.backend->async_hash2(holder,lt::piece_index_t{0},16384,{},[&](lt::piece_index_t,const lt::sha256_hash& result,const lt::storage_error& error){failed|=bool(error.ec);tail=QByteArray::fromStdString(result.to_string());single=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),single),5000);QVERIFY(!failed);QCOMPARE(tail,sha256(std::string_view(data).substr(16384)));QVERIFY(tail!=sha256(std::string(std::string_view(data).substr(16384))+std::string(12767,'\0'))); // No invented full-block tail.
  }
  void compactMissingZeroHoleFailsReadAndHash(){
    QTemporaryDir area;QVERIFY(area.isValid());auto metadata=v1({{"compact.bin",std::string(49153,'\0'),false,Kind::Compact}});Bundle bundle(area.path(),metadata,{Kind::Compact});auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(bundle.route,bundle.files));Disk disk(policy);auto holder=disk.backend->new_torrent(bundle.parameters(),{});bool readDone=false,hashDone=false;lt::storage_error readError,hashError;
    disk.backend->async_read(holder,{lt::piece_index_t{0},0,16384},[&](lt::disk_buffer_holder,const lt::storage_error& error){readError=error;readDone=true;});disk.backend->async_hash(holder,lt::piece_index_t{0},{},lt::disk_interface::v1_hash,[&](lt::piece_index_t,const lt::sha1_hash&,const lt::storage_error& error){hashError=error;hashDone=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),readDone&&hashDone),5000);QVERIFY(readError.ec);QVERIFY(hashError.ec);QCOMPARE(readError.ec.value(),ENOENT);QCOMPARE(hashError.ec.value(),ENOENT);QCOMPARE(actual(bundle.physical[0]).size(),0);
  }
  void expiredReservationAndStaleGlobalIndexNeverReachMmap(){
    QTemporaryDir area;QVERIFY(area.isValid());auto metadata=v1({{"file.bin",std::string(16384,'A')}});Bundle bundle(area.path(),metadata,{Kind::Linear});auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(bundle.route,bundle.files));Disk disk(policy);auto holder=disk.backend->new_torrent(bundle.parameters(),{});const auto stale=static_cast<lt::storage_index_t>(holder);holder.reset();std::weak_ptr<ed::OriginalDiskFiles> weak=bundle.files;bundle.files.reset();QVERIFY(weak.expired());QVERIFY(policy->isOriginalPath(bundle.route));QVERIFY_THROWS_EXCEPTION(std::invalid_argument,disk.backend->new_torrent(bundle.parameters(),{}));QVERIFY_THROWS_EXCEPTION(std::logic_error,disk.backend->async_read(stale,{lt::piece_index_t{0},0,1},[](lt::disk_buffer_holder,const lt::storage_error&){}));QVERIFY(!QFileInfo::exists(QString::fromStdString(bundle.route)));QCOMPARE(actual(bundle.physical[0]).size(),0);
  }
  void actualQueuedJobRetainsOwnerThroughRemovalAndDelivery(){
    QTemporaryDir area;QVERIFY(area.isValid());auto gate=std::make_shared<Gate>();Bundle bundle(area.path(),v1({{"queued.bin",std::string(65536,'A')}}),{Kind::Reordered},gate);auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(bundle.route,bundle.files));Disk disk(policy);auto owner=std::make_shared<int>(3);std::weak_ptr<int> weakOwner=owner;auto holder=disk.backend->new_torrent(bundle.parameters(),owner);ReleaseOnExit release{gate};std::weak_ptr<ed::OriginalDiskFiles> weakFiles=bundle.files;bool delivered=false,failed=false;const std::string block(16384,'X');disk.backend->async_write(holder,{lt::piece_index_t{0},0,16384},block.data(),{},[&](const auto& error){failed=bool(error.ec);delivered=true;});QVERIFY(gate->wait());holder.reset();owner.reset();bundle.files.reset();QVERIFY(!weakOwner.expired());QVERIFY(!weakFiles.expired());gate->release();QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),delivered&&weakOwner.expired()&&weakFiles.expired()),5000);QVERIFY(!failed);QCOMPARE(actual(bundle.physical[0]),QByteArray(16384,'X'));
  }
  void flushFailureReturnsErrorAndRemainsStickyAtVoidBarrier(){
    QTemporaryDir area;QVERIFY(area.isValid());Bundle bundle(area.path(),v1({{"flush.bin",std::string(65536,'A')}}),{Kind::Reordered});auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(bundle.route,bundle.files));Disk disk(policy);auto holder=disk.backend->new_torrent(bundle.parameters(),{});const std::string block(16384,'A');bool wrote=false,writeFailed=false;disk.backend->async_write(holder,{lt::piece_index_t{0},0,16384},block.data(),{},[&](const auto& error){writeFailed=bool(error.ec);wrote=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),wrote),5000);QVERIFY(!writeFailed);bundle.stores[0]->fail.store(true);bool hashed=false;lt::storage_error hashError;disk.backend->async_hash(holder,lt::piece_index_t{0},{},lt::disk_interface::v1_hash|lt::disk_interface::flush_piece,[&](lt::piece_index_t,const lt::sha1_hash&,const lt::storage_error& error){hashError=error;hashed=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),hashed),5000);QVERIFY(hashError.ec);QVERIFY(bundle.files->failure());QCOMPARE(bundle.files->failure()->code,st::ErrorCode::Io);const auto before=actual(bundle.physical[0]);bool barrier=false;disk.backend->async_stop_torrent(holder,[&]{barrier=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),barrier),5000);QVERIFY(bundle.files->failure());bool retried=false;lt::storage_error retryError;const std::string replacement(16384,'B');disk.backend->async_write(holder,{lt::piece_index_t{0},0,16384},replacement.data(),{},[&](const auto& error){retryError=error;retried=true;});QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),retried),5000);QVERIFY(retryError.ec);QCOMPARE(actual(bundle.physical[0]),before);
  }
  void actualQueuedBytesBackpressureResumesBelowLowWater(){
    QTemporaryDir area;QVERIFY(area.isValid());auto gate=std::make_shared<Gate>();Bundle bundle(area.path(),v1({{"pressure.bin",std::string(65536,'A')}}),{Kind::Reordered},gate);auto policy=std::make_shared<ed::MetadataDiskPolicy>();QVERIFY(policy->registerOriginalPath(bundle.route,bundle.files));Disk disk(policy);auto holder=disk.backend->new_torrent(bundle.parameters(),{});ReleaseOnExit release{gate};auto observer=std::make_shared<Observer>();const std::string data(16384,'A');int callbacks=0;bool failed=false,full=false;disk.backend->async_write(holder,{lt::piece_index_t{0},0,16384},data.data(),observer,[&](const auto& error){failed|=bool(error.ec);++callbacks;});QVERIFY(gate->wait());for(int n=1;n<264;++n){full|=disk.backend->async_write(holder,{lt::piece_index_t{0},0,16384},data.data(),observer,[&](const auto& error){failed|=bool(error.ec);++callbacks;});}QVERIFY(full);QCOMPARE(callbacks,0);QCOMPARE(observer->calls,0U);gate->release();QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),callbacks==264),10000);QVERIFY(!failed);QVERIFY(observer->calls>0);QVERIFY(!disk.backend->async_write(holder,{lt::piece_index_t{0},0,16384},data.data(),observer,[&](const auto& error){failed|=bool(error.ec);++callbacks;}));QTRY_VERIFY_WITH_TIMEOUT((disk.pump(),callbacks==265),5000);QVERIFY(!failed);QCOMPARE(actual(bundle.physical[0]),QByteArray(16384,'A'));
  }
};
QTEST_GUILESS_MAIN(OriginalDiskIoTests)
#include "original_disk_io_tests.moc"

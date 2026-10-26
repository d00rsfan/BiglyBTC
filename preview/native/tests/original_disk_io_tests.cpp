#include "metadata_disk_router.hpp"
#include "original_disk_io.hpp"
#include <QtTest>
#include <QCryptographicHash>
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
#include <fcntl.h>
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

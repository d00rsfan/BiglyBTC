#include "managed_checkpoint_inventory.hpp"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <filesystem>
using namespace biglybtc;
namespace {
std::string b(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
void write(const std::filesystem::path& p,const std::string& s){QFile f(QString::fromStdString(p.string()));if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)||f.write(s.data(),s.size())!=static_cast<qint64>(s.size())){qFatal("Generated write");}}
std::string read(const std::filesystem::path& p){QFile f(QString::fromStdString(p.string()));if(!f.open(QIODevice::ReadOnly)){qFatal("Generated read");}return f.readAll().toStdString();}
core::Document parsed(std::string s){auto v=core::Document::parse(std::move(s));if(!v){qFatal("Generated parse");}return std::move(*v);}
std::string encoded(const core::Node& node){auto s=core::encodeValue(node);if(!s){qFatal("Generated encode");}return std::move(*s);}
struct Fixture{
 QTemporaryDir temporary;std::filesystem::path root=temporary.path().toStdString();std::vector<core::TorrentHash> hashes;std::string original;
 explicit Fixture(bool legacy=false,std::size_t count=32){
  std::string rows;
  for(std::size_t n=0;n<count;++n){std::string hash(20,'x');hash[0]=static_cast<char>(n);hashes.push_back(*core::TorrentHash::parse(hash));
   auto entry=parsed("de");entry.replaceValue("torrent_hash",b(hash)).value();entry.replaceValue("torrent",b("generated.torrent")).value();entry.replaceValue("save_dir",b("/generated")).value();entry.replaceValue("save_file",b("generated.bin")).value();entry.replaceValue("state","i70e").value();entry.replaceValue("downloaded","i0e").value();entry.replaceValue("uploaded","i0e").value();entry.replaceValue("opaque",b("preserve generated field")).value();
   rows+=(legacy?b(std::to_string(n)):std::string{})+entry.serialize();
  }
  original=legacy?"d"+rows+"e":"d9:downloadsl"+rows+"e6:opaque8:retained10:pause_datalee";
  write(root/"downloads.config",original);
 }
};
}
class InventoryBatchTests final:public QObject{
 Q_OBJECT
private slots:
 void equivalentToOneExactMergedPublication_data(){QTest::addColumn<bool>("legacy");QTest::newRow("current-list")<<false;QTest::newRow("legacy-map")<<true;}
 void equivalentToOneExactMergedPublication(){
  QFETCH(bool,legacy);Fixture f(legacy);auto lease=storage::ProfileLease::acquire(f.root);QVERIFY(lease);engine::detail::ManagedCheckpointInventory batch;auto expected=parsed(f.original);std::vector<std::string> changed;
  for(std::size_t n=0;n<f.hashes.size();++n){auto saved=batch.select(*lease,f.hashes[n]);QVERIFY2(saved,saved?"":saved.error().message.c_str());const auto& doc=batch.document();const auto& node=legacy?doc.root().children[saved->sourceIndex*2+1]:doc.find("downloads")->children[saved->sourceIndex];auto row=parsed(encoded(node));row.replaceValue("uploaded","i"+std::to_string(n+1)+"e").value();changed.push_back(row.serialize());QVERIFY(batch.stage(*saved,row.serialize()));QCOMPARE(read(f.root/"downloads.config"),f.original);}
  if(legacy){for(std::size_t n=0;n<changed.size();++n){expected.replaceValue(std::to_string(n),changed[n]).value();}}
  else{std::string rows="l";for(const auto& row:changed){rows+=row;}expected.replaceValue("downloads",rows+'e').value();}
  QCOMPARE(batch.pending(),std::size_t{32});QVERIFY(batch.commit(*lease));QCOMPARE(batch.pending(),std::size_t{0});QCOMPARE(read(f.root/"downloads.config"),expected.serialize());QCOMPARE(read(f.root/"downloads.config.bak"),f.original);
 }
 void changedInventoryCannotBeOverwritten(){
  Fixture f;auto lease=storage::ProfileLease::acquire(f.root);QVERIFY(lease);engine::detail::ManagedCheckpointInventory batch;auto saved=batch.select(*lease,f.hashes.front());QVERIFY2(saved,saved?"":saved.error().message.c_str());const auto row=encoded(batch.document().find("downloads")->children.front());QVERIFY(batch.stage(*saved,row));const auto external=parsed(f.original).serialize()+" ";write(f.root/"downloads.config",external);const auto result=batch.commit(*lease);QVERIFY(!result);QCOMPARE(result.error().code,storage::ErrorCode::Conflict);QCOMPARE(read(f.root/"downloads.config"),external);QCOMPARE(batch.pending(),std::size_t{0});
 }
 void existingSavingRefusesTheBatchWithoutRetry(){
  Fixture f;auto lease=storage::ProfileLease::acquire(f.root);QVERIFY(lease);engine::detail::ManagedCheckpointInventory batch;auto saved=batch.select(*lease,f.hashes.front());QVERIFY2(saved,saved?"":saved.error().message.c_str());QVERIFY(batch.stage(*saved,encoded(batch.document().find("downloads")->children.front())));write(f.root/"downloads.config.saving","owned blocker");const auto result=batch.commit(*lease);QVERIFY(!result);QCOMPARE(read(f.root/"downloads.config"),f.original);QCOMPARE(read(f.root/"downloads.config.saving"),std::string("owned blocker"));QCOMPARE(batch.pending(),std::size_t{0});
 }
 void duplicateTargetIsRefusedAndDestructorDoesNotPublish(){
  Fixture f;auto lease=storage::ProfileLease::acquire(f.root);QVERIFY(lease);{engine::detail::ManagedCheckpointInventory batch;auto saved=batch.select(*lease,f.hashes.front());QVERIFY2(saved,saved?"":saved.error().message.c_str());auto row=encoded(batch.document().find("downloads")->children.front());QVERIFY(batch.stage(*saved,row));QVERIFY(!batch.stage(*saved,row));}QCOMPARE(read(f.root/"downloads.config"),f.original);QVERIFY(!std::filesystem::exists(f.root/"downloads.config.bak"));
 }
};
QTEST_GUILESS_MAIN(InventoryBatchTests)
#include "managed_inventory_batch_tests.moc"

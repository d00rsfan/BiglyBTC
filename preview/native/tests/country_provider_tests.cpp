#include "country_provider.hpp"
#include "biglybtc/plugin_host.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc;
core::PeerNumericEndpoint ip(unsigned char last=8){core::PeerNumericEndpoint p;p.family=core::PeerAddressFamily::V4;p.address={8,8,8,last};p.port=1234;return p;}
void state(const QString& directory,char c){QFile file(directory+QStringLiteral("/provider-state"));if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)
    ||file.write(&c,1)!=1||!file.flush()){throw std::runtime_error("Synthetic fixture state write failed");}}
}
class CountryProviderTests final:public QObject{
  Q_OBJECT
private slots:
  void explicitGeneratedModuleHashGuardAndAvailabilityDistinction(){
    // Root supplies the freshly built synthetic module and exact fixture hash.
    // This is controlled test authentication, not production installer proof.
    const auto artifact=qEnvironmentVariable("BIGLYBTC_COUNTRY_PROVIDER_TEST_MODULE");
    const auto expected=qgetenv("BIGLYBTC_COUNTRY_PROVIDER_TEST_SHA256");QVERIFY(!artifact.isEmpty());QCOMPARE(expected.size(),64);
    QFile source(artifact);QVERIFY(source.open(QIODevice::ReadOnly));QVERIFY(source.size()>0&&source.size()<=64*1024*1024);const auto bytes=source.readAll();
    QCOMPARE(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),expected);
    QTemporaryDir area(QStringLiteral("/tmp/biglybtc-country-provider-XXXXXX"));QVERIFY(area.isValid());
    QVERIFY(::chmod(QFile::encodeName(area.path()).constData(),0700)==0);
    const auto installed=area.path()+QStringLiteral("/authenticated-test.so");QFile copied(installed);
    QVERIFY(copied.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(copied.write(bytes),bytes.size());QVERIFY(copied.flush());copied.close();
    QVERIFY(::chmod(QFile::encodeName(installed).constData(),0600)==0);
    state(area.path(),'M');const int fd=::open(QFile::encodeName(area.path()).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);QVERIFY(fd>=0);
    struct Close{int fd;~Close(){::close(fd);}} close{fd};
    bt_plugin_options_v1 options{sizeof(bt_plugin_options_v1),BT_PLUGIN_ABI_V1,fd,0,"0.0",nullptr,nullptr};
    auto module=plugins::Module::load(installed.toStdString(),"azlocprov",BT_CAP_LOCATION,options,"country.test");QVERIFY(module);
    auto provider=storage::CountryProvider::bind(std::move(*module));QVERIFY(provider);QVERIFY(!(*provider)->available());
    QCOMPARE((*provider)->lookup(ip()).kind,core::CountryLookupKind::Unavailable); // Missing DB is not Unknown.
    state(area.path(),'A');QVERIFY((*provider)->available());const auto known=(*provider)->lookup(ip());
    QCOMPARE(known.kind,core::CountryLookupKind::Country);QCOMPARE(QByteArray(known.code.data(),2),QByteArray("US"));
    QCOMPARE((*provider)->lookup(ip(0)).kind,core::CountryLookupKind::Unknown);
    QCOMPARE((*provider)->lookup(ip(255)).kind,core::CountryLookupKind::Unavailable); // Provider lookup error.
    core::PeerPayloadSnapshot ledger;ledger.identity={1,2,3};ledger.sequence=7;ledger.count=2;
    for(std::size_t i=0;i<ledger.count;++i){auto& p=ledger.records[i];p.connection=i+11;p.operation=1;
      p.phase=core::PeerPayloadPhase::Connected;p.remote=ip();p.latestSampleAvailable=true;
      p.lastObserved=core::PeerPayloadSample{9999999,8888888,101,23};p.network=core::PeerNetworkClass::NonLocal;}
    ledger.records[0].network=core::PeerNetworkClass::Local;ledger.records[0].lastObserved=core::PeerPayloadSample{};
    const auto classified=(*provider)->snapshot(ledger);QVERIFY(classified);QCOMPARE(classified->rowCount,std::size_t{2});
    QCOMPARE(classified->rows[0].kind,core::CountryRowKind::Local);QCOMPARE(classified->rows[0].observedDownloadRate,std::optional<std::int64_t>{0});
    QCOMPARE(classified->rows[1].kind,core::CountryRowKind::Country);QCOMPARE(classified->rows[1].observedDownloadRate,std::optional<std::int64_t>{101});
    state(area.path(),'M');QVERIFY(!(*provider)->available());QCOMPARE((*provider)->lookup(ip()).kind,core::CountryLookupKind::Unavailable);
    const auto unavailable=(*provider)->snapshot(ledger);QVERIFY(unavailable);QCOMPARE(unavailable->sequence,classified->sequence);
    QCOMPARE(unavailable->rows[0].kind,core::CountryRowKind::Local);QCOMPARE(unavailable->rows[1].kind,core::CountryRowKind::Unavailable);
    QCOMPARE(known.kind,core::CountryLookupKind::Country); // Previous value owned, not reused as current.
    bool foreign=true;std::thread other([&]{foreign=(*provider)->available();});other.join();QVERIFY(!foreign);
    QVERIFY(!storage::CountryProvider::bind(std::move(*module))); // Moved-from Module rejected without stale API read.
  }
  void actualLocationModuleUsesOnlyExplicitPinnedSyntheticDatabaseAndDetectsRemoval(){
    // No fetch/search fallback: root must provide the already owned official
    // synthetic MaxMind test DB and the built amended actual location module.
    const auto artifact=qEnvironmentVariable("BIGLYBTC_COUNTRY_LOCATION_TEST_MODULE");
    const auto expected=qgetenv("BIGLYBTC_COUNTRY_LOCATION_TEST_SHA256");
    const auto nativeVersion=qgetenv("BIGLYBTC_COUNTRY_LOCATION_TEST_VERSION");
    QVERIFY(!artifact.isEmpty());QCOMPARE(expected.size(),64);QVERIFY(!nativeVersion.isEmpty());
    QFile binary(artifact);QVERIFY(binary.open(QIODevice::ReadOnly));QVERIFY(binary.size()>0&&binary.size()<=64*1024*1024);const auto moduleBytes=binary.readAll();
    QCOMPARE(QCryptographicHash::hash(moduleBytes,QCryptographicHash::Sha256).toHex(),expected);
    QFile fixture(qEnvironmentVariable("BIGLYBTC_COUNTRY_MMDB_TEST_FIXTURE"));QVERIFY(fixture.open(QIODevice::ReadOnly));
    QVERIFY(fixture.size()>0&&fixture.size()<=1024*1024);const auto database=fixture.readAll();
    QCOMPARE(QCryptographicHash::hash(database,QCryptographicHash::Sha256).toHex(),
        QByteArray("ff5a273c91d332bcfb4d808cc128c4f98cb59bb55f75931b64ff69022ddfa1bc"));
    QTemporaryDir area(QStringLiteral("/tmp/biglybtc-country-real-module-XXXXXX"));QVERIFY(area.isValid());
    QVERIFY(::chmod(QFile::encodeName(area.path()).constData(),0700)==0);
    const auto installed=area.path()+QStringLiteral("/authenticated-location.so");QFile copied(installed);
    QVERIFY(copied.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(copied.write(moduleBytes),moduleBytes.size());QVERIFY(copied.flush());copied.close();
    QVERIFY(::chmod(QFile::encodeName(installed).constData(),0400)==0);
    const int fd=::open(QFile::encodeName(area.path()).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);QVERIFY(fd>=0);
    struct Close{int fd;~Close(){::close(fd);}} close{fd};
    const auto create=[&]{bt_plugin_options_v1 options{sizeof(bt_plugin_options_v1),BT_PLUGIN_ABI_V1,fd,0,"0.0",nullptr,nullptr};
      auto module=plugins::Module::load(installed.toStdString(),"azlocprov",BT_CAP_LOCATION,options,nativeVersion.toStdString());
      if(!module){throw std::runtime_error("Actual fixture Module guard rejected artifact");}
      auto result=storage::CountryProvider::bind(std::move(*module));if(!result){throw std::runtime_error("Actual fixture provider bind failed");}return std::move(*result);};
    auto missing=create();QVERIFY(!missing->available());QCOMPARE(missing->lookup(ip()).kind,core::CountryLookupKind::Unavailable);
    const auto dbPath=area.path()+QStringLiteral("/GeoLite2-Country.mmdb");
    QVERIFY(::mkfifo(QFile::encodeName(dbPath).constData(),0600)==0);auto fifo=create();QVERIFY(!fifo->available());QVERIFY(QFile::remove(dbPath));
    const auto target=area.path()+QStringLiteral("/owned-db-target");QFile targetFile(target);
    QVERIFY(targetFile.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(targetFile.write(database),database.size());QVERIFY(targetFile.flush());targetFile.close();
    QVERIFY(::symlink("owned-db-target",QFile::encodeName(dbPath).constData())==0);auto symlink=create();QVERIFY(!symlink->available());QVERIFY(QFile::remove(dbPath));
    QVERIFY(::link(QFile::encodeName(target).constData(),QFile::encodeName(dbPath).constData())==0);auto hardlink=create();QVERIFY(!hardlink->available());QVERIFY(QFile::remove(dbPath));
    QFile db(dbPath);
    QVERIFY(db.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(db.write(database),database.size());QVERIFY(db.flush());db.close();
    QVERIFY(::chmod(QFile::encodeName(dbPath).constData(),0600)==0);struct stat before{};
    QVERIFY(::lstat(QFile::encodeName(dbPath).constData(),&before)==0);
    QVERIFY(::chmod(QFile::encodeName(area.path()).constData(),0755)==0);auto publicDirectory=create();QVERIFY(!publicDirectory->available());
    QVERIFY(::chmod(QFile::encodeName(area.path()).constData(),0700)==0);
    auto actual=create();QVERIFY(actual->available());auto knownIP=ip();knownIP.address={81,2,69,160};
    const auto known=actual->lookup(knownIP);QCOMPARE(known.kind,core::CountryLookupKind::Country);
    QCOMPARE(QByteArray(known.code.data(),2),QByteArray("GB"));QCOMPARE(QByteArray(known.name.data()),QByteArray("United Kingdom"));
    auto local=ip();local.address={127,0,0,1};QCOMPARE(actual->lookup(local).kind,core::CountryLookupKind::Unknown);
    QFile unchanged(dbPath);QVERIFY(unchanged.open(QIODevice::ReadOnly));QCOMPARE(unchanged.readAll(),database);unchanged.close();
    struct stat after{};QVERIFY(::lstat(QFile::encodeName(dbPath).constData(),&after)==0);
    QCOMPARE(after.st_dev,before.st_dev);QCOMPARE(after.st_ino,before.st_ino);QCOMPARE(after.st_size,before.st_size);
    QCOMPARE(after.st_mtim.tv_sec,before.st_mtim.tv_sec);QCOMPARE(after.st_mtim.tv_nsec,before.st_mtim.tv_nsec);
    QCOMPARE(after.st_ctim.tv_sec,before.st_ctim.tv_sec);QCOMPARE(after.st_ctim.tv_nsec,before.st_ctim.tv_nsec);
    // Provider descriptor/map survives, but a removed DB is no longer usable.
    QVERIFY(QFile::remove(dbPath));QVERIFY(!actual->available());
    QCOMPARE(actual->lookup(knownIP).kind,core::CountryLookupKind::Unavailable);
    QCOMPARE(QByteArray(known.code.data(),2),QByteArray("GB")); // Owned old result, never current-cache fallback.
  }
};
QTEST_GUILESS_MAIN(CountryProviderTests)
#include "country_provider_tests.moc"

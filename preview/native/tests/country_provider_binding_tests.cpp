#include "country_provider_binding.hpp"
#include "biglybtc/plugin_host.hpp"
#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>

using namespace biglybtc;
namespace {
void require(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
QByteArray bytes(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot read supplied owned test input");require(f.size()>0&&f.size()<=64*1024*1024,"Fixture input exceeds bound");return f.readAll();}
void write(const QString& path,const QByteArray& data){QFile f(path);require(f.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create owned fixture");require(f.write(data)==data.size()&&f.flush(),"Cannot write owned fixture");}
struct Fixture {
  QTemporaryDir directory{QStringLiteral("/tmp/biglybtc-country-binding-XXXXXX")};
  QString module,db,profile;storage::CountryModuleApproval approval;
  QByteArray moduleBytes,databaseBytes;
  Fixture(){
    require(directory.isValid(),"Cannot create country binding fixture");
    moduleBytes=bytes(qEnvironmentVariable("BIGLYBTC_COUNTRY_LOCATION_TEST_MODULE"));
    const auto trustedHash=qgetenv("BIGLYBTC_COUNTRY_LOCATION_TEST_SHA256");
    require(trustedHash.size()==64&&QCryptographicHash::hash(moduleBytes,QCryptographicHash::Sha256).toHex()==trustedHash,"Expected approved actual module hash");
    const auto version=qgetenv("BIGLYBTC_COUNTRY_LOCATION_TEST_VERSION");require(!version.isEmpty(),"Expected actual module version");
    approval={trustedHash.toStdString(),static_cast<std::uint64_t>(moduleBytes.size()),version.toStdString()};
    databaseBytes=bytes(qEnvironmentVariable("BIGLYBTC_COUNTRY_MMDB_TEST_FIXTURE"));
    require(QCryptographicHash::hash(databaseBytes,QCryptographicHash::Sha256).toHex()==QByteArray("ff5a273c91d332bcfb4d808cc128c4f98cb59bb55f75931b64ff69022ddfa1bc"),"Expected pinned official synthetic MMDB");
    module=directory.path()+QStringLiteral("/approved-location.so");write(module,moduleBytes);require(::chmod(QFile::encodeName(module).constData(),0400)==0,"Cannot protect fixture module");
    db=directory.path()+QStringLiteral("/db");profile=directory.path()+QStringLiteral("/profile");
    require(::mkdir(QFile::encodeName(db).constData(),0700)==0&&::mkdir(QFile::encodeName(profile).constData(),0700)==0,"Cannot create private fixture directories");
    write(db+QStringLiteral("/GeoLite2-Country.mmdb"),databaseBytes);
    require(::chmod(QFile::encodeName(db+QStringLiteral("/GeoLite2-Country.mmdb")).constData(),0600)==0,"Cannot protect fixture database");
  }
};
core::PeerNumericEndpoint gb(){core::PeerNumericEndpoint p;p.family=core::PeerAddressFamily::V4;p.address={81,2,69,160};return p;}
}
class CountryProviderBindingTests final:public QObject {
  Q_OBJECT
private slots:
  void approvedModuleAndExplicitDataProvideActualGeography(){
    Fixture fixture;auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);
    const auto raw=bytes(fixture.db+QStringLiteral("/GeoLite2-Country.mmdb"));
    auto provider=storage::loadApprovedCountryProvider(*lease,fixture.module.toStdString(),fixture.approval,fixture.db.toStdString());
    QVERIFY2(provider,provider?"":provider.error().message.c_str());QVERIFY((*provider)->available());
    const auto value=(*provider)->lookup(gb());QCOMPARE(value.kind,core::CountryLookupKind::Country);
    QCOMPARE(QByteArray(value.code.data(),2),QByteArray("GB"));QCOMPARE(QByteArray(value.name.data()),QByteArray("United Kingdom"));
    auto unknown=gb();unknown.address={127,0,0,1};QCOMPARE((*provider)->lookup(unknown).kind,core::CountryLookupKind::Unknown);
    QCOMPARE(bytes(fixture.module),fixture.moduleBytes);QCOMPARE(bytes(fixture.db+QStringLiteral("/GeoLite2-Country.mmdb")),raw);
    QVERIFY(QFile::remove(fixture.db+QStringLiteral("/GeoLite2-Country.mmdb")));QVERIFY(!(*provider)->available());
    QCOMPARE((*provider)->lookup(gb()).kind,core::CountryLookupKind::Unavailable);
    QCOMPARE(QByteArray(value.code.data(),2),QByteArray("GB")); // Owned old result, not fresh fallback.
  }
  void rejectedInputsNeverBecomeAnApprovedProvider_data(){
    QTest::addColumn<int>("mutation");for(int i=0;i<11;++i){auto name=QByteArray::number(i);QTest::newRow(name.constData())<<i;}
  }
  void rejectedInputsNeverBecomeAnApprovedProvider(){
    QFETCH(int,mutation);Fixture fixture;auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);
    auto module=fixture.module;auto db=fixture.db;auto approval=fixture.approval;
    switch(mutation){
      case 0:approval.sha256[0]=approval.sha256[0]=='0'?'1':'0';break;
      case 1:++approval.bytes;break;
      case 2:approval.version="99.99";break;
      case 3:approval.sha256="profile-controlled-trust-is-not-a-hash";break;
      case 4:QVERIFY(::chmod(QFile::encodeName(module).constData(),0666)==0);break;
      case 5:QVERIFY(::chmod(QFile::encodeName(db).constData(),0755)==0);break;
      case 6:{const auto alias=fixture.directory.path()+QStringLiteral("/module-alias");QVERIFY(::symlink(QFile::encodeName(module).constData(),QFile::encodeName(alias).constData())==0);module=alias;break;}
      case 7:{const auto alias=fixture.directory.path()+QStringLiteral("/module-hardlink");QVERIFY(::link(QFile::encodeName(module).constData(),QFile::encodeName(alias).constData())==0);module=alias;break;}
      case 8:{const auto fifo=fixture.directory.path()+QStringLiteral("/module-fifo");QVERIFY(::mkfifo(QFile::encodeName(fifo).constData(),0600)==0);module=fifo;break;}
      case 9:{const auto alias=fixture.directory.path()+QStringLiteral("/db-alias");QVERIFY(::symlink(QFile::encodeName(db).constData(),QFile::encodeName(alias).constData())==0);db=alias;break;}
      case 10:module=QStringLiteral("relative-module.so");break;
    }
    const auto result=storage::loadApprovedCountryProvider(*lease,module.toStdString(),approval,db.toStdString());QVERIFY(!result);
  }
  void descriptorLoaderUsesAuthenticatedInodeAfterLeafReplacement(){
    Fixture fixture;const int file=::open(QFile::encodeName(fixture.module).constData(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    QVERIFY(file>=0);struct Close{int file;~Close(){::close(file);}} close{file};
    // Hash this descriptor before replacement, as the binding does. Then make
    // the old path untrustworthy; loading it instead of the fd cannot succeed.
    QFile held;QVERIFY(held.open(file,QIODevice::ReadOnly,QFileDevice::DontCloseHandle));
    QCOMPARE(QCryptographicHash::hash(held.readAll(),QCryptographicHash::Sha256).toHex().toStdString(),fixture.approval.sha256);held.close();
    QVERIFY(QFile::rename(fixture.module,fixture.module+QStringLiteral(".retained")));write(fixture.module,QByteArray("untrusted replacement, never execute"));
    const int db=::open(QFile::encodeName(fixture.db).constData(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);QVERIFY(db>=0);Close closeDb{db};
    const bt_plugin_options_v1 options{sizeof(bt_plugin_options_v1),BT_PLUGIN_ABI_V1,db,0,"0.0",nullptr,nullptr};
    auto loaded=plugins::Module::loadDescriptor(file,"azlocprov",BT_CAP_LOCATION,options,fixture.approval.version);QVERIFY(loaded);
    auto provider=storage::CountryProvider::bind(std::move(*loaded));QVERIFY(provider);QVERIFY((*provider)->available());
    QCOMPARE((*provider)->lookup(gb()).kind,core::CountryLookupKind::Country);
    QVERIFY(!plugins::Module::loadDescriptor(-1,"azlocprov",BT_CAP_LOCATION,options,fixture.approval.version));
  }
};
QTEST_GUILESS_MAIN(CountryProviderBindingTests)
#include "country_provider_binding_tests.moc"

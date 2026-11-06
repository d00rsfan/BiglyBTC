// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_rss_source.hpp"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {
namespace s=biglybtc::search;
namespace storage=biglybtc::storage;
using Fields=std::vector<std::pair<std::string,std::string>>;
std::string b(std::string_view text){return std::to_string(text.size())+':'+std::string(text);}
std::string n(std::int64_t value){return 'i'+std::to_string(value)+'e';}
std::string d(const Fields& fields){std::string out="d";for(const auto& [key,value]:fields){out+=b(key)+value;}return out+'e';}
void set(Fields& fields,std::string key,std::string value){
  const auto found=std::ranges::find_if(fields,[&](const auto& entry){return entry.first==key;});
  if(found==fields.end()){fields.emplace_back(std::move(key),std::move(value));}else{found->second=std::move(value);}
}
void eraseField(Fields& fields,std::string_view key){std::erase_if(fields,[&](const auto& entry){return entry.first==key;});}
Fields row(std::string url="https://feeds.example.org/a/feed.xml?literal=1"){
  return {{"type",n(4)},{"id",n(7)},{"uid",b(std::string("u\0id",4))},{"name",b("Owned RSS")},
      {"selected",n(0)},{"web.search_url_format",b(url)},{"web.maps","le"},{"opaque",b(std::string("\0\xff",2))}};
}
std::string catalog(const Fields& one){return d({{"engines","l"+d(one)+"e"},{"future",b(std::string("\0\xfe",2))}});}
QByteArray emptyJks(){
  QByteArray body;QDataStream stream(&body,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::BigEndian);
  stream<<quint32{0xfeedfeed}<<quint32{2}<<quint32{0};
  return body+QCryptographicHash::hash(QByteArray::fromHex("006300680061006e0067006500690074")+"Mighty Aphrodite"+body,QCryptographicHash::Sha1);
}
void write(const QString& path,const QByteArray& bytes){
  QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)||file.write(bytes)!=bytes.size()||!file.flush()){throw std::runtime_error("Cannot write private source fixture");}
}
QByteArray read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("Cannot read private source fixture");}return file.readAll();}
struct Fixture {
  QTemporaryDir root;
  QString profile;
  std::optional<storage::ProfileLease> lease;
  explicit Fixture(const QByteArray& meta=QByteArray::fromStdString(catalog(row())),const QByteArray& settings="de"){
    if(!root.isValid()){throw std::runtime_error("Cannot create private profile fixture");}
    profile=root.filePath("profile");if(!QDir().mkpath(profile)){throw std::runtime_error("Cannot create private profile directory");}
    write(profile+"/metasearch.config",meta);write(profile+"/biglybt.config",settings);
    auto actual=storage::ProfileLease::acquire(profile.toStdString());if(!actual){throw std::runtime_error("Cannot acquire actual private ProfileLease");}lease.emplace(std::move(*actual));
  }
  static s::SavedRssSelector selector(){return {0,7,std::string("u\0id",4)};}
  std::expected<s::SavedRssSourceSnapshot,s::SavedRssSourceError> snapshot(){
    return s::SavedRssSourceSnapshot::read(*lease,profile.toStdString(),41,selector(),s::SavedRssReadReason::ExplicitOneShotDescription);
  }
};
} // namespace

class SavedRssSourceTests final:public QObject {
  Q_OBJECT
private slots:
  void actualLeaseOwnsDescriptionWithoutSelectionOrEffectAuthority(){
    const auto url="https://feeds.example.org/a/feed.xml?next=/nested/value";
    const auto meta=QByteArray::fromStdString(catalog(row(url)));Fixture f(meta);
    auto made=f.snapshot();QVERIFY(made);QVERIFY(made->current(*f.lease,41));
    const auto& value=made->description();QVERIFY(value.literalGetUrl==url);QVERIFY(value.selected.uid==Fixture::selector().uid);
    QCOMPARE(value.context.rootPage,QStringLiteral("https://feeds.example.org"));
    QCOMPARE(value.context.basePage,QStringLiteral("https://feeds.example.org/a/feed.xml?next=/nested/"));
    QCOMPARE(value.documentLimits.bytes,std::size_t{4*1024*1024});QCOMPARE(value.responseOctets,std::size_t{8*1024*1024});
    QCOMPARE(value.maximumGets,6u);QVERIFY(!value.trustAnchors);QCOMPARE(read(f.profile+"/metasearch.config"),meta);
    auto invalid=s::SavedRssSourceSnapshot::read(*f.lease,f.profile.toStdString(),41,Fixture::selector(),static_cast<s::SavedRssReadReason>(99));QVERIFY(!invalid);
    QVERIFY(!s::SavedRssSourceSnapshot::read(*f.lease,f.profile.toStdString(),0,Fixture::selector(),s::SavedRssReadReason::ExplicitOneShotDescription));
    auto selected=Fixture::selector();selected.uid="other";QVERIFY(!s::SavedRssSourceSnapshot::read(*f.lease,f.profile.toStdString(),41,selected,s::SavedRssReadReason::ExplicitOneShotDescription));
    selected=Fixture::selector();selected.sourceIndex=1;QVERIFY(!s::SavedRssSourceSnapshot::read(*f.lease,f.profile.toStdString(),41,selected,s::SavedRssReadReason::ExplicitOneShotDescription));
  }
  void sameOFDWorkerSessionAndLastAliasLifetimeAreConcrete(){
    Fixture f;auto made=f.snapshot();QVERIFY(made);std::optional<s::SavedRssSourceSnapshot> snapshot; snapshot.emplace(std::move(*made));
    auto alias=f.lease->retain();QVERIFY(alias);QVERIFY(snapshot->current(**alias,41));
    Fixture other;QVERIFY(!snapshot->current(*other.lease,41));QVERIFY(!snapshot->current(*f.lease,42));
    std::optional<s::SavedRssSourceError> error;
    std::thread wrong([&]{const auto result=snapshot->current(*f.lease,41);if(!result){error=result.error();}});wrong.join();
    QVERIFY(error&&*error==s::SavedRssSourceError::WrongWorker);QVERIFY(snapshot->current(*f.lease,41));
    f.lease.reset();QVERIFY(!storage::ProfileLease::acquire(f.profile.toStdString()));QVERIFY(snapshot->current(**alias,41));
    alias->reset();QVERIFY(!storage::ProfileLease::acquire(f.profile.toStdString()));snapshot.reset();
    auto reacquired=storage::ProfileLease::acquire(f.profile.toStdString());QVERIFY(reacquired);
  }
  void unrelatedProfileWritesCanRefreshObservationButNestedAndRelevantChangesRefuse(){
    Fixture f;auto original=f.snapshot();QVERIFY(original);const auto before=original->current(*f.lease,41);QVERIFY(before);
    auto unrelated=storage::ConfigFile::createMissing(*f.lease,"unrelated.config","d6:futurei1ee");QVERIFY(unrelated);
    const auto refreshed=original->current(*f.lease,41);QVERIFY(refreshed&&*refreshed>*before);QCOMPARE(*refreshed,f.lease->sourceRevision());
    {auto nested=f.lease->sourceAccess();QVERIFY(nested);QVERIFY(!original->current(*f.lease,41));QVERIFY(!f.snapshot());}
    QVERIFY(original->current(*f.lease,41));
    write(f.profile+"/biglybt.config",QByteArray::fromStdString(d({{"future",b("changed")}})));
    QVERIFY(!original->current(*f.lease,41));auto fresh=f.snapshot();QVERIFY(fresh&&fresh->current(*f.lease,41));
  }
  void namedSourceMutationIsDetectedAndFreshSnapshotRecovers_data(){
    QTest::addColumn<int>("mode");QTest::newRow("primary-bytes")<<0;QTest::newRow("same-bytes-new-inode")<<1;
    QTest::newRow("settings-unknown-field")<<2;QTest::newRow("new-backup-candidate")<<3;QTest::newRow("saving-requires-recovery")<<4;
    QTest::newRow("trust-appearance")<<5;QTest::newRow("directory-replaced")<<6;
  }
  void namedSourceMutationIsDetectedAndFreshSnapshotRecovers(){
    QFETCH(int,mode);Fixture f;auto old=f.snapshot();QVERIFY(old);
    if(mode==0){auto changed=row();set(changed,"opaque",b("new"));write(f.profile+"/metasearch.config",QByteArray::fromStdString(catalog(changed)));}
    else if(mode==1){const auto bytes=read(f.profile+"/metasearch.config");QVERIFY(QFile::rename(f.profile+"/metasearch.config",f.profile+"/old-meta"));write(f.profile+"/metasearch.config",bytes);}
    else if(mode==2){write(f.profile+"/biglybt.config",QByteArray::fromStdString(d({{"future",b("changed")}})));}
    else if(mode==3){write(f.profile+"/metasearch.config.bak","de");}
    else if(mode==4){write(f.profile+"/metasearch.config.saving","de");}
    else if(mode==5){write(f.profile+"/.certs",emptyJks());}
    else{QVERIFY(QDir().rename(f.profile,f.root.filePath("old-profile")));QVERIFY(QDir().mkpath(f.profile));}
    QVERIFY(!old->current(*f.lease,41));
    if(mode==4){QVERIFY(!f.snapshot());QVERIFY(QFile::remove(f.profile+"/metasearch.config.saving"));}
    if(mode!=6){auto fresh=f.snapshot();QVERIFY(fresh);QVERIFY(fresh->current(*f.lease,41));}
    else{QVERIFY(!f.snapshot());}
  }
  void realSavedTrustAbsenceEmptyStoreAndReplacementStayDistinct(){
    Fixture f;auto system=f.snapshot();QVERIFY(system&&!system->description().trustAnchors);
    write(f.profile+"/.certs",emptyJks());QVERIFY(!system->current(*f.lease,41));
    auto empty=f.snapshot();QVERIFY(empty&&empty->description().trustAnchors&&empty->description().trustAnchors->empty());
    write(f.profile+"/.certs","not-a-JKS");QVERIFY(!empty->current(*f.lease,41));QVERIFY(!f.snapshot());
  }
  void unsupportedOrAmbiguousRawDefinitions_data(){
    QTest::addColumn<QByteArray>("meta");QTest::addColumn<QByteArray>("settings");
    const auto add=[&](const char* name,const Fields& changed,const std::string& config="de"){
      QTest::newRow(name)<<QByteArray::fromStdString(catalog(changed))<<QByteArray::fromStdString(config);
    };
    const auto alter=[&](const char* name,const char* key,std::string value){auto changed=row();set(changed,key,std::move(value));add(name,changed);};
    alter("parameter-template","web.search_url_format",b("https://feeds.example.org/f?q=%s"));
    alter("post-marker","web.search_url_format",b("https://feeds.example.org/f?azmethod=post_basic:a=b"));
    alter("cookie-marker","web.search_url_format",b("https://feeds.example.org/f:COOKIE:secret"));
    alter("needs-auth","web.needs_auth",n(1));alter("cookies","web.full_cookies",b("secret"));
    alter("required-cookies","web.required_cookies","l"+b("sid")+"e");alter("login-page","web.login_page",b("https://login.example.org/"));
    alter("custom-headers","web.headers",b("X-Token: secret"));alter("auth-provider","web.auth_method",b("proxy"));
    alter("plugin-type","type",n(3));alter("newer-az-version","az_version",n(6));alter("java-wrap-not-literal-type","type",n(4294967300LL));
    alter("mapping-provider","web.maps","ldee");alter("local-mapping","l1_map","ldee");
    alter("update-provider","update_url",b("https://update.example.org/"));alter("malformed-selection","selected",b("yes"));
    auto noMaps=row();eraseField(noMaps,"web.maps");add("missing-required-web-maps",noMaps);
    auto duplicateKey=row();duplicateKey.emplace_back("id",n(7));add("duplicate-key",duplicateKey);
    auto second=row();set(second,"uid",b("second"));QTest::newRow("duplicate-id")<<QByteArray::fromStdString(d({{"engines","l"+d(row())+d(second)+"e"}}))<<QByteArray("de");
    second=row();set(second,"id",n(8));QTest::newRow("duplicate-uid")<<QByteArray::fromStdString(d({{"engines","l"+d(row())+d(second)+"e"}}))<<QByteArray("de");
    add("proxy-type",row(),d({{"metasearch.config.proxy.type",n(1)}}));add("legacy-tor-proxy",row(),d({{"metasearch.config.proxy.enable",n(1)}}));
    add("global-proxy",row(),d({{"Enable.Proxy",n(1)}}));add("engine-local-cookies",row(),d({{"metasearch.engine.7.local",d({{"cookies",b("secret")}})}}));
    add("malformed-settings",row(),"broken");
    for(const auto& [name,url]:std::vector<std::pair<const char*,std::string>>{
        {"userinfo","https://user:pass@feeds.example.org/f"},{"fragment","https://feeds.example.org/f#x"},
        {"tor","tor:https://feeds.example.org/f"},{"i2p","i2p:https://feeds.example.org/f"},
        {"onion","https://owned.onion/f"},{"loopback","http://127.0.0.1/f"},{"private-v4","http://10.0.0.1/f"},
        {"mapped-v6","http://[::ffff:127.0.0.1]/f"},{"scoped-v6","http://[fe80::1%25lo]/f"},
        {"noncanonical-scheme","HTTPS://feeds.example.org/f"},{"percent-literal-unqualified","https://feeds.example.org/a%20b"}}){
      alter(name,"web.search_url_format",b(url));
    }
    QTest::newRow("malformed-catalog")<<QByteArray("d7:enginesi1ee")<<QByteArray("de");
  }
  void unsupportedOrAmbiguousRawDefinitions(){
    QFETCH(QByteArray,meta);QFETCH(QByteArray,settings);Fixture f(meta,settings);
    QVERIFY(!f.snapshot());QCOMPARE(read(f.profile+"/metasearch.config"),meta);QCOMPARE(read(f.profile+"/biglybt.config"),settings);
  }
  void unsafeCandidatesAndClosedByteBounds_data(){
    QTest::addColumn<int>("mode");QTest::newRow("symlink-primary")<<0;QTest::newRow("hardlink-primary")<<1;
    QTest::newRow("backup-without-primary")<<2;QTest::newRow("meta-over-four-MiB")<<3;
    QTest::newRow("settings-over-one-MiB")<<4;QTest::newRow("trust-over-four-MiB")<<5;
  }
  void unsafeCandidatesAndClosedByteBounds(){
    QFETCH(int,mode);Fixture f;const auto meta=f.profile+"/metasearch.config";
    if(mode==0){QVERIFY(QFile::rename(meta,f.profile+"/real-meta"));QVERIFY(QFile::link(f.profile+"/real-meta",meta));}
    else if(mode==1){QVERIFY(::link(meta.toStdString().c_str(),(f.profile+"/linked-meta").toStdString().c_str())==0);}
    else if(mode==2){QVERIFY(QFile::rename(meta,meta+".bak"));}
    else if(mode==3){write(meta,QByteArray(4*1024*1024+1,'x'));}
    else if(mode==4){write(f.profile+"/biglybt.config",QByteArray(1024*1024+1,'x'));}
    else{write(f.profile+"/.certs",QByteArray(4*1024*1024+1,'x'));}
    QVERIFY(!f.snapshot());
  }
};
QTEST_GUILESS_MAIN(SavedRssSourceTests)
#include "saved_rss_source_tests.moc"

// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_azureus_service.hpp"
#include "biglybtc/azureus_endpoint.hpp"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QScopeGuard>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <algorithm>
#include <future>
#include <map>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
namespace {
using namespace biglybtc;using namespace std::chrono_literals;namespace udp=services::azureus_udp;
bool namespaceChanged(const char* environment,const char* name){bool parsed=false;const int fd=qEnvironmentVariableIntValue(environment,&parsed);if(!parsed||fd<0){return false;}const auto path=QByteArray("/proc/self/fd/")+QByteArray::number(fd);std::array<char,128> target{};const auto size=::readlink(path.constData(),target.data(),target.size());if(size<=0||size>=static_cast<ssize_t>(target.size())){return false;}const std::string_view link(target.data(),static_cast<std::size_t>(size));if((std::string_view(name).ends_with("/net")&&!link.starts_with("net:["))||(std::string_view(name).ends_with("/user")&&!link.starts_with("user:["))){return false;}struct stat parent{},current{};return ::fstat(fd,&parent)==0&&::stat(name,&current)==0&&(parent.st_dev!=current.st_dev||parent.st_ino!=current.st_ino);}
bool success(const app::OwnedAzureusService& owner,std::uint64_t request){const auto v=owner.observation();return v.lastResult&&v.lastResult->request==request&&v.lastResult->terminal&&!v.lastResult->error&&!v.lastResult->transportFailure;}
QByteArray read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly)){return {};}return file.readAll();}
std::string encoded(std::string_view bytes){return std::to_string(bytes.size())+':'+std::string(bytes);}
std::string configuration(const storage::AzureusPublicBind& bind,const std::string& address){
  std::map<std::string,std::string> values{{"Additional Service Bind IPs",encoded("")},{"Bind IP",encoded(address)},{"Enable.Proxy","i0e"},{"Enable.SOCKS","i0e"},{"Enforce Bind IP","i1e"},{"IPV6 Enable Support","i1e"},{"Ip Filter Enabled","i0e"},{"Plugin.DHT.dht.advanced","i0e"},{"Plugin.DHT.dht.enabled","i1e"},{"Plugin.DHT.dht.override.ip",encoded("203.0.113.3")},{"Proxy.Data.Enable","i0e"},{"UDP.NonData.Listen.Port","i"+std::to_string(bind.port)+"e"},{"dht.net.main_v4.enable","i1e"},{"dht.net.main_v6.enable","i1e"},{"future",encoded("retained-public-policy")}};
  std::string out="d";for(const auto& [key,value]:values){out+=encoded(key)+value;}return out+'e';
}
storage::AzureusPublicBind binding(const QHostAddress& address,quint16 port){storage::AzureusPublicBind out;out.family=address.protocol()==QAbstractSocket::IPv4Protocol?4:16;out.network=out.family==4?0:3;out.port=port;if(out.family==4){const auto ip=address.toIPv4Address();for(unsigned i=0;i<4;++i){out.address[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}else{const auto ip=address.toIPv6Address();std::copy_n(ip.c,16,out.address.begin());}return out;}
struct Normal {
  QTemporaryDir root;std::optional<storage::ProfileLease> lease;std::shared_ptr<storage::AzureusValueVersions> versions;std::shared_ptr<storage::AzureusContacts> contacts;std::shared_ptr<storage::AzureusPublicPolicy> policy;std::unique_ptr<app::OwnedAzureusService> owner;QHostAddress address;quint16 port=0;
  static std::expected<std::unique_ptr<Normal>,QString> make(const QString& address,quint16 port){
    auto out=std::make_unique<Normal>();out->address=QHostAddress(address);out->port=port;if(!out->root.isValid()){return std::unexpected(QStringLiteral("Owned public profile directory unavailable"));}auto lease=storage::ProfileLease::acquire(out->root.path().toStdString());if(!lease){return std::unexpected(QString::fromStdString(lease.error().message));}out->lease.emplace(std::move(*lease));const auto selected=binding(out->address,port);auto config=storage::ConfigFile::createMissing(*out->lease,"biglybt.config",configuration(selected,address.toStdString()));if(!config){return std::unexpected(QString::fromStdString(config.error().message));}auto started=out->start();if(!started){return std::unexpected(started.error());}return out;
  }
  std::expected<void,QString> start(){
    auto v=storage::AzureusValueVersions::open(*lease,binding(address,port).network);if(!v){return std::unexpected(QString::fromStdString(v.error().message));}versions=*v;
    auto c=storage::AzureusContacts::open(*lease,versions->network(),binding(address,port).family,storage::AzureusContactScope::PublicBound);if(!c){return std::unexpected(QString::fromStdString(c.error().message));}contacts=*c;
    auto p=storage::AzureusPublicPolicy::open(*lease,versions,binding(address,port));if(!p){return std::unexpected(QString::fromStdString(p.error().message));}policy=*p;
    auto created=app::OwnedAzureusService::create(options());if(!created){return std::unexpected(QString("Public service create error=%1").arg(static_cast<int>(created.error())));}owner=std::move(*created);return {};
  }
  app::AzureusServiceOptions options()const{
    app::AzureusServiceOptions out;out.enabled=true;out.bindAddress=address;out.bindPort=port;out.network=versions->network();out.servePings=true;out.serveValues=true;out.valueVersions=versions;out.contacts=contacts;out.publicPolicy=policy;out.maintenance=true;out.routingScope=udp::RoutingScope::PublicBound;return out;
  }
  bool stop(){if(owner&&!owner->close()){return false;}owner.reset();policy.reset();contacts.reset();versions.reset();return true;}
  QString cache()const{return root.path()+ (address.protocol()==QAbstractSocket::IPv4Protocol?"/dht/contacts.dat":"/dht/net3/contacts.dat");}
};
QString describe(const Normal& n){const auto v=n.owner->observation();return QString("state=%1 io=%2/%3 commands=%4 transport=%5 routing=%6 seeded=%7").arg(static_cast<int>(v.state)).arg(static_cast<qulonglong>(v.profileIoRunning)).arg(static_cast<qulonglong>(v.profileIoPending)).arg(static_cast<qulonglong>(v.outstandingCommands)).arg(v.lastResult&&v.lastResult->transportFailure?static_cast<int>(*v.lastResult->transportFailure):-1).arg(v.routingSnapshot&&v.routingSnapshot->failure?static_cast<int>(*v.routingSnapshot->failure):-1).arg(v.routingSnapshot&&v.routingSnapshot->seeded);}
}
class AzureusPublicBoundTests final:public QObject {
  Q_OBJECT
private slots:
  void initTestCase(){
    QVERIFY2(namespaceChanged("BIGLYBTC_OWNED_PUBLIC_NET_FD","/proc/self/ns/net")&&namespaceChanged("BIGLYBTC_OWNED_PUBLIC_USER_FD","/proc/self/ns/user"),"Refuse public-address fixture outside its independently owned user/network namespace");
    for(const auto& interface:QNetworkInterface::allInterfaces()){QVERIFY(interface.name()==QStringLiteral("lo"));}
  }
  void numericEnvelopeNeverAcceptsPrivateWildcardMappedOrMulticast(){
    for(const auto& text:{"0.0.0.0","10.0.0.1","127.0.0.1","169.254.1.2","172.16.0.1","192.168.1.1","100.64.0.1","224.1.1.1","255.255.255.255","::","::1","::ffff:203.0.113.1","fc00::1","fe80::1","fec0::1","ff02::1"}){const auto selected=binding(QHostAddress(QString::fromLatin1(text)),31301);QVERIFY(!core::azureusPublicAddress(std::span(selected.address).first(selected.family)));}
    for(const auto& text:{"203.0.113.1","2001:db8::1"}){const auto selected=binding(QHostAddress(QString::fromLatin1(text)),31301);QVERIFY(core::azureusPublicAddress(std::span(selected.address).first(selected.family)));}
  }
  void publicNodeControllerScopeCannotGrantLegacyValueOrLoopbackAdmission(){
    namespace control=services::azureus_control;control::Contact contact;contact.id[0]=1;contact.protocol=55;contact.address.length=4;contact.address.port=31301;contact.address.bytes[0]=203;contact.address.bytes[1]=0;contact.address.bytes[2]=113;contact.address.bytes[3]=2;
    const std::array seeds{control::AdmittedContact{contact,true}};control::LookupOptions options;options.kind=control::LookupKind::Node;
    control::LookupController oldDefault;QVERIFY(!oldDefault.start(1,1,options,seeds,1));options.addressScope=control::LookupAddressScope::PublicNode;
    control::LookupController actual;QVERIFY(actual.start(1,2,options,seeds,1));auto intents=actual.takeIntents(2);QVERIFY(intents);QCOMPARE(intents->size(),std::size_t{1});QVERIFY(actual.snapshot());
    options.kind=control::LookupKind::Value;control::LookupController value;QVERIFY(!value.start(1,3,options,seeds,1));options.kind=control::LookupKind::Node;
    auto loopback=seeds;loopback[0].contact.address.bytes={127,0,0,1};control::LookupController refused;QVERIFY(!refused.start(1,4,options,loopback,1));
  }
  void enabledFilterProxyWrongTupleAndChangedSourceRefuseBeforeAnyRebind(){
    auto created=Normal::make(QStringLiteral("203.0.113.1"),31301);QVERIFY2(created,qPrintable(created?QString{}:created.error()));auto normal=std::move(*created);QVERIFY(normal->stop());
    auto versions=storage::AzureusValueVersions::open(*normal->lease,0);QVERIFY(versions);auto selected=binding(normal->address,normal->port);
    for(const char* key:{"Ip Filter Enabled","Enable.Proxy","Enable.SOCKS","Proxy.Data.Enable"}){auto config=storage::ConfigFile::load(*normal->lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().setInteger(key,1));QVERIFY(config->save(*normal->lease));auto refused=storage::AzureusPublicPolicy::open(*normal->lease,*versions,selected);QVERIFY(!refused);QCOMPARE(refused.error().code,storage::ErrorCode::Unsupported);QVERIFY(config->document().setInteger(key,0));QVERIFY(config->save(*normal->lease));}
    auto advanced=storage::ConfigFile::load(*normal->lease,"biglybt.config");QVERIFY(advanced);QVERIFY(advanced->document().setInteger("Plugin.DHT.dht.advanced",1));QVERIFY(advanced->save(*normal->lease));auto overridden=storage::AzureusPublicPolicy::open(*normal->lease,*versions,selected);QVERIFY(!overridden);QCOMPARE(overridden.error().code,storage::ErrorCode::Unsupported);QVERIFY(advanced->document().setInteger("Plugin.DHT.dht.advanced",0));QVERIFY(advanced->save(*normal->lease));
    auto wrong=selected;wrong.network=4;QVERIFY(!storage::AzureusPublicPolicy::open(*normal->lease,*versions,wrong));wrong=selected;++wrong.port;QVERIFY(!storage::AzureusPublicPolicy::open(*normal->lease,*versions,wrong));
    auto policy=storage::AzureusPublicPolicy::open(*normal->lease,*versions,selected);QVERIFY(policy);auto config=storage::ConfigFile::load(*normal->lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().setInteger("Plugin.DHT.dht.enabled",0));QVERIFY(config->save(*normal->lease));QVERIFY(!(*policy)->validate());QVERIFY(!(*policy)->current());QVERIFY(!(*policy)->validate());
  }
  void savedBindEmbeddedNulCannotGrantNumericPrefixOrChangeSourceBytes(){
    QTemporaryDir directory;QVERIFY(directory.isValid());auto lease=storage::ProfileLease::acquire(directory.path().toStdString());QVERIFY(lease);auto versions=storage::AzureusValueVersions::open(*lease,0);QVERIFY(versions);
    const auto selected=binding(QHostAddress(QStringLiteral("203.0.113.1")),31301);std::string malformed="203.0.113.1";malformed.push_back('\0');malformed+="unsupported";const auto source=configuration(selected,malformed);auto config=storage::ConfigFile::createMissing(*lease,"biglybt.config",source);QVERIFY(config);
    auto policy=storage::AzureusPublicPolicy::open(*lease,*versions,selected);QVERIFY(!policy);QCOMPARE(policy.error().code,storage::ErrorCode::Unsupported);QCOMPARE(read(directory.path()+"/biglybt.config"),QByteArray(source.data(),static_cast<qsizetype>(source.size())));QVERIFY(!QFile::exists(directory.path()+"/biglybt.config.saving"));
  }
  void foreignCacheScopeAndUnapprovedSourcesNeverAcquirePublicAuthority(){
    QTemporaryDir directory;QVERIFY(directory.isValid());auto lease=storage::ProfileLease::acquire(directory.path().toStdString());QVERIFY(lease);QVERIFY(QDir().mkpath(directory.path()+"/dht"));
    storage::AzureusSavedContact foreign;foreign.protocol=55;foreign.length=4;foreign.address={127,0,0,1};foreign.port=31301;const std::array rows{foreign};auto wire=storage::encodeAzureusContactState(rows);QVERIFY(wire);const auto path=directory.path()+"/dht/contacts.dat";QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(wire->data(),static_cast<qint64>(wire->size())),static_cast<qint64>(wire->size()));file.close();
    auto contacts=storage::AzureusContacts::open(*lease,0,4,storage::AzureusContactScope::PublicBound);QVERIFY(contacts);auto snapshot=(*contacts)->snapshot();QVERIFY(snapshot);QVERIFY(!(*snapshot)->writable);auto saved=(*contacts)->save(std::span<const storage::AzureusSavedContact>{});QVERIFY(!saved);QCOMPARE(saved.error().code,storage::ErrorCode::Unsupported);QCOMPARE(read(path),QByteArray(wire->data(),static_cast<qsizetype>(wire->size())));QVERIFY(!QFile::exists(directory.path()+"/dht/contacts.saving"));
    auto madeA=Normal::make(QStringLiteral("203.0.113.1"),31301);QVERIFY2(madeA,qPrintable(madeA?QString{}:madeA.error()));auto a=std::move(*madeA);auto madeB=Normal::make(QStringLiteral("203.0.113.2"),31302);QVERIFY2(madeB,qPrintable(madeB?QString{}:madeB.error()));auto b=std::move(*madeB);
    auto options=a->options();options.maximumPeers=33;auto full=app::OwnedAzureusService::create(options);QVERIFY(!full);QCOMPARE(full.error(),app::AzureusServiceError::Invalid);options=a->options();options.routingScope=udp::RoutingScope::OwnedLoopback;QVERIFY(!app::OwnedAzureusService::create(options));
    QVERIFY(!a->owner->ping({QHostAddress(QStringLiteral("127.0.0.1")),31302}));QVERIFY(!a->owner->ping({QHostAddress(QStringLiteral("2001:db8::2")),31302}));
    const auto before=a->owner->observation().snapshot->transport().counters();auto request=b->owner->ping(a->owner->endpoint(),200ms);QVERIFY(request);
    QTRY_VERIFY_WITH_TIMEOUT(b->owner->observation().lastResult&&b->owner->observation().lastResult->request==*request&&b->owner->observation().lastResult->terminal,3000);QCOMPARE(b->owner->observation().lastResult->transportFailure,std::optional{udp::Failure::Timeout});QTRY_VERIFY_WITH_TIMEOUT(a->owner->observation().snapshot->transport().counters().unowned>before.unowned,3000);const auto actual=a->owner->observation();QCOMPARE(actual.snapshot->transport().counters().requests,before.requests);QCOMPARE(actual.snapshot->transport().counters().sent,before.sent);QVERIFY(actual.routerSnapshot);QCOMPARE(actual.routerSnapshot->counts()[2],std::uint64_t{1});QVERIFY(!actual.routingSnapshot||!actual.routingSnapshot->seeded);QVERIFY(a->stop());QVERIFY(b->stop());
  }
  void explicitPublicKnownHintIsNotExportedAndOnlyActualPingMakesItAlive(){
    auto madeA=Normal::make(QStringLiteral("203.0.113.1"),31301);QVERIFY2(madeA,qPrintable(madeA?QString{}:madeA.error()));auto a=std::move(*madeA);auto madeB=Normal::make(QStringLiteral("203.0.113.2"),31302);QVERIFY2(madeB,qPrintable(madeB?QString{}:madeB.error()));auto b=std::move(*madeB);
    const auto ba=b->owner->allowPeer(a->owner->endpoint(),55);QVERIFY(ba);QTRY_VERIFY2_WITH_TIMEOUT(success(*b->owner,*ba),qPrintable(describe(*b)),3000);
    // Consume B's first real PING while A still refuses its unknown source.
    // This ordered network barrier prevents a queued old PING racing A's grant.
    QTRY_VERIFY_WITH_TIMEOUT(a->owner->observation().snapshot->transport().counters().unowned>0,3000);
    auto access=b->lease->sourceAccess();QVERIFY(access);auto blocked=std::make_unique<storage::ProfileSourceAccess>(std::move(*access));const auto release=qScopeGuard([&]{blocked.reset();});const auto ab=a->owner->allowPeer(b->owner->endpoint(),51);QVERIFY(ab);QTRY_VERIFY2_WITH_TIMEOUT(success(*a->owner,*ab),qPrintable(describe(*a)),3000);QTRY_VERIFY_WITH_TIMEOUT(b->owner->observation().profileIoRunning>0,3000);
    const auto hint=a->owner->observation();QVERIFY(hint.routingSnapshot&&hint.routerSnapshot);QCOMPARE(hint.routingSnapshot->contactCount,std::size_t{1});QCOMPARE(hint.routingSnapshot->contacts[0].protocolSource,udp::RoutingProtocolSource::ProbeHint);QCOMPARE(hint.routerSnapshot->counts()[4],std::uint64_t{1});QVERIFY(!hint.routingSnapshot->seeded);
    const auto flush=a->owner->flushContacts();QVERIFY(flush);QTRY_VERIFY2_WITH_TIMEOUT(success(*a->owner,*flush),qPrintable(describe(*a)),5000);const auto saved=storage::decodeAzureusContactState(read(a->cache()).toStdString());QVERIFY(saved);QCOMPARE(saved->size(),std::size_t{1});QCOMPARE(saved->front().port,a->port);
    blocked.reset();QTRY_VERIFY2_WITH_TIMEOUT(a->owner->observation().routingSnapshot&&a->owner->observation().routingSnapshot->contacts[0].protocolSource==udp::RoutingProtocolSource::Packet,qPrintable(describe(*a)),3000);const auto observed=a->owner->observation();QCOMPARE(observed.routingSnapshot->contacts[0].contact.protocol,std::int8_t{55});QCOMPARE(observed.routerSnapshot->counts()[4],std::uint64_t{2});QVERIFY(!observed.routingSnapshot->seeded);QVERIFY(a->stop());QVERIFY(b->stop());
  }
  void realPublicTupleRoutingPublicationAndCacheOnlyRestart_data(){QTest::addColumn<bool>("ipv6");QTest::newRow("IPv4-original-network0")<<false;QTest::newRow("IPv6-original-network3")<<true;}
  void realPublicTupleRoutingPublicationAndCacheOnlyRestart(){
    QFETCH(bool,ipv6);const auto host=[&](int n){return ipv6?QString("2001:db8::%1").arg(n):QString("203.0.113.%1").arg(n);};
    auto madeA=Normal::make(host(1),31301);QVERIFY2(madeA,qPrintable(madeA?QString{}:madeA.error()));auto a=std::move(*madeA);
    // A real existing10s turn separates the local getTimeAlive from contacts;
    // never replace original TreeSet equal-age semantics with fake timestamps.
    QTRY_VERIFY2_WITH_TIMEOUT(a->owner->observation().maintenance&&a->owner->observation().maintenance->routing.ticks>=1,qPrintable(describe(*a)),15000);
    auto madeB=Normal::make(host(2),31302);QVERIFY2(madeB,qPrintable(madeB?QString{}:madeB.error()));auto b=std::move(*madeB);auto madeC=Normal::make(host(3),31303);QVERIFY2(madeC,qPrintable(madeC?QString{}:madeC.error()));auto c=std::move(*madeC);
    auto cb=c->owner->allowPeer(b->owner->endpoint());QVERIFY(cb);QTRY_VERIFY2_WITH_TIMEOUT(success(*c->owner,*cb),qPrintable(describe(*c)),3000);auto bc=b->owner->integrateRouting(c->owner->endpoint());QVERIFY(bc);QTRY_VERIFY2_WITH_TIMEOUT(success(*b->owner,*bc),qPrintable(describe(*b)),10000);
    auto ba=b->owner->allowPeer(a->owner->endpoint()),ca=c->owner->allowPeer(a->owner->endpoint());QVERIFY(ba&&ca);QTRY_VERIFY_WITH_TIMEOUT(success(*b->owner,*ba)&&success(*c->owner,*ca),3000);
    auto ab=a->owner->integrateRouting(b->owner->endpoint());QVERIFY(ab);QTRY_VERIFY2_WITH_TIMEOUT(success(*a->owner,*ab),qPrintable(describe(*a)),10000);const auto first=a->owner->observation();QVERIFY(first.routingSnapshot&&first.routingSnapshot->seeded&&first.routingSnapshot->finished&&first.routingSnapshot->lookup);QVERIFY(first.routingSnapshot->lookup->nodes.size()>=2);QVERIFY(std::ranges::count_if(first.routingSnapshot->lookup->nodes,[](const auto& n){return n.state==services::azureus_control::LookupNodeState::Replied;})>=2);
    auto published=a->owner->publishValue(b->owner->endpoint(),"owned-public-key","real namespace readback",5s,app::AzureusPublicationFlags::Anonymous);QVERIFY(published);QTRY_VERIFY2_WITH_TIMEOUT(success(*a->owner,*published),qPrintable(describe(*a)),5000);const auto publication=a->owner->observation().publishSnapshot;QVERIFY(publication&&publication->state==app::AzureusPublishState::Complete&&publication->localValue);QVERIFY(std::ranges::any_of(publication->readback,[](const auto& value){return value.bytes=="real namespace readback"&&value.flags==0x10;}));
    auto flushed=a->owner->flushContacts();QVERIFY(flushed);QTRY_VERIFY2_WITH_TIMEOUT(success(*a->owner,*flushed),qPrintable(describe(*a)),5000);const auto cache=read(a->cache());QVERIFY(!cache.isEmpty());const auto records=storage::decodeAzureusContactState(cache.toStdString());QVERIFY(records&&records->size()>1);const auto endpoint=a->owner->endpoint();const auto generation=first.routingSnapshot->generation;
    QVERIFY(a->stop());QVERIFY(a->start());QVERIFY(a->owner->endpoint()==endpoint);QVERIFY(!a->owner->observation().routingSnapshot||!a->owner->observation().routingSnapshot->seeded);QCOMPARE(a->owner->observation().snapshot->transport().counters().received,std::uint64_t{0});
    auto warm=a->owner->bootstrapSavedContacts();QVERIFY(warm);QTRY_VERIFY2_WITH_TIMEOUT(success(*a->owner,*warm),qPrintable(describe(*a)),10000);const auto restarted=a->owner->observation();QVERIFY(restarted.routingSnapshot&&restarted.routingSnapshot->seeded&&restarted.routingSnapshot->generation!=generation);QCOMPARE(read(a->cache()),cache);QVERIFY(a->stop());QVERIFY(b->stop());QVERIFY(c->stop());
  }
  void blockedCommandCancellationAndSourceWithdrawalCloseWithoutAnyLatePacket(){
    auto madeA=Normal::make(QStringLiteral("203.0.113.1"),31301);QVERIFY2(madeA,qPrintable(madeA?QString{}:madeA.error()));auto a=std::move(*madeA);auto madeB=Normal::make(QStringLiteral("203.0.113.2"),31302);QVERIFY2(madeB,qPrintable(madeB?QString{}:madeB.error()));auto b=std::move(*madeB);
    auto allowed=b->owner->allowPeer(a->owner->endpoint());QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(success(*b->owner,*allowed),3000);auto integrated=a->owner->integrateRouting(b->owner->endpoint());QVERIFY(integrated);QTRY_VERIFY2_WITH_TIMEOUT(success(*a->owner,*integrated),qPrintable(describe(*a)),10000);
    auto gate=a->lease->sourceAccess(true);QVERIFY(gate);auto blocked=std::make_unique<storage::ProfileSourceAccess>(std::move(*gate));const auto release=qScopeGuard([&]{blocked.reset();});const auto sent=a->owner->observation().snapshot->transport().counters().sent;auto ping=a->owner->ping(b->owner->endpoint(),2s);QVERIFY(ping);QTRY_VERIFY_WITH_TIMEOUT(a->owner->observation().profileIoRunning>0,3000);auto cancel=a->owner->cancel(*ping);QVERIFY(cancel);QTRY_VERIFY_WITH_TIMEOUT(success(*a->owner,*cancel)&&a->owner->observation().outstandingCommands==0,3000);QCOMPARE(a->owner->observation().snapshot->transport().counters().sent,sent);
    auto config=storage::ConfigFile::load(*a->lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().setInteger("Plugin.DHT.dht.enabled",0));QVERIFY(config->save(*a->lease));blocked.reset();
    // The existing maintenance turn revalidates an otherwise idle service.
    QTRY_VERIFY2_WITH_TIMEOUT(a->owner->observation().state==app::AzureusServiceState::Closed,qPrintable(describe(*a)),15000);QCOMPARE(a->owner->observation().snapshot->transport().counters().sent,sent);QVERIFY(!a->policy->current());QVERIFY(a->owner->close());QUdpSocket rebound;rebound.setProxy(QNetworkProxy::NoProxy);QVERIFY(rebound.bind(a->address,a->port,QUdpSocket::DontShareAddress));QVERIFY(b->stop());
  }
};
QTEST_GUILESS_MAIN(AzureusPublicBoundTests)
#include "azureus_public_bound_tests.moc"

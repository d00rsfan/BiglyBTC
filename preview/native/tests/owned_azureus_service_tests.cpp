#include "owned_azureus_service.hpp"
#include "owned_ddb_fixture.hpp"
#include "torrent_state.hpp"
#include <QCoreApplication>
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QNetworkDatagram>
#include <limits>
#include <unistd.h>
#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>

namespace {
using namespace biglybtc;
namespace udp=services::azureus_udp;
using namespace std::chrono_literals;
auto options(bool reply=true){app::AzureusServiceOptions result;result.enabled=true;result.servePings=reply;return result;}
auto make(bool reply=true){auto result=app::OwnedAzureusService::create(options(reply));if(!result){throw std::runtime_error("Explicit owned Azureus service failed to start");}return std::move(*result);}
bool finished(const app::OwnedAzureusService& owner,std::uint64_t request){const auto value=owner.observation();return value.lastResult&&value.lastResult->request==request&&value.lastResult->terminal;}
bool successful(const app::OwnedAzureusService& owner,std::uint64_t request){const auto value=owner.observation();return value.lastResult&&value.lastResult->request==request&&value.lastResult->terminal&&!value.lastResult->error&&!value.lastResult->transportFailure;}
bool hasEvent(const app::AzureusServiceObservation& value,udp::OperationEventKind kind){if(!value.snapshot){return false;}for(const auto& event:value.snapshot->events()){if(event.kind==kind){return true;}}return false;}
struct NormalValues {
 QTemporaryDir root;
 std::optional<storage::ProfileLease> held;
 std::shared_ptr<storage::AzureusValueVersions> versions;
 std::unique_ptr<app::OwnedAzureusService> owner;
 QHostAddress address;
 explicit NormalValues(const char* ip):address(QString::fromLatin1(ip)){
   if(!root.isValid()){throw std::runtime_error("Cannot create normal owned profile");}
   auto lease=storage::ProfileLease::acquire(root.path().toStdString());if(!lease){throw std::runtime_error("Cannot own normal profile");}held.emplace(std::move(*lease));restart();
 }
 void restart(){
   if(owner){if(!owner->close()){throw std::runtime_error("Cannot join normal owner");}owner.reset();}versions.reset();
   auto value=storage::AzureusValueVersions::open(*held,4);if(!value){throw std::runtime_error("Cannot open normal versions");}versions=*value;
   auto policy=options();policy.bindAddress=address;policy.valueVersions=versions;policy.serveValues=true;
   auto made=app::OwnedAzureusService::create(policy);if(!made){throw std::runtime_error("Cannot start normal receiving service");}owner=std::move(*made);
 }
};
services::azureus_nodes::Contact actualContact(const udp::Peer& peer){
 services::azureus_nodes::NodeCache cache;const auto address=test::ddbAddress(peer);auto id=cache.lookup({address,{}},55);
 if(!id){throw std::runtime_error("Cannot derive actual owned contact");}services::azureus_nodes::Contact result;result.protocol=55;result.id=id->id;result.address=address;return result;
}
}

std::string ratingBytes(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string ratingDictionary(std::initializer_list<std::pair<std::string,std::string>> entries){
 std::vector<std::pair<std::string,std::string>> values(entries);std::ranges::sort(values);std::string out="d";for(const auto& [key,value]:values){out+=ratingBytes(key)+value;}return out+'e';
}
void writeRatingFixture(const QString& path,const std::string& bytes){QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)||file.write(bytes.data(),static_cast<qint64>(bytes.size()))!=static_cast<qint64>(bytes.size())||!file.flush()){throw std::runtime_error("Cannot write generated rating fixture");}}
QByteArray readRatingFixture(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("Cannot read generated rating fixture");}return file.readAll();}
struct RatingOwner {
 QTemporaryDir root;std::optional<storage::ProfileLease> held;std::shared_ptr<storage::AzureusValueVersions> versions;std::unique_ptr<app::OwnedAzureusService> owner;
 std::string info=ratingDictionary({{"length","i3e"},{"name",ratingBytes("rating-fixture.bin")},{"piece length","i16384e"},{"pieces",ratingBytes(std::string(20,'x'))}});
 core::TorrentHash hash;
 int score;std::string nick,comment,network="Public";bool dht=true;std::int64_t flags=0;
 QString statePath()const{return root.path()+"/active/"+QString::fromStdString(hash.hex())+".dat";}
 static core::TorrentHash identity(std::string_view info){const auto digest=QCryptographicHash::hash(QByteArray(info.data(),static_cast<qsizetype>(info.size())),QCryptographicHash::Sha1);auto value=core::TorrentHash::parse(std::string_view(digest.constData(),static_cast<std::size_t>(digest.size())));if(!value){throw std::runtime_error("Rating info hash");}return *value;}
 RatingOwner(const char* address,int value,std::string name,std::string text):hash(identity(info)),score(value),nick(std::move(name)),comment(std::move(text)){
   if(!root.isValid()||!QDir(root.path()).mkdir("active")){throw std::runtime_error("Rating profile");}files();
   auto acquired=storage::ProfileLease::acquire(root.path().toStdString());if(!acquired){throw std::runtime_error(acquired.error().message);}held.emplace(std::move(*acquired));
   auto opened=storage::AzureusValueVersions::open(*held,0);if(!opened){throw std::runtime_error(opened.error().message);}versions=*opened;
   auto policy=options();policy.network=0;policy.bindAddress=QHostAddress(QString::fromLatin1(address));policy.valueVersions=versions;policy.serveValues=true;auto made=app::OwnedAzureusService::create(policy);if(!made){throw std::runtime_error("Cannot create normal rating owner");}owner=std::move(*made);
 }
 void files(){
   const auto id=hash.bytes();writeRatingFixture(root.path()+"/downloads.config",ratingDictionary({{"downloads","l"+ratingDictionary({{"state","i70e"},{"save_dir",ratingBytes("/owned/rating-payload-never-opened")},{"torrent",ratingBytes("owned-rating.torrent")},{"torrent_hash",ratingBytes(std::string(reinterpret_cast<const char*>(id.data()),id.size()))}})+"e"}}));
   state();config();
 }
 void state(){const auto attrs=ratingDictionary({{"Plugin.azrating.comment",ratingBytes(comment)},{"Plugin.azrating.rating",ratingBytes(std::to_string(score))},{"flags","i"+std::to_string(flags)+"e"},{"networks","l"+ratingBytes(network)+"e"},{"future",ratingBytes("retained-rating-opaque")}});writeRatingFixture(statePath(),ratingDictionary({{"attributes",attrs},{"info",info},{"resume",ratingBytes("opaque-resume-untouched")}}));}
 void config(){writeRatingFixture(root.path()+"/biglybt.config",ratingDictionary({{"Plugin.DHT.dht.enabled",dht?"i1e":"i0e"},{"Plugin.azrating.nick",ratingBytes(nick)},{"future",ratingBytes("configuration-opaque")}}));}
 storage::DistributedRatingSnapshot target()const{auto value=storage::readDistributedRating(*held,hash);if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
};
void emptyRatingReply(QUdpSocket& socket,const QNetworkDatagram& datagram){
 const auto raw=datagram.data();udp::lookup::Context context;context.header.network=0;context.header.now=QDateTime::currentMSecsSinceEpoch();services::azureus_nodes::NodeCache cache;
 auto decoded=udp::lookup::decode(udp::packet::udp::Kind::Request,std::string(raw.constData(),static_cast<std::size_t>(raw.size())),context,cache);if(!decoded){throw std::runtime_error("Invalid actual rating GET");}
 const auto& incoming=decoded->header().fields();udp::lookup::ReplyFields reply;reply.header.network=0;reply.header.localVersion=55;reply.header.remoteVersion=incoming.originatorVersion;reply.header.connection=incoming.connection;reply.header.transaction=incoming.transaction;reply.header.now=context.header.now;reply.header.receivedAt=context.header.now;reply.contacts=std::span<const udp::lookup::nodes::Snapshot>{};reply.positions=std::span<const std::optional<udp::lookup::positions::WirePosition>>{};
 const auto encoded=udp::lookup::encodeReply(reply);if(!encoded||datagram.senderPort()<=0||datagram.senderPort()>65535||socket.writeDatagram(encoded->data(),static_cast<qint64>(encoded->size()),datagram.senderAddress(),static_cast<quint16>(datagram.senderPort()))!=static_cast<qint64>(encoded->size())){throw std::runtime_error("Cannot send owned held rating GET reply");}
}

class OwnedAzureusServiceTests final:public QObject {
 Q_OBJECT
private slots:

 void savedRatingsUseOriginalAnonymousKeyPayloadAndTwoNormalPublicOwners(){
   RatingOwner a("127.11.0.1",4,"Alice","from A"),b("127.12.0.1",2,"Bob","from B");QCOMPARE(a.hash,b.hash);const auto sa=a.target(),sb=b.target();const auto stateA=readRatingFixture(a.statePath()),stateB=readRatingFixture(b.statePath());
   auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*aa)&&successful(*b.owner,*bb),3000);
   const auto pa=a.owner->publishSavedRating(b.owner->endpoint(),a.hash,sa.sourceRevision);QVERIFY(pa);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*pa),5000);auto published=a.owner->observation().publishSnapshot;QVERIFY(published);QCOMPARE(published->state,app::AzureusPublishState::Complete);QCOMPARE(published->localValue->flags(),std::uint8_t{0x10});QCOMPARE(published->readback[0].flags,std::uint8_t{0x10});QCOMPARE(published->readback[0].bytes,sa.local.local.value.encode().value());
   const std::string applicationKey(reinterpret_cast<const char*>(sa.key.data()),sa.key.size());QCOMPARE(applicationKey.substr(20),std::string(".rating"));const auto digest=QCryptographicHash::hash(QByteArray(applicationKey.data(),static_cast<qsizetype>(applicationKey.size())),QCryptographicHash::Sha1);QVERIFY(std::equal(published->key.begin(),published->key.end(),reinterpret_cast<const std::uint8_t*>(digest.constData())));
   const auto pb=b.owner->publishSavedRating(a.owner->endpoint(),b.hash,sb.sourceRevision);QVERIFY(pb);QTRY_VERIFY_WITH_TIMEOUT(finished(*b.owner,*pb),5000);const auto second=b.owner->observation().publishSnapshot;QVERIFY(second);QVERIFY2(second->state==app::AzureusPublishState::Complete,qPrintable(QStringLiteral("second publish state=%1 error=%2 transport=%3 values=%4").arg(static_cast<int>(second->state)).arg(second->error?static_cast<int>(*second->error):-1).arg(second->transportFailure?static_cast<int>(*second->transportFailure):-1).arg(static_cast<qulonglong>(second->readback.size()))));
   QCOMPARE(second->readback.size(),std::size_t{2});QVERIFY(second->localValue);const auto& local=*second->localValue;const auto* origin=local.originator().get();const auto payload=local.payload().view();QVERIFY(origin&&payload);QVERIFY(std::ranges::any_of(second->readback,[&](const auto& item){return item.version==local.version()&&item.bytes==*payload&&item.originator.id==origin->id&&item.originator.address==origin->address&&item.flags==local.flags()&&item.lifeHours==local.lifeTimeHours()&&item.replication==local.replicationControl();}));
   const auto readA=a.owner->readSavedRatings(b.owner->endpoint(),a.hash,sa.sourceRevision);QVERIFY(readA);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*readA),5000);const auto result=a.owner->observation().ratingReadSnapshot;QVERIFY(result);QCOMPARE(result->state,app::AzureusRatingReadState::Complete);QCOMPARE(result->receivedValues,std::size_t{2});QVERIFY(result->current&&result->possiblyPartial&&!result->wholeNetwork);QCOMPARE(result->maximumValues,std::size_t{64});QCOMPARE(result->originalMaximumValues,std::size_t{256});QCOMPARE(result->ratings.count(),std::size_t{2});QCOMPARE(result->ratings.average(),3.0F);QCOMPARE(result->ratings.commentCount(),2);QVERIFY(!result->localFallback);QCOMPARE(result->decodeErrors,std::size_t{0});
   const auto readB=b.owner->readSavedRatings(a.owner->endpoint(),b.hash,sb.sourceRevision);QVERIFY(readB);QTRY_VERIFY_WITH_TIMEOUT(finished(*b.owner,*readB),5000);QCOMPARE(b.owner->observation().ratingReadSnapshot->ratings.average(),3.0F);QCOMPARE(readRatingFixture(a.statePath()),stateA);QCOMPARE(readRatingFixture(b.statePath()),stateB);QVERIFY(a.owner->close());QVERIFY(b.owner->close());QVERIFY(a.owner->observation().joined&&b.owner->observation().joined);QCOMPARE(result->ratings.average(),3.0F);
 }
 void publicationReadbackWithoutTheExactValueRemainsBounded_data(){
   QTest::addColumn<int>("parts");QTest::addColumn<int>("valuesPerPart");QTest::addColumn<bool>("capacity");
   QTest::newRow("two-final-mismatches")<<2<<1<<false;
   QTest::newRow("sixty-four-final-mismatches")<<4<<16<<false;
   QTest::newRow("continued-sixty-four-withdraws")<<5<<16<<true;
 }
 void publicationReadbackWithoutTheExactValueRemainsBounded(){
   QFETCH(int,parts);QFETCH(int,valuesPerPart);QFETCH(bool,capacity);QTemporaryDir root;QVERIFY(root.isValid());auto held=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(held);auto versions=storage::AzureusValueVersions::open(*held,4);QVERIFY(versions);auto policy=options();policy.valueVersions=*versions;auto made=app::OwnedAzureusService::create(policy);QVERIFY(made);auto client=std::move(*made);
   // This inert generated receiver deliberately ACKs without storing the value.
   // Actual wire fragments cannot become success without the exact readback.
   udp::Config config;config.findNodeReply.emplace();config.findNodeReply->randomId=17;config.storeReply=std::string(1,char(1));config.findValueReply.emplace();
   for(int i=0;i<parts;++i){udp::FindValueSlice slice;slice.continuation=i+1<parts;slice.values.emplace();for(int j=0;j<valuesPerPart;++j){udp::lookup::values::Value item;item.version=i*valuesPerPart+j;item.bytes="unrelated";item.originator=actualContact(client->endpoint());slice.values->push_back(std::move(item));}config.findValueReply->push_back(std::move(slice));}
   udp::OwnedPingTransport server(std::move(config));QVERIFY(server.start());QVERIFY(server.allowPeer(client->endpoint()));const auto admitted=client->allowPeer(server.endpoint());QVERIFY(admitted);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*admitted),3000);
   const auto request=client->publishValue(server.endpoint(),"bounded-readback","must-match");QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(finished(*client,*request),5000);const auto observed=client->observation();QVERIFY(observed.publishSnapshot);QCOMPARE(observed.publishSnapshot->state,app::AzureusPublishState::Failed);QCOMPARE(observed.publishSnapshot->error,std::optional{capacity?app::AzureusServiceError::Capacity:app::AzureusServiceError::Readback});QCOMPARE(observed.publishSnapshot->readback.size(),capacity?std::size_t{64}:static_cast<std::size_t>(parts*valuesPerPart));QCOMPARE(observed.outstandingCommands,std::size_t{0});QVERIFY(client->close());server.close();
 }
 void savedRatingPublicationRefusesUnadmittedZeroOversizeAndStaleTargets(){
   RatingOwner a("127.13.0.1",0,"Owned","comment"),b("127.14.0.1",2,"Receiver","");auto source=a.target();const auto missing=a.owner->publishSavedRating(b.owner->endpoint(),a.hash,source.sourceRevision);QVERIFY(missing);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*missing),3000);QCOMPARE(a.owner->observation().lastResult->error,std::optional{app::AzureusServiceError::Invalid});
   const auto admit=a.owner->allowPeer(b.owner->endpoint());QVERIFY(admit);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*admit),3000);const auto zero=a.owner->publishSavedRating(b.owner->endpoint(),a.hash,source.sourceRevision);QVERIFY(zero);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*zero),3000);QCOMPARE(a.owner->observation().lastResult->error,std::optional{app::AzureusServiceError::Invalid});
   a.score=3;a.comment=std::string(600,'x');a.state();source=a.target();const auto large=a.owner->publishSavedRating(b.owner->endpoint(),a.hash,source.sourceRevision);QVERIFY(large);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*large),3000);QCOMPARE(a.owner->observation().lastResult->error,std::optional{app::AzureusServiceError::Invalid});
   a.comment="small";a.state();source=a.target();a.nick="changed";a.config();const auto stale=a.owner->publishSavedRating(b.owner->endpoint(),a.hash,source.sourceRevision);QVERIFY(stale);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*stale),3000);QCOMPARE(a.owner->observation().lastResult->error,std::optional{app::AzureusServiceError::Profile});QCOMPARE(a.owner->observation().snapshot->transport().counters().sent,std::uint64_t{0});QCOMPARE(a.owner->observation().databaseSnapshot->localPuts,std::uint64_t{0});
   const auto flags=a.owner->publishValue(b.owner->endpoint(),"invalid-flags","body",1s,static_cast<app::AzureusPublicationFlags>(0x40));QVERIFY(!flags);QCOMPARE(flags.error(),app::AzureusServiceError::Invalid);
 }
 void savedRatingMalformedRemoteValueDoesNotBecomeLocalFallback(){
   RatingOwner a("127.15.0.1",5,"Local","fallback"),b("127.16.0.1",2,"Remote","");auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*aa)&&successful(*b.owner,*bb),3000);const auto source=a.target();
   const auto empty=a.owner->readSavedRatings(b.owner->endpoint(),a.hash,source.sourceRevision);QVERIFY(empty);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*empty),3000);auto observed=a.owner->observation().ratingReadSnapshot;QCOMPARE(observed->receivedValues,std::size_t{0});QVERIFY(observed->localFallback);QCOMPARE(observed->ratings.average(),5.0F);
   const std::string key(reinterpret_cast<const char*>(source.key.data()),source.key.size());const auto malformed=b.owner->publishValue(a.owner->endpoint(),key,"not-bencode");QVERIFY(malformed);QTRY_VERIFY_WITH_TIMEOUT(finished(*b.owner,*malformed),5000);QCOMPARE(b.owner->observation().publishSnapshot->state,app::AzureusPublishState::Complete);
   const auto read=a.owner->readSavedRatings(b.owner->endpoint(),a.hash,source.sourceRevision);QVERIFY(read);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*read),3000);observed=a.owner->observation().ratingReadSnapshot;QCOMPARE(observed->state,app::AzureusRatingReadState::Complete);QCOMPARE(observed->receivedValues,std::size_t{1});QCOMPARE(observed->decodeErrors,std::size_t{1});QVERIFY(!observed->localFallback);QCOMPARE(observed->ratings.count(),std::size_t{0});
 }

 void completedRatingValuesWithdrawAtTheNextRealWorkerSourceFence(){
   RatingOwner a("127.21.0.1",4,"Local","retained"),b("127.22.0.1",2,"Remote","");const auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*aa)&&successful(*b.owner,*bb),3000);const auto source=a.target();const auto read=a.owner->readSavedRatings(b.owner->endpoint(),a.hash,source.sourceRevision);QVERIFY(read);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*read),3000);const auto retained=a.owner->observation().ratingReadSnapshot;QVERIFY(retained->current);QCOMPARE(retained->ratings.count(),std::size_t{1});
   a.dht=false;a.config();const auto next=a.owner->allowPeer(b.owner->endpoint());QVERIFY(next);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*next),3000);QTRY_VERIFY_WITH_TIMEOUT(!a.owner->observation().ratingReadSnapshot->current,3000);QCOMPARE(a.owner->observation().ratingReadSnapshot->ratings.count(),std::size_t{0});QCOMPARE(retained->ratings.count(),std::size_t{1});QVERIFY(!storage::readDistributedRating(*a.held,a.hash));
 }
 void savedRatingPermissionAndIdentityChangesWithdrawHeldActualReplies_data(){
   QTest::addColumn<int>("change");QTest::newRow("nickname")<<0;QTest::newRow("Public-removed")<<1;QTest::newRow("DHT-disabled")<<2;QTest::newRow("metadata-flag")<<3;QTest::newRow("full-info-changed")<<4;QTest::newRow("inventory-removed")<<5;
 }
 void savedRatingPermissionAndIdentityChangesWithdrawHeldActualReplies(){
   QFETCH(int,change);RatingOwner a("127.17.0.1",4,"Owned","before");QUdpSocket server;QVERIFY(server.bind(QHostAddress(QStringLiteral("127.18.0.1")),0,QUdpSocket::DontShareAddress));const udp::Peer peer{server.localAddress(),server.localPort()};const auto admitted=a.owner->allowPeer(peer);QVERIFY(admitted);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*admitted),3000);const auto source=a.target();
   const auto request=a.owner->readSavedRatings(peer,a.hash,source.sourceRevision);QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingDatagrams(),3000);const auto received=server.receiveDatagram();QVERIFY(received.isValid());
   if(change==0){a.nick="new";a.config();}else if(change==1){a.network="I2P";a.state();}else if(change==2){a.dht=false;a.config();}else if(change==3){a.flags=0x200;a.state();}else if(change==4){a.info=ratingDictionary({{"length","i4e"},{"name",ratingBytes("changed")},{"piece length","i16384e"},{"pieces",ratingBytes(std::string(20,'z'))}});a.state();}else{writeRatingFixture(a.root.path()+"/downloads.config","d9:downloadslee");}
   emptyRatingReply(server,received);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*request),3000);const auto result=a.owner->observation().ratingReadSnapshot;QVERIFY(result);QCOMPARE(result->state,app::AzureusRatingReadState::Failed);QCOMPARE(result->error,std::optional{app::AzureusServiceError::Profile});QCOMPARE(result->ratings.count(),std::size_t{0});QVERIFY(!result->localFallback);QCOMPARE(a.owner->observation().snapshot->transport().counters().sent,std::uint64_t{1});
 }

 void savedRatingDuplicateInventoryRowsCannotGrantAuthority_data(){
   QTest::addColumn<bool>("legacy");QTest::addColumn<int>("field");
   for(const bool legacy:{false,true}){for(int field=0;field<3;++field){QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(legacy?"legacy":"list").arg(field)))<<legacy<<field;}}
 }
 void savedRatingDuplicateInventoryRowsCannotGrantAuthority(){
   QFETCH(bool,legacy);QFETCH(int,field);RatingOwner a("127.23.0.1",4,"Owned","unchanged");const auto source=a.target();const auto bytes=a.hash.bytes();
   auto row=ratingDictionary({{"persistent","i1e"},{"save_dir",ratingBytes("/owned/rating-payload-never-opened")},{"state","i70e"},{"torrent",ratingBytes("owned-rating.torrent")},{"torrent_hash",ratingBytes(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()))}});
   const std::string name=field==0?"torrent_hash":field==1?"state":"persistent";const auto original=ratingBytes(name)+(field==0?ratingBytes(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size())):field==1?"i70e":"i1e");const auto extra=ratingBytes(name)+(field==0?ratingBytes(std::string(20,'z')):field==1?"i75e":"i0e");const auto pos=row.find(original);QVERIFY(pos!=std::string::npos);row.insert(pos+original.size(),extra);
   writeRatingFixture(a.root.path()+"/downloads.config",legacy?ratingDictionary({{a.hash.hex(),row}}):ratingDictionary({{"downloads","l"+row+"e"}}));
   // The existing parser rejects duplicate keys before either reader can
   // select a row. Keep ordinary local behavior and distributed refusal exact.
   const auto parsed=core::Document::parse(readRatingFixture(a.root.path()+"/downloads.config").toStdString());QVERIFY(!parsed);QCOMPARE(parsed.error().code,std::string("duplicate_key"));
   const auto ordinary=storage::readLocalRating(*a.held,a.hash);QVERIFY(!ordinary);QCOMPARE(ordinary.error().code,storage::ErrorCode::InvalidFormat);const auto refused=storage::readDistributedRating(*a.held,a.hash);QVERIFY(!refused);QCOMPARE(refused.error().code,storage::ErrorCode::InvalidFormat);
   QUdpSocket peer;QVERIFY(peer.bind(QHostAddress(QStringLiteral("127.24.0.1")),0,QUdpSocket::DontShareAddress));const udp::Peer endpoint{peer.localAddress(),peer.localPort()};const auto admitted=a.owner->allowPeer(endpoint);QVERIFY(admitted);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*admitted),3000);
   const auto request=a.owner->publishSavedRating(endpoint,a.hash,source.sourceRevision);QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*request),3000);QCOMPARE(a.owner->observation().lastResult->error,std::optional{app::AzureusServiceError::Profile});QCOMPARE(a.owner->observation().snapshot->transport().counters().sent,std::uint64_t{0});
 }
 void savedRatingActualTimeoutCancelCloseAndMetadataPermissionsRemainBounded(){
   RatingOwner a("127.19.0.1",3,"Owned","fallback");QUdpSocket server;QVERIFY(server.bind(QHostAddress(QStringLiteral("127.20.0.1")),0,QUdpSocket::DontShareAddress));const udp::Peer peer{server.localAddress(),server.localPort()};const auto admitted=a.owner->allowPeer(peer);QVERIFY(admitted);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*admitted),3000);auto source=a.target();
   const auto timed=a.owner->readSavedRatings(peer,a.hash,source.sourceRevision,100ms);QVERIFY(timed);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*timed),3000);QCOMPARE(a.owner->observation().ratingReadSnapshot->state,app::AzureusRatingReadState::TimedOut);QVERIFY(a.owner->observation().ratingReadSnapshot->localFallback);
   while(server.hasPendingDatagrams()){server.receiveDatagram();}const auto pending=a.owner->readSavedRatings(peer,a.hash,source.sourceRevision);QVERIFY(pending);QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingDatagrams(),3000);const auto held=server.receiveDatagram();const auto cancel=a.owner->cancel(*pending);QVERIFY(cancel);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().outstandingCommands==0,3000);QCOMPARE(a.owner->observation().ratingReadSnapshot->state,app::AzureusRatingReadState::Cancelled);emptyRatingReply(server,held);
   const auto closing=a.owner->readSavedRatings(peer,a.hash,source.sourceRevision);QVERIFY(closing);QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingDatagrams(),3000);QVERIFY(a.owner->close());QVERIFY(a.owner->observation().joined);QCOMPARE(a.owner->observation().ratingReadSnapshot->state,app::AzureusRatingReadState::Closed);
   a.network="public";a.state();QVERIFY(!storage::readDistributedRating(*a.held,a.hash));a.network="Public";a.flags=0x200;a.state();QVERIFY(!storage::readDistributedRating(*a.held,a.hash));a.flags=0;a.dht=false;a.state();a.config();QVERIFY(!storage::readDistributedRating(*a.held,a.hash));
 }

 void twoNormalProfileOwnersExchangeDirectValuesWithoutGeneratedClosestFacts(){
   NormalValues a("127.1.0.1"),b("127.2.0.1");QVERIFY(actualContact(a.owner->endpoint()).id!=actualContact(b.owner->endpoint()).id);
   const auto admitA=a.owner->allowPeer(b.owner->endpoint()),admitB=b.owner->allowPeer(a.owner->endpoint());QVERIFY(admitA&&admitB);
   QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*admitA)&&successful(*b.owner,*admitB),3000);
   const std::string bytes("owned\0normal",12);const auto first=a.owner->publishValue(b.owner->endpoint(),"normal-A",bytes);QVERIFY(first);
   QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*first),5000);auto published=a.owner->observation().publishSnapshot;QVERIFY(published);QCOMPARE(published->state,app::AzureusPublishState::Complete);QVERIFY(published->localValue&&published->localValue->isLocal());QCOMPARE(published->readback.size(),std::size_t{1});QCOMPARE(published->readback[0].bytes,bytes);
   QTRY_VERIFY_WITH_TIMEOUT(b.owner->observation().databaseSnapshot&&b.owner->observation().databaseSnapshot->stores==1,3000);
   const auto before=b.owner->observation().databaseSnapshot;QCOMPARE(before->localPuts,std::uint64_t{0});QCOMPARE(before->values,std::size_t{1});QVERIFY(b.owner->observation().routerSnapshot->counts()[4]>=2);
   const auto second=b.owner->publishValue(a.owner->endpoint(),"normal-B","other-owned-value");QVERIFY(second);QTRY_VERIFY_WITH_TIMEOUT(finished(*b.owner,*second),5000);QCOMPARE(b.owner->observation().publishSnapshot->state,app::AzureusPublishState::Complete);
   QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().databaseSnapshot->stores==1&&b.owner->observation().databaseSnapshot->localPuts==1,3000);
   QCOMPARE(a.owner->observation().databaseSnapshot->localPuts,std::uint64_t{1});QCOMPARE(a.owner->observation().databaseSnapshot->keys,std::size_t{2});QCOMPARE(b.owner->observation().databaseSnapshot->keys,std::size_t{2});QCOMPARE(before->localPuts,std::uint64_t{0});
   QVERIFY(a.owner->close());QVERIFY(b.owner->close());QVERIFY(a.owner->observation().joined&&b.owner->observation().joined);QVERIFY(a.owner->observation().databaseSnapshot->closed);QCOMPARE(published->state,app::AzureusPublishState::Complete);
 }
 void normalReceiverRequiresPeerTokenAndKeepsUnseededCacheForwardsOut(){
   NormalValues server("127.3.0.1");udp::Config config;config.network=4;udp::OwnedPingTransport client(std::move(config));QVERIFY(client.start(QHostAddress(QStringLiteral("127.4.0.1"))));QVERIFY(client.allowPeer(server.owner->endpoint()));QObject receiver;
   std::optional<udp::DetailedFindNodeOutcome> token;auto ignored=client.requestStoreToken(server.owner->endpoint(),55,&receiver,[&](auto result){token.emplace(std::move(result));},100ms);QVERIFY(ignored);
   QTRY_VERIFY_WITH_TIMEOUT(token.has_value(),3000);QVERIFY(!*token);QCOMPARE(token->error(),udp::Failure::Timeout);QVERIFY(server.owner->observation().databaseSnapshot);QCOMPARE(server.owner->observation().databaseSnapshot->stores,std::uint64_t{0});
   const auto admitted=server.owner->allowPeer(client.endpoint());QVERIFY(admitted);QTRY_VERIFY_WITH_TIMEOUT(successful(*server.owner,*admitted),3000);
   token.reset();QVERIFY(client.requestStoreToken(server.owner->endpoint(),55,&receiver,[&](auto result){token.emplace(std::move(result));}));QTRY_VERIFY_WITH_TIMEOUT(token.has_value(),3000);QVERIFY(bool(*token));const auto* decoded=std::get_if<udp::find::Decoded>(&**token);QVERIFY(decoded);const auto cookie=std::get<udp::find::ReplyBody>(decoded->body()).randomId;
   const auto origin=actualContact(client.endpoint());const std::string key(20,'K');
   const auto request=[&](std::int32_t supplied,const services::azureus_nodes::Contact& contact){udp::StoreRequest result;result.randomId=supplied;result.keys={key};result.valueSets={{{1,QDateTime::currentMSecsSinceEpoch(),"real-body",contact,0,0,-1}}};return result;};
   std::optional<udp::StoreOutcome> reply;QVERIFY(client.store(server.owner->endpoint(),55,request(cookie^1,origin),&receiver,[&](auto result){reply.emplace(std::move(result));}));QTRY_VERIFY_WITH_TIMEOUT(reply.has_value(),3000);QVERIFY(bool(*reply));QTRY_VERIFY_WITH_TIMEOUT(server.owner->observation().snapshot->transport().counters().sent==2,3000);QCOMPARE(server.owner->observation().databaseSnapshot->stores,std::uint64_t{0});
   auto forwarded=origin;forwarded.address.bytes[1]=9;forwarded.id=actualContact({QHostAddress(QStringLiteral("127.9.0.1")),client.endpoint().port}).id;QVERIFY(forwarded.id!=origin.id);
   reply.reset();QVERIFY(client.store(server.owner->endpoint(),55,request(cookie,forwarded),&receiver,[&](auto result){reply.emplace(std::move(result));}));QTRY_VERIFY_WITH_TIMEOUT(reply.has_value(),3000);QVERIFY(bool(*reply));QTRY_VERIFY_WITH_TIMEOUT(server.owner->observation().snapshot->transport().counters().sent==3,3000);QCOMPARE(server.owner->observation().databaseSnapshot->stores,std::uint64_t{0});
   reply.reset();QVERIFY(client.store(server.owner->endpoint(),55,request(cookie,origin),&receiver,[&](auto result){reply.emplace(std::move(result));}));QTRY_VERIFY_WITH_TIMEOUT(reply.has_value(),3000);QVERIFY(bool(*reply));QTRY_VERIFY_WITH_TIMEOUT(server.owner->observation().databaseSnapshot->stores==1,3000);
   std::optional<udp::FindValueOutcome> found;services::azureus_nodes::NodeId target;target.fill('K');QVERIFY(client.findValue(server.owner->endpoint(),55,target,0,32,&receiver,[&](auto result){found.emplace(std::move(result));}));QTRY_VERIFY_WITH_TIMEOUT(found.has_value(),3000);QVERIFY(bool(*found));const auto* values=std::get_if<udp::lookup::FoundValues>(&std::get<udp::lookup::ReplyBody>((*found)->body()).result);QVERIFY(values);QCOMPARE(values->values.size(),std::size_t{1});QCOMPARE(values->values[0].bytes,std::string("real-body"));QVERIFY(server.owner->close());client.close();
 }
 void normalReceiverChangedProfileWithdrawsBeforeAnyRemoteStore(){
   NormalValues server("127.5.0.1");udp::Config config;config.network=4;udp::OwnedPingTransport client(std::move(config));QVERIFY(client.start(QHostAddress(QStringLiteral("127.6.0.1"))));QVERIFY(client.allowPeer(server.owner->endpoint()));const auto admitted=server.owner->allowPeer(client.endpoint());QVERIFY(admitted);QTRY_VERIFY_WITH_TIMEOUT(successful(*server.owner,*admitted),3000);QObject receiver;
   std::optional<udp::DetailedFindNodeOutcome> token;QVERIFY(client.requestStoreToken(server.owner->endpoint(),55,&receiver,[&](auto value){token.emplace(std::move(value));}));QTRY_VERIFY_WITH_TIMEOUT(token.has_value(),3000);QVERIFY(bool(*token));const auto* decoded=std::get_if<udp::find::Decoded>(&**token);QVERIFY(decoded);const auto cookie=std::get<udp::find::ReplyBody>(decoded->body()).randomId;
   QFile changed(server.root.path()+"/dht/net4/version.dat");QVERIFY(changed.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(changed.write("d4:nexti1ee"),qint64{11});changed.close();
   udp::StoreRequest request;request.randomId=cookie;request.keys={std::string(20,'S')};request.valueSets={{{1,QDateTime::currentMSecsSinceEpoch(),"must-not-store",actualContact(client.endpoint()),0,0,-1}}};std::optional<udp::StoreOutcome> reply;
   QVERIFY(client.store(server.owner->endpoint(),55,std::move(request),&receiver,[&](auto value){reply.emplace(std::move(value));},100ms));QTRY_VERIFY_WITH_TIMEOUT(reply.has_value(),3000);QVERIFY(!*reply);QCOMPARE(reply->error(),udp::Failure::Timeout);QCOMPARE(server.owner->observation().databaseSnapshot->stores,std::uint64_t{0});QTRY_VERIFY_WITH_TIMEOUT(server.owner->observation().snapshot->transport().counters().authorityErrors>0,3000);QVERIFY(server.owner->close());QVERIFY(server.owner->observation().joined);client.close();
 }
 void normalValueOwnersCancelWithoutAdmissionAndRestartWithReservedVersions(){
   NormalValues a("127.7.0.1"),b("127.8.0.1");auto admit=a.owner->allowPeer(b.owner->endpoint());QVERIFY(admit);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*admit),3000);
   const auto cancelled=a.owner->publishValue(b.owner->endpoint(),"normal-cancel","one");QVERIFY(cancelled);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().snapshot->transport().pending()==1,3000);const auto cancel=a.owner->cancel(*cancelled);QVERIFY(cancel);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().outstandingCommands==0,3000);QCOMPARE(a.owner->observation().publishSnapshot->state,app::AzureusPublishState::Cancelled);QCOMPARE(b.owner->observation().databaseSnapshot->stores,std::uint64_t{0});
   admit=b.owner->allowPeer(a.owner->endpoint());QVERIFY(admit);QTRY_VERIFY_WITH_TIMEOUT(successful(*b.owner,*admit),3000);const auto first=a.owner->publishValue(b.owner->endpoint(),"normal-restart","before");QVERIFY(first);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*first),5000);QCOMPARE(a.owner->observation().publishSnapshot->state,app::AzureusPublishState::Complete);const auto oldVersion=a.owner->observation().publishSnapshot->localValue->version();
   a.restart();b.restart();QCOMPARE(b.owner->observation().databaseSnapshot->stores,std::uint64_t{0});QCOMPARE(b.owner->observation().databaseSnapshot->keys,std::size_t{0});const auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(successful(*a.owner,*aa)&&successful(*b.owner,*bb),3000);
   const auto second=a.owner->publishValue(b.owner->endpoint(),"normal-restart","after");QVERIFY(second);QTRY_VERIFY_WITH_TIMEOUT(finished(*a.owner,*second),5000);const auto final=a.owner->observation().publishSnapshot;QCOMPARE(final->state,app::AzureusPublishState::Complete);QVERIFY(final->localValue->version()>oldVersion);QCOMPARE(final->readback[0].bytes,std::string("after"));
 }
 void normalReceivingCannotBeEnabledWithoutAHeldProfile(){
   auto invalid=options();invalid.serveValues=true;const auto result=app::OwnedAzureusService::create(invalid);QVERIFY(!result);QCOMPARE(result.error(),app::AzureusServiceError::Invalid);
 }
 void originalVersionReservationPreservesSignedJavaWidthsAndChunkBoundary(){
   using storage::originalAzureusVersionReservation;auto first=originalAzureusVersionReservation({},1000000);QCOMPARE(first.first,1000);QCOMPARE(first.next,1128);
   auto rollback=originalAzureusVersionReservation(2000,1000000);QCOMPARE(rollback.first,2000);QCOMPARE(rollback.next,2128);
   auto wide=originalAzureusVersionReservation(0x100000005LL,0);QCOMPARE(wide.first,5);QCOMPARE(wide.next,133);
   auto wrap=originalAzureusVersionReservation(std::numeric_limits<std::int32_t>::max()-63,0);QCOMPARE(wrap.next,std::numeric_limits<std::int32_t>::min()+64);
 }
 void profileVersionsReserveBeforeUsePreserveUnknownsAndRecoverOnlyCanonicalPending(){
   QTemporaryDir root;QVERIFY(root.isValid());QDir dir(root.path());QVERIFY(dir.mkpath("dht/net4"));
   const auto path=root.path()+"/dht/net4/version.dat";auto write=[&](const QString& name,const QByteArray& bytes){QFile file(name);return file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(bytes)==bytes.size()&&file.flush();};
   QVERIFY(write(path,"d4:nexti2000e6:future3:rawe"));auto held=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(held);auto versions=storage::AzureusValueVersions::open(*held,4);QVERIFY(versions);
   QCOMPARE((*versions)->next(1000000).value(),2000);QFile saved(path);QVERIFY(saved.open(QIODevice::ReadOnly));const auto firstBytes=saved.readAll();saved.close();auto parsed=core::Document::parse(firstBytes.toStdString());QVERIFY(parsed);QCOMPARE(parsed->find("next")->integer,std::int64_t{2128});QCOMPARE(parsed->find("future")->bytes,std::string_view("raw"));
   for(int i=1;i<128;++i){auto next=(*versions)->next(0);QVERIFY(next);QCOMPARE(*next,2000+i);}QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.readAll(),firstBytes);saved.close();QCOMPARE((*versions)->next(0).value(),2128);
   versions->reset();auto restarted=storage::AzureusValueVersions::open(*held,4);QVERIFY(restarted);QCOMPARE((*restarted)->next(0).value(),2256);restarted->reset();
   QVERIFY(QFile::remove(path));QVERIFY(write(root.path()+"/dht/net4/version.saving","d4:nexti9000e6:future3:rawe"));auto recovered=storage::AzureusValueVersions::open(*held,4);QVERIFY(recovered);QCOMPARE((*recovered)->next(0).value(),9000);QVERIFY(!QFile::exists(root.path()+"/dht/net4/version.saving"));
   QVERIFY(write(root.path()+"/dht/net4/version.saving","d4:nexti9999ee"));QVERIFY(!(*recovered)->next(0));QVERIFY(!storage::AzureusValueVersions::open(*held,4));QVERIFY(QFile::exists(root.path()+"/dht/net4/version.saving"));
 }
 void versionOwnerRetainsCallerLeaseAndRefusesLockOrDirectoryReplacement(){
   QTemporaryDir root;QVERIFY(root.isValid());auto acquired=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(acquired);std::optional<storage::ProfileLease> held{std::move(*acquired)};auto versions=storage::AzureusValueVersions::open(*held,0);QVERIFY(versions);held.reset();QVERIFY(!storage::ProfileLease::acquire(root.path().toStdString()));QVERIFY((*versions)->next(1000000));
   QVERIFY(QFile::rename(root.path()+"/.azlock",root.path()+"/.azlock.old"));auto replacementResult=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(replacementResult);std::optional<storage::ProfileLease> replacement{std::move(*replacementResult)};const auto stale=(*versions)->next(1000000);QVERIFY(!stale);QCOMPARE(stale.error().code,storage::ErrorCode::Conflict);versions->reset();replacement.reset();
   QTemporaryDir another;QVERIFY(another.isValid());auto other=storage::ProfileLease::acquire(another.path().toStdString());QVERIFY(other);auto owned=storage::AzureusValueVersions::open(*other,0);QVERIFY(owned);QVERIFY(QDir(another.path()).rename("dht","dht-old"));QVERIFY(QDir(another.path()).mkdir("dht"));QVERIFY(!(*owned)->next(1000));
 }
 void localPublicationUsesActualTokenStoreAndReadbackFromGeneratedReceiver(){
   QTemporaryDir root;QVERIFY(root.isValid());auto held=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(held);auto versions=storage::AzureusValueVersions::open(*held,4);QVERIFY(versions);
   auto policy=options();policy.valueVersions=*versions;auto made=app::OwnedAzureusService::create(policy);QVERIFY(made);auto client=std::move(*made);
   const std::string key="owned-original-rating-key",payload("d1:ci4e1:n4:teste\0",18);const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(key),QCryptographicHash::Sha1);const std::string encodedKey(digest.constData(),static_cast<std::size_t>(digest.size()));
   udp::Config config;config.generatedStoreService.emplace();config.generatedStoreService->volatileDatabase.emplace();services::azureus_store_policy::Key expected;expected.bytes=encodedKey;expected.closest=true;config.generatedStoreService->keyStates={expected};
   udp::OwnedPingTransport server(std::move(config));QVERIFY(server.start());QVERIFY(server.allowPeer(client->endpoint()));
   // The generated receiver's explicit test-only closest fact is not production
   // routing authority. The real owner must separately admit its endpoint.
   const auto rejected=client->publishValue(server.endpoint(),key,payload);QVERIFY(rejected);QTRY_VERIFY_WITH_TIMEOUT(finished(*client,*rejected),3000);QCOMPARE(client->observation().publishSnapshot->error,std::optional{app::AzureusServiceError::Invalid});QCOMPARE(server.statistics().received,std::uint64_t{0});
   const auto allowed=client->allowPeer(server.endpoint());QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*allowed),3000);
   const auto request=client->publishValue(server.endpoint(),key,payload);QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(finished(*client,*request),5000);const auto snapshot=client->observation().publishSnapshot;QVERIFY(snapshot);QCOMPARE(snapshot->request,*request);QCOMPARE(snapshot->state,app::AzureusPublishState::Complete);QVERIFY(snapshot->localValue&&snapshot->localValue->isLocal());QCOMPARE(*snapshot->localValue->payload().view(),std::string_view(payload));QVERIFY(!snapshot->readback.empty());QCOMPARE(snapshot->readback[0].bytes,payload);QCOMPARE(snapshot->readback[0].version,snapshot->localValue->version());QVERIFY(snapshot->readback[0].originator.address==test::ddbAddress(client->endpoint()));
   const auto state=server.databaseState();QVERIFY(state);QCOMPARE(state->stores,std::uint64_t{1});QCOMPARE(state->reads,std::uint64_t{1});QVERIFY(client->close());QVERIFY(client->observation().joined);QCOMPARE(snapshot->state,app::AzureusPublishState::Complete);server.close();
 }
 void localPublicationCancellationAndChangedProfileNeverSendStore(){
   QTemporaryDir root;QVERIFY(root.isValid());auto held=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(held);auto versions=storage::AzureusValueVersions::open(*held,4);QVERIFY(versions);auto policy=options();policy.valueVersions=*versions;auto made=app::OwnedAzureusService::create(policy);QVERIFY(made);auto client=std::move(*made);
   QUdpSocket sink;QVERIFY(sink.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress));const udp::Peer peer{sink.localAddress(),sink.localPort()};const auto allowed=client->allowPeer(peer);QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*allowed),3000);
   const auto request=client->publishValue(peer,"owned-cancel","binary");QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(sink.hasPendingDatagrams()&&client->observation().snapshot->transport().pending()==1,3000);const auto tokenRequest=sink.receiveDatagram();QVERIFY(tokenRequest.isValid());const auto cancel=client->cancel(*request);QVERIFY(cancel);QTRY_VERIFY_WITH_TIMEOUT(client->observation().outstandingCommands==0,3000);QCOMPARE(client->observation().publishSnapshot->state,app::AzureusPublishState::Cancelled);QCOMPARE(client->observation().snapshot->transport().counters().sent,std::uint64_t{1});
   QFile changed(root.path()+"/dht/net4/version.dat");QVERIFY(changed.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(changed.write("d4:nexti1ee"),qint64{11});changed.close();const auto stale=client->publishValue(peer,"owned-stale","binary");QVERIFY(stale);QTRY_VERIFY_WITH_TIMEOUT(finished(*client,*stale),3000);QCOMPARE(client->observation().publishSnapshot->error,std::optional{app::AzureusServiceError::Profile});QCOMPARE(client->observation().snapshot->transport().counters().sent,std::uint64_t{1});QVERIFY(client->close());QVERIFY(client->observation().joined);
 }
 void defaultAndInvalidPolicyNeverCreateAnOwner(){
   auto disabled=app::OwnedAzureusService::create();QVERIFY(!disabled);QCOMPARE(disabled.error(),app::AzureusServiceError::Disabled);
   auto invalid=options();invalid.bindAddress=QHostAddress(QStringLiteral("192.0.2.1"));auto result=app::OwnedAzureusService::create(invalid);QVERIFY(!result);QCOMPARE(result.error(),app::AzureusServiceError::Invalid);
   invalid=options();invalid.maximumPending=33;result=app::OwnedAzureusService::create(invalid);QVERIFY(!result);invalid=options();invalid.maximumCommands=65;QVERIFY(!app::OwnedAzureusService::create(invalid));
 }
 void stdWorkerCreatesAndJoinsTheDedicatedQtOwner(){
   std::promise<bool> done;auto result=done.get_future();std::jthread worker([&]{try{auto service=make();const auto before=service->observation();const bool valid=before.snapshot&&before.snapshot->transport().state()==udp::TransportStatisticsState::Bound&&before.snapshot->transport().generation()!=0&&before.snapshot->transport().family()==udp::TransportStatisticsFamily::IPv4;
     const auto closed=service->close();const auto after=service->observation();done.set_value(valid&&bool(closed)&&after.joined&&!after.current&&after.state==app::AzureusServiceState::Closed&&after.snapshot&&after.snapshot->transport().state()==udp::TransportStatisticsState::Closed);}catch(...){done.set_value(false);}});
   QTRY_VERIFY_WITH_TIMEOUT(result.wait_for(0ms)==std::future_status::ready,5000);QVERIFY(result.get());
 }
 void twoActualServiceOwnersExchangeRttCoordinatesAndKeepOldSnapshots(){
   auto server=make(),client=make();QVERIFY(server->endpoint().port);QVERIFY(client->endpoint().port);const auto allow=server->allowPeer(client->endpoint());QVERIFY(allow);QTRY_VERIFY_WITH_TIMEOUT(successful(*server,*allow),3000);
   const auto initial=client->observation();QVERIFY(initial.snapshot);QCOMPARE(initial.snapshot->transport().counters().sent,std::uint64_t{0});
   const auto request=client->ping(server->endpoint());QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*request),3000);QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->coordinateObservations()==1,3000);
   const auto first=client->observation();QVERIFY(first.current);QVERIFY(first.snapshot->lastRttNanoseconds()&&*first.snapshot->lastRttNanoseconds()>0);QVERIFY(first.snapshot->remotePosition());QVERIFY(hasEvent(first,udp::OperationEventKind::Sent));QVERIFY(hasEvent(first,udp::OperationEventKind::Reply));
   const auto reverse=server->ping(client->endpoint());QVERIFY(reverse);QTRY_VERIFY_WITH_TIMEOUT(successful(*server,*reverse),3000);QTRY_VERIFY_WITH_TIMEOUT(server->observation().snapshot->coordinateObservations()==1,3000);
   const auto second=client->ping(server->endpoint());QVERIFY(second);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*second),3000);QCOMPARE(initial.snapshot->transport().counters().sent,std::uint64_t{0});QCOMPARE(first.snapshot->coordinateObservations(),std::uint64_t{1});QVERIFY(client->observation().snapshot->events().size()<=64);
 }
 void existingActualFindNodeAndStreamingFindValueRunOnTheQtOwner(){
   udp::Config remote{};remote.findNodeReply.emplace();udp::FindValueSlice partial;partial.continuation=true;partial.values=std::vector<udp::lookup::values::Value>{};udp::FindValueSlice terminal;terminal.values=std::vector<udp::lookup::values::Value>{};remote.findValueReply=std::vector<udp::FindValueSlice>{partial,terminal};
   udp::OwnedPingTransport server(std::move(remote));QVERIFY(server.start());auto client=make();QVERIFY(server.allowPeer(client->endpoint()));
   const auto node=client->findNode(server.endpoint(),udp::find::nodes::NodeId{});QVERIFY(node);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*node),3000);
   const auto values=client->findValue(server.endpoint(),udp::lookup::nodes::NodeId{},0,255);QVERIFY(values);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*values),3000);
   const auto observed=client->observation();QVERIFY(hasEvent(observed,udp::OperationEventKind::PartialReply));QVERIFY(hasEvent(observed,udp::OperationEventKind::Reply));QVERIFY(observed.snapshot->transport().counters().replies>=3);QCOMPARE(observed.outstandingCommands,std::size_t{0});
 }
 void actualTimeoutCancellationAndJoinedCloseRetainTheirOwnEvents(){
   auto silent=make(false),client=make();const auto allowed=silent->allowPeer(client->endpoint());QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*silent,*allowed),3000);
   const auto timeout=client->ping(silent->endpoint(),30ms);QVERIFY(timeout);QTRY_VERIFY_WITH_TIMEOUT(finished(*client,*timeout),3000);QCOMPARE(client->observation().lastResult->transportFailure,std::optional{udp::Failure::Timeout});QVERIFY(hasEvent(client->observation(),udp::OperationEventKind::Timeout));
   const auto pending=client->ping(silent->endpoint(),2s);QVERIFY(pending);QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->transport().pending()==1,3000);const auto cancel=client->cancel(*pending);QVERIFY(cancel);
   QTRY_VERIFY_WITH_TIMEOUT(client->observation().outstandingCommands==0,3000);QVERIFY(hasEvent(client->observation(),udp::OperationEventKind::Cancelled));
   const auto closing=client->ping(silent->endpoint(),2s);QVERIFY(closing);QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->transport().pending()==1,3000);const auto retained=client->observation().snapshot;
   QVERIFY(client->close());const auto closed=client->observation();QVERIFY(closed.joined);QVERIFY(!closed.current);QCOMPARE(closed.state,app::AzureusServiceState::Closed);QVERIFY(hasEvent(closed,udp::OperationEventKind::Closed));QCOMPARE(closed.snapshot->transport().pending(),std::size_t{0});QCOMPARE(retained->transport().pending(),std::size_t{1});QVERIFY(client->close());QVERIFY(!client->ping(silent->endpoint()));
 }
 void boundedAdmissionAndCloseDoNotDependOnFreeCommandSlots(){
   auto silent=make(false);auto policy=options();policy.maximumCommands=1;policy.maximumPending=1;auto made=app::OwnedAzureusService::create(policy);QVERIFY(made);auto client=std::move(*made);const auto allowed=silent->allowPeer(client->endpoint());QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*silent,*allowed),3000);
   const auto first=client->ping(silent->endpoint(),2s);QVERIFY(first);QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->transport().pending()==1,3000);const auto second=client->ping(silent->endpoint());QVERIFY(!second);QCOMPARE(second.error(),app::AzureusServiceError::Capacity);
   QVERIFY(client->close());QVERIFY(client->observation().joined);QCOMPARE(client->observation().outstandingCommands,std::size_t{0});
 }
 void invalidPublicPeersAndDeadlinesNeverSendOrChangeTheMailbox(){
   auto service=make();const auto before=service->observation();QVERIFY(before.snapshot);QVERIFY(!service->ping({QHostAddress(QStringLiteral("192.0.2.1")),1234}));QVERIFY(!service->ping({QHostAddress::LocalHost,0}));QVERIFY(!service->ping({QHostAddress::LocalHost,1234},0ns));QVERIFY(!service->ping({QHostAddress::LocalHost,1234},31s));QVERIFY(!service->cancel(0));QVERIFY(!service->findValue({QHostAddress::LocalHost,1234},{},0,257));
   const auto after=service->observation();QCOMPARE(after.sequence,before.sequence);QVERIFY(after.snapshot==before.snapshot);QCOMPARE(after.snapshot->transport().counters().sent,std::uint64_t{0});
 }
 void passiveReadsNeverQueueCommandsOrRepublishUnchangedFacts(){
   auto service=make();const auto before=service->observation();QVERIFY(before.snapshot);for(unsigned i=0;i<1000;++i){const auto value=service->observation();QCOMPARE(value.sequence,before.sequence);QVERIFY(value.snapshot==before.snapshot);QCOMPARE(value.outstandingCommands,std::size_t{0});}
   QCOMPARE(service->observation().snapshot->coordinateObservations(),std::uint64_t{0});QVERIFY(service->observation().snapshot->events().empty());
 }
 void concurrentMailboxReadsAndImmediatePendingDestructionJoinSafely(){
   auto silent=make(false);for(unsigned iteration=0;iteration<4;++iteration){auto client=make();const auto request=client->ping(silent->endpoint(),2s);QVERIFY(request);std::atomic<bool> stop=false;std::atomic<unsigned> reads=0;
     std::jthread reader([&](std::stop_token token){while(!token.stop_requested()&&!stop.load()){const auto value=client->observation();if(value.snapshot){++reads;}}});QTRY_VERIFY_WITH_TIMEOUT(reads.load()>0,3000);QVERIFY(client->close());stop=true;reader.request_stop();reader.join();QVERIFY(client->observation().joined);}
 }
};
QTEST_GUILESS_MAIN(OwnedAzureusServiceTests)
#include "owned_azureus_service_tests.moc"

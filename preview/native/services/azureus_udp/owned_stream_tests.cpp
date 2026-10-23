// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_transport.hpp"
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QThread>
#include <QScopeGuard>
#include <QtTest>
#include <memory>
#include <stdexcept>
namespace udp=biglybtc::services::azureus_udp;
namespace lookup=udp::lookup;
using namespace std::chrono_literals;
namespace {
bool bind(QUdpSocket& socket) { socket.setProxy(QNetworkProxy::NoProxy);return socket.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress); }
udp::Peer endpoint(const QUdpSocket& socket) { return {socket.localAddress(),socket.localPort()}; }
bool send(QUdpSocket& socket,const std::string& bytes,const udp::Peer& peer) { return socket.writeDatagram(bytes.data(),static_cast<qint64>(bytes.size()),peer.address,peer.port)==static_cast<qint64>(bytes.size()); }
QNetworkDatagram take(QUdpSocket& socket) { if (!QTest::qWaitFor([&]{return socket.hasPendingDatagrams();},2000)) { return {}; }return socket.receiveDatagram(1401); }
lookup::nodes::NodeId target() { lookup::nodes::NodeId id{};for (std::size_t i=0;i<id.size();++i) { id[i]=static_cast<std::uint8_t>(i); }return id; }
lookup::values::Value value(char data) {
  lookup::values::Value value;value.version=100+data;value.created=1700000000000;value.bytes=std::string(1,data);value.flags=255;value.lifeHours=5;value.replication=-1;
  value.originator.protocol=55;value.originator.address.bytes[0]=192;value.originator.address.bytes[2]=2;value.originator.address.bytes[3]=1;value.originator.address.port=6881;return value;
}
std::vector<udp::FindValueSlice> pieces() {
  std::vector<udp::FindValueSlice> out;for (char c='A';c<='C';++c) { udp::FindValueSlice part;part.continuation=c!='C';part.values=std::vector<lookup::values::Value>{value(c)};out.push_back(std::move(part)); }return out;
}
std::expected<lookup::Decoded,lookup::Error> request(const QNetworkDatagram& datagram,lookup::nodes::NodeCache& cache) {
  lookup::Context context;context.header.network=4;const auto bytes=datagram.data();return lookup::decode(lookup::udp::Kind::Request,{bytes.constData(),static_cast<std::size_t>(bytes.size())},context,cache);
}
std::expected<std::string,lookup::Error> reply(const lookup::Decoded& request,bool more,char data='A',std::int32_t network=4) {
  lookup::ReplyFields f;const auto& h=request.header().fields();f.header.network=network;f.header.connection=h.connection;f.header.transaction=h.transaction;f.header.remoteVersion=h.originatorVersion;f.continuation=more;
  const auto owned=value(data);lookup::values::Snapshot s;s.version=owned.version;s.created=owned.created;s.bytes=owned.bytes;s.originator=lookup::nodes::Snapshot{true,55,owned.originator.address};s.flags=owned.flags;s.lifeHours=owned.lifeHours;s.replication=owned.replication;
  std::vector<std::optional<lookup::values::Snapshot>> values{s};f.values=values;return lookup::encodeReply(f);
}
}
class OwnedStreamTests final : public QObject {
 Q_OBJECT
private slots:
 void nativeStreams_data() {
  QTest::addColumn<bool>("ipv6");QTest::addColumn<int>("network");QTest::addColumn<int>("version");QTest::addColumn<int>("remote");
  QTest::newRow("ipv4-bigly")<<false<<4<<55<<55;QTest::newRow("ipv4-upgrade")<<false<<4<<55<<51;QTest::newRow("ipv4-azureus")<<false<<0<<54<<54;QTest::newRow("ipv6-bigly")<<true<<4<<55<<55;QTest::newRow("ipv6-azureus")<<true<<3<<54<<54;
 }
 void nativeStreams() {
  QFETCH(bool,ipv6);QFETCH(int,network);QFETCH(int,version);QFETCH(int,remote);
  udp::Config sc;sc.network=network;sc.version=static_cast<std::int8_t>(version);sc.instance=42;sc.servePings=true;sc.findNodeReply=udp::FindNodeReply{};sc.findValueReply=pieces();udp::Config cc;cc.network=network;cc.version=static_cast<std::int8_t>(version);
  udp::OwnedPingTransport server(std::move(sc)),client(std::move(cc));const QHostAddress ip=ipv6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost;QVERIFY(server.start(ip));QVERIFY(client.start(ip));QVERIFY(server.allowPeer(client.endpoint()));QVERIFY(client.allowPeer(server.endpoint()));QObject receiver;
  int calls=0,other=0;bool failed=false;std::string data;std::vector<bool> more;std::optional<lookup::Decoded> last;
  QVERIFY(client.findValue(server.endpoint(),static_cast<std::int8_t>(remote),target(),-1,255,&receiver,[&](udp::FindValueOutcome r){++calls;if(!r){failed=true;return;}const auto& b=std::get<lookup::ReplyBody>(r->body());more.push_back(b.continuation);const auto& v=std::get<lookup::FoundValues>(b.result).values;data+=v.at(0).bytes;last.emplace(std::move(*r));},2s));
  QVERIFY(client.ping(server.endpoint(),static_cast<std::int8_t>(remote),&receiver,[&](udp::Outcome r){if(!r){failed=true;}++other;},2s));QVERIFY(client.findNode(server.endpoint(),static_cast<std::int8_t>(remote),target(),&receiver,[&](udp::FindNodeOutcome r){if(!r){failed=true;}++other;},2s));
  QTRY_COMPARE_WITH_TIMEOUT(calls,3,3000);QTRY_COMPARE(other,2);QVERIFY(!failed);QCOMPARE(data,std::string("ABC"));QVERIFY(more==std::vector<bool>({true,true,false}));QVERIFY(last);QCOMPARE(last->header().fields().version,version);QCOMPARE(last->header().fields().network,network);QCOMPARE(last->header().fields().instance,42);QVERIFY(last->trailing().empty());QCOMPARE(client.pendingCount()+client.completionCount(),std::size_t(0));QVERIFY(!client.deadlineActive());QCOMPARE(client.statistics().replies,std::uint64_t(5));
  const auto snapshot=client.statisticsSnapshot();QVERIFY(snapshot);QCOMPARE(snapshot->network(),network);
  QCOMPARE(snapshot->counters().sent,std::uint64_t{3});QCOMPARE(snapshot->counters().replies,std::uint64_t{5});
  QCOMPARE(snapshot->pending()+snapshot->completions(),std::size_t{0}); // Replies are actual datagrams, not logical transaction count.

 }
 void partialThenTimeoutKeepsCallback() {
  udp::OwnedPingTransport client(udp::Config{});QVERIFY(client.start());QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));QObject receiver;std::vector<udp::Failure> errors;int parts=0;
  QVERIFY(client.findValue(endpoint(peer),55,target(),0,255,&receiver,[&](udp::FindValueOutcome r){if(r){++parts;}else{errors.push_back(r.error());}},80ms));lookup::nodes::NodeCache cache;const auto q=request(take(peer),cache);QVERIFY(q);const auto first=reply(*q,true);QVERIFY(first);QVERIFY(send(peer,*first,client.endpoint()));QTRY_COMPARE(parts,1);QCOMPARE(client.pendingCount(),std::size_t(1));QTRY_COMPARE(errors.size(),std::size_t(1));QCOMPARE(errors[0],udp::Failure::Timeout);QCOMPARE(client.pendingCount(),std::size_t(0));const auto final=reply(*q,false);QVERIFY(final);QVERIFY(send(peer,*final,client.endpoint()));QTRY_COMPARE(client.statistics().foreign,std::uint64_t(1));QCOMPARE(parts,1);
  const auto snapshot=client.statisticsSnapshot();QVERIFY(snapshot);QCOMPARE(snapshot->counters().replies,std::uint64_t{1});
  QCOMPARE(snapshot->counters().timeouts,std::uint64_t{1});QCOMPARE(snapshot->pending()+snapshot->completions(),std::size_t{0});

 }
 void finalAfterDeadlineIsNotAccepted() {
  udp::OwnedPingTransport client(udp::Config{});QVERIFY(client.start());QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));QObject receiver;int parts=0,timeout=0;
  QVERIFY(client.findValue(endpoint(peer),55,target(),0,255,&receiver,[&](udp::FindValueOutcome r){if(r){++parts;}else if(r.error()==udp::Failure::Timeout){++timeout;}},100ms));lookup::nodes::NodeCache cache;const auto q=request(take(peer),cache);QVERIFY(q);const auto first=reply(*q,true),final=reply(*q,false);QVERIFY(first && final);QVERIFY(send(peer,*first,client.endpoint()));QTRY_COMPARE(parts,1);QThread::msleep(110);QVERIFY(send(peer,*final,client.endpoint()));QTRY_COMPARE(timeout,1);QCOMPARE(parts,1);QVERIFY(!client.deadlineActive());
 }
 void capacityKeepsQueuedPartAndTerminalError_data() { QTest::addColumn<int>("capacity");QTest::newRow("one")<<1;QTest::newRow("two")<<2; }
 void capacityKeepsQueuedPartAndTerminalError() {
  QFETCH(int,capacity);udp::Config config;config.maximumPending=static_cast<std::size_t>(capacity);udp::OwnedPingTransport client(std::move(config));QVERIFY(client.start());QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));QObject receiver;int calls=0,parts=0;std::optional<udp::Failure> error;
  QVERIFY(client.findValue(endpoint(peer),55,target(),0,255,&receiver,[&](udp::FindValueOutcome r){++calls;if(r){++parts;}else{error=r.error();}},2s));lookup::nodes::NodeCache cache;const auto q=request(take(peer),cache);QVERIFY(q);const auto first=reply(*q,true,'A'),second=reply(*q,true,'B'),final=reply(*q,false,'C');QVERIFY(first && second && final);QVERIFY(send(peer,*first,client.endpoint()));QVERIFY(send(peer,*second,client.endpoint()));QVERIFY(send(peer,*final,client.endpoint()));QTRY_COMPARE(calls,capacity);QCOMPARE(parts,capacity-1);QVERIFY(error);QCOMPARE(*error,udp::Failure::Capacity);QCOMPARE(client.pendingCount()+client.completionCount(),std::size_t(0));
 }
 void cancelAndThrowFromPartialCallback() {
  udp::OwnedPingTransport client(udp::Config{});QVERIFY(client.start());QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));QObject receiver;udp::Ticket ticket;int calls=0;bool cancelled=false;
  const auto started=client.findValue(endpoint(peer),55,target(),0,255,&receiver,[&](udp::FindValueOutcome r){++calls;if(r){QVERIFY(client.cancel(ticket));throw std::runtime_error("owned callback");}cancelled=r.error()==udp::Failure::Cancelled;},2s);QVERIFY(started);ticket=*started;lookup::nodes::NodeCache cache;const auto q=request(take(peer),cache);QVERIFY(q);const auto first=reply(*q,true);QVERIFY(first);QVERIFY(send(peer,*first,client.endpoint()));QTRY_COMPARE(calls,2);QVERIFY(cancelled);QCOMPARE(client.statistics().callbackErrors,std::uint64_t(1));QVERIFY(!client.deadlineActive());
 }
 void closeAfterPartialAndDropDeadReceiver() {
  udp::OwnedPingTransport client(udp::Config{});QVERIFY(client.start());QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));QObject receiver;int calls=0;bool closed=false;
  QVERIFY(client.findValue(endpoint(peer),55,target(),0,255,&receiver,[&](udp::FindValueOutcome r){++calls;if(r){client.close();}else{closed=r.error()==udp::Failure::Closed;}},2s));lookup::nodes::NodeCache cache;const auto q=request(take(peer),cache);QVERIFY(q);const auto first=reply(*q,true);QVERIFY(first);QVERIFY(send(peer,*first,client.endpoint()));QTRY_COMPARE(calls,2);QVERIFY(closed);
  udp::OwnedPingTransport other(udp::Config{});QVERIFY(other.start());QVERIFY(other.allowPeer(endpoint(peer)));auto dead=std::make_unique<QObject>();int deadCalls=0;QVERIFY(other.findValue(endpoint(peer),55,target(),0,255,dead.get(),[&](udp::FindValueOutcome){++deadCalls;},2s));dead.reset();QCOMPARE(other.pendingCount(),std::size_t(0));QCOMPARE(deadCalls,0);QCOMPARE(other.statistics().abandoned,std::uint64_t(1));
 }
 void callbackCanDeleteActorWithQueuedParts() {
  udp::Config config;config.findValueReply=pieces();udp::OwnedPingTransport server(std::move(config));QVERIFY(server.start());auto* client=new udp::OwnedPingTransport(udp::Config{});QPointer<udp::OwnedPingTransport> guard(client);QVERIFY(client->start());QVERIFY(server.allowPeer(client->endpoint()));QVERIFY(client->allowPeer(server.endpoint()));QObject receiver;int calls=0;
  QVERIFY(client->findValue(server.endpoint(),55,target(),0,255,&receiver,[&](udp::FindValueOutcome r){QVERIFY(r);++calls;delete client;},2s));QTRY_COMPARE(calls,1);QVERIFY(guard.isNull());
 }
 void wrongTypeEndpointAndMalformedRetainStream() {
  udp::OwnedPingTransport client(udp::Config{});QVERIFY(client.start());QUdpSocket peer,spoof;QVERIFY(bind(peer));QVERIFY(bind(spoof));QVERIFY(client.allowPeer(endpoint(peer)));QVERIFY(client.allowPeer(endpoint(spoof)));QObject receiver;int calls=0;
  QVERIFY(client.findValue(endpoint(peer),55,target(),0,255,&receiver,[&](udp::FindValueOutcome r){QVERIFY(r);++calls;},2s));lookup::nodes::NodeCache cache;const auto q=request(take(peer),cache);QVERIFY(q);const auto first=reply(*q,true),final=reply(*q,false),network=reply(*q,true,'A',3);QVERIFY(first && final && network);
  udp::packet::ReplyFields ping;ping.header.network=4;ping.header.connection=q->header().fields().connection;ping.header.transaction=q->header().fields().transaction;udp::packet::positions::Positions empty;ping.positions=empty;const auto wrong=udp::packet::encodeReply(ping);QVERIFY(wrong);QVERIFY(send(peer,*wrong,client.endpoint()));QVERIFY(send(spoof,*first,client.endpoint()));QVERIFY(send(peer,*network,client.endpoint()));QVERIFY(send(peer,first->substr(0,first->size()-1),client.endpoint()));QTRY_COMPARE(client.statistics().foreign,std::uint64_t(2));QTRY_COMPARE(client.statistics().malformed,std::uint64_t(2));QCOMPARE(client.pendingCount(),std::size_t(1));QCOMPARE(calls,0);QVERIFY(send(peer,*first,client.endpoint()));QVERIFY(send(peer,*final,client.endpoint()));QTRY_COMPARE(calls,2);
 }
 void contactModeAndOwnedSnapshotLimits() {
  udp::Config config;udp::FindValueSlice contact;const auto v=value('X');contact.contacts.push_back({true,55,v.originator.address});config.findValueReply=std::vector<udp::FindValueSlice>{contact};udp::OwnedPingTransport server(std::move(config)),client(udp::Config{});QVERIFY(server.start());QVERIFY(client.start());QVERIFY(server.allowPeer(client.endpoint()));QVERIFY(client.allowPeer(server.endpoint()));QObject receiver;int calls=0;bool contactMode=false;
  QVERIFY(client.findValue(server.endpoint(),55,target(),0,1,&receiver,[&](udp::FindValueOutcome r){++calls;if(r){const auto& b=std::get<lookup::ReplyBody>(r->body());contactMode=!b.continuation && std::holds_alternative<lookup::FoundContacts>(b.result) && std::get<lookup::FoundContacts>(b.result).contacts.size()==1;}},2s));QTRY_COMPARE(calls,1);QVERIFY(contactMode);
  udp::Config tooMany;tooMany.findValueReply=std::vector<udp::FindValueSlice>(17);udp::OwnedPingTransport bad(std::move(tooMany));const auto rejected=bad.start();QVERIFY(!rejected);QCOMPARE(rejected.error(),udp::Failure::Capacity);
  udp::Config borrowed;auto own=value('Q');own.bytes.assign(513,'!');udp::FindValueSlice invalid;invalid.values=std::vector<lookup::values::Value>{own};borrowed.findValueReply=std::vector<udp::FindValueSlice>{invalid};udp::OwnedPingTransport oversized(std::move(borrowed));const auto invalidBytes=oversized.start();QVERIFY(!invalidBytes);QCOMPARE(invalidBytes.error(),udp::Failure::Encode);
 }
 void receiverMovedToOtherOwnerIsNotInvoked() {
  udp::OwnedPingTransport client(udp::Config{});QVERIFY(client.start());QUdpSocket peer;QVERIFY(bind(peer));QVERIFY(client.allowPeer(endpoint(peer)));auto* receiver=new QObject;QThread worker;worker.start();auto* main=QThread::currentThread();const auto cleanup=qScopeGuard([&]{QMetaObject::invokeMethod(receiver,[&]{receiver->moveToThread(main);},Qt::BlockingQueuedConnection);worker.quit();if(!worker.wait(3000)){qFatal("owned worker stop");}delete receiver;});int calls=0;
  QVERIFY(client.findValue(endpoint(peer),55,target(),0,255,receiver,[&](udp::FindValueOutcome){++calls;},2s));lookup::nodes::NodeCache cache;const auto q=request(take(peer),cache);QVERIFY(q);receiver->moveToThread(&worker);const auto final=reply(*q,false);QVERIFY(final);QVERIFY(send(peer,*final,client.endpoint()));QTRY_COMPARE(client.pendingCount()+client.completionCount(),std::size_t(0));QCOMPARE(calls,0);QCOMPARE(client.statistics().abandoned,std::uint64_t(1));
 }
};
QTEST_GUILESS_MAIN(OwnedStreamTests)
#include "owned_stream_tests.moc"

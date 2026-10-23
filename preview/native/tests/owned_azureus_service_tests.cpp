#include "owned_azureus_service.hpp"
#include <QCoreApplication>
#include <QtTest>
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
}
class OwnedAzureusServiceTests final:public QObject {
 Q_OBJECT
private slots:
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
   udp::Config remote;remote.findNodeReply=udp::FindNodeReply{};udp::FindValueSlice partial;partial.continuation=true;partial.values=std::vector<udp::lookup::values::Value>{};udp::FindValueSlice terminal;terminal.values=std::vector<udp::lookup::values::Value>{};remote.findValueReply=std::vector<udp::FindValueSlice>{partial,terminal};
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

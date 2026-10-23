#include "owned_azureus_service.hpp"
#include "azureus_statistics_presentation.hpp"
#include "owned_ddb_fixture.hpp"
#include <QCoreApplication>
#include <QtTest>

namespace {
using namespace biglybtc;
namespace control=services::azureus_control;
namespace udp=services::azureus_udp;
using namespace std::chrono_literals;
auto owner(){app::AzureusServiceOptions options;options.enabled=true;auto value=app::OwnedAzureusService::create(options);if(!value){throw std::runtime_error("Cannot create actual owned GET service");}return std::move(*value);}
bool successful(const app::OwnedAzureusService& actor,std::uint64_t request){const auto value=actor.observation();return value.lastResult&&value.lastResult->request==request&&value.lastResult->terminal&&!value.lastResult->error&&!value.lastResult->transportFailure;}
bool terminal(const app::OwnedAzureusService& actor,std::uint64_t request){const auto value=actor.observation();return value.lookupSnapshot&&value.lookupSnapshot->lookup==request&&control::lookupTerminal(value.lookupSnapshot->state)&&value.outstandingCommands==0;}
const control::LookupNode* node(const control::LookupSnapshot& value,const udp::Peer& peer){for(const auto& found:value.nodes){if(found.contact.address==test::ddbAddress(peer)){return &found;}}return nullptr;}
control::NodeId target(){control::NodeId value{};value[0]=0x52;value[19]=0xa7;return value;}
}
class OwnedDdbBindingTests final:public QObject {
 Q_OBJECT
private slots:
 void explicitThreePeerGetProducesActualParentChildTicketsAndValues(){
   auto client=owner();test::OwnedDdbPeer root("127.0.0.2"),middle("127.0.0.3"),leaf("127.0.0.4");
   root.contacts({middle.endpoint()});middle.contacts({leaf.endpoint()});const std::string value("\0actual\xffvalue",13);leaf.values({{value}});
   for(const auto& peer:{root.endpoint(),middle.endpoint(),leaf.endpoint()}){const auto allowed=client->allowPeer(peer);QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*allowed),3000);}
   const auto before=client->observation();QVERIFY(!before.lookupSnapshot);QCOMPARE(before.snapshot->transport().counters().sent,std::uint64_t{0});
   const auto request=client->lookupValue(root.endpoint(),target());QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(terminal(*client,*request),5000);
   const auto observed=client->observation();const auto tree=observed.lookupSnapshot;QVERIFY(tree);QVERIFY(control::validLookupSnapshot(*tree));
   QCOMPARE(tree->generation,observed.snapshot->transport().generation());QCOMPARE(tree->lookup,*request);QCOMPARE(tree->state,control::LookupState::Complete);
   QCOMPARE(tree->nodes.size(),std::size_t{3});QCOMPARE(tree->results.size(),std::size_t{1});QCOMPARE(tree->results[0].value.bytes,value);
   const auto* a=node(*tree,root.endpoint());const auto* b=node(*tree,middle.endpoint());const auto* c=node(*tree,leaf.endpoint());QVERIFY(a&&b&&c);
   QCOMPARE(a->parent,std::uint64_t{0});QCOMPARE(b->parent,a->id);QCOMPARE(c->parent,b->id);QCOMPARE(c->depth,2U);QCOMPARE(tree->results[0].node,c->id);
   for(const auto& item:tree->nodes){QVERIFY(item.owned);QVERIFY(item.ticket);QVERIFY(item.sentMillis&&item.finishedMillis);QCOMPARE(item.state,control::LookupNodeState::Replied);}
   QVERIFY(root.valid()&&middle.valid()&&leaf.valid());QCOMPARE(root.received(),std::size_t{1});QCOMPARE(middle.received(),std::size_t{1});QCOMPARE(leaf.received(),std::size_t{1});
   const auto wanted=std::string(reinterpret_cast<const char*>(target().data()),target().size());
   QCOMPARE(root.targets().front(),wanted);QCOMPARE(middle.targets().front(),wanted);QCOMPARE(leaf.targets().front(),wanted);
   QVERIFY(!root.replies().empty()&&!middle.replies().empty()&&!leaf.replies().empty());QVERIFY(leaf.replies().front().contains(QByteArray(value.data(),static_cast<qsizetype>(value.size()))));
   QCOMPARE(observed.snapshot->transport().counters().sent,std::uint64_t{3});QCOMPARE(observed.snapshot->transport().counters().replies,std::uint64_t{3});
   const auto sequence=observed.sequence;for(unsigned i=0;i<100;++i){const auto pull=client->observation();QCOMPARE(pull.sequence,sequence);QVERIFY(pull.lookupSnapshot==tree);}
   // Real producer data exercises the shared packet fence. These packets carry
   // actual snapshots; mutations below are negative DTO policy fixtures only.
   auto first=std::make_shared<ui::AzureusStatisticsPacket>();first->session=7;first->sequence=1;first->state=ui::AzureusStatisticsState::Observed;first->snapshot=observed.snapshot;first->lookupSnapshot=tree;
   QVERIFY(ui::acceptAzureusStatistics({},first,7));
   auto wrong=std::make_shared<ui::AzureusStatisticsPacket>(*first);wrong->sequence=2;auto bad=std::make_shared<control::LookupSnapshot>(*tree);++bad->generation;wrong->lookupSnapshot=bad;
   QVERIFY(!ui::acceptAzureusStatistics(first,wrong,7));
   bad=std::make_shared<control::LookupSnapshot>(*tree);--bad->lookup;wrong->lookupSnapshot=bad;QVERIFY(!ui::acceptAzureusStatistics(first,wrong,7));
   wrong->lookupSnapshot.reset();QVERIFY(ui::acceptAzureusStatistics(first,wrong,7)); // Optional failure clears, not a current retained tree.
   QVERIFY(client->close());const auto closed=client->observation();QVERIFY(closed.joined&&!closed.current);QVERIFY(closed.lookupSnapshot);QCOMPARE(closed.lookupSnapshot->state,control::LookupState::Complete);
   QCOMPARE(tree->results[0].value.bytes,value); // Earlier immutable tree remains owned.
 }
 void discoveryNeverAdmitsAnUnownedPeerAndOldDirectFindValueStillWorks(){
   auto client=owner();test::OwnedDdbPeer root("127.0.0.2"),unowned("127.0.0.3");root.contacts({unowned.endpoint()});unowned.values({{"not queried"}});
   const auto allowed=client->allowPeer(root.endpoint());QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*allowed),3000);
   const auto request=client->lookupValue(root.endpoint(),target());QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(terminal(*client,*request),5000);
   const auto tree=client->observation().lookupSnapshot;QVERIFY(tree&&control::validLookupSnapshot(*tree));QCOMPARE(unowned.received(),std::size_t{0});QVERIFY(tree->results.empty());QCOMPARE(tree->rejectedContacts,std::uint64_t{1});
   const auto* denied=node(*tree,unowned.endpoint());QVERIFY(denied);QVERIFY(!denied->owned);QCOMPARE(denied->state,control::LookupNodeState::Rejected);QVERIFY(!denied->ticket);
   // Direct findValue is still a single explicit RPC and does not create or
   // rewrite a recursive tree. Its explicit peer authorization is not promoted
   // into recursive admission unless allowPeer succeeds for that exact endpoint.
   const auto direct=client->findValue(unowned.endpoint(),target(),0,32);QVERIFY(direct);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*direct),3000);QCOMPARE(unowned.received(),std::size_t{1});
   QVERIFY(client->observation().lookupSnapshot==tree);
   const auto forbidden=client->lookupValue(unowned.endpoint(),target());QVERIFY(forbidden);
   QTRY_VERIFY_WITH_TIMEOUT(client->observation().lastResult&&client->observation().lastResult->request==*forbidden&&client->observation().lastResult->terminal,3000);
   QCOMPARE(client->observation().lastResult->transportFailure,std::optional{udp::Failure::Peer});QCOMPARE(unowned.received(),std::size_t{1});
 }
 void actualChildCancellationThenNewLookupCloseRetainTheirOwnTerminalStates(){
   auto client=owner();test::OwnedDdbPeer root("127.0.0.2"),silent("127.0.0.3");root.contacts({silent.endpoint()});silent.silent();
   for(const auto& peer:{root.endpoint(),silent.endpoint()}){const auto allowed=client->allowPeer(peer);QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*allowed),3000);}
   const auto request=client->lookupValue(root.endpoint(),target(),0,32,5s);QVERIFY(request);
   QTRY_VERIFY_WITH_TIMEOUT(silent.received()==1&&client->observation().lookupSnapshot&&client->observation().lookupSnapshot->active==1,3000);
   const auto pending=client->observation().lookupSnapshot;const auto* oldNode=node(*pending,silent.endpoint());QVERIFY(oldNode&&oldNode->ticket);QCOMPARE(oldNode->state,control::LookupNodeState::Sent);
   const auto cancel=client->cancel(*request);QVERIFY(cancel);QTRY_VERIFY_WITH_TIMEOUT(terminal(*client,*request),3000);
   const auto cancelled=client->observation().lookupSnapshot;QCOMPARE(cancelled->state,control::LookupState::Cancelled);QCOMPARE(cancelled->stop,std::optional{control::LookupStop::Cancelled});
   const auto* child=node(*cancelled,silent.endpoint());QVERIFY(child);QCOMPARE(child->state,control::LookupNodeState::Cancelled);QVERIFY(child->finishedMillis);QVERIFY(client->observation().snapshot->transport().counters().cancelled>0);
   QCOMPARE(oldNode->state,control::LookupNodeState::Sent);QVERIFY(!oldNode->finishedMillis); // Old captured frame did not mutate.
   const auto again=client->lookupValue(root.endpoint(),target(),0,32,5s);QVERIFY(again);QVERIFY(*again>*request);
   QTRY_VERIFY_WITH_TIMEOUT(silent.received()==2&&client->observation().lookupSnapshot&&client->observation().lookupSnapshot->lookup==*again&&client->observation().lookupSnapshot->active==1,3000);
   QVERIFY(client->close());const auto ended=client->observation();QVERIFY(ended.joined&&!ended.current);QCOMPARE(ended.outstandingCommands,std::size_t{0});QCOMPARE(ended.snapshot->transport().pending(),std::size_t{0});
   QVERIFY(ended.lookupSnapshot&&control::validLookupSnapshot(*ended.lookupSnapshot));QCOMPARE(ended.lookupSnapshot->lookup,*again);QCOMPARE(ended.lookupSnapshot->state,control::LookupState::Closed);
   const auto* closed=node(*ended.lookupSnapshot,silent.endpoint());QVERIFY(closed);QCOMPARE(closed->state,control::LookupNodeState::Closed);QVERIFY(closed->finishedMillis);
   QCOMPARE(cancelled->lookup,*request);QCOMPARE(cancelled->state,control::LookupState::Cancelled);QVERIFY(!client->lookupValue(root.endpoint(),target()));
 }
 void actualStreamingValueSlicesAndDeadlineRemainBoundedWithoutInventedSuccess(){
   auto client=owner();test::OwnedDdbPeer peer("127.0.0.2");peer.values({{"first"},{"first","second"}});
   const auto allowed=client->allowPeer(peer.endpoint());QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*allowed),3000);
   const auto request=client->lookupValue(peer.endpoint(),target());QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(terminal(*client,*request),3000);
   const auto tree=client->observation().lookupSnapshot;QVERIFY(tree&&control::validLookupSnapshot(*tree));QCOMPARE(tree->state,control::LookupState::Complete);QCOMPARE(tree->results.size(),std::size_t{2});QCOMPARE(tree->duplicateValues,std::uint64_t{1});QCOMPARE(tree->nodes.front().slices,2U);
   QCOMPARE(tree->results[0].value.bytes,std::string("first"));QCOMPARE(tree->results[1].value.bytes,std::string("second"));QCOMPARE(peer.received(),std::size_t{1});QCOMPARE(peer.replies().size(),std::size_t{2});
   peer.silent();const auto timeout=client->lookupValue(peer.endpoint(),target(),0,32,30ms);QVERIFY(timeout);QTRY_VERIFY_WITH_TIMEOUT(terminal(*client,*timeout),3000);
   const auto expired=client->observation().lookupSnapshot;QVERIFY(expired&&control::validLookupSnapshot(*expired));QCOMPARE(expired->state,control::LookupState::TimedOut);QVERIFY(expired->results.empty());
   QVERIFY(client->observation().snapshot->transport().pending()==0);QCOMPARE(client->observation().lastResult->transportFailure,std::optional{udp::Failure::Timeout});
 }
 void invalidLookupsAndPassivePullsNeverStartARequest(){
   auto client=owner();const auto before=client->observation();QVERIFY(!before.lookupSnapshot);
   QVERIFY(!client->lookupValue({QHostAddress(QStringLiteral("192.0.2.1")),1234},target()));
   QVERIFY(!client->lookupValue({QHostAddress::LocalHost,1234},target(),1));QVERIFY(!client->lookupValue({QHostAddress::LocalHost,1234},target(),0,0));
   QVERIFY(!client->lookupValue({QHostAddress::LocalHost,1234},target(),0,65));QVERIFY(!client->lookupValue({QHostAddress::LocalHost,1234},target(),0,32,0ns));
   for(unsigned i=0;i<100;++i){const auto pull=client->observation();QCOMPARE(pull.sequence,before.sequence);QVERIFY(!pull.lookupSnapshot);}
   QCOMPARE(client->observation().snapshot->transport().counters().sent,std::uint64_t{0});
 }
};
QTEST_GUILESS_MAIN(OwnedDdbBindingTests)
#include "owned_ddb_binding_tests.moc"

// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/i2p_dht/saved_routing.hpp"
#include <QtTest>
#include <limits>
namespace d=biglybtc::services::i2p::dht;
namespace ar=biglybtc::services::azureus_router;
namespace i2p=biglybtc::services::i2p;
namespace {
d::Contact contact(unsigned n){
  d::Contact out;out.hash.fill(static_cast<i2p::Byte>(n+1));out.port=static_cast<std::uint16_t>(10000+2*n);
  const auto id=d::makeNodeId(out.hash,out.port,i2p::Bytes(14,static_cast<i2p::Byte>(n+3)));
  if(!id){qFatal("Cannot construct a secure deterministic routing fixture");}out.id=*id;return out;
}
ar::RouterBestContacts best(std::span<const d::Contact> contacts){
  ar::RouterBestContacts out;out.count=contacts.size();
  for(std::size_t i=0;i<contacts.size();++i){out.values[i].contact.id=contacts[i].id;out.values[i].timeAlive=100+static_cast<std::int64_t>(i);}
  return out;
}
}
class RoutingPersistenceTests final:public QObject {
  Q_OBJECT
private slots:
  void originalFourPassPriorityAndFreshRouterTime(){
    std::vector<d::Contact> nodes;for(unsigned i=0;i<6;++i){nodes.push_back(contact(i));}auto state=best(nodes);
    state.values[2].contact.hasBeenAlive=state.values[3].contact.hasBeenAlive=true;
    state.values[4].contact.failures=state.values[5].contact.failures=1;
    std::vector<d::SavedContact> imported;
    for(unsigned i:{1U,3U,4U}){imported.push_back({nodes[i],std::numeric_limits<std::int64_t>::min(),0,0});}
    const auto result=d::selectSavedRoutingContacts(state,nodes,imported);QVERIFY(result);QCOMPARE(result->size(),std::size_t{6});
    const std::array<unsigned,6> expected{3,2,1,0,4,5};
    for(std::size_t i=0;i<expected.size();++i){QCOMPARE((*result)[i].contact.id,nodes[expected[i]].id);QCOMPARE((*result)[i].timeAlive,std::int64_t{100}+expected[i]);QCOMPARE((*result)[i].serializationVersion,std::uint8_t{1});}
    // Original imported_state does not re-establish Alive or old timeAlive.
    QVERIFY(!state.values[1].contact.hasBeenAlive);QCOMPARE(imported.front().timeAlive,std::numeric_limits<std::int64_t>::min());
  }
  void invalidRowsDoNotConsumeTheThirtyTwoSlotBudget(){
    std::vector<d::Contact> nodes;for(unsigned i=0;i<40;++i){nodes.push_back(contact(i));}auto state=best(nodes);
    nodes[0].port=65535;QVERIFY(!d::validContact(nodes[0]));
    const auto result=d::selectSavedRoutingContacts(state,nodes,{});QVERIFY(result);QCOMPARE(result->size(),d::MaximumSavedContacts);
    QCOMPARE(result->front().contact.id,nodes[1].id);QCOMPARE(result->back().contact.id,nodes[32].id);
    const auto wire=d::encodeSavedContacts(*result);QVERIFY(wire);const auto decoded=d::decodeSavedContacts(*wire);QVERIFY(decoded);QCOMPARE(decoded->size(),d::MaximumSavedContacts);
  }
  void localVersionAndLastImportedMetadataRemainSeparateFromLiveness(){
    const std::vector<d::Contact> nodes{contact(0),contact(1),contact(2)};auto state=best(nodes);state.values[0].contact.local=true;
    std::vector<d::SavedContact> imported{{nodes[1],123,1,1},{nodes[1],456,1,-1}};
    const auto result=d::selectSavedRoutingContacts(state,nodes,imported);QVERIFY(result);QCOMPARE(result->size(),std::size_t{3});
    QCOMPARE(result->front().contact.id,nodes[1].id);QCOMPARE(result->front().protocolVersion,std::int8_t{-1});
    QCOMPARE((*result)[1].contact.id,nodes[0].id);QCOMPARE((*result)[1].protocolVersion,std::int8_t{4});
    QCOMPARE(result->back().contact.id,nodes[2].id);QCOMPARE(result->back().protocolVersion,std::int8_t{0});
    QCOMPARE(result->front().timeAlive,std::int64_t{101});QVERIFY(!state.values[1].contact.alive());
  }
  void observedVersionsDoNotCreateImportedPriorityOrLiveness(){
    const std::vector<d::Contact> nodes{contact(0),contact(1),contact(2),contact(3)};auto state=best(nodes);state.values[0].contact.local=true;
    const std::vector<d::SavedContact> imported{{nodes[2],-999999,1,1}};
    std::vector<d::RoutingProtocolObservation> versions{{nodes[1].id,nodes[1].hash,nodes[1].port,4},{nodes[2].id,nodes[2].hash,nodes[2].port,-1},{nodes[0].id,nodes[0].hash,nodes[0].port,0}};
    auto result=d::selectSavedRoutingContacts(state,nodes,imported,versions);QVERIFY(result);QCOMPARE(result->size(),std::size_t{4});
    // Imported unknown stays ahead of all new unknowns. An observed version
    // gives node1 no imported membership and does not establish Alive/time.
    QCOMPARE(result->front().contact.id,nodes[2].id);QCOMPARE(result->front().protocolVersion,std::int8_t{-1});
    QCOMPARE((*result)[1].contact.id,nodes[0].id);QCOMPARE((*result)[1].protocolVersion,std::int8_t{4});
    QCOMPARE((*result)[2].contact.id,nodes[1].id);QCOMPARE((*result)[2].protocolVersion,std::int8_t{4});
    QCOMPARE(result->back().protocolVersion,std::int8_t{0});QVERIFY(!state.values[1].contact.alive());QCOMPARE((*result)[2].timeAlive,std::int64_t{101});
    versions[1].hash[7]^=1;result=d::selectSavedRoutingContacts(state,nodes,imported,versions);QVERIFY(result);QCOMPARE(result->front().protocolVersion,std::int8_t{1});
    versions.push_back(versions.front());QVERIFY(!d::selectSavedRoutingContacts(state,nodes,imported,versions));
    const std::vector<d::RoutingProtocolObservation> tooMany(ar::MaximumRouterContacts+1);QVERIFY(!d::selectSavedRoutingContacts(state,nodes,imported,tooMany));
  }
  void boundedOrAmbiguousInputsFailBeforeExport(){
    const std::vector<d::Contact> nodes{contact(0)};auto state=best(nodes);
    auto duplicate=nodes;duplicate.push_back(nodes.front());QVERIFY(!d::selectSavedRoutingContacts(state,duplicate,{}));
    state.count=ar::MaximumRouterContacts+1;QVERIFY(!d::selectSavedRoutingContacts(state,nodes,{}));state.count=1;
    const std::vector<d::SavedContact> tooMany(d::MaximumSavedContacts+1);QVERIFY(!d::selectSavedRoutingContacts(state,nodes,tooMany));
    ar::RouterBestContacts empty;QVERIFY(d::selectSavedRoutingContacts(empty,{},{}));
  }
};
QTEST_GUILESS_MAIN(RoutingPersistenceTests)
#include "i2p_routing_persistence_tests.moc"

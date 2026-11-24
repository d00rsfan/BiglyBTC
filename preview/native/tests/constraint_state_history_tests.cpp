// SPDX-License-Identifier: GPL-2.0-or-later
#include "managed_tag_state.hpp"
#include <QtTest>
using namespace biglybtc;
using History=engine::detail::ManagedTagState;
using M=core::tags::runtime::Membership;
using P=engine::ManagedPhase;
namespace {
engine::ManagedDownloadStatus status(){
  engine::ManagedDownloadStatus value;value.phase=P::Seeding;value.checked=true;value.selectedComplete=true;
  return value;
}
}
class ConstraintStateHistoryTests final:public QObject {
 Q_OBJECT
private slots:
 void transientRetainsOnlyObservedMain(){
  History observed;auto value=status();auto now=History::Clock::time_point{};
  observed.observe(value,now);QCOMPARE(observed.members()[2],M::Present);
  value.phase=P::Checking;value.checked=false;observed.observe(value,now+std::chrono::seconds(1));
  QCOMPARE(observed.members()[2],M::Present);QCOMPARE(observed.members()[10],M::Unknown);
  value.phase=P::Allocating;observed.observe(value,now+std::chrono::seconds(2));
  QCOMPARE(observed.members()[2],M::Present);
  History late;late.observe(value,now);QCOMPARE(late.members()[2],M::Unknown);
  value.checked=true;late.observe(value,now+std::chrono::seconds(1));
  QCOMPARE(late.members()[10],M::Present);QCOMPARE(late.members()[11],M::Absent);
  for(unsigned id=12;id<15;++id){QCOMPARE(observed.members()[id],M::Unknown);}
 }
 void firstQuietSampleCannotInventAnEarlierPrefix(){
  History history;auto value=status();const auto now=History::Clock::time_point{};
  history.observe(value,now);QCOMPARE(history.members()[7],M::Unknown);
  history.observe(value,now+std::chrono::seconds(9));QCOMPARE(history.members()[7],M::Unknown);
  history.observe(value,now+std::chrono::seconds(10));QCOMPARE(history.members()[7],M::Unknown);
  history.observe(value,now+std::chrono::seconds(20));QCOMPARE(history.members()[7],M::Absent);QCOMPARE(history.members()[9],M::Present);
  value.sent=1;history.observe(value,now+std::chrono::seconds(21));QCOMPARE(history.members()[7],M::Absent);
  history.observe(value,now+std::chrono::seconds(30));QCOMPARE(history.members()[7],M::Present);
  history.observe(value,now+std::chrono::seconds(40));QCOMPARE(history.members()[7],M::Absent);
 }
 void rateAndPrefixChangesFollowOriginalActivityPolicy(){
  History history;auto value=status();const auto now=History::Clock::time_point{};
  value.uploadRate=17;history.observe(value,now);history.observe(value,now+std::chrono::seconds(10));
  QCOMPARE(history.members()[7],M::Present);QCOMPARE(history.members()[9],M::Absent);
  value.uploadRate=0;value.sent=71;history.observe(value,now+std::chrono::seconds(20));
  QCOMPARE(history.members()[7],M::Present);
  value.phase=P::Stopped;history.observe(value,now+std::chrono::seconds(21));
  QCOMPARE(history.members()[7],M::Absent);QCOMPARE(history.members()[9],M::Present);
  value.phase=P::Seeding;history.observe(value,now+std::chrono::seconds(22));QCOMPARE(history.members()[9],M::Present);
 }
 void unknownQueuedCompletionInvalidatesHistoryUntilObserved(){
  History history;auto value=status();const auto now=History::Clock::time_point{};
  history.observe(value,now);value.phase=P::Queued;value.checked=false;
  history.observe(value,now+std::chrono::seconds(1));QCOMPARE(history.members()[2],M::Unknown);QCOMPARE(history.members()[7],M::Unknown);
  value.checked=true;history.observe(value,now+std::chrono::seconds(2));QCOMPARE(history.members()[4],M::Present);
 }
 void pauseAndCompletionUseCurrentObservedInputs(){
  History history;auto value=status();const auto now=History::Clock::time_point{};
  value.phase=P::Paused;value.manuallyPaused=true;history.observe(value,now);
  QCOMPARE(history.members()[5],M::Present);QCOMPARE(history.members()[8],M::Present);
  value.phase=P::Stopped;value.manuallyPaused=false;history.observe(value,now+std::chrono::seconds(1));QCOMPARE(history.members()[8],M::Absent);
  value.selectedComplete=false;history.observe(value,now+std::chrono::seconds(2));QCOMPARE(history.members()[10],M::Absent);QCOMPARE(history.members()[11],M::Present);
 }
 void missingPayloadObservationAndBackwardClockDropHistory(){
  History history;auto value=status();const auto now=History::Clock::time_point{};
  history.observe(value,now);value.sent=-1;history.observe(value,now+std::chrono::seconds(10));QCOMPARE(history.members()[7],M::Unknown);
  value.sent=0;history.observe(value,now+std::chrono::seconds(20));QCOMPARE(history.members()[7],M::Unknown);
  history.observe(value,now+std::chrono::seconds(30));QCOMPARE(history.members()[7],M::Absent);
  value.phase=P::Checking;history.observe(value,now);QCOMPARE(history.members()[2],M::Unknown);
 }
 void activeSamplesRefreshTheOriginalTimestamp() {
  using namespace std::chrono_literals;
  History history;
  auto value = status();
  const auto now = History::Clock::time_point{};
  value.uploadRate = 17;
  history.observe(value, now, 100000);
  QVERIFY(!history.secondsSinceLastActive(100000));
  history.observe(value, now + 9s, 109500);
  QVERIFY(!history.secondsSinceLastActive(109500));
  history.observe(value, now + 10s, 110000);
  QCOMPARE(history.secondsSinceLastActive(114999), std::optional<std::int64_t>{4});
  // Active was already present, so this exercises the independent timestamp update.
  history.observe(value, now + 20s, 120000);
  QCOMPARE(history.members()[7], M::Present);
  QCOMPARE(history.secondsSinceLastActive(123000), std::optional<std::int64_t>{3});
  QCOMPARE(history.secondsSinceLastActive(117999), std::optional<std::int64_t>{-2});
  value.uploadRate = 0;
  history.observe(value, now + 30s, 130000);
  QCOMPARE(history.members()[7], M::Absent);
  QCOMPARE(history.secondsSinceLastActive(130000), std::optional<std::int64_t>{10});
  value.sent = -1;
  history.observe(value, now + 40s, 140000);
  QVERIFY(!history.secondsSinceLastActive(140000));
 }
};
QTEST_GUILESS_MAIN(ConstraintStateHistoryTests)
#include "constraint_state_history_tests.moc"

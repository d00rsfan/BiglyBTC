// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_profile_io.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <QScopeGuard>
#include <thread>
#include <future>
using namespace biglybtc;using namespace std::chrono_literals;
class ChatProfileIoTests final:public QObject {
 Q_OBJECT
private slots:
 void identityAndOptionsCompleteAfterReleasingTheirSourceGate(){
  QTemporaryDir root;QVERIFY(root.isValid());auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);QVERIFY(storage::ConfigFile::createMissing(*lease,"biglybt.config","de"));auto retained=lease->retain();QVERIFY(retained);
  app::ChatProfileIo io(*retained);auto load=io.submit(app::ChatProfileLoad{"Generated shared room",true},1,std::chrono::steady_clock::now()+3s);QVERIFY(load);
  std::vector<app::ChatProfileCompletion> result;
  QTRY_VERIFY_WITH_TIMEOUT(([&]{if(result.empty()){result=io.advance();}return !result.empty();})(),3000);QCOMPARE(result.front().id,*load);QVERIFY(result.front().result);QVERIFY(result.front().result->identity);
  const auto publicKey=std::string(result.front().result->identity->signer.publicKey());
  // Another thread can acquire the gate when a completion becomes visible.
  auto proof=std::async(std::launch::async,[&]{return bool(lease->sourceAccess());});QCOMPARE(proof.wait_for(1s),std::future_status::ready);QVERIFY(proof.get());
  result.clear();auto request=io.submit(app::ChatProfileLoad{"Generated shared room",false},1,std::chrono::steady_clock::now()+3s);QVERIFY(request);
  QTRY_VERIFY_WITH_TIMEOUT(([&]{if(result.empty()){result=io.advance();}return !result.empty();})(),3000);QVERIFY(result.front().result);QCOMPARE(std::string(result.front().result->identity->signer.publicKey()),publicKey);
  io.requestStop();io.join();QVERIFY(!io.submit(app::ChatProfileList{},1,std::chrono::steady_clock::now()+1s));
 }
 void busyProfileDoesNotBlockTimersOrStopAndCancellationDropsIdentity(){
  QTemporaryDir root;QVERIFY(root.isValid());auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);QVERIFY(storage::ConfigFile::createMissing(*lease,"biglybt.config","de"));auto retained=lease->retain();QVERIFY(retained);app::ChatProfileIo io(*retained);
  std::promise<void> locked,release;auto released=release.get_future();std::jthread holder([&]{auto gate=lease->sourceAccess();locked.set_value();released.wait();});locked.get_future().wait();
  const auto cleanup=qScopeGuard([&]{release.set_value();holder.join();io.requestStop();io.join();});
  auto id=io.submit(app::ChatProfileLoad{"Cancelled generated room",true},1,std::chrono::steady_clock::now()+3s);QVERIFY(id);unsigned ticks=0;QTimer timer;connect(&timer,&QTimer::timeout,this,[&]{++ticks;});timer.start(1);QTRY_VERIFY_WITH_TIMEOUT(ticks>=5,1000);QVERIFY(io.cancel(*id));io.requestStop();
  // The external lifetime joins only after the source holder releases. Qt
  // owner/timers never wait for that lock. No cancelled identity is delivered.
  std::vector<app::ChatProfileCompletion> rows=io.advance();for(const auto& row:rows){QVERIFY(!row.result);}
 }
};
QTEST_GUILESS_MAIN(ChatProfileIoTests)
#include "chat_profile_io_tests.moc"

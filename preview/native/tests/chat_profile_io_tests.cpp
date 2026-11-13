// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_profile_io.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <QDateTime>
#include <QScopeGuard>
#include <thread>
#include <future>
using namespace biglybtc;using namespace std::chrono_literals;
class ChatProfileIoTests final:public QObject {
 Q_OBJECT
private slots:
 void preferenceWritesRequireLiveRoomAndOriginalIdentity(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);QVERIFY(storage::ConfigFile::createMissing(*lease,"biglybt.config","de"));auto held=lease->retain();QVERIFY(held);app::ChatProfileIo io(*held);
  const auto finish=[&](app::ChatProfileRequest request){std::vector<app::ChatProfileCompletion> rows;auto id=io.submit(std::move(request),8,std::chrono::steady_clock::now()+3s);if(!id){return rows;}const auto end=std::chrono::steady_clock::now()+3s;while(rows.empty()&&std::chrono::steady_clock::now()<end){rows=io.advance();QTest::qWait(2);}return rows;};
  auto loaded=finish(app::ChatProfileLoad{"Generated preference room",true});QVERIFY(loaded.size()==1&&loaded[0].result&&loaded[0].result->identity);const auto stamp=loaded[0].result->identity->sourceStamp;app::ChatProfilePreference edit{"Generated preference room",{},app::ChatPreferenceField::Ratings,false,true,stamp,[]{return false;}};auto denied=finish(edit);QVERIFY(denied.size()==1&&!denied[0].result);edit.live=[]{return true;};edit.stamp[0]^=1;denied=finish(edit);QVERIFY(denied.size()==1&&!denied[0].result);edit.stamp=stamp;auto saved=finish(edit);QVERIFY(saved.size()==1&&saved[0].result);auto read=finish(app::ChatProfilePreferences{{},{}});QVERIFY(read.size()==1&&read[0].result&&read[0].result->preferences);QVERIFY(read[0].result->preferences->display.hideRatings);denied=finish(edit);QVERIFY(denied.size()==1&&!denied[0].result);app::ChatProfileReadMarker marker{"Generated preference room",{},"1700000123/1700000000/IFBEG",stamp,[]{return false;}};denied=finish(marker);QVERIFY(denied.size()==1&&!denied[0].result);marker.live=[]{return true;};marker.stamp[0]^=1;denied=finish(marker);QVERIFY(denied.size()==1&&!denied[0].result);marker.stamp=stamp;saved=finish(marker);QVERIFY(saved.size()==1&&saved[0].result&&saved[0].result->readMarker==marker.value);denied=finish(marker);QVERIFY(denied.size()==1&&!denied[0].result);io.requestStop();io.join();
 }
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
 void historyPublicationBindsTheOriginalIdentityAndSaveChoice(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);QVERIFY(storage::ConfigFile::createMissing(*lease,"biglybt.config","de"));auto held=lease->retain();QVERIFY(held);app::ChatProfileIo io(*held);
  auto finish=[&](app::ChatProfileRequest request){std::vector<app::ChatProfileCompletion> rows;auto id=io.submit(std::move(request),1,std::chrono::steady_clock::now()+3s);if(!id){return rows;}const auto end=std::chrono::steady_clock::now()+3s;while(rows.empty()&&std::chrono::steady_clock::now()<end){rows=io.advance();QTest::qWait(2);}return rows;};
  auto loaded=finish(app::ChatProfileLoad{"Generated history room",true});QVERIFY(loaded.size()==1&&loaded[0].result&&loaded[0].result->identity);auto identity=loaded[0].result->identity;
  auto room=services::msgsync::Room::open("Generated history room",identity->uid,std::move(identity->signer),"de");QVERIFY(room);QVERIFY(room->send("Generated retained message","Generated",QDateTime::currentMSecsSinceEpoch()));auto encoded=room->exportHistory(QDateTime::currentMSecsSinceEpoch());QVERIFY(encoded);
  auto options=identity->settings;options.saveHistory=true;auto initial=finish(app::ChatProfileOptions{options});QVERIFY(initial.size()==1&&initial[0].result);
  auto wrong=identity->sourceStamp;wrong[0]^=1;auto refused=finish(app::ChatProfileHistory{"Generated history room",*encoded,wrong});QVERIFY(refused.size()==1&&!refused[0].result);
  auto saved=finish(app::ChatProfileHistory{"Generated history room",*encoded,identity->sourceStamp});QVERIFY(saved.size()==1&&saved[0].result);
  options.saveHistory=false;auto off=finish(app::ChatProfileOptions{options});QVERIFY(off.size()==1&&off[0].result);
  auto disabled=finish(app::ChatProfileLoad{"Generated history room",false});QVERIFY(disabled.size()==1&&disabled[0].result&&!disabled[0].result->history);
  options.saveHistory=true;auto enabled=finish(app::ChatProfileOptions{options});QVERIFY(enabled.size()==1&&enabled[0].result&&enabled[0].result->history);
  io.requestStop();io.join();
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

#include "../ui/saved_speed_manager_view.hpp"
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>
#include <functional>
#include <limits>
#include <stdexcept>
namespace {
using namespace biglybtc;
template<class T>T& child(QObject& parent,const char* id){auto* value=parent.findChild<T*>(QString::fromLatin1(id));if(!value){throw std::runtime_error("Missing saved estimate control");}return *value;}
auto packet(quint64 sequence=1,quint64 session=7,std::string speed="i123456e"){
 const auto settings=core::readSavedSpeedSettings("d11:ASN Detailsld2:as5:123453:bgp12:192.0.2.0/244:name10:Example ASee25:Auto Upload Speed Versioni3ee");
 const auto history=core::readSavedSpeedHistory("d7:downcapd1:hi0e1:m3:0.01:si0e1:t4:-0.11:wi0ee5:pingsle5:upcapd1:hi4e1:m5:0.6251:s"+speed+"1:t3:0.91:wi1700000000000eee");
 if(!settings||!history){throw std::runtime_error("Invalid exact-format generated SpeedManager fixture");}
 auto value=std::make_shared<ui::SavedSpeedManagerPacket>();value->session=session;value->sequence=sequence;value->state=ui::SavedSpeedManagerState::Ready;value->livePolicy=ui::AutoSpeedPolicy::Inactive;
 value->settings=std::make_shared<const core::SavedSpeedSettings>(*settings);value->history=std::make_shared<const core::SavedSpeedHistory>(*history);value->source=ui::SavedSpeedManagerSource::Primary;return value;
}
void show(QWidget& view){view.resize(1150,800);view.show();QCoreApplication::processEvents();}
class Hook final:public QObject{public:bool armed=true;std::function<void()> callback;bool eventFilter(QObject*,QEvent* event)override{if(armed&&event->type()==QEvent::ToolTipChange){armed=false;callback();return true;}return false;}};
}
class SavedSpeedManagerUiTests final:public QObject{
 Q_OBJECT
private slots:
 void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
 void actualSavedTypesRemainSeparateFromInactiveLivePolicy(){
  ui::SavedSpeedManagerView view;view.beginSession(7);show(view);QVERIFY(view.setPresentation(packet()));
  QVERIFY(child<QLabel>(view,"saved-speed-upload").text().contains("123456 bytes/s (saved); Measured"));QVERIFY(child<QLabel>(view,"saved-speed-download").text().contains("Unlimited (saved); Unknown estimate"));
  QVERIFY(child<QLabel>(view,"saved-autospeed-policy").text().contains("inactive by native host policy"));QVERIFY(child<QLabel>(view,"saved-speed-asn").text().contains("12345; name Example AS"));
  QVERIFY(child<QLabel>(view,"saved-speed-algorithm").text().contains("option 2"));QVERIFY(child<QLabel>(view,"saved-speed-algorithm").toolTip().contains("Long: 3"));
  QVERIFY(child<QLabel>(view,"saved-speed-upload-details").text().contains("0.625; hits 4; recorded at 1700000000000 epoch ms"));QVERIFY(child<QLabel>(view,"saved-speed-records").text().contains("0 (not live pings or routes)"));
  QVERIFY(view.findChildren<QTimer*>().isEmpty());
 }
 void low32NegativeAndRecoveryProvenanceStayHonest(){
  ui::SavedSpeedManagerView view;view.beginSession(7);show(view);auto value=packet(1,7,"i4294967297e");value->source=ui::SavedSpeedManagerSource::Backup;QVERIFY(view.setPresentation(value));
  QVERIFY(child<QLabel>(view,"saved-speed-upload").text().contains("1 bytes/s"));QVERIFY(child<QLabel>(view,"saved-speed-upload").toolTip().contains("4294967297"));QVERIFY(child<QLabel>(view,"saved-speed-status").text().contains("backup"));
  auto negative=packet(2,7,"i2147483648e");QVERIFY(view.setPresentation(negative));QCOMPARE(child<QLabel>(view,"saved-speed-upload").text(),QStringLiteral("Upload capacity: Unavailable"));
 }
 void missingAndFailedNeverInventDefaultCapacityOrRunningAutospeed(){
  ui::SavedSpeedManagerView view;view.beginSession(7);show(view);auto absent=packet();absent->state=ui::SavedSpeedManagerState::Missing;absent->history.reset();absent->source.reset();absent->livePolicy=ui::AutoSpeedPolicy::Unavailable;QVERIFY(view.setPresentation(absent));
  QVERIFY(child<QLabel>(view,"saved-speed-status").text().contains("No saved mapper history"));QCOMPARE(child<QLabel>(view,"saved-speed-upload").text(),QStringLiteral("Upload capacity: Unavailable"));QVERIFY(child<QLabel>(view,"saved-autospeed-policy").text().contains("status unavailable"));
  auto failed=std::make_shared<ui::SavedSpeedManagerPacket>(*absent);failed->sequence=2;failed->state=ui::SavedSpeedManagerState::Failed;failed->failure="<b>failure</b>";QVERIFY(view.setPresentation(failed));QVERIFY(child<QLabel>(view,"saved-speed-status").text().contains(failed->failure));QCOMPARE(child<QLabel>(view,"saved-speed-status").textFormat(),Qt::PlainText);
 }
 void identityShapeAndNonfiniteProjectionAreRejected(){
  ui::SavedSpeedManagerView view;view.beginSession(7);const auto first=packet();QVERIFY(view.setPresentation(first));QVERIFY(!view.setPresentation(first));QVERIFY(!view.setPresentation(packet(2,8)));
  auto invalid=packet(2);invalid->source=static_cast<ui::SavedSpeedManagerSource>(99);QVERIFY(!view.setPresentation(invalid));invalid=packet(2);auto edited=std::make_shared<core::SavedSpeedHistory>(*invalid->history);edited->upload.metric=std::numeric_limits<float>::quiet_NaN();invalid->history=edited;QVERIFY(!view.setPresentation(invalid));
  invalid=packet(2);invalid->history.reset();QVERIFY(!view.setPresentation(invalid));invalid=packet(2);invalid->livePolicy=static_cast<ui::AutoSpeedPolicy>(99);QVERIFY(!view.setPresentation(invalid));QVERIFY(view.presentation()==first);
 }
 void lazyFirstReadAndExplicitRefreshDoNotStartAnyProvider(){
  ui::SavedSpeedManagerView view;QSignalSpy reads(&view,&ui::SavedSpeedManagerView::readRequested);view.beginSession(7);QCoreApplication::processEvents();QCOMPARE(reads.count(),0);show(view);QTRY_COMPARE(reads.count(),1);QCOMPARE(reads[0][0].toULongLong(),7ULL);
  QVERIFY(view.setPresentation(packet()));view.hide();show(view);QCoreApplication::processEvents();QCOMPARE(reads.count(),1);child<QPushButton>(view,"saved-speed-refresh").click();QTRY_COMPARE(reads.count(),2);
 }
 void hiddenMinimizedRefreshDefersAndPacketRendersOnlyOnRestore(){
  ui::SavedSpeedManagerView view;view.beginSession(7);QSignalSpy reads(&view,&ui::SavedSpeedManagerView::readRequested);show(view);QTRY_COMPARE(reads.count(),1);QVERIFY(view.setPresentation(packet()));const auto before=child<QLabel>(view,"saved-speed-upload").text();view.hide();QVERIFY(view.setPresentation(packet(2,7,"i9e")));child<QPushButton>(view,"saved-speed-refresh").click();QCoreApplication::processEvents();QCOMPARE(reads.count(),1);QCOMPARE(child<QLabel>(view,"saved-speed-upload").text(),before);show(view);QTRY_COMPARE(reads.count(),2);QTRY_VERIFY(child<QLabel>(view,"saved-speed-upload").text().contains("9 bytes/s"));
  view.showMinimized();QTRY_VERIFY(view.isMinimized());QVERIFY(view.setPresentation(packet(3,7,"i8e")));QCoreApplication::processEvents();QVERIFY(child<QLabel>(view,"saved-speed-upload").text().contains("9 bytes/s"));view.showNormal();QTRY_VERIFY(child<QLabel>(view,"saved-speed-upload").text().contains("8 bytes/s"));
 }
 void newSessionClearsFactsAndOldDeliveriesCannotRepopulate(){
  ui::SavedSpeedManagerView view;view.beginSession(7);show(view);QVERIFY(view.setPresentation(packet()));QVERIFY(view.beginSession(8));QVERIFY(!view.presentation());QVERIFY(!view.setPresentation(packet(99,7)));QVERIFY(child<QLabel>(view,"saved-speed-status").text().contains("unavailable"));QVERIFY(view.setPresentation(packet(1,8)));
 }
 void directReadCallbackCanDeleteOwningWidget(){
  QPointer<ui::SavedSpeedManagerView> view=new ui::SavedSpeedManagerView;connect(view,&ui::SavedSpeedManagerView::readRequested,view,[&](quint64){delete view.data();});view->beginSession(7);show(*view);QTRY_VERIFY(view.isNull());
 }
 void tooltipReentryAndDeletionCannotWriteOldTrailingFacts(){
  ui::SavedSpeedManagerView view;view.beginSession(7);show(view);Hook rebind;rebind.callback=[&]{view.beginSession(8);view.setPresentation(packet(1,8,"i7e"));};child<QLabel>(view,"saved-speed-upload").installEventFilter(&rebind);QVERIFY(view.setPresentation(packet()));QVERIFY(!rebind.armed);QVERIFY(view.presentation()->session==8);QVERIFY(child<QLabel>(view,"saved-speed-upload").text().contains("7 bytes/s"));
  QPointer<ui::SavedSpeedManagerView> victim=new ui::SavedSpeedManagerView;victim->beginSession(7);show(*victim);child<QLabel>(*victim,"saved-speed-upload").setToolTip({});Hook remove;remove.callback=[&]{delete victim.data();};child<QLabel>(*victim,"saved-speed-upload").installEventFilter(&remove);victim->setPresentation(packet());QVERIFY(!victim);QVERIFY(!remove.armed);
 }
};
QTEST_MAIN(SavedSpeedManagerUiTests)
#include "saved_speed_manager_ui_tests.moc"

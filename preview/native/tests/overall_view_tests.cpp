#include "overall_view.hpp"
#include "overall_presentation.hpp"
#include "library_window.hpp"
#include "statistics_page.hpp"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>

#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc;
template<class T> T& child(QObject& owner, const char* id) {
  auto* found = owner.findChild<T*>(QString::fromLatin1(id));
  if (!found) { throw std::runtime_error("Missing Overall test control"); }
  return *found;
}
QLabel& value(QObject& owner, int index) {
  auto* found = owner.findChild<QLabel*>(QStringLiteral("overall-live-value-%1").arg(index));
  if (!found) { throw std::runtime_error("Missing Overall value"); }
  return *found;
}
std::shared_ptr<ui::OverallPacket> packet(quint64 sequence = 1, quint64 session = 7, qint64 downloaded = 101) {
  auto result = std::make_shared<ui::OverallPacket>();
  result->session = session; result->sequence = sequence; result->available = true; result->writable = true;
  result->values = {downloaded, downloaded + 10, 600, 0, 1, 2, 3, 19, 23, 31, 37, 41, 43};
  result->saved = result->values;
  return result;
}
void show(QWidget& widget) { widget.resize(1200, 1000); widget.show(); QCoreApplication::processEvents(); }
void syntheticSavedOwner(ui::LibraryWindow& window) {
  // Explicit synthetic saved-read responder only: no files, worker or engine.
  QObject::connect(&window, &ui::LibraryWindow::statisticsRequested, &window,
      [&window](quint64 session, quint64 request) {
        window.setBusy(true);
        QTimer::singleShot(0, &window, [&window, session, request] {
          auto result = std::make_shared<ui::StatisticsPacket>();
          result->session = session; result->request = request; result->state = ui::StatisticsReadState::Missing;
          window.setStatistics(result); window.setBusy(false);
        });
      });
}
bool bind(ui::LibraryWindow& window, quint64 session = 7) {
  QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested);
  window.beginColumnLayoutSession(session);
  return QTest::qWaitFor([&] { return requests.count() == 1; }, 3000);
}
ui::StatisticsPage& open(ui::LibraryWindow& window) {
  child<QAction>(window, "classic-statistics").trigger();
  if (!window.statisticsPage()) { throw std::runtime_error("Statistics page did not open"); }
  window.statisticsPage()->tabs().setCurrentIndex(1);
  child<QTabWidget>(*window.statisticsPage(), "statistics-transfer-tabs").setCurrentIndex(0);
  return *window.statisticsPage();
}
struct AccessArm { QObject* object; std::function<void()> callback; bool fired = false; };
thread_local AccessArm* activeArm = nullptr;
void accessible(QAccessibleEvent* event) {
  auto* arm = activeArm;
  if (arm && event->object() == arm->object && event->type() == QAccessible::NameChanged) {
    activeArm = nullptr; arm->fired = true; arm->callback();
  }
}
class AccessScope final {
public:
  AccessArm arm;
  AccessScope(QObject* object, std::function<void()> callback)
      : arm{object, std::move(callback)}, wasActive_(QAccessible::isActive()),
        previous_(QAccessible::installUpdateHandler(accessible)) {
    QAccessible::setActive(true); (void)QAccessible::queryAccessibleInterface(object); activeArm = &arm;
  }
  ~AccessScope() { activeArm = nullptr; QAccessible::installUpdateHandler(previous_); QAccessible::setActive(wasActive_); }
private:
  bool wasActive_;
  QAccessible::UpdateHandler previous_;
};
} // namespace

class OverallViewTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
    qRegisterMetaType<ui::OverallPresentation>();
  }
  void unstartedIsUnknownPlainTextAndInert() {
    ui::OverallView view; view.beginSession(7); show(view);
    for (int i = 0; i < 8; ++i) { QCOMPARE(value(view, i).text(), QStringLiteral("Unavailable")); }
    QVERIFY(!child<QPushButton>(view, "statistics-set-mark").isEnabled());
    QVERIFY(!child<QPushButton>(view, "statistics-clear-mark").isEnabled());
    QVERIFY(view.findChildren<QTimer*>().isEmpty());
    for (auto* label : view.findChildren<QLabel*>()) { QCOMPARE(label->textFormat(), Qt::PlainText); }
    QSignalSpy marks(&view, &ui::OverallView::markRequested);
    child<QPushButton>(view, "statistics-set-mark").clicked(false);
    QCOMPARE(marks.count(), 0);
  }
  void exactSignedFieldsClampFutureMarksAndPreserveLiteralFailures() {
    ui::OverallView view; view.beginSession(7); show(view);
    auto data = packet();
    data->values[0] = std::numeric_limits<qint64>::max(); data->values[4] = -1;
    data->values[1] = -7; data->values[5] = 20;
    data->values[2] = 2; data->values[6] = 9;
    view.setPresentation(data);
    QCOMPARE(value(view, 0).text(), QStringLiteral("9223372036854775807"));
    QCOMPARE(value(view, 1).text(), QStringLiteral("-7"));
    QCOMPARE(value(view, 3).text(), QStringLiteral("No mark"));
    QCOMPARE(value(view, 4).text(), QStringLiteral("-9223372036854775808"));
    QCOMPARE(value(view, 5).text(), QStringLiteral("0"));
    QCOMPARE(value(view, 6).text(), QStringLiteral("0"));
    QCOMPARE(value(view, 7).text(), QStringLiteral("19 / 23 bytes (separate)"));
    auto failed = packet(2); failed->writable = false; failed->failure = QStringLiteral("<b>disk & rename</b>");
    failed->historyFailure = QStringLiteral("<i>history</i>"); view.setPresentation(failed);
    const auto& status = child<QLabel>(view, "overall-save-status");
    QVERIFY(status.text().contains(failed->failure)); QVERIFY(status.text().contains(failed->historyFailure));
    QCOMPARE(status.textFormat(), Qt::PlainText);
  }
  void staleSessionSequenceAndClosedPacketsCannotReplaceAcceptedValues() {
    ui::OverallView view; view.beginSession(7); show(view); view.setPresentation(packet(3, 7, 303));
    view.setPresentation(packet(4, 8, 808)); view.setPresentation(packet(3, 7, 999));
    view.setPresentation(packet(0)); view.setPresentation({});
    QCOMPARE(value(view, 0).text(), QStringLiteral("303"));
    auto closed = packet(4, 7, 404); closed->closed = true; closed->finalCounters = true;
    view.setPresentation(closed); view.setPresentation(packet(5, 7, 505));
    QCOMPARE(value(view, 0).text(), QStringLiteral("404"));
    QVERIFY(child<QLabel>(view, "overall-live-status").text().contains(QStringLiteral("settled")));
    view.beginSession(7); QCOMPARE(value(view, 0).text(), QStringLiteral("404"));
    view.beginSession(8); QCOMPARE(value(view, 0).text(), QStringLiteral("Unavailable"));
    view.setPresentation(packet(1, 8, 808)); QCOMPARE(value(view, 0).text(), QStringLiteral("808"));
  }
  void busyBlockedAndClosedStatesGateBothClicksAndDirectSignals() {
    ui::OverallView view; view.beginSession(7); show(view); view.setPresentation(packet());
    auto& mark = child<QPushButton>(view, "statistics-set-mark");
    auto& clear = child<QPushButton>(view, "statistics-clear-mark");
    QSignalSpy requests(&view, &ui::OverallView::markRequested);
    QVERIFY(mark.isEnabled()); QVERIFY(clear.isEnabled());
    mark.click(); clear.click(); QCOMPARE(requests.count(), 2);
    QCOMPARE(requests.at(0).at(0).toBool(), false); QCOMPARE(requests.at(1).at(0).toBool(), true);
    view.setCommandBusy(true); QVERIFY(!mark.isEnabled()); QVERIFY(!clear.isEnabled());
    mark.clicked(false); clear.clicked(false); QCOMPARE(requests.count(), 2);
    view.setCommandBusy(false); QVERIFY(mark.isEnabled());
    auto blocked = packet(2); blocked->writable = false; blocked->failure = QStringLiteral("Saving blocked");
    view.setPresentation(blocked); QVERIFY(!mark.isEnabled());
    mark.clicked(false); clear.clicked(false); QCOMPARE(requests.count(), 2);
    auto closed = packet(3); closed->closed = true; view.setPresentation(closed);
    mark.clicked(false); clear.clicked(false); QCOMPARE(requests.count(), 2);
  }
  void hiddenAndMinimizedViewsCacheButDeferRendering() {
    ui::OverallView view; view.beginSession(7); show(view); view.setPresentation(packet(1, 7, 101));
    view.hide(); view.setPresentation(packet(2, 7, 202));
    QCOMPARE(value(view, 0).text(), QStringLiteral("101"));
    view.show(); QTRY_COMPARE(value(view, 0).text(), QStringLiteral("202"));
    view.showMinimized(); QTRY_VERIFY(view.isMinimized());
    view.setPresentation(packet(3, 7, 303)); QCoreApplication::processEvents();
    QCOMPARE(value(view, 0).text(), QStringLiteral("202"));
    view.showNormal(); QTRY_COMPARE(value(view, 0).text(), QStringLiteral("303"));
  }
  void renderingCallbackNewerPacketWinsAllRemainingFields() {
    ui::OverallView view; view.beginSession(7); show(view); view.setPresentation(packet(1, 7, 101));
    AccessScope replace(&value(view, 0), [&] { view.setPresentation(packet(3, 7, 303)); });
    view.setPresentation(packet(2, 7, 202));
    QVERIFY2(replace.arm.fired, "Actual QLabel NameChanged callback must be exercised");
    QCOMPARE(value(view, 0).text(), QStringLiteral("303")); QCOMPARE(value(view, 1).text(), QStringLiteral("313"));
    QCOMPARE(value(view, 4).text(), QStringLiteral("302"));
  }
  void renderingCallbackSessionRebindCannotResumeOldFacts() {
    ui::OverallView view; view.beginSession(7); show(view); view.setPresentation(packet(1, 7, 101));
    AccessScope rebind(&value(view, 0), [&] { view.beginSession(8); view.setPresentation(packet(1, 8, 808)); });
    view.setPresentation(packet(2, 7, 202));
    QVERIFY2(rebind.arm.fired, "Actual QLabel NameChanged callback must rebind session");
    QCOMPARE(value(view, 0).text(), QStringLiteral("808")); QCOMPARE(value(view, 1).text(), QStringLiteral("818"));
  }
  void renderingCallbackCanDeleteTheView() {
    auto view = std::make_unique<ui::OverallView>(); view->beginSession(7); show(*view); view->setPresentation(packet());
    QPointer<ui::OverallView> guard(view.get());
    AccessScope remove(&value(*view, 0), [&] { view.reset(); });
    view->setPresentation(packet(2, 7, 202));
    QVERIFY2(remove.arm.fired, "Actual QLabel NameChanged callback must delete view"); QVERIFY(guard.isNull());
  }
  void windowRetainsImmutableClosedPacketAcrossPageRecreation() {
    ui::LibraryWindow window; syntheticSavedOwner(window); QVERIFY(bind(window)); show(window);
    auto live = packet(1, 7, 101); auto terminal = packet(2, 7, 202);
    terminal->closed = true; terminal->writable = false; terminal->finalCounters = true;
    std::weak_ptr<const ui::OverallPacket> retained = terminal;
    window.setOverall(live); window.setOverall(terminal); terminal.reset(); live.reset();
    auto& page = open(window); QTRY_VERIFY(!page.loading());
    QCOMPARE(value(page, 0).text(), QStringLiteral("202"));
    QPointer<ui::StatisticsPage> old(&page); child<QAction>(window, "classic-close-tab").trigger();
    QTRY_VERIFY(old.isNull()); QVERIFY(!retained.expired());
    window.setOverall(packet(3, 7, 999));
    auto& reopened = open(window); QTRY_VERIFY(!reopened.loading());
    QCOMPARE(value(reopened, 0).text(), QStringLiteral("202"));
    QVERIFY(!child<QPushButton>(reopened, "statistics-set-mark").isEnabled());
    QCOMPARE(retained.lock()->values[0], std::int64_t{202});
  }
  void windowMarkSignalsBindCurrentPageAndRejectDeferredClosedSender() {
    ui::LibraryWindow window; syntheticSavedOwner(window); QVERIFY(bind(window)); show(window);
    auto& page = open(window); QTRY_VERIFY(!page.loading()); window.setOverall(packet());
    QSignalSpy marks(&window, &ui::LibraryWindow::markRequested);
    child<QPushButton>(page, "statistics-set-mark").click();
    QCOMPARE(marks.count(), 1); QCOMPARE(marks.at(0).at(0).toULongLong(), quint64{7}); QVERIFY(!marks.at(0).at(1).toBool());
    window.setBusy(true); page.markRequested(true); QCOMPARE(marks.count(), 1); window.setBusy(false);
    QPointer<ui::StatisticsPage> old(&page); child<QAction>(window, "classic-close-tab").trigger();
    QVERIFY(!window.statisticsPage()); QVERIFY(old); old->markRequested(false);
    QCOMPARE(marks.count(), 1); // Before DeferredDelete: this is still a real connected QObject.
    QTRY_VERIFY(old.isNull());
    auto& reopened = open(window); QTRY_VERIFY(!reopened.loading());
    child<QPushButton>(reopened, "statistics-clear-mark").click();
    QCOMPARE(marks.count(), 2); QVERIFY(marks.last().at(1).toBool());
    window.close(); reopened.markRequested(false); QCOMPARE(marks.count(), 2);
  }
  void advancingProfileDropsCachedPacketBeforeNewCallbacks() {
    ui::LibraryWindow window; syntheticSavedOwner(window); QVERIFY(bind(window)); show(window);
    auto data = packet(); std::weak_ptr<const ui::OverallPacket> oldPacket = data;
    window.setOverall(data); data.reset(); auto& page = open(window); QTRY_VERIFY(!page.loading());
    QCOMPARE(value(page, 0).text(), QStringLiteral("101"));
    QVERIFY(bind(window, 8)); QTRY_VERIFY(!page.loading());
    QCOMPARE(value(page, 0).text(), QStringLiteral("Unavailable")); QVERIFY(oldPacket.expired());
    window.setOverall(packet(99, 7, 707)); QCOMPARE(value(page, 0).text(), QStringLiteral("Unavailable"));
    window.setOverall(packet(1, 8, 808)); QCOMPARE(value(page, 0).text(), QStringLiteral("808"));
    QSignalSpy marks(&window, &ui::LibraryWindow::markRequested);
    child<QPushButton>(page, "statistics-set-mark").click();
    QCOMPARE(marks.count(), 1); QCOMPARE(marks.first().at(0).toULongLong(), quint64{8});
  }
  void renderingCallbackCanDeleteWindowWithoutTrailingAccess() {
    auto window = std::make_unique<ui::LibraryWindow>(); syntheticSavedOwner(*window); QVERIFY(bind(*window)); show(*window);
    auto& page = open(*window); QTRY_VERIFY(!page.loading()); window->setOverall(packet());
    QPointer<ui::LibraryWindow> guard(window.get());
    AccessScope remove(&value(page, 0), [&] { window.reset(); });
    window->setOverall(packet(2, 7, 202));
    QVERIFY2(remove.arm.fired, "Actual rendering callback must delete Window"); QVERIFY(guard.isNull());
  }
};
QTEST_MAIN(OverallViewTests)
#include "overall_view_tests.moc"

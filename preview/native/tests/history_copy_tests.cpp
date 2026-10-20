#include "saved_history_plot.hpp"
#include "saved_history_traffic.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc;
using Traffic = ui::SavedHistoryTraffic;
constexpr core::HistoryDate Date{2024, 2, 29}, End{2024, 3, 2};
constexpr std::array<Traffic, 3> Kinds{Traffic::Payload, Traffic::Protocol, Traffic::Dht};
const std::array<QString, 3> Names{QStringLiteral("Payload"), QStringLiteral("Protocol"), QStringLiteral("DHT")};
const QString Sentinel = QStringLiteral("offscreen History copy sentinel <untouched>&");
std::shared_ptr<core::SavedHistoryDay> day(core::HistoryDate date = Date, bool extreme = false) {
  auto d = std::make_shared<core::SavedHistoryDay>(); d->date = date; d->observations = 2;
  d->hours[1] = core::HistoryCounters{};
  const auto low = std::numeric_limits<std::int64_t>::min(), high = std::numeric_limits<std::int64_t>::max();
  d->hours[23] = extreme ? core::HistoryCounters{high, high, low, low, high, low}
                        : core::HistoryCounters{101, 202, 303, 404, 505, 606};
  d->totals = *d->hours[23]; return d;
}
std::shared_ptr<core::SavedHistoryWeek> week(bool extreme = false) {
  auto w = std::make_shared<core::SavedHistoryWeek>(); w->endingDate = End; const auto dates = core::savedHistoryWeekDates(End);
  if (!dates) { throw std::runtime_error("Finite week invalid"); }
  for (std::size_t i = 0; i < 7; ++i) { w->days[i].date = (*dates)[i]; }
  auto zero = day(w->days[1].date); zero->hours = {}; zero->hours[0] = core::HistoryCounters{}; zero->totals = core::HistoryCounters{}; zero->observations = 1;
  w->days[1].snapshot = *zero; w->days[6].snapshot = *day(w->days[6].date, extreme); return w;
}
void frame(ui::SavedHistoryPlot& p, int mode = 0, Traffic kind = Traffic::Payload, bool extreme = false) {
  if (mode == 0) { p.setDay(Date, day(Date, extreme), kind); }
  else if (mode == 1) { p.setWeek(End, week(extreme), kind); }
  else { p.setWeekHourly(End, week(extreme), kind); }
}
void show(ui::SavedHistoryPlot& p) {
  p.resize(1100, 240); p.show(); p.activateWindow(); p.setFocus(Qt::OtherFocusReason); QCoreApplication::processEvents();
}
QAction* action(ui::SavedHistoryPlot& p) { return p.findChild<QAction*>(QStringLiteral("saved-history-copy-selection")); }
// A deleting observer must not receive QTest::keyClick's later KeyRelease.
void press(ui::SavedHistoryPlot* p, int key) {
  QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier); QCoreApplication::sendEvent(p, &event);
}
void copyShortcut(QWidget& widget) {
  const QKeySequence sequence(QKeySequence::Copy); QCOMPARE(sequence.count(), 1);
  QTest::keyClick(&widget, sequence[0].key(), sequence[0].keyboardModifiers());
}
void select(ui::SavedHistoryPlot& p, std::size_t index) {
  QTest::keyClick(&p, Qt::Key_Home); for (std::size_t i = 0; i < index; ++i) { QTest::keyClick(&p, Qt::Key_Right); }
  QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{index});
}
void hover(ui::SavedHistoryPlot& p, std::size_t index) {
  const auto area = p.plotRect(); const auto count = p.presentation()->buckets().size();
  const QPointF local{area.left() + area.width() * static_cast<qreal>(index) / static_cast<qreal>(count - 1), area.center().y()};
  QMouseEvent event(QEvent::MouseMove, local, QPointF(p.mapToGlobal(local.toPoint())), Qt::NoButton, Qt::NoButton, Qt::NoModifier); QCoreApplication::sendEvent(&p, &event);
}
QString label(int mode, int sample) {
  const std::array<std::array<QString, 3>, 3> labels{{
      {QStringLiteral("2024-02-29 00:00 UTC"), QStringLiteral("2024-02-29 01:00 UTC"), QStringLiteral("2024-02-29 23:00 UTC")},
      {QStringLiteral("2024-02-25 UTC"), QStringLiteral("2024-02-26 UTC"), QStringLiteral("2024-03-02 UTC")},
      {QStringLiteral("2024-02-25 00:00 UTC"), QStringLiteral("2024-02-26 00:00 UTC"), QStringLiteral("2024-03-02 23:00 UTC")}}};
  return labels[static_cast<std::size_t>(mode)][static_cast<std::size_t>(sample)];
}
QString expected(int mode, std::size_t kind, int sample, bool extreme = false) {
  const std::array<QString, 3> downloads{QStringLiteral("404 bytes"), QStringLiteral("303 bytes"), QStringLiteral("606 bytes")};
  const std::array<QString, 3> uploads{QStringLiteral("202 bytes"), QStringLiteral("101 bytes"), QStringLiteral("505 bytes")};
  const auto down = sample == 0 ? QStringLiteral("Unknown") : sample == 1 ? QStringLiteral("0 bytes")
      : extreme ? QStringLiteral("-9223372036854775808 bytes") : downloads[kind];
  const auto up = sample == 0 ? QStringLiteral("Unknown") : sample == 1 ? QStringLiteral("0 bytes")
      : extreme ? QStringLiteral("9223372036854775807 bytes") : uploads[kind];
  return label(mode, sample) + QLatin1Char('\n') + Names[kind] + QStringLiteral(" downloaded: ") + down
      + QLatin1Char('\n') + Names[kind] + QStringLiteral(" uploaded: ") + up;
}
std::unique_ptr<QMimeData> clone(const QMimeData* data) {
  auto result = std::make_unique<QMimeData>(); if (data) { for (const auto& format : data->formats()) { result->setData(format, data->data(format)); } } return result;
}
using MimeBytes = QList<QPair<QString, QByteArray>>;
MimeBytes mimeBytes(const QMimeData* data) {
  MimeBytes result; if (data) { for (const auto& format : data->formats()) { result.append(qMakePair(format, data->data(format))); } } return result;
}
// Only the offscreen plugin is admitted by initTestCase. Preserve original MIME
// bytes for every supported mode; no real system/compositor clipboard is tested.
class ClipboardFixture final {
public:
  QClipboard* const clipboard = QGuiApplication::clipboard();
  ClipboardFixture() {
    modes_[0] = {QClipboard::Clipboard, true, clone(clipboard->mimeData(QClipboard::Clipboard)), {}};
    modes_[1].mode = QClipboard::Selection; modes_[1].supported = clipboard->supportsSelection();
    modes_[2].mode = QClipboard::FindBuffer; modes_[2].supported = clipboard->supportsFindBuffer();
    for (std::size_t i = 1; i < modes_.size(); ++i) { auto& m = modes_[i]; if (m.supported) { m.original = clone(clipboard->mimeData(m.mode)); m.before = mimeBytes(m.original.get()); } }
    sentinel();
  }
  ~ClipboardFixture() { for (auto& m : modes_) { if (m.supported) { clipboard->setMimeData(m.original.release(), m.mode); } } }
  ClipboardFixture(const ClipboardFixture&) = delete; ClipboardFixture& operator=(const ClipboardFixture&) = delete;
  void sentinel() { clipboard->setText(Sentinel, QClipboard::Clipboard); }
  QString text() const { return clipboard->text(QClipboard::Clipboard); }
  void otherModesUnchanged() const { for (std::size_t i = 1; i < modes_.size(); ++i) { const auto& m = modes_[i]; if (m.supported) { QVERIFY(mimeBytes(clipboard->mimeData(m.mode)) == m.before); } } }
private:
  struct Mode { QClipboard::Mode mode = QClipboard::Clipboard; bool supported = false; std::unique_ptr<QMimeData> original; MimeBytes before; };
  std::array<Mode, 3> modes_;
};
void clipboardOnly(const QSignalSpy& spy) {
  for (const auto& call : spy) { QCOMPARE(call.size(), 1); QCOMPARE(qvariant_cast<QClipboard::Mode>(call[0]), QClipboard::Clipboard); }
}
class ActionHook final {
public:
  bool armed = true; std::function<void()> callback;
  ActionHook(QAction* a, std::function<void()> f) : callback(std::move(f)) {
    connection_ = QObject::connect(a, &QAction::changed, &context_, [this] { if (armed) { armed = false; callback(); } });
  }
  ~ActionHook() { QObject::disconnect(connection_); }
  ActionHook(const ActionHook&) = delete; ActionHook& operator=(const ActionHook&) = delete;
private:
  QObject context_; QMetaObject::Connection connection_;
};
class ClipboardHook final {
public:
  bool armed = true; std::function<void()> callback;
  ClipboardHook(QClipboard* c, std::function<void()> f) : callback(std::move(f)) {
    connection_ = QObject::connect(c, &QClipboard::changed, &context_, [this](QClipboard::Mode mode) { if (armed && mode == QClipboard::Clipboard) { armed = false; callback(); } });
  }
  ~ClipboardHook() { QObject::disconnect(connection_); }
  ClipboardHook(const ClipboardHook&) = delete; ClipboardHook& operator=(const ClipboardHook&) = delete;
private:
  QObject context_; QMetaObject::Connection connection_;
};
} // namespace

class HistoryCopyTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false); qRegisterMetaType<QClipboard::Mode>();
    qInfo("Offscreen clipboard modes: Selection=%d FindBuffer=%d; unsupported modes are not exercised as supported", QGuiApplication::clipboard()->supportsSelection(), QGuiApplication::clipboard()->supportsFindBuffer());
  }
  void actionContractAndDirectDisabledSignalsNeverCopyWithoutSelection() {
    ClipboardFixture c; ui::SavedHistoryPlot p; auto* a = action(p); QVERIFY(a); QCOMPARE(a->parent(), &p);
    QCOMPARE(p.findChildren<QAction*>(QStringLiteral("saved-history-copy-selection")).size(), 1);
    QCOMPARE(a->text(), QStringLiteral("Copy selected values")); QCOMPARE(a->shortcut(), QKeySequence(QKeySequence::Copy)); QCOMPARE(a->shortcutContext(), Qt::WidgetShortcut);
    QCOMPARE(p.contextMenuPolicy(), Qt::ActionsContextMenu); QVERIFY(p.actions().contains(a)); QVERIFY(!a->isEnabled()); QSignalSpy changes(c.clipboard, &QClipboard::changed);
    a->trigger(); QVERIFY(QMetaObject::invokeMethod(a, "triggered", Qt::DirectConnection, Q_ARG(bool, false))); QCOMPARE(c.text(), Sentinel);
    frame(p); show(p); QVERIFY(!p.selectedBucket()); QVERIFY(!a->isEnabled()); a->trigger(); copyShortcut(p);
    QVERIFY(QMetaObject::invokeMethod(a, "triggered", Qt::DirectConnection, Q_ARG(bool, false))); QCOMPARE(changes.count(), 0); QCOMPARE(c.text(), Sentinel); c.otherModesUnchanged();
  }
  void allNineModeTrafficActionsAndShortcutsCopyExactCurrentUtcCounterText() {
    ClipboardFixture c;
    for (int mode = 0; mode < 3; ++mode) { for (std::size_t k = 0; k < Kinds.size(); ++k) {
      c.sentinel(); ui::SavedHistoryPlot p; frame(p, mode, Kinds[k]); show(p); auto* a = action(p); QVERIFY(a); QVERIFY(!a->isEnabled());
      QTest::keyClick(&p, Qt::Key_End); QVERIFY(a->isEnabled()); const auto last = p.presentation()->buckets().size() - 1;
      hover(p, 0);
      QCOMPARE(p.toolTip(), expected(mode, k, 0));
      QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{last});
      const auto text = expected(mode, k, 2); QCOMPARE(p.presentation()->bucketToolTip(last), text); a->trigger(); QCOMPARE(c.text(), text);
      const auto* mime = c.clipboard->mimeData(QClipboard::Clipboard); QVERIFY(mime); QVERIFY(mime->hasText()); QVERIFY(!mime->hasHtml()); QVERIFY(!c.text().contains(QStringLiteral("bytes/s"))); c.otherModesUnchanged();
      c.sentinel(); QTRY_VERIFY(p.hasFocus()); copyShortcut(p); QTRY_COMPARE(c.text(), text); c.otherModesUnchanged();
    } }
  }
  void allNineModesPreserveUnknownRecordedZeroAndFullSignedExtremes() {
    ClipboardFixture c;
    for (int mode = 0; mode < 3; ++mode) { for (std::size_t k = 0; k < Kinds.size(); ++k) {
      ui::SavedHistoryPlot p; frame(p, mode, Kinds[k], true); show(p); auto* a = action(p); QVERIFY(a);
      select(p, 0); a->trigger(); QCOMPARE(c.text(), expected(mode, k, 0));
      select(p, mode == 2 ? 24 : 1); a->trigger(); QCOMPARE(c.text(), expected(mode, k, 1));
      QTest::keyClick(&p, Qt::Key_End); a->trigger(); QCOMPARE(c.text(), expected(mode, k, 2, true)); c.otherModesUnchanged();
    } }
  }
  void publicationIsPlainTextAndOnlyEmitsClipboardModeChanges() {
    ClipboardFixture c; ui::SavedHistoryPlot p; frame(p, 2, Traffic::Dht, true); show(p); QTest::keyClick(&p, Qt::Key_End);
    auto* a = action(p); QVERIFY(a); QSignalSpy changes(c.clipboard, &QClipboard::changed); a->trigger(); QVERIFY(!changes.isEmpty()); clipboardOnly(changes);
    QCOMPARE(c.text(), expected(2, 2, 2, true)); const auto* mime = c.clipboard->mimeData(QClipboard::Clipboard); QVERIFY(mime); QVERIFY(mime->hasText()); QVERIFY(!mime->hasHtml()); c.otherModesUnchanged();
  }
  void startupHoverNavigationFrameAndFocusChangesNeverCopyAutomatically() {
    ClipboardFixture c; QSignalSpy changes(c.clipboard, &QClipboard::changed); ui::SavedHistoryPlot p; frame(p, 2); show(p); auto* a = action(p); QVERIFY(a);
    hover(p, 167); QTest::keyClick(&p, Qt::Key_Home); QVERIFY(a->isEnabled()); hover(p, 167); QTest::keyClick(&p, Qt::Key_Right);
    p.clearFocus(); p.hide(); show(p); frame(p, 1, Traffic::Protocol); QVERIFY(!p.selectedBucket()); QVERIFY(!a->isEnabled());
    frame(p, 0, Traffic::Dht, true); QCoreApplication::processEvents(); QCOMPARE(c.text(), Sentinel); QCOMPARE(changes.count(), 0); c.otherModesUnchanged();
  }
  void actualQtCopyShortcutIsWidgetLocalAndUsesCurrentSelection() {
    ClipboardFixture c; QWidget host; QVBoxLayout layout(&host); ui::SavedHistoryPlot p; QPushButton other(QStringLiteral("Other focus")); layout.addWidget(&p); layout.addWidget(&other);
    frame(p, 1, Traffic::Protocol); host.resize(1000, 340); host.show(); host.activateWindow(); p.setFocus(); QTRY_VERIFY(p.hasFocus()); QTest::keyClick(&p, Qt::Key_End);
    copyShortcut(p); QTRY_COMPARE(c.text(), expected(1, 1, 2)); c.sentinel(); other.setFocus(); QTRY_VERIFY(other.hasFocus()); copyShortcut(other); QCOMPARE(c.text(), Sentinel);
    p.setFocus(); QTRY_VERIFY(p.hasFocus()); QTest::keyClick(&p, Qt::Key_Home); copyShortcut(p); QTRY_COMPARE(c.text(), expected(1, 1, 0)); c.otherModesUnchanged();
  }
  void focusLossKeepsActionButAcceptedReplacementDisablesAndInvalidSetterDoesNot() {
    ClipboardFixture c; ui::SavedHistoryPlot p; frame(p); show(p); auto* a = action(p); QVERIFY(a); QTest::keyClick(&p, Qt::Key_End); const auto old = p.presentation();
    p.clearFocus(); QVERIFY(!p.hasFocus()); QVERIFY(a->isEnabled()); a->trigger(); QCOMPARE(c.text(), expected(0, 0, 2));
    p.setWeek(End, week(), static_cast<Traffic>(99)); QCOMPARE(p.presentation(), old); QVERIFY(a->isEnabled()); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{23});
    c.sentinel(); frame(p, 1, Traffic::Dht); QVERIFY(p.presentation() != old); QVERIFY(!p.selectedBucket()); QVERIFY(!a->isEnabled()); a->trigger();
    QVERIFY(QMetaObject::invokeMethod(a, "triggered", Qt::DirectConnection, Q_ARG(bool, false))); QCOMPARE(c.text(), Sentinel); c.otherModesUnchanged();
  }
  void actionEnableCallbackNestedKeyOnSameFrameKeepsNewestSelection() {
    ClipboardFixture c; ui::SavedHistoryPlot p; frame(p); show(p); auto* a = action(p); QVERIFY(a); const auto original = p.presentation(); ActionHook hook(a, [&] { press(&p, Qt::Key_Right); });
    press(&p, Qt::Key_Home); QVERIFY(!hook.armed); QCOMPARE(p.presentation(), original); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{1}); QVERIFY(a->isEnabled());
    QCOMPARE(p.toolTip(), expected(0, 0, 1)); QCOMPARE(p.accessibleDescription(), expected(0, 0, 1)); QCOMPARE(c.text(), Sentinel); a->trigger(); QCOMPARE(c.text(), expected(0, 0, 1));
  }
  void actionEnableCallbackNewFrameCannotResumeOldSelectionOrPublication() {
    ClipboardFixture c; ui::SavedHistoryPlot p; frame(p); show(p); auto* a = action(p); QVERIFY(a); ActionHook hook(a, [&] { p.setWeek(End, {}, Traffic::Dht); });
    press(&p, Qt::Key_Home); QVERIFY(!hook.armed); QCOMPARE(p.presentation()->mode(), ui::SavedHistoryPlotMode::Week); QCOMPARE(p.presentation()->traffic(), Traffic::Dht);
    QVERIFY(!p.selectedBucket()); QVERIFY(!a->isEnabled()); QVERIFY(p.accessibleDescription() != expected(0, 0, 0)); a->trigger(); QCOMPARE(c.text(), Sentinel);
  }
  void actionEnableCallbackMayDirectlyDeleteReceivingPlot() {
    ClipboardFixture c; auto owner = std::make_unique<ui::SavedHistoryPlot>(); frame(*owner); show(*owner); auto* a = action(*owner); QVERIFY(a); QPointer guard(owner.get());
    ActionHook hook(a, [&] { owner.reset(); }); press(owner.get(), Qt::Key_Home); QVERIFY(!hook.armed); QVERIFY(guard.isNull()); QCOMPARE(c.text(), Sentinel);
  }
  void actionDisableCallbackMaySelectNewestReplacementAndCopyIt() {
    ClipboardFixture c; ui::SavedHistoryPlot p; frame(p); show(p); auto* a = action(p); QVERIFY(a); QTest::keyClick(&p, Qt::Key_End);
    ActionHook hook(a, [&] { press(&p, Qt::Key_End); a->trigger(); }); frame(p, 1, Traffic::Dht);
    QVERIFY(!hook.armed); QCOMPARE(p.presentation()->mode(), ui::SavedHistoryPlotMode::Week); QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{6}); QVERIFY(a->isEnabled());
    QCOMPARE(p.accessibleName(), QStringLiteral("Saved daily DHT traffic in bytes, seven independent UTC days ending 2024-03-02"));
    QCOMPARE(p.toolTip(), expected(1, 2, 2)); QCOMPARE(p.accessibleDescription(), expected(1, 2, 2)); QCOMPARE(c.text(), expected(1, 2, 2));
  }
  void actionDisableCallbackMayDeleteDuringAcceptedFrameReplacement() {
    ClipboardFixture c; auto owner = std::make_unique<ui::SavedHistoryPlot>(); frame(*owner); show(*owner); auto* a = action(*owner); QVERIFY(a); QTest::keyClick(owner.get(), Qt::Key_End); QPointer guard(owner.get());
    ActionHook hook(a, [&] { owner.reset(); }); owner->setWeek(End, week()); QVERIFY(!hook.armed); QVERIFY(guard.isNull()); QCOMPARE(c.text(), Sentinel);
  }
  void actionEnableCallbackExplicitNestedCopyPublishesNewestSelectionOnly() {
    ClipboardFixture c; ui::SavedHistoryPlot p; frame(p, 2, Traffic::Dht); show(p); auto* a = action(p); QVERIFY(a); QSignalSpy changes(c.clipboard, &QClipboard::changed);
    ActionHook hook(a, [&] { press(&p, Qt::Key_End); a->trigger(); }); press(&p, Qt::Key_Home); QVERIFY(!hook.armed);
    QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{167}); QCOMPARE(p.toolTip(), expected(2, 2, 2)); QCOMPARE(p.accessibleDescription(), expected(2, 2, 2));
    QCOMPARE(c.text(), expected(2, 2, 2)); QCOMPARE(changes.count(), 1); clipboardOnly(changes);
  }
  void clipboardCallbackCanAdvanceSelectionWithoutTrailingOldWritesOrAutomaticCopy() {
    ClipboardFixture c; ui::SavedHistoryPlot p; frame(p); show(p); auto* a = action(p); QVERIFY(a); QTest::keyClick(&p, Qt::Key_Home);
    ClipboardHook hook(c.clipboard, [&] { press(&p, Qt::Key_Right); }); a->trigger(); QVERIFY(!hook.armed);
    QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{1}); QCOMPARE(p.toolTip(), expected(0, 0, 1)); QCOMPARE(p.accessibleDescription(), expected(0, 0, 1)); QCOMPARE(c.text(), expected(0, 0, 0));
    a->trigger(); QCOMPARE(c.text(), expected(0, 0, 1)); c.otherModesUnchanged();
  }
  void clipboardCallbackCanReplaceFrameAndLeaveCopyDisabled() {
    ClipboardFixture c; ui::SavedHistoryPlot p; frame(p); show(p); auto* a = action(p); QVERIFY(a); QTest::keyClick(&p, Qt::Key_End);
    ClipboardHook hook(c.clipboard, [&] { p.setWeek(End, {}, Traffic::Dht); }); a->trigger(); QVERIFY(!hook.armed); QCOMPARE(c.text(), expected(0, 0, 2));
    QVERIFY(!p.selectedBucket()); QVERIFY(!a->isEnabled()); QCOMPARE(p.presentation()->traffic(), Traffic::Dht); a->trigger(); QCOMPARE(c.text(), expected(0, 0, 2));
  }
  void clipboardCallbackMayDirectlyDeleteOwningPlotAndItsAction() {
    ClipboardFixture c; auto owner = std::make_unique<ui::SavedHistoryPlot>(); frame(*owner, 2, Traffic::Protocol); show(*owner); auto* a = action(*owner); QVERIFY(a); QTest::keyClick(owner.get(), Qt::Key_End);
    QPointer guard(owner.get()); QPointer actionGuard(a); ClipboardHook hook(c.clipboard, [&] { owner.reset(); }); a->trigger(); QVERIFY(!hook.armed); QVERIFY(guard.isNull()); QVERIFY(actionGuard.isNull());
    QCOMPARE(c.text(), expected(2, 1, 2)); c.otherModesUnchanged();
  }
  void clipboardCallbackNestedCopyLeavesNewestAuthorizedTextAndSelection() {
    ClipboardFixture c; ui::SavedHistoryPlot p; frame(p, 2, Traffic::Dht); show(p); auto* a = action(p); QVERIFY(a); QTest::keyClick(&p, Qt::Key_Home); QSignalSpy changes(c.clipboard, &QClipboard::changed);
    ClipboardHook hook(c.clipboard, [&] { press(&p, Qt::Key_End); a->trigger(); }); a->trigger(); QVERIFY(!hook.armed);
    QCOMPARE(p.selectedBucket(), std::optional<std::size_t>{167}); QCOMPARE(p.toolTip(), expected(2, 2, 2)); QCOMPARE(p.accessibleDescription(), expected(2, 2, 2)); QCOMPARE(c.text(), expected(2, 2, 2));
    QCOMPARE(changes.count(), 2); clipboardOnly(changes); c.otherModesUnchanged();
  }
};
QTEST_MAIN(HistoryCopyTests)
#include "history_copy_tests.moc"

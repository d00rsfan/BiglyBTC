#include "classic_columns.hpp"
#include "column_layout_presentation.hpp"
#include "library_window.hpp"
#include "session_bridge.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace biglybtc;
std::string b(std::string_view s) { return std::to_string(s.size()) + ':' + std::string(s); }
const std::string Original = "d" + b("opaque") + b(std::string("\0\xff", 2))
    + b("Table.MyTorrents") + "d" + b("Column.status") + "li1ei1ei124ei0ei1ee"
    + b("SortColumns") + "l" + b("name") + b("unknown-plugin") + "ee"
    + b("Table.UnknownPlugin") + "d" + b("untouched") + "li-1e" + b(std::string("binary\0tail", 11)) + "eee";
void write(const QString& path, std::string_view raw) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
      || file.write(raw.data(), static_cast<qint64>(raw.size())) != static_cast<qint64>(raw.size()) || !file.flush()) {
    throw std::runtime_error("Generated fixture write failed");
  }
}
std::string read(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) { throw std::runtime_error("Generated fixture read failed"); }
  return file.readAll().toStdString();
}
struct Profile {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-classic-preferences-integration-XXXXXX")};
  QString path;
  explicit Profile(const std::string& tables = Original) {
    if (!root.isValid() || !QDir(root.path()).mkdir(QStringLiteral("profile"))) { throw std::runtime_error("Generated directory failed"); }
    path = root.path() + QStringLiteral("/profile");
    write(path + QStringLiteral("/downloads.config"), "d9:downloadslee");
    write(path + QStringLiteral("/tables.config"), tables);
  }
  std::filesystem::path native() const { return path.toStdString(); }
  QString tables() const { return path + QStringLiteral("/tables.config"); }
};
QAction* action(ui::LibraryWindow& w, const char* id) { return w.findChild<QAction*>(QString::fromLatin1(id)); }
void wire(ui::LibraryWindow& w, app::SessionBridge& s) {
  QObject::connect(&s, &app::SessionBridge::snapshotReady, &w, &ui::LibraryWindow::setPresentation);
  QObject::connect(&s, &app::SessionBridge::columnLayoutSessionReady, &w, &ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&s, &app::SessionBridge::columnLayoutsReady, &w, &ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&s, &app::SessionBridge::columnLayoutSaved, &w, &ui::LibraryWindow::setColumnLayoutSaved);
  QObject::connect(&s, &app::SessionBridge::busyChanged, &w, &ui::LibraryWindow::setBusy);
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &s, &app::SessionBridge::columnLayouts);
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutSaveRequested, &s, &app::SessionBridge::saveColumnLayout);
}
void show(ui::LibraryWindow& w) { w.resize(1500, 900); w.show(); QCoreApplication::processEvents(); }
ui::ColumnLayoutsPresentation originalPacket(quint64 session, quint64 request) {
  auto parsed = core::Document::parse(Original);
  if (!parsed) { throw std::runtime_error("Generated original tables parse failed"); }
  auto projected = core::readColumnLayouts(*parsed);
  if (!projected) { throw std::runtime_error("Generated original tables project failed"); }
  auto p = std::make_shared<ui::ColumnLayoutsPacket>();
  p->session = session; p->request = request; p->state = ui::ColumnLayoutReadState::Ready;
  p->source = ui::ColumnLayoutReadSource::Primary;
  p->snapshot = std::make_shared<const core::ColumnLayouts>(std::move(*projected));
  return p;
}
bool fakeRead(ui::LibraryWindow& w) {
  QSignalSpy requests(&w, &ui::LibraryWindow::columnLayoutsRequested);
  QObject::connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &w, [&w](quint64, quint64) { w.setBusy(true); });
  w.beginColumnLayoutSession(57);
  if (!QTest::qWaitFor([&] { return requests.count() == 1; }, 2000)) { return false; }
  w.setColumnLayouts(originalPacket(57, requests.at(0).at(1).toULongLong())); w.setBusy(false);
  return action(w, "classic-save-column-layout")->isEnabled();
}
ui::ColumnLayoutSavePresentation ack(ui::ColumnLayoutSaveSubmission request, bool success = true) {
  auto p = std::make_shared<ui::ColumnLayoutSavePacket>(); p->session = request->session; p->request = request->request; p->saved = success; return p;
}
std::string value(const core::Document& doc, std::string_view key) {
  const auto* n = doc.find(key);
  if (!n) { throw std::runtime_error("Missing generated key"); }
  return doc.originalBytes().substr(n->begin, n->end - n->begin);
}
} // namespace

class ClassicPreferencesIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void realSaveReloadAndRestartPreserveOriginalRecords() {
    Profile p; std::string saved;
    {
      ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
      QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
      QSignalSpy writes(&bridge, &app::SessionBridge::columnLayoutSaved);
      QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
      QVERIFY(bridge.open(p.native(), 0));
      QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
      QCOMPARE(w.incompleteTable().columnWidth(1), 124);
      auto* save = action(w, "classic-save-column-layout"); QVERIFY(save && save->isEnabled());
      w.incompleteTable().setColumnWidth(1, 319);
      w.incompleteTable().setColumnHidden(12, false);
      w.incompleteTable().horizontalHeader()->moveSection(w.incompleteTable().horizontalHeader()->visualIndex(12), 0);
      w.completeTable().setColumnWidth(7, 277);
      w.completeTable().sortByColumn(7, Qt::DescendingOrder);
      w.filter().setText(QStringLiteral("kept filter"));
      save->trigger(); QVERIFY(bridge.busy());
      // A newer local edit before queued completion must not be overwritten or saved.
      w.incompleteTable().setColumnWidth(1, 501);
      QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && !bridge.busy(), 30000);
      const auto written = qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0));
      QVERIFY(written && written->saved); QCOMPARE(w.incompleteTable().columnWidth(1), 501);
      bool unsavedMessage = false;
      for (auto* label : w.findChildren<QLabel*>()) { unsavedMessage |= label->text().contains(QStringLiteral("newer column edits remain unsaved")); }
      QVERIFY(unsavedMessage); QCOMPARE(snapshots.count(), 1); QCOMPARE(layouts.count(), 1);
      saved = read(p.tables());
      auto original = core::Document::parse(Original), stored = core::Document::parse(saved);
      QVERIFY(original && stored);
      for (const auto* key : {"opaque", "Table.MyTorrents", "Table.UnknownPlugin"}) { QCOMPARE(value(*stored, key), value(*original, key)); }
      QVERIFY(stored->find("BiglyBTC.ClassicColumns.v1"));
      QCOMPARE(read(p.tables() + QStringLiteral(".bak")), Original);
      auto prefs = core::readClassicPreferences(*stored); QVERIFY(prefs && prefs->has_value());
      QCOMPARE((**prefs).incomplete.columns[1].width, std::optional<int>{319});
      auto* reload = action(w, "classic-reload-column-layout"); reload->trigger();
      QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 2 && !bridge.busy(), 30000);
      QCOMPARE(w.incompleteTable().columnWidth(1), 319);
      QCOMPARE(w.filter().text(), QStringLiteral("kept filter"));
      QVERIFY(!w.incompleteTable().isColumnHidden(12));
      QCOMPARE(w.incompleteTable().horizontalHeader()->logicalIndex(0), 12);
      QCOMPARE(w.completeTable().columnWidth(7), 277);
      QCOMPARE(w.completeTable().horizontalHeader()->sortIndicatorSection(), 7);
      QCOMPARE(w.completeTable().horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
      // Second save without another read must use the newly committed baseline.
      w.incompleteTable().setColumnWidth(1, 401); save->trigger();
      QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 2 && !bridge.busy(), 30000);
      QVERIFY(qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(1).at(0))->saved);
      QCOMPARE(read(p.tables() + QStringLiteral(".bak")), saved);
      saved = read(p.tables()); QCOMPARE(snapshots.count(), 1);
    }
    {
      ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
      QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
      QVERIFY(bridge.open(p.native(), 0));
      QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
      QCOMPARE(w.incompleteTable().columnWidth(1), 401);
      QVERIFY(!w.incompleteTable().isColumnHidden(12));
      QCOMPARE(w.incompleteTable().horizontalHeader()->logicalIndex(0), 12);
      QCOMPARE(w.completeTable().columnWidth(7), 277);
      QCOMPARE(read(p.tables()), saved);
      auto* reset = w.incompleteTable().findChild<QAction*>(QStringLiteral("classic-columns-incomplete-reset"));
      QVERIFY(reset); reset->trigger(); QVERIFY(w.incompleteTable().isColumnHidden(12));
      QCOMPARE(w.incompleteTable().horizontalHeader()->logicalIndex(0), 0);
      QCOMPARE(read(p.tables()), saved); // Reset remains local until explicit Save.
    }
  }
  void allThirteenStableIdsSurviveRealSaveAndRestartInBothPanes() {
    Profile p; core::ClassicPreferences expected;
    core::ClassicPanePreferences* panes[] = {&expected.incomplete, &expected.complete};
    for (int pane = 0; pane < 2; ++pane) {
      for (int logical = 0; logical < 13; ++logical) {
        panes[pane]->columns[logical].visible = (logical + pane) % 3 != 0;
        if (logical != 0) { panes[pane]->columns[logical].width = 150 + pane * 120 + logical * 23; }
        panes[pane]->order[logical] = core::ClassicPreferenceIds[(logical + 5 + pane) % 13];
      }
      panes[pane]->sortId = core::ClassicPreferenceIds[pane == 0 ? 12 : 2];
      panes[pane]->ascending = pane != 0;
    }
    {
      ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
      QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady), writes(&bridge, &app::SessionBridge::columnLayoutSaved);
      QSignalSpy captures(&w, &ui::LibraryWindow::columnLayoutSaveRequested);
      QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
      QTableView* tables[] = {&w.incompleteTable(), &w.completeTable()};
      for (int pane = 0; pane < 2; ++pane) {
        auto* table = tables[pane]; auto* header = table->horizontalHeader();
        for (int logical = 0; logical < 13; ++logical) {
          if (logical != 0) { header->resizeSection(logical, *panes[pane]->columns[logical].width); }
          table->setColumnHidden(logical, !panes[pane]->columns[logical].visible);
        }
        for (int visual = 0; visual < 13; ++visual) {
          const int logical = (visual + 5 + pane) % 13;
          header->moveSection(header->visualIndex(logical), visual);
        }
        table->sortByColumn(pane == 0 ? 12 : 2, panes[pane]->ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
      }
      action(w, "classic-save-column-layout")->trigger();
      QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && !bridge.busy(), 30000);
      QVERIFY(qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0))->saved);
      QCOMPARE(captures.count(), 1);
      QVERIFY(*qvariant_cast<ui::ColumnLayoutSaveSubmission>(captures.at(0).at(0))->preferences == expected);
      auto stored = core::Document::parse(read(p.tables())); QVERIFY(stored);
      auto preferences = core::readClassicPreferences(*stored); QVERIFY(preferences && preferences->has_value());
      QVERIFY(**preferences == expected);
    }
    {
      ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
      QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
      QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
      QTableView* tables[] = {&w.incompleteTable(), &w.completeTable()};
      for (int pane = 0; pane < 2; ++pane) {
        ui::ClassicColumns* columns = nullptr;
        for (auto* child : tables[pane]->children()) { if (auto* candidate = dynamic_cast<ui::ClassicColumns*>(child)) { columns = candidate; break; } }
        QVERIFY(columns); const auto captured = columns->capturePreferences();
        QVERIFY(captured && *captured == *panes[pane]);
        QVERIFY(!captured->columns[0].width);
        QCOMPARE(tables[pane]->horizontalHeader()->sectionResizeMode(0), QHeaderView::Stretch);
      }
    }
  }
  void changedPrimaryRejectsWriteWithoutRefreshingLibrary() {
    Profile p; ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady), writes(&bridge, &app::SessionBridge::columnLayoutSaved);
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    const auto foreign = "d" + b("external") + "i8ee"; write(p.tables(), foreign);
    w.incompleteTable().setColumnWidth(1, 455); action(w, "classic-save-column-layout")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && !bridge.busy(), 30000);
    QVERIFY(!qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0))->saved);
    QCOMPARE(read(p.tables()), foreign); QVERIFY(!QFile::exists(p.tables() + QStringLiteral(".bak")));
    QVERIFY(!action(w, "classic-save-column-layout")->isEnabled());
    QCOMPARE(w.incompleteTable().columnWidth(1), 455); QCOMPARE(snapshots.count(), 1);
  }
  void unknownPrivateVersionDoesNotBlockLibraryOpenOrPermitSave() {
    const auto raw = "d" + b("BiglyBTC.ClassicColumns.v2") + "dee";
    Profile p(raw); ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady), snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    QCOMPARE(snapshots.count(), 1);
    QCOMPARE(qvariant_cast<ui::ColumnLayoutsPresentation>(layouts.at(0).at(0))->state, ui::ColumnLayoutReadState::Failed);
    QVERIFY(!action(w, "classic-save-column-layout")->isEnabled()); QCOMPARE(read(p.tables()), raw);
  }
  void rejectedSaveDoesNotLatchAndCaptureIsImmutable() {
    ui::LibraryWindow w; show(w); QVERIFY(fakeRead(w));
    QSignalSpy saves(&w, &ui::LibraryWindow::columnLayoutSaveRequested);
    auto* save = action(w, "classic-save-column-layout");
    w.incompleteTable().setColumnWidth(1, 366); save->trigger(); QCOMPARE(saves.count(), 1);
    auto capture = qvariant_cast<ui::ColumnLayoutSaveSubmission>(saves.at(0).at(0)); QVERIFY(capture && capture->preferences);
    w.incompleteTable().setColumnWidth(1, 444);
    QCOMPARE(capture->preferences->incomplete.columns[1].width, std::optional<int>{366});
    QVERIFY(save->isEnabled()); save->trigger(); QCOMPARE(saves.count(), 2);
  }
  void reloadQueuedInsideRejectedSaveRequestStillDispatches() {
    ui::LibraryWindow w; show(w); QVERIFY(fakeRead(w));
    QSignalSpy reads(&w, &ui::LibraryWindow::columnLayoutsRequested);
    QObject::connect(&w, &ui::LibraryWindow::columnLayoutSaveRequested, &w,
        [&](ui::ColumnLayoutSaveSubmission) { action(w, "classic-reload-column-layout")->trigger(); });
    action(w, "classic-save-column-layout")->trigger();
    QTRY_COMPARE(reads.count(), 1);
    const auto request = reads.at(0).at(1).toULongLong();
    w.setColumnLayouts(originalPacket(57, request)); w.setBusy(false);
    QVERIFY(action(w, "classic-save-column-layout")->isEnabled());
  }
  void missingOrStaleAckCannotConsumeCurrentSave() {
    ui::LibraryWindow w; show(w); QVERIFY(fakeRead(w));
    ui::ColumnLayoutSaveSubmission capture;
    QObject::connect(&w, &ui::LibraryWindow::columnLayoutSaveRequested, &w,
        [&](ui::ColumnLayoutSaveSubmission r) { capture = std::move(r); w.setBusy(true); });
    action(w, "classic-save-column-layout")->trigger(); QVERIFY(capture);
    auto wrong = std::make_shared<ui::ColumnLayoutSavePacket>(*ack(capture)); ++wrong->request;
    w.setColumnLayoutSaved(wrong); QVERIFY(!action(w, "classic-save-column-layout")->isEnabled());
    w.setBusy(false); QVERIFY(!action(w, "classic-save-column-layout")->isEnabled());
    w.setColumnLayoutSaved(ack(capture)); QVERIFY(!action(w, "classic-save-column-layout")->isEnabled());
  }
  void directWorkerRequiresReadBaselineAndDeliversBeforeBusyRelease() {
    Profile p; app::SessionBridge bridge;
    QSignalSpy sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady);
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
    QSignalSpy writes(&bridge, &app::SessionBridge::columnLayoutSaved);
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(p.native(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(sessions.count() == 1 && !bridge.busy(), 30000);
    ui::LibraryWindow captureWindow; show(captureWindow); QVERIFY(fakeRead(captureWindow));
    QSignalSpy captured(&captureWindow, &ui::LibraryWindow::columnLayoutSaveRequested);
    action(captureWindow, "classic-save-column-layout")->trigger(); QCOMPARE(captured.count(), 1);
    auto submission = std::make_shared<ui::ColumnLayoutSaveRequest>(
        *qvariant_cast<ui::ColumnLayoutSaveSubmission>(captured.at(0).at(0)));
    submission->session = sessions.at(0).at(0).toULongLong(); submission->request = 44;
    auto wrong = std::make_shared<ui::ColumnLayoutSaveRequest>(*submission); wrong->session = 0;
    QVERIFY(!bridge.saveColumnLayout(wrong)); QVERIFY(!bridge.saveColumnLayout({}));
    QVERIFY(bridge.saveColumnLayout(submission));
    QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && !bridge.busy(), 30000);
    QVERIFY(!qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0))->saved);
    QCOMPARE(read(p.tables()), Original); QCOMPARE(snapshots.count(), 1);
    QVERIFY(bridge.columnLayouts(submission->session, 88));
    QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    bool heldBusy = false, refusedReentry = false;
    QObject::connect(&bridge, &app::SessionBridge::columnLayoutSaved, &bridge,
        [&](ui::ColumnLayoutSavePresentation result) {
          heldBusy = bridge.busy(); refusedReentry = !bridge.saveColumnLayout(submission) && !bridge.refresh();
          bridge.cancel(); // Cancellation in an actual save-result slot cannot erase that result.
          QVERIFY(result && result->saved);
        });
    QVERIFY(bridge.saveColumnLayout(submission));
    QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 2 && !bridge.busy(), 30000);
    QVERIFY(heldBusy && refusedReentry); QCOMPARE(snapshots.count(), 1); QCOMPARE(layouts.count(), 1);
    QVERIFY(qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(1).at(0))->saved);
    auto parsed = core::Document::parse(read(p.tables())); QVERIFY(parsed);
    auto prefs = core::readClassicPreferences(*parsed); QVERIFY(prefs && prefs->has_value());
    QVERIFY(**prefs == *submission->preferences);
  }
  void reloadRequestedWhileSaveRunsWaitsForActualSaveResult() {
    Profile p; ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady), writes(&bridge, &app::SessionBridge::columnLayoutSaved);
    QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    w.incompleteTable().setColumnWidth(1, 395);
    action(w, "classic-save-column-layout")->trigger(); QVERIFY(bridge.busy());
    action(w, "classic-reload-column-layout")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && layouts.count() == 2 && !bridge.busy(), 30000);
    QVERIFY(qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0))->saved);
    QCOMPARE(w.incompleteTable().columnWidth(1), 395);
    QVERIFY(action(w, "classic-save-column-layout")->isEnabled());
  }
  void deletingWindowInsideSaveRequestIsSafe() {
    auto owner = std::make_unique<ui::LibraryWindow>(); show(*owner); QVERIFY(fakeRead(*owner));
    QPointer guard(owner.get());
    QObject::connect(owner.get(), &ui::LibraryWindow::columnLayoutSaveRequested, qApp,
        [&](ui::ColumnLayoutSaveSubmission r) { QVERIFY(r && r->preferences); owner.reset(); }, Qt::DirectConnection);
    action(*owner, "classic-save-column-layout")->trigger(); QVERIFY(guard.isNull());
  }
};
QTEST_MAIN(ClassicPreferencesIntegrationTests)
#include "classic_preferences_integration_tests.moc"

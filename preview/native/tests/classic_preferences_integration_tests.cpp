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
  void missingConfigurationFirstSaveAndSecondSaveSurviveRestart() {
    Profile p; QVERIFY(QFile::remove(p.tables())); std::string first, second;
    {
      ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
      QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady), writes(&bridge, &app::SessionBridge::columnLayoutSaved);
      QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
      QVERIFY(bridge.open(p.native(), 0));
      QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
      QCOMPARE(qvariant_cast<ui::ColumnLayoutsPresentation>(layouts.at(0).at(0))->state, ui::ColumnLayoutReadState::Missing);
      QVERIFY(!QFile::exists(p.tables())); QVERIFY(!QFile::exists(p.tables() + QStringLiteral(".bak")));
      auto* save = action(w, "classic-save-column-layout"); QVERIFY(save && save->isEnabled());
      w.incompleteTable().setColumnWidth(1, 328); w.completeTable().setColumnWidth(7, 276);
      w.filter().setText(QStringLiteral("retained first save filter"));
      save->trigger(); QVERIFY(bridge.busy()); w.incompleteTable().setColumnWidth(1, 401);
      QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && !bridge.busy(), 30000);
      QVERIFY(qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0))->saved);
      first = read(p.tables()); auto doc = core::Document::parse(first); QVERIFY(doc);
      QCOMPARE(doc->root().children.size(), std::size_t{2});
      auto prefs = core::readClassicPreferences(*doc); QVERIFY(prefs && prefs->has_value());
      QCOMPARE((**prefs).incomplete.columns[1].width, std::optional<int>{328});
      QCOMPARE(w.incompleteTable().columnWidth(1), 401);
      QCOMPARE(w.filter().text(), QStringLiteral("retained first save filter"));
      QVERIFY(!QFile::exists(p.tables() + QStringLiteral(".bak")));
      QVERIFY(!QFile::exists(p.tables() + QStringLiteral(".saving")));
      QCOMPARE(snapshots.count(), 1); QCOMPARE(layouts.count(), 1);
      QVERIFY(save->isEnabled()); save->trigger();
      QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 2 && !bridge.busy(), 30000);
      QVERIFY(qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(1).at(0))->saved);
      second = read(p.tables()); QVERIFY(second != first);
      QCOMPARE(read(p.tables() + QStringLiteral(".bak")), first);
    }
    {
      ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
      QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
      QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
      QCOMPARE(w.incompleteTable().columnWidth(1), 401); QCOMPARE(w.completeTable().columnWidth(7), 276);
      QCOMPARE(read(p.tables()), second); QVERIFY(action(w, "classic-save-column-layout")->isEnabled());
    }
  }
  void candidateAppearingAfterMissingReadRequiresReload_data() {
    QTest::addColumn<QString>("suffix");
    QTest::newRow("primary") << QString{};
    QTest::newRow("backup") << QStringLiteral(".bak");
    QTest::newRow("saving") << QStringLiteral(".saving");
  }
  void candidateAppearingAfterMissingReadRequiresReload() {
    QFETCH(QString, suffix);
    Profile p; QVERIFY(QFile::remove(p.tables()));
    ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady), writes(&bridge, &app::SessionBridge::columnLayoutSaved);
    QSignalSpy captures(&w, &ui::LibraryWindow::columnLayoutSaveRequested);
    QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    auto* save = action(w, "classic-save-column-layout"); QVERIFY(save->isEnabled());
    const auto candidate = p.tables() + suffix; const std::string foreign = "d7:foreigni83ee"; write(candidate, foreign);
    save->trigger(); QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && !bridge.busy(), 30000);
    QVERIFY(!qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0))->saved);
    QCOMPARE(read(candidate), foreign); QVERIFY(!save->isEnabled());
    QVERIFY(QFile::remove(candidate)); QVERIFY(!QFile::exists(p.tables()));
    // A direct caller cannot reuse Missing admission after any failed creation.
    auto request = std::make_shared<ui::ColumnLayoutSaveRequest>(*qvariant_cast<ui::ColumnLayoutSaveSubmission>(captures.at(0).at(0)));
    ++request->request; QVERIFY(bridge.saveColumnLayout(request));
    QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 2 && !bridge.busy(), 30000);
    QVERIFY(!qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(1).at(0))->saved);
    QVERIFY(!QFile::exists(p.tables()));
    action(w, "classic-reload-column-layout")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 2 && !bridge.busy(), 30000);
    QVERIFY(save->isEnabled()); save->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 3 && !bridge.busy(), 30000);
    QVERIFY(qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(2).at(0))->saved);
  }
  void recoveryAndMalformedRecoveryNeverAdmitFirstSave_data() {
    QTest::addColumn<QString>("suffix"); QTest::addColumn<QByteArray>("raw"); QTest::addColumn<bool>("failed");
    QTest::newRow("backup") << QStringLiteral(".bak") << QByteArray("de") << false;
    QTest::newRow("saving") << QStringLiteral(".saving") << QByteArray("de") << false;
    QTest::newRow("malformed-backup") << QStringLiteral(".bak") << QByteArray("invalid") << true;
  }
  void recoveryAndMalformedRecoveryNeverAdmitFirstSave() {
    QFETCH(QString, suffix); QFETCH(QByteArray, raw); QFETCH(bool, failed);
    Profile p; QVERIFY(QFile::remove(p.tables())); write(p.tables() + suffix, raw.toStdString());
    ui::LibraryWindow w; app::SessionBridge bridge; wire(w, bridge); show(w);
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady), snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    const auto packet = qvariant_cast<ui::ColumnLayoutsPresentation>(layouts.at(0).at(0)); QVERIFY(packet);
    QCOMPARE(packet->state, failed ? ui::ColumnLayoutReadState::Failed : ui::ColumnLayoutReadState::Ready);
    QVERIFY(!action(w, "classic-save-column-layout")->isEnabled());
    QVERIFY(!QFile::exists(p.tables())); QCOMPARE(read(p.tables() + suffix), raw.toStdString()); QCOMPARE(snapshots.count(), 1);
  }
  void missingReadCancellationCannotAuthorizeDirectCreation() {
    Profile p; QVERIFY(QFile::remove(p.tables())); app::SessionBridge bridge;
    QSignalSpy sessions(&bridge, &app::SessionBridge::columnLayoutSessionReady), reads(&bridge, &app::SessionBridge::columnLayoutsReady);
    QSignalSpy writes(&bridge, &app::SessionBridge::columnLayoutSaved);
    QVERIFY(bridge.open(p.native(), 0)); QTRY_VERIFY_WITH_TIMEOUT(sessions.count() == 1 && !bridge.busy(), 30000);
    ui::LibraryWindow capture; show(capture); QVERIFY(fakeRead(capture));
    QSignalSpy captured(&capture, &ui::LibraryWindow::columnLayoutSaveRequested);
    action(capture, "classic-save-column-layout")->trigger(); QCOMPARE(captured.count(), 1);
    auto request = std::make_shared<ui::ColumnLayoutSaveRequest>(*qvariant_cast<ui::ColumnLayoutSaveSubmission>(captured.at(0).at(0)));
    request->session = sessions.at(0).at(0).toULongLong(); request->request = 1;
    QVERIFY(bridge.saveColumnLayout(request)); QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && !bridge.busy(), 30000);
    QVERIFY(!qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0))->saved); QVERIFY(!QFile::exists(p.tables()));
    const auto connection = QObject::connect(&bridge, &app::SessionBridge::columnLayoutsReady, &bridge,
        [&](ui::ColumnLayoutsPresentation result) { QVERIFY(result && result->state == ui::ColumnLayoutReadState::Missing); bridge.cancel(); });
    QVERIFY(bridge.columnLayouts(request->session, 7)); QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 1 && !bridge.busy(), 30000);
    ++request->request; QVERIFY(bridge.saveColumnLayout(request)); QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 2 && !bridge.busy(), 30000);
    QVERIFY(!qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(1).at(0))->saved); QVERIFY(!QFile::exists(p.tables()));
    QObject::disconnect(connection);
    QVERIFY(bridge.columnLayouts(request->session, 8)); QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 2 && !bridge.busy(), 30000);
    ++request->request; QVERIFY(bridge.saveColumnLayout(request)); QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 3 && !bridge.busy(), 30000);
    QVERIFY(qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(2).at(0))->saved);
  }
  void contradictoryMissingPacketDoesNotAdmitSave() {
    ui::LibraryWindow w; show(w);
    QSignalSpy requests(&w, &ui::LibraryWindow::columnLayoutsRequested);
    QObject::connect(&w, &ui::LibraryWindow::columnLayoutsRequested, &w, [&w](quint64, quint64) { w.setBusy(true); });
    w.beginColumnLayoutSession(73); QTRY_COMPARE(requests.count(), 1);
    auto bad = std::make_shared<ui::ColumnLayoutsPacket>(*originalPacket(73, requests.at(0).at(1).toULongLong()));
    bad->state = ui::ColumnLayoutReadState::Missing; w.setColumnLayouts(bad); w.setBusy(false);
    QVERIFY(!action(w, "classic-save-column-layout")->isEnabled());
    action(w, "classic-reload-column-layout")->trigger(); QTRY_COMPARE(requests.count(), 2);
    auto missing = std::make_shared<ui::ColumnLayoutsPacket>(); missing->session = 73;
    missing->request = requests.at(1).at(1).toULongLong(); missing->state = ui::ColumnLayoutReadState::Missing;
    w.setColumnLayouts(missing); w.setBusy(false); QVERIFY(action(w, "classic-save-column-layout")->isEnabled());
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

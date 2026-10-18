#include "classic_columns.hpp"
#include "column_layout_presentation.hpp"
#include "library_window.hpp"

#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTabWidget>
#include <QtTest>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Declared native layout-only admission policy. Real Qt widgets/proxies and
// real bounded core projections; no Java, files, backend or config writer.
namespace {
using namespace biglybtc;
using Pairs = std::vector<std::pair<std::string, std::string>>;
std::string b(std::string_view text) { return std::to_string(text.size()) + ':' + std::string(text); }
std::string i(std::int64_t number) { return 'i' + std::to_string(number) + 'e'; }
std::string l(const std::vector<std::string>& values) {
  std::string raw = "l"; for (const auto& value : values) { raw += value; } return raw + 'e';
}
std::string d(const Pairs& pairs) {
  std::string raw = "d"; for (const auto& [key, value] : pairs) { raw += b(key) + value; } return raw + 'e';
}
std::string col(std::int64_t visible, std::int64_t position, std::int64_t width, std::int64_t direction = 1) {
  return l({i(visible), i(position), i(width), i(0), i(direction)});
}
std::string tables(std::string incomplete, std::string complete = "de") {
  return d({{"Table.MyTorrents", std::move(incomplete)}, {"Table.MySeeders", std::move(complete)}});
}
std::shared_ptr<const core::ColumnLayouts> projected(const std::string& raw) {
  auto source = core::Document::parse(raw);
  if (!source) { throw std::runtime_error("Generated layout must parse"); }
  auto result = core::readColumnLayouts(*source);
  if (!result) { throw std::runtime_error("Generated layout must project under real bounded core"); }
  return std::make_shared<const core::ColumnLayouts>(std::move(*result));
}
ui::ColumnLayoutsPresentation ready(quint64 session, quint64 request, const std::string& raw) {
  auto packet = std::make_shared<ui::ColumnLayoutsPacket>();
  packet->session = session; packet->request = request;
  packet->state = ui::ColumnLayoutReadState::Ready;
  packet->source = ui::ColumnLayoutReadSource::Primary;
  packet->snapshot = projected(raw);
  return packet;
}
ui::ColumnLayoutsPresentation disposition(quint64 session, quint64 request, ui::ColumnLayoutReadState state) {
  auto packet = std::make_shared<ui::ColumnLayoutsPacket>();
  packet->session = session; packet->request = request; packet->state = state;
  if (state == ui::ColumnLayoutReadState::Failed) { packet->failure = QStringLiteral("Generated layout read failed"); }
  return packet;
}
std::shared_ptr<ui::LibraryPresentation> sample(quint64 revision = 1) {
  auto result = std::make_shared<ui::LibraryPresentation>(); result->revision = revision;
  const QStringList names{QStringLiteral("Alpha.bin"), QStringLiteral("Beta.bin"),
                          QStringLiteral("Gamma.bin"), QStringLiteral("Complete.bin")};
  const qint64 sent[]{2, 100, 9, 50};
  for (int row = 0; row < names.size(); ++row) {
    const auto hash = QStringLiteral("%1").arg(row + 1, 40, 16, QChar('0')).toUpper();
    result->items.push_back({.key = hash, .hash = hash, .name = names[row],
        .state = row == 3 ? QStringLiteral("Stopped") : QStringLiteral("Queued"),
        .bytes = 1000, .completed = row == 3 ? 1000 : 100, .received = 120,
        .sent = sent[row], .canContinue = row != 3, .savedPosition = row});
  }
  return result;
}
QAction* action(ui::LibraryWindow& window, const char* name) {
  return window.findChild<QAction*>(QString::fromLatin1(name));
}
QAction* chooseAction(QTableView& table, bool complete, int column) {
  return table.findChild<QAction*>(QStringLiteral("classic-columns-%1-column-%2")
      .arg(complete ? QStringLiteral("complete") : QStringLiteral("incomplete")).arg(column));
}
QAction* resetAction(QTableView& table, bool complete) {
  return table.findChild<QAction*>(complete ? QStringLiteral("classic-columns-complete-reset")
                                           : QStringLiteral("classic-columns-incomplete-reset"));
}
void prepare(ui::LibraryWindow& window) {
  window.resize(1600, 900); window.setPresentation(sample()); window.show();
  QCoreApplication::processEvents();
}
void bindBusyOwner(ui::LibraryWindow& window) {
  QObject::connect(&window, &ui::LibraryWindow::columnLayoutsRequested, &window,
      [&window](quint64, quint64) { window.setBusy(true); });
}
quint64 requestAt(const QSignalSpy& spy, int index = 0) { return spy.at(index).at(1).toULongLong(); }
quint64 sessionAt(const QSignalSpy& spy, int index = 0) { return spy.at(index).at(0).toULongLong(); }
bool start(ui::LibraryWindow& window, QSignalSpy& spy, quint64 session = 41) {
  const auto before = spy.count(); window.beginColumnLayoutSession(session);
  return QTest::qWaitFor([&] { return spy.count() == before + 1; }, 2000)
      && sessionAt(spy, static_cast<int>(before)) == session
      && requestAt(spy, static_cast<int>(before)) != 0;
}
void deliver(ui::LibraryWindow& window, quint64 session, quint64 request, const std::string& raw) {
  window.setColumnLayouts(ready(session, request, raw)); window.setBusy(false);
  QCoreApplication::processEvents();
}
struct HeaderState {
  std::vector<int> order, widths;
  std::vector<bool> hidden;
  int sort;
  Qt::SortOrder direction;
  bool operator==(const HeaderState&) const = default;
};
HeaderState state(QTableView& table) {
  auto* header = table.horizontalHeader(); HeaderState result;
  for (int index = 0; index < header->count(); ++index) {
    result.order.push_back(header->logicalIndex(index));
    result.widths.push_back(header->sectionSize(index));
    result.hidden.push_back(table.isColumnHidden(index));
  }
  result.sort = header->sortIndicatorSection(); result.direction = header->sortIndicatorOrder();
  return result;
}
std::vector<int> order(QTableView& table) { return state(table).order; }
bool select(ui::LibraryWindow& window, int sourceRow = 1) {
  auto& table = window.incompleteTable();
  auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
  if (!proxy) { return false; }
  const auto index = proxy->mapFromSource(window.model().index(sourceRow, 0));
  if (!index.isValid()) { return false; }
  table.selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  return table.selectionModel()->isSelected(index);
}
std::optional<ui::Target> selected(ui::LibraryWindow& window) {
  auto& table = window.incompleteTable();
  auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
  return proxy && table.currentIndex().isValid()
      ? window.model().targetAt(proxy->mapToSource(table.currentIndex()).row()) : std::nullopt;
}
void supplyFiles(ui::LibraryWindow& window, const ui::Target& target) {
  auto files = std::make_shared<ui::FileDetails>(); files->target = target;
  files->items = {{0, QStringLiteral("selected.bin"), 1000, 0, false, false}};
  window.setDetails({target, QStringLiteral("Beta.bin"), 1000, files});
}
} // namespace

class ClassicLayoutImportTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }

  void whitelistExclusionsAndNameStretch() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested);
    QVERIFY(start(window, requests));
    const auto before = state(window.incompleteTable());
    Pairs fields;
    for (const auto* id : {"name", "status", "downspeed", "upspeed", "up", "torrenthash"}) {
      fields.emplace_back(std::string("Column.") + id, col(1, -1, 401));
    }
    for (const auto* id : {"size", "remaining", "done", "down", "#", "Name", "state", "plugin.name"}) {
      fields.emplace_back(std::string("Column.") + id, col(0, 0, 707));
    }
    fields.emplace_back("Column." + std::string("name\0alias", 10), col(0, 0, 707));
    fields.emplace_back("Column." + std::string("name\xff", 5), col(0, 0, 707));
    deliver(window, 41, requestAt(requests), tables(d(fields)));
    auto& table = window.incompleteTable(); auto* header = table.horizontalHeader();
    QVERIFY(!table.isColumnHidden(0));
    for (const int logical : {1, 4, 5, 7, 8}) {
      QVERIFY(!table.isColumnHidden(logical)); QCOMPARE(header->sectionSize(logical), 401);
      auto* chooser = chooseAction(table, false, logical); QVERIFY(chooser); QVERIFY(chooser->isChecked());
    }
    QCOMPARE(header->sectionResizeMode(0), QHeaderView::Stretch);
    QVERIFY(header->sectionSize(0) != 401);
    for (const int logical : {2, 3, 6, 9, 10, 11, 12}) {
      QCOMPARE(table.isColumnHidden(logical), before.hidden[static_cast<std::size_t>(logical)]);
      QCOMPARE(header->sectionSize(logical), before.widths[static_cast<std::size_t>(logical)]);
    }
    QVERIFY(order(table) == before.order);
  }

  void missingWrongAndEmptyPaneNeverResetOtherPane() {
    for (const auto& first : {std::string("absent"), std::string("de"), i(9)}) {
      ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
      QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested);
      QVERIFY(start(window, requests)); const auto before = state(window.incompleteTable());
      const auto valid = d({{"Column.status", col(1, -1, 609)}});
      const auto raw = first == "absent" ? d({{"Table.MySeeders", valid}}) : tables(first, valid);
      deliver(window, 41, requestAt(requests), raw);
      QVERIFY(state(window.incompleteTable()) == before);
      QCOMPARE(window.completeTable().horizontalHeader()->sectionSize(1), 609);
    }
  }

  void widthsAdmittedIndependentlyAtNativeBoundaries() {
    for (const qint64 width : {qint64{15}, qint64{16}, qint64{8192}, qint64{8193}, qint64{-1}}) {
      ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
      QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested);
      QVERIFY(start(window, requests)); auto& table = window.incompleteTable();
      const auto old = table.horizontalHeader()->sectionSize(1);
      deliver(window, 41, requestAt(requests), tables(d({{"Column.status", col(0, -1, width)}})));
      QVERIFY(table.isColumnHidden(1));
      auto* chooser = chooseAction(table, false, 1); QVERIFY(chooser); chooser->trigger();
      QVERIFY(!table.isColumnHidden(1));
      const auto expected = width >= 16 && width <= 8192
          ? std::max(static_cast<int>(width), table.horizontalHeader()->minimumSectionSize()) : old;
      QCOMPARE(table.horizontalHeader()->sectionSize(1), expected); // Qt's existing minimum still applies.
    }
  }

  void uniquePositionsCompactGapsAndKeepOtherRelativeOrder() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    auto* header = window.incompleteTable().horizontalHeader();
    header->moveSection(header->visualIndex(12), 2); // A pre-session local edit affects automatic import.
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested);
    QVERIFY(start(window, requests));
    window.setColumnLayouts(disposition(41, requestAt(requests), ui::ColumnLayoutReadState::Missing));
    window.setBusy(false);
    auto* reload = action(window, "classic-reload-column-layout"); QVERIFY(reload); reload->trigger();
    QTRY_COMPARE(requests.count(), 2);
    const auto before = order(window.incompleteTable());
    deliver(window, 41, requestAt(requests, 1), tables(d({{"Column.status", col(1, 90, 301)},
        {"Column.torrenthash", col(1, 3, 302)}, {"Column.up", col(1, 20, 303)}})));
    const auto after = order(window.incompleteTable());
    QCOMPARE(after[0], 8); QCOMPARE(after[1], 7); QCOMPARE(after[2], 1);
    std::vector<int> remainder;
    for (const auto value : before) { if (value != 8 && value != 7 && value != 1) { remainder.push_back(value); } }
    QVERIFY(std::vector<int>(after.begin() + 3, after.end()) == remainder);
  }

  void duplicateAndNegativePositionsDoNotInventAnOrder() {
    for (const bool duplicate : {false, true}) {
      ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
      QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
      const auto before = order(window.incompleteTable());
      const auto raw = duplicate ? d({{"Column.status", col(0, 5, 456)}, {"Column.up", col(1, 5, 457)}})
                                 : d({{"Column.status", col(0, -1, 456)}, {"Column.up", col(1, -2, 457)}});
      deliver(window, 41, requestAt(requests), tables(raw));
      QVERIFY(order(window.incompleteTable()) == before);
      QVERIFY(window.incompleteTable().isColumnHidden(1));
      QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(7), 457);
    }
  }

  void singleCurrentSortNeedsExplicitNarrowedDirection() {
    for (const qint64 direction : {qint64{0}, qint64{1}, qint64{2}, qint64{4294967297LL}}) {
      ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
      QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
      deliver(window, 41, requestAt(requests), tables(d({{"Column.up", col(1, -1, 180, direction)},
                                                        {"SortColumns", l({b("up")})}})));
      QCOMPARE(window.incompleteTable().horizontalHeader()->sortIndicatorSection(), 7);
      QCOMPARE(window.incompleteTable().horizontalHeader()->sortIndicatorOrder(),
          direction == 1 || direction == 4294967297LL ? Qt::AscendingOrder : Qt::DescendingOrder);
    }
  }

  void unsupportedSortFormsKeepNativeSort() {
    const std::vector<std::string> forms{
      d({{"Column.up", col(1, -1, 180)}, {"SortColumns", "le"}}),
      d({{"Column.up", col(1, -1, 180)}, {"SortColumns", l({b("up"), b("name")})}}),
      d({{"Column.up", col(1, -1, 180)}, {"SortColumns", l({i(1)})}}),
      d({{"Column.up", col(1, -1, 180)}, {"SortColumns", l({b("unknown"), b("up")})}}),
      d({{"Column.up", col(1, -1, 180)}, {"SortColumn", b("up")}}),
      d({{"Column.up", col(1, -1, 180, -1)}, {"SortColumns", l({b("up")})}}),
      d({{"Column.up", l({i(1), i(-1), i(180)})}, {"SortColumns", l({b("up")})}}),
      d({{"Column.up", col(1, -1, 180)}, {"SortColumns", l({b("size")})}}),
      d({{"Column.up", col(1, -1, 180)}, {"SortColumns", l({b(std::string("up\0", 3))})}})};
    for (const auto& form : forms) {
      ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
      QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
      auto* header = window.incompleteTable().horizontalHeader();
      const auto old = std::pair{header->sortIndicatorSection(), header->sortIndicatorOrder()};
      deliver(window, 41, requestAt(requests), tables(form));
      QCOMPARE(header->sortIndicatorSection(), old.first); QCOMPARE(header->sortIndicatorOrder(), old.second);
    }
  }

  void nativeUserRoleSortRetainsTargetFilterDetailsAndModel() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    window.filter().setText(QStringLiteral(".BIN")); QVERIFY(select(window));
    const auto target = selected(window); QVERIFY(target);
    window.filesAction().trigger(); supplyFiles(window, *target);
    const auto detailsRows = window.filesModel().rowCount();
    QSignalSpy details(&window, &ui::LibraryWindow::detailsRequested);
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    QAbstractItemModelTester tester(&window.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto* tableModel = window.incompleteTable().model();
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    deliver(window, 41, requestAt(requests), tables(d({{"Column.up", col(1, 0, 222)},
                                                      {"SortColumns", l({b("up")})}})));
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(tableModel); QVERIFY(proxy);
    QCOMPARE(proxy->sortRole(), int(Qt::UserRole));
    QCOMPARE(proxy->data(proxy->index(0, 7), Qt::UserRole).toLongLong(), qint64{2});
    QCOMPARE(proxy->data(proxy->index(1, 7), Qt::UserRole).toLongLong(), qint64{9});
    QCOMPARE(proxy->data(proxy->index(2, 7), Qt::UserRole).toLongLong(), qint64{100});
    QVERIFY(selected(window) == target); QCOMPARE(window.filter().text(), QStringLiteral(".BIN"));
    QCOMPARE(window.filesModel().rowCount(), detailsRows); QVERIFY(window.model().hasDetails(*target));
    QCOMPARE(window.incompleteTable().model(), tableModel); QCOMPARE(resets.count(), 0); QCOMPARE(details.count(), 0);
  }

  void chooserSyncAndResetUsesConstructionBaseline() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    const auto baseline = state(window.incompleteTable());
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    deliver(window, 41, requestAt(requests), tables(d({{"Column.status", col(0, 0, 600)}, {"Column.torrenthash", col(1, 1, 610)}})));
    auto* status = chooseAction(window.incompleteTable(), false, 1);
    auto* hash = chooseAction(window.incompleteTable(), false, 8);
    auto* reset = resetAction(window.incompleteTable(), false); QVERIFY(status && hash && reset);
    QVERIFY(!status->isChecked()); QVERIFY(hash->isChecked());
    reset->trigger(); QCoreApplication::processEvents();
    const auto after = state(window.incompleteTable());
    QVERIFY(after.order == baseline.order && after.hidden == baseline.hidden);
    for (int index = 1; index < ui::LibraryModel::Columns; ++index) { QCOMPARE(after.widths[static_cast<std::size_t>(index)], baseline.widths[static_cast<std::size_t>(index)]); }
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionResizeMode(0), QHeaderView::Stretch);
    for (int index = 10; index < 13; ++index) { QVERIFY(window.incompleteTable().isColumnHidden(index)); }
    QVERIFY(status->isChecked()); QVERIFY(!hash->isChecked());
    QCOMPARE(requests.count(), 1); // Reset is local, not a file reload.
  }

  void hiddenWhitelistCanBeRecoveredThroughRealChooser() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    Pairs fields;
    for (const auto* id : {"name", "status", "downspeed", "upspeed", "up", "torrenthash"}) {
      fields.emplace_back(std::string("Column.") + id, col(0, -1, 300));
    }
    deliver(window, 41, requestAt(requests), tables(d(fields)));
    for (const int logical : {2, 3, 6, 9, 10, 11, 12}) {
      auto* chooser = chooseAction(window.incompleteTable(), false, logical); QVERIFY(chooser);
      if (chooser->isChecked()) { chooser->trigger(); }
    }
    for (int logical = 0; logical < ui::LibraryModel::Columns; ++logical) { QVERIFY(window.incompleteTable().isColumnHidden(logical)); }
    auto* name = chooseAction(window.incompleteTable(), false, 0); QVERIFY(name); QVERIFY(!name->isChecked());
    name->trigger(); QVERIFY(!window.incompleteTable().isColumnHidden(0)); QVERIFY(name->isChecked());
    QCOMPARE(requests.count(), 1);
  }

  void editsAfterRequestWinIndependentlyPerPane() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    window.incompleteTable().horizontalHeader()->resizeSection(1, 333);
    deliver(window, 41, requestAt(requests), tables(d({{"Column.status", col(1, -1, 501)}}),
                                                      d({{"Column.status", col(1, -1, 502)}})));
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(1), 333);
    QCOMPARE(window.completeTable().horizontalHeader()->sectionSize(1), 502);
  }

  void editsBeforeSessionAndNoopResetBeatAutomaticImport() {
    for (const bool reset : {false, true}) {
      ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
      if (reset) { auto* command = resetAction(window.incompleteTable(), false); QVERIFY(command); command->trigger(); }
      else { window.incompleteTable().horizontalHeader()->resizeSection(1, 333); }
      const auto before = state(window.incompleteTable());
      QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
      deliver(window, 41, requestAt(requests), tables(d({{"Column.status", col(0, -1, 701)}}),
                                                        d({{"Column.status", col(1, -1, 702)}})));
      QVERIFY(state(window.incompleteTable()) == before);
      QCOMPARE(window.completeTable().horizontalHeader()->sectionSize(1), 702);
    }
  }

  void explicitReloadCanReplaceEarlierButNotLaterEdits() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    window.incompleteTable().horizontalHeader()->resizeSection(1, 333);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    window.setColumnLayouts(disposition(41, requestAt(requests), ui::ColumnLayoutReadState::Missing)); window.setBusy(false);
    auto* reload = action(window, "classic-reload-column-layout"); QVERIFY(reload); reload->trigger();
    QTRY_COMPARE(requests.count(), 2);
    deliver(window, 41, requestAt(requests, 1), tables(d({{"Column.status", col(1, -1, 501)}})));
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(1), 501);
    reload->trigger(); QTRY_COMPARE(requests.count(), 3);
    window.incompleteTable().horizontalHeader()->resizeSection(1, 334);
    deliver(window, 41, requestAt(requests, 2), tables(d({{"Column.status", col(1, -1, 503)}})));
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(1), 334);
  }

  void rowRefreshRevisionDoesNotStaleHeaders() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    window.setPresentation(sample(2));
    deliver(window, 41, requestAt(requests), tables(d({{"Column.status", col(1, -1, 501)}})));
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(1), 501);
    QVERIFY(window.model().targetAt(0) && window.model().targetAt(0)->generation == 2);
  }

  void staleAndZeroPacketsCannotConsumeCurrentRequest() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    const auto id = requestAt(requests); const auto before = state(window.incompleteTable());
    const auto raw = tables(d({{"Column.status", col(1, -1, 555)}}));
    for (const auto& [session, request] : {std::pair<quint64, quint64>{0, id}, {41, 0}, {42, id}, {41, id + 1}}) {
      window.setColumnLayouts(ready(session, request, raw)); QVERIFY(state(window.incompleteTable()) == before);
    }
    deliver(window, 41, id, raw);
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(1), 555);
  }

  void missingFailedAndNullPacketsDoNotChangeHeaders() {
    for (const auto kind : {ui::ColumnLayoutReadState::Missing, ui::ColumnLayoutReadState::Failed}) {
      ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
      QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
      const auto first = state(window.incompleteTable()); const auto second = state(window.completeTable());
      window.setColumnLayouts({});
      window.setColumnLayouts(disposition(41, requestAt(requests), kind)); window.setBusy(false);
      QVERIFY(state(window.incompleteTable()) == first && state(window.completeTable()) == second);
    }
  }

  void busyReloadsCoalesceToLatestWithoutParallelDispatch() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    auto* reload = action(window, "classic-reload-column-layout"); QVERIFY(reload); QVERIFY(reload->isEnabled());
    const auto first = requestAt(requests); reload->trigger(); reload->trigger();
    QCoreApplication::processEvents(); QCOMPARE(requests.count(), 1);
    deliver(window, 41, first, tables("de"));
    QTRY_COMPARE(requests.count(), 2); const auto latest = requestAt(requests, 1);
    QVERIFY(latest > first + 1); QVERIFY(window.cancelAction().isEnabled());
    deliver(window, 41, latest, tables(d({{"Column.status", col(1, -1, 601)}})));
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(1), 601);
    QCOMPARE(requests.count(), 2);
  }

  void cancelledNoPacketReleasesActiveAndHonorsPending() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    const auto old = requestAt(requests);
    auto* reload = action(window, "classic-reload-column-layout"); QVERIFY(reload); reload->trigger(); reload->trigger();
    window.setBusy(false); // Accepted owner command completed/cancelled without a packet.
    QTRY_COMPARE(requests.count(), 2);
    const auto before = state(window.incompleteTable());
    window.setColumnLayouts(ready(41, old, tables(d({{"Column.status", col(1, -1, 999)}}))));
    QVERIFY(state(window.incompleteTable()) == before);
    deliver(window, 41, requestAt(requests, 1), tables(d({{"Column.status", col(1, -1, 602)}})));
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(1), 602);
  }

  void offTabLateImportDoesNotReattachOrRequestDetails() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    QVERIFY(select(window)); const auto target = selected(window); QVERIFY(target); supplyFiles(window, *target);
    auto* page = window.mainTabs().currentWidget(); QVERIFY(page);
    QSignalSpy details(&window, &ui::LibraryWindow::detailsRequested);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    auto* close = action(window, "classic-close-tab"); QVERIFY(close); close->trigger();
    QCOMPARE(window.mainTabs().indexOf(page), -1); const auto rows = window.filesModel().rowCount();
    deliver(window, 41, requestAt(requests), tables(d({{"Column.status", col(1, -1, 603)}})));
    QCOMPARE(window.mainTabs().indexOf(page), -1); QCOMPARE(window.mainTabs().count(), 0); QCOMPARE(details.count(), 0);
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(1), 603);
    auto* reopen = action(window, "classic-my-torrents"); QVERIFY(reopen); reopen->trigger();
    QCOMPARE(window.mainTabs().currentWidget(), page); QVERIFY(selected(window) == target);
    QCOMPARE(window.filesModel().rowCount(), rows); QCOMPARE(details.count(), 0);
  }

  void newSessionRejectsOldRepliesAndQueuedDestructionIsSilent() {
    {
      ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
      QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
      const auto old = requestAt(requests); window.beginColumnLayoutSession(42);
      window.setBusy(false); QTRY_COMPARE(requests.count(), 2);
      const auto before = state(window.incompleteTable());
      window.setColumnLayouts(ready(41, old, tables(d({{"Column.status", col(1, -1, 999)}}))));
      QVERIFY(state(window.incompleteTable()) == before);
      deliver(window, 42, requestAt(requests, 1), tables(d({{"Column.status", col(1, -1, 604)}})));
      QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(1), 604);
    }
    auto window = std::make_unique<ui::LibraryWindow>();
    QSignalSpy requests(window.get(), &ui::LibraryWindow::columnLayoutsRequested);
    window->beginColumnLayoutSession(41); window.reset(); QCoreApplication::processEvents();
    QCOMPARE(requests.count(), 0);
  }

  void directHeaderReentryQueuesFreshRequest() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    auto* reload = action(window, "classic-reload-column-layout"); QVERIFY(reload);
    bool entered = false;
    QObject::connect(window.incompleteTable().horizontalHeader(), &QHeaderView::sectionResized, &window,
        [&](int logical, int, int) { if (logical == 1 && !entered) { entered = true; reload->trigger(); } });
    deliver(window, 41, requestAt(requests), tables(d({{"Column.status", col(1, -1, 605)}})));
    QVERIFY(entered); QTRY_COMPARE(requests.count(), 2);
    QVERIFY(requestAt(requests, 1) > requestAt(requests));
    deliver(window, 41, requestAt(requests, 1), tables(d({{"Column.status", col(1, -1, 606)}})));
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionSize(1), 606);
  }

  void missingOwnedSourceAndOversizedSyntheticPaneCannotApply() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    const auto raw = tables(d({{"Column.status", col(1, -1, 607)}}), d({{"Column.status", col(1, -1, 608)}}));
    const auto before = state(window.incompleteTable());
    auto invalid = std::make_shared<core::ColumnLayouts>(*projected(raw)); invalid->source.reset();
    auto packet = std::make_shared<ui::ColumnLayoutsPacket>(*ready(41, requestAt(requests), raw)); packet->snapshot = invalid;
    window.setColumnLayouts(packet); QVERIFY(state(window.incompleteTable()) == before);
    window.setBusy(false);
    auto* reload = action(window, "classic-reload-column-layout"); QVERIFY(reload); reload->trigger(); QTRY_COMPARE(requests.count(), 2);
    auto oversized = std::make_shared<core::ColumnLayouts>(*projected(raw));
    oversized->incomplete.columns.resize(1025); // Deliberately forged DTO guard, not a core/source fixture.
    packet = std::make_shared<ui::ColumnLayoutsPacket>(*ready(41, requestAt(requests, 1), raw)); packet->snapshot = oversized;
    window.setColumnLayouts(packet); window.setBusy(false);
    QVERIFY(state(window.incompleteTable()) == before);
    QCOMPARE(window.completeTable().horizontalHeader()->sectionSize(1), 608);
    reload->trigger(); QTRY_COMPARE(requests.count(), 3);
    const auto secondRaw = tables(d({{"Column.status", col(1, -1, 607)}}), d({{"Column.status", col(1, -1, 609)}}));
    auto tooManySortSlots = std::make_shared<core::ColumnLayouts>(*projected(secondRaw));
    tooManySortSlots->incomplete.sort.resize(33); // Same defensive packet cap, no source/equality credit.
    packet = std::make_shared<ui::ColumnLayoutsPacket>(*ready(41, requestAt(requests, 2), secondRaw));
    packet->snapshot = tooManySortSlots; window.setColumnLayouts(packet); window.setBusy(false);
    QVERIFY(state(window.incompleteTable()) == before);
    QCOMPARE(window.completeTable().horizontalHeader()->sectionSize(1), 609);
  }

  void deletionInsideHeaderSignalAbortsRemainingApplication() {
    auto owner = std::make_unique<ui::LibraryWindow>(); prepare(*owner); bindBusyOwner(*owner);
    QPointer guard(owner.get());
    QSignalSpy requests(owner.get(), &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(*owner, requests));
    const auto packet = ready(41, requestAt(requests), tables(d({{"Column.status", col(1, -1, 701)}}),
                                                            d({{"Column.status", col(1, -1, 702)}})));
    bool destroyedInHeader = false; int otherPaneApplied = 0;
    QObject::connect(owner->completeTable().horizontalHeader(), &QHeaderView::sectionResized, qApp,
        [&](int logical, int, int width) { if (logical == 1 && width == 702) { ++otherPaneApplied; } }, Qt::DirectConnection);
    QObject::connect(owner->incompleteTable().horizontalHeader(), &QHeaderView::sectionResized, qApp,
        [&](int logical, int, int width) {
          if (logical == 1 && width == 701 && !destroyedInHeader) { destroyedInHeader = true; owner.reset(); }
        }, Qt::DirectConnection);
    owner->setColumnLayouts(packet); // Actual direct deletion inside the Qt import stack.
    QVERIFY(destroyedInHeader); QVERIFY(guard.isNull()); QCOMPARE(otherPaneApplied, 0);
    QCoreApplication::processEvents();
  }

  void directResetDuringImportWinsForOnlyItsPane() {
    ui::LibraryWindow window; prepare(window); bindBusyOwner(window);
    const auto baseline = state(window.incompleteTable());
    auto* reset = resetAction(window.incompleteTable(), false); QVERIFY(reset);
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested); QVERIFY(start(window, requests));
    bool resetInsideHeader = false;
    QObject::connect(window.incompleteTable().horizontalHeader(), &QHeaderView::sectionResized, &window,
        [&](int logical, int, int width) {
          if (logical == 1 && width == 701 && !resetInsideHeader) { resetInsideHeader = true; reset->trigger(); }
        }, Qt::DirectConnection);
    deliver(window, 41, requestAt(requests), tables(d({{"Column.status", col(1, 3, 701)},
        {"Column.up", col(0, 0, 702)}, {"Column.torrenthash", col(1, 1, 703)},
        {"SortColumns", l({b("up")})}}), d({{"Column.status", col(1, -1, 704)}})));
    QVERIFY(resetInsideHeader);
    QVERIFY(state(window.incompleteTable()) == baseline); // No later hide/order/width/sort can overwrite Reset.
    QCOMPARE(window.completeTable().horizontalHeader()->sectionSize(1), 704);
    QCOMPARE(requests.count(), 1); // Reset remains local and does not reread the profile.
  }

  void directPredicateResetCannotAuthorizeStaleApplication() {
    ui::LibraryWindow window; prepare(window);
    auto* object = window.incompleteTable().findChild<QObject*>(QStringLiteral("classic-columns-incomplete-controller"));
    QVERIFY(object);
    auto* controller = static_cast<ui::ClassicColumns*>(object); // No Q_OBJECT on this concrete controller.
    const auto revision = controller->editRevision(); QVERIFY(revision);
    const auto baseline = state(window.incompleteTable());
    const auto snapshot = projected(tables(d({{"Column.status", col(0, 0, 711)},
        {"Column.up", col(0, 1, 712)}, {"SortColumns", l({b("up")})}})));
    int callbacks = 0;
    const std::function<bool()> predicate = [&] { ++callbacks; controller->reset(); return true; };
    QVERIFY(!controller->applyLayout(snapshot->incomplete, *revision, predicate));
    QCOMPARE(callbacks, 1);
    QVERIFY(controller->editRevision() && *controller->editRevision() > *revision);
    QVERIFY(state(window.incompleteTable()) == baseline);
  }

  void directPredicateDeletionCannotResumeApplication() {
    auto owner = std::make_unique<ui::LibraryWindow>(); prepare(*owner);
    QPointer windowGuard(owner.get());
    auto* object = owner->incompleteTable().findChild<QObject*>(QStringLiteral("classic-columns-incomplete-controller"));
    QVERIFY(object); QPointer<QObject> controllerGuard(object);
    auto* controller = static_cast<ui::ClassicColumns*>(object);
    const auto revision = controller->editRevision(); QVERIFY(revision);
    const auto snapshot = projected(tables(d({{"Column.status", col(0, 0, 713)}})));
    int callbacks = 0;
    const std::function<bool()> predicate = [&] { ++callbacks; owner.reset(); return true; };
    const bool applied = controller->applyLayout(snapshot->incomplete, *revision, predicate);
    QVERIFY(!applied); QCOMPARE(callbacks, 1);
    QVERIFY(windowGuard.isNull()); QVERIFY(controllerGuard.isNull());
    QCoreApplication::processEvents();
  }

  void failedDispatchWithoutBusyOrPacketReleasesItsRequest() {
    ui::LibraryWindow window; prepare(window); // Deliberately no accepting/busy-setting owner.
    QSignalSpy requests(&window, &ui::LibraryWindow::columnLayoutsRequested);
    QVERIFY(start(window, requests)); const auto first = requestAt(requests);
    QCoreApplication::processEvents();
    QVERIFY(!window.cancelAction().isEnabled());
    auto* reload = action(window, "classic-reload-column-layout"); QVERIFY(reload); QVERIFY(reload->isEnabled());
    reload->trigger(); QTRY_COMPARE(requests.count(), 2);
    QVERIFY(requestAt(requests, 1) > first); QVERIFY(!window.cancelAction().isEnabled());
    const auto baseline = state(window.incompleteTable());
    window.setColumnLayouts(ready(41, first, tables(d({{"Column.status", col(0, 0, 714)}}))));
    QVERIFY(state(window.incompleteTable()) == baseline); // Rejected work cannot retarget a newer request.
  }

  void deletionFromPublicRequestSlotIsGuarded() {
    auto owner = std::make_unique<ui::LibraryWindow>(); QPointer guard(owner.get());
    int requests = 0;
    QObject::connect(owner.get(), &ui::LibraryWindow::columnLayoutsRequested, qApp,
        [&](quint64, quint64) { ++requests; owner.reset(); }, Qt::DirectConnection);
    owner->beginColumnLayoutSession(41);
    QTRY_VERIFY(guard.isNull()); QCOMPARE(requests, 1);
    QCoreApplication::processEvents();
  }
};

QTEST_MAIN(ClassicLayoutImportTests)
#include "classic_layout_import_tests.moc"

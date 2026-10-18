#include "classic_columns.hpp"
#include "classic_filter.hpp"
#include "library_window.hpp"

#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QtTest>

#include <memory>

namespace {
using namespace biglybtc;

std::shared_ptr<ui::LibraryPresentation> sample(quint64 revision = 1) {
  auto value = std::make_shared<ui::LibraryPresentation>();
  value->revision = revision;
  const QStringList names{QStringLiteral("Alpha.bin"), QStringLiteral("Beta.bin"),
      QStringLiteral("Gamma.bin"), QStringLiteral("Complete.bin")};
  const qint64 sizes[]{1000, 9, 100, 100};
  for (int row = 0; row < names.size(); ++row) {
    const auto hash = QStringLiteral("%1").arg(row + 1, 40, 16, QChar('0')).toUpper();
    value->items.push_back({.key = hash, .hash = hash, .name = names[row],
        .state = row == 3 ? QStringLiteral("Stopped") : QStringLiteral("Queued"),
        .bytes = sizes[row], .completed = row == 3 ? 100 : 2, .received = 100, .sent = 0,
        .canContinue = row != 3});
  }
  return value;
}

QString prefix(bool complete) {
  return complete ? QStringLiteral("classic-columns-complete") : QStringLiteral("classic-columns-incomplete");
}

QAction* columnAction(QTableView& table, bool complete, int logical) {
  return table.findChild<QAction*>(prefix(complete) + QStringLiteral("-column-%1").arg(logical));
}

QAction* resetAction(QTableView& table, bool complete) {
  return table.findChild<QAction*>(prefix(complete) + QStringLiteral("-reset"));
}

void configure(QTableView& table, ui::LibraryModel& model, bool complete, bool customOrder = false) {
  auto* filter = new ui::ClassicFilter(complete, &table);
  filter->setSourceModel(&model);
  table.setModel(filter);
  table.resize(1100, 320);
  table.setSelectionBehavior(QAbstractItemView::SelectRows);
  table.setSelectionMode(QAbstractItemView::ExtendedSelection);
  table.setSortingEnabled(true);
  table.sortByColumn(0, Qt::AscendingOrder);
  auto* header = table.horizontalHeader();
  header->setSectionsMovable(true);
  header->setSectionResizeMode(QHeaderView::Interactive);
  for (int logical = 0; logical < model.columnCount(); ++logical) { header->resizeSection(logical, 96 + logical * 11); }
  table.setColumnHidden(complete ? 4 : 5, true);
  table.setColumnHidden(8, true);
  if (customOrder) { header->moveSection(header->visualIndex(6), 1); }
}

struct Tables {
  ui::LibraryModel model;
  QTableView incomplete;
  QTableView complete;
  ui::ClassicColumns* incompleteColumns;
  ui::ClassicColumns* completeColumns;

  explicit Tables(bool customOrder = false) {
    model.apply(sample());
    configure(incomplete, model, false, customOrder);
    configure(complete, model, true, customOrder);
    incompleteColumns = new ui::ClassicColumns(incomplete, false);
    completeColumns = new ui::ClassicColumns(complete, true);
  }
};

QSortFilterProxyModel* proxy(QTableView& table) { return qobject_cast<QSortFilterProxyModel*>(table.model()); }

bool choose(QTableView& table, ui::LibraryModel& model, int sourceRow) {
  auto* filter = proxy(table);
  if (!filter) { return false; }
  const auto index = filter->mapFromSource(model.index(sourceRow, 0));
  if (!index.isValid()) { return false; }
  table.selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  return table.currentIndex() == index && table.selectionModel()->isSelected(index);
}

std::optional<ui::Target> selected(QTableView& table, ui::LibraryModel& model) {
  auto* filter = proxy(table);
  return filter && table.currentIndex().isValid()
      ? model.targetAt(filter->mapToSource(table.currentIndex()).row()) : std::nullopt;
}

bool requestHeaderMenu(QTableView& table) {
  auto* header = table.horizontalHeader();
  return QMetaObject::invokeMethod(header, "customContextMenuRequested", Qt::DirectConnection,
      Q_ARG(QPoint, QPoint(10, header->viewport()->height() / 2)));
}

bool containsMenu(const QMenu& root, const QMenu* wanted, int remaining = 8) {
  if (remaining <= 0) { return false; }
  for (auto* action : root.actions()) {
    if (action->menu() == wanted) { return true; }
    if (action->menu() && containsMenu(*action->menu(), wanted, remaining - 1)) { return true; }
  }
  return false;
}
} // namespace

class ColumnTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }

  void perPaneDefaultsAndRealColumnActions_data() {
    QTest::addColumn<bool>("complete");
    QTest::newRow("incomplete-download-and-upload") << false;
    QTest::newRow("complete-upload-only") << true;
  }

  void perPaneDefaultsAndRealColumnActions() {
    QFETCH(bool, complete);
    Tables fixture;
    auto& table = complete ? fixture.complete : fixture.incomplete;
    auto* columns = complete ? fixture.completeColumns : fixture.incompleteColumns;
    QCOMPARE(columns->parent(), &table);
    QCOMPARE(columns->objectName(), prefix(complete) + QStringLiteral("-controller"));
    QVERIFY(columns->menu());
    QCOMPARE(columns->menu()->objectName(), prefix(complete) + QStringLiteral("-menu"));
    QCOMPARE(table.horizontalHeader()->contextMenuPolicy(), Qt::CustomContextMenu);
    QCOMPARE(table.model()->columnCount(), 10);
    for (int logical = 0; logical < table.model()->columnCount(); ++logical) {
      auto* action = columnAction(table, complete, logical);
      QVERIFY(action);
      QVERIFY(action->isCheckable());
      QVERIFY(action->isEnabled());
      QCOMPARE(action->text(), fixture.model.headerData(logical, Qt::Horizontal, Qt::DisplayRole).toString());
      QCOMPARE(action->isChecked(), !table.isColumnHidden(logical));
      const bool hidden = logical == 8 || (complete && logical == 4);
      QCOMPARE(table.isColumnHidden(logical), hidden);
    }
    QVERIFY(!table.isColumnHidden(5));
    QCOMPARE(table.isColumnHidden(4), complete);
    QVERIFY(resetAction(table, complete));
  }

  void actualHeaderPopupTogglesUploadColumn() {
    Tables fixture;
    auto& table = fixture.incomplete;
    table.show();
    QVERIFY(choose(table, fixture.model, 0));
    const auto target = fixture.model.targetAt(0).value();
    auto* menu = fixture.incompleteColumns->menu();
    auto* upload = columnAction(table, false, 5);
    QVERIFY(menu);
    QVERIFY(upload);
    QSignalSpy resets(&fixture.model, &QAbstractItemModel::modelReset);
    QSignalSpy toggles(upload, &QAction::toggled);
    QVERIFY(requestHeaderMenu(table));
    QTRY_VERIFY(menu->isVisible());
    const auto rect = menu->actionGeometry(upload);
    QVERIFY(rect.isValid());
    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, rect.center());
    QTRY_VERIFY(table.isColumnHidden(5));
    QVERIFY(!upload->isChecked());
    QCOMPARE(toggles.count(), 1);
    QVERIFY(requestHeaderMenu(table));
    QTRY_VERIFY(menu->isVisible());
    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(upload).center());
    QTRY_VERIFY(!table.isColumnHidden(5));
    QVERIFY(upload->isChecked());
    QCOMPARE(toggles.count(), 2);
    QCOMPARE(resets.count(), 0);
    QVERIFY(selected(table, fixture.model) == target);
    QCOMPARE(fixture.model.rowCount(), 4);
  }

  void panesKeepIndependentChoicesAndReset() {
    Tables fixture;
    auto* incompleteState = columnAction(fixture.incomplete, false, 1);
    auto* completeHash = columnAction(fixture.complete, true, 8);
    QVERIFY(incompleteState);
    QVERIFY(completeHash);
    QVERIFY(fixture.incompleteColumns->menu() != fixture.completeColumns->menu());
    incompleteState->trigger();
    QVERIFY(fixture.incomplete.isColumnHidden(1));
    QVERIFY(!fixture.complete.isColumnHidden(1));
    completeHash->trigger();
    QVERIFY(!fixture.complete.isColumnHidden(8));
    QVERIFY(fixture.incomplete.isColumnHidden(8));
    fixture.incompleteColumns->reset();
    QVERIFY(!fixture.incomplete.isColumnHidden(1));
    QVERIFY(!fixture.complete.isColumnHidden(8));
    QVERIFY(!fixture.incomplete.isColumnHidden(4));
    QVERIFY(!fixture.incomplete.isColumnHidden(5));
    QVERIFY(fixture.complete.isColumnHidden(4));
    QVERIFY(!fixture.complete.isColumnHidden(5));
    QCOMPARE(fixture.incomplete.model()->rowCount(), 3);
    QCOMPARE(fixture.complete.model()->rowCount(), 1);
  }

  void numericSortSelectionAndRevisionSurviveColumnOperations() {
    Tables fixture;
    auto& table = fixture.incomplete;
    QAbstractItemModelTester tester(&fixture.model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    table.sortByColumn(2, Qt::AscendingOrder);
    auto* filter = proxy(table);
    QVERIFY(filter);
    QCOMPARE(filter->data(filter->index(0, 2), Qt::UserRole).toLongLong(), qint64{9});
    QCOMPARE(filter->data(filter->index(1, 2), Qt::UserRole).toLongLong(), qint64{100});
    QCOMPARE(filter->data(filter->index(2, 2), Qt::UserRole).toLongLong(), qint64{1000});
    QVERIFY(choose(table, fixture.model, 0));
    const auto old = fixture.model.targetAt(0).value();
    const auto currentIndex = table.currentIndex();
    auto* tableModel = table.model();
    QSignalSpy resets(&fixture.model, &QAbstractItemModel::modelReset);
    auto* header = table.horizontalHeader();
    header->moveSection(header->visualIndex(2), 0);
    auto* received = columnAction(table, false, 6);
    QVERIFY(received);
    received->trigger();
    QVERIFY(table.isColumnHidden(6));
    QCOMPARE(table.currentIndex(), currentIndex);
    QVERIFY(selected(table, fixture.model) == old);
    QVERIFY(fixture.model.apply(sample(2)));
    const auto refreshed = selected(table, fixture.model);
    QVERIFY(refreshed.has_value());
    QCOMPARE(refreshed->hash, old.hash);
    QCOMPARE(refreshed->generation, quint64{2});
    QVERIFY(!fixture.model.accepts(old));
    QVERIFY(table.isColumnHidden(6));
    QCOMPARE(header->visualIndex(2), 0);
    fixture.incompleteColumns->reset();
    QCOMPARE(table.model(), tableModel);
    QCOMPARE(header->sortIndicatorSection(), 2);
    QCOMPARE(header->sortIndicatorOrder(), Qt::AscendingOrder);
    QCOMPARE(table.currentIndex(), currentIndex);
    QVERIFY(selected(table, fixture.model) == refreshed);
    QCOMPARE(table.selectionModel()->selectedRows().size(), 1);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(filter->data(filter->index(0, 2), Qt::UserRole).toLongLong(), qint64{9});
    QCOMPARE(filter->data(filter->index(2, 2), Qt::UserRole).toLongLong(), qint64{1000});
  }

  void resetRestoresConstructionOrderAndHiddenWidths() {
    Tables fixture(true);
    auto& table = fixture.incomplete;
    auto* header = table.horizontalHeader();
    const int originalOrder[]{0, 6, 1, 2, 3, 4, 5, 7, 8, 9};
    for (int visual = 0; visual < ui::LibraryModel::Columns; ++visual) { QCOMPARE(header->logicalIndex(visual), originalOrder[visual]); }
    table.sortByColumn(2, Qt::DescendingOrder);
    for (int logical = 0; logical < ui::LibraryModel::Columns; ++logical) { header->resizeSection(logical, 240 + logical * 7); }
    header->moveSection(0, 8);
    table.setColumnHidden(1, true);
    table.setColumnHidden(8, false);
    auto* reset = resetAction(table, false);
    QVERIFY(reset);
    reset->trigger();
    for (int visual = 0; visual < ui::LibraryModel::Columns; ++visual) { QCOMPARE(header->logicalIndex(visual), originalOrder[visual]); }
    for (int logical = 0; logical < ui::LibraryModel::Columns; ++logical) {
      QCOMPARE(table.isColumnHidden(logical), logical == 8);
      const bool hidden = table.isColumnHidden(logical);
      if (hidden) { table.showColumn(logical); }
      QCOMPARE(header->sectionSize(logical), 96 + logical * 11);
      if (hidden) { table.hideColumn(logical); }
    }
    QCOMPARE(header->sortIndicatorSection(), 2);
    QCOMPARE(header->sortIndicatorOrder(), Qt::DescendingOrder);
  }

  void externallyChangedVisibilitySynchronizesWithoutToggleSignals() {
    Tables fixture;
    auto& table = fixture.incomplete;
    table.show();
    auto* menu = fixture.incompleteColumns->menu();
    auto* size = columnAction(table, false, 2);
    auto* hash = columnAction(table, false, 8);
    QVERIFY(menu);
    QVERIFY(size);
    QVERIFY(hash);
    QSignalSpy sizeToggles(size, &QAction::toggled);
    QSignalSpy hashToggles(hash, &QAction::toggled);
    table.hideColumn(2);
    table.showColumn(8);
    QVERIFY(requestHeaderMenu(table));
    QTRY_VERIFY(menu->isVisible());
    QVERIFY(!size->isChecked());
    QVERIFY(hash->isChecked());
    QCOMPARE(sizeToggles.count(), 0);
    QCOMPARE(hashToggles.count(), 0);
    QVERIFY(table.isColumnHidden(2));
    QVERIFY(!table.isColumnHidden(8));
    menu->close();
    table.showColumn(2);
    table.hideColumn(8);
    QVERIFY(requestHeaderMenu(table));
    QTRY_VERIFY(menu->isVisible());
    QVERIFY(size->isChecked());
    QVERIFY(!hash->isChecked());
    QCOMPARE(sizeToggles.count(), 0);
    QCOMPARE(hashToggles.count(), 0);
    menu->close();
  }

  void allHiddenCanFocusAndResetWithoutChangingSource() {
    Tables fixture;
    auto& table = fixture.incomplete;
    table.show();
    table.activateWindow();
    QVERIFY(choose(table, fixture.model, 0));
    const auto target = fixture.model.targetAt(0).value();
    auto* tableModel = table.model();
    QSignalSpy resets(&fixture.model, &QAbstractItemModel::modelReset);
    for (int logical = 0; logical < ui::LibraryModel::Columns; ++logical) {
      auto* action = columnAction(table, false, logical);
      QVERIFY(action);
      if (action->isChecked()) { action->trigger(); }
      QVERIFY(table.isColumnHidden(logical));
    }
    QCOMPARE(table.horizontalHeader()->hiddenSectionCount(), ui::LibraryModel::Columns);
    table.setFocus();
    QTRY_VERIFY(table.hasFocus());
    auto* menu = fixture.incompleteColumns->menu();
    auto* reset = resetAction(table, false);
    QVERIFY(menu);
    QVERIFY(reset);
    menu->popup(table.mapToGlobal(QPoint(10, 10)));
    QTRY_VERIFY(menu->isVisible());
    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(reset).center());
    QTRY_COMPARE(table.horizontalHeader()->hiddenSectionCount(), 1);
    QVERIFY(table.isColumnHidden(8));
    QCOMPARE(table.model(), tableModel);
    QCOMPARE(fixture.model.rowCount(), 4);
    QVERIFY(fixture.model.accepts(target));
    QVERIFY(selected(table, fixture.model) == target);
    QCOMPARE(resets.count(), 0);
  }

  void controllerDeletionDisconnectsQueuedHeaderPopup() {
    ui::LibraryModel model;
    QVERIFY(model.apply(sample()));
    QTableView table;
    configure(table, model, false);
    auto* columns = new ui::ClassicColumns(table, false);
    QPointer<ui::ClassicColumns> guardedColumns(columns);
    QPointer<QMenu> menu(columns->menu());
    QPointer<QAction> upload(columnAction(table, false, 5));
    QVERIFY(menu);
    QVERIFY(upload);
    QVERIFY(QMetaObject::invokeMethod(table.horizontalHeader(), "customContextMenuRequested", Qt::QueuedConnection,
        Q_ARG(QPoint, QPoint(10, 10))));
    delete columns;
    QVERIFY(guardedColumns.isNull());
    QVERIFY(menu.isNull());
    QVERIFY(upload.isNull());
    QCoreApplication::processEvents();
    QCOMPARE(table.model()->rowCount(), 3);
    QCOMPARE(table.findChildren<QMenu*>().size(), 0);
    QVERIFY(requestHeaderMenu(table));
    QCoreApplication::processEvents();
    QCOMPARE(table.findChildren<QMenu*>().size(), 0);
  }

  void tableDeletionDisposesVisibleMenuAndPendingCallback() {
    ui::LibraryModel model;
    QVERIFY(model.apply(sample()));
    auto table = std::make_unique<QTableView>();
    configure(*table, model, false);
    auto* columns = new ui::ClassicColumns(*table, false);
    QPointer<QTableView> guardedTable(table.get());
    QPointer<ui::ClassicColumns> guardedColumns(columns);
    QPointer<QMenu> menu(columns->menu());
    QPointer<QAction> reset(resetAction(*table, false));
    table->show();
    QVERIFY(requestHeaderMenu(*table));
    QTRY_VERIFY(menu->isVisible());
    QVERIFY(QMetaObject::invokeMethod(table->horizontalHeader(), "customContextMenuRequested", Qt::QueuedConnection,
        Q_ARG(QPoint, QPoint(10, 10))));
    table.reset();
    QVERIFY(guardedTable.isNull());
    QVERIFY(guardedColumns.isNull());
    QVERIFY(menu.isNull());
    QVERIFY(reset.isNull());
    QCoreApplication::processEvents();
    QCOMPARE(model.rowCount(), 4);
  }

  void windowViewMenuKeepsBothChoosersReachable() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    auto* incompleteMenu = window.findChild<QMenu*>(QStringLiteral("classic-columns-incomplete-menu"));
    auto* completeMenu = window.findChild<QMenu*>(QStringLiteral("classic-columns-complete-menu"));
    QVERIFY(incompleteMenu);
    QVERIFY(completeMenu);
    QMenu* view = nullptr;
    for (auto* action : window.menuBar()->actions()) {
      if (action->text().remove(QChar('&')) == QStringLiteral("View")) { view = action->menu(); }
    }
    QVERIFY(view);
    QVERIFY(containsMenu(*view, incompleteMenu));
    QVERIFY(containsMenu(*view, completeMenu));
    QVERIFY(!window.incompleteTable().isColumnHidden(4));
    QVERIFY(!window.incompleteTable().isColumnHidden(5));
    QVERIFY(window.completeTable().isColumnHidden(4));
    QVERIFY(!window.completeTable().isColumnHidden(5));
    for (int logical = 0; logical < ui::LibraryModel::Columns; ++logical) { window.incompleteTable().hideColumn(logical); }
    auto* reset = resetAction(window.incompleteTable(), false);
    QVERIFY(reset);
    reset->trigger();
    QCOMPARE(window.incompleteTable().horizontalHeader()->hiddenSectionCount(), 1);
    QCOMPARE(window.completeTable().horizontalHeader()->hiddenSectionCount(), 2);
  }
};

QTEST_MAIN(ColumnTests)
#include "column_tests.moc"

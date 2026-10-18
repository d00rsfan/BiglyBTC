#include "classic_columns.hpp"

#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPointer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QtTest>

#include <algorithm>
#include <memory>

namespace {
using namespace biglybtc;

core::ClassicPanePreferences requested() {
  core::ClassicPanePreferences result;
  for (std::size_t logical = 0; logical < core::ClassicPreferenceIds.size(); ++logical) {
    result.columns[logical] = {logical % 3 != 0, logical == 0 ? std::nullopt : std::optional<int>{100 + static_cast<int>(logical) * 7}};
    result.order[logical] = core::ClassicPreferenceIds[core::ClassicPreferenceIds.size() - logical - 1];
  }
  result.columns[0].visible = true;
  result.sortId = "saved-selected-done";
  result.ascending = false;
  return result;
}

struct Fixture {
  QStandardItemModel model{3, 13};
  QSortFilterProxyModel proxy;
  std::unique_ptr<QTableView> table = std::make_unique<QTableView>();
  QPointer<ui::ClassicColumns> columns;

  explicit Fixture(bool complete = false) {
    for (int row = 0; row < 3; ++row) {
      for (int logical = 0; logical < 13; ++logical) {
        model.setData(model.index(row, logical), (row + 1) * (logical + 1));
      }
    }
    proxy.setSourceModel(&model);
    table->setModel(&proxy);
    table->setSortingEnabled(true);
    table->sortByColumn(0, Qt::AscendingOrder);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    auto* header = table->horizontalHeader();
    header->setSectionsMovable(true);
    header->setMinimumSectionSize(16);
    header->setSectionResizeMode(QHeaderView::Interactive);
    for (int logical = 0; logical < 13; ++logical) { header->resizeSection(logical, 96 + logical * 11); }
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int logical = 10; logical < 13; ++logical) { table->setColumnHidden(logical, true); }
    columns = new ui::ClassicColumns(*table, complete);
    table->resize(1400, 240);
    table->show();
    QCoreApplication::processEvents();
  }
};

QAction* choice(Fixture& fixture, bool complete, int logical) {
  const auto prefix = complete ? QStringLiteral("classic-columns-complete") : QStringLiteral("classic-columns-incomplete");
  return fixture.table->findChild<QAction*>(prefix + QStringLiteral("-column-%1").arg(logical));
}

bool equalCaptured(ui::ClassicColumns& columns, const core::ClassicPanePreferences& expected) {
  const auto actual = columns.capturePreferences();
  return actual && *actual == expected;
}
} // namespace

class ClassicPreferencesColumnsTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }

  void captureRestoreAllThirteen_data() {
    QTest::addColumn<bool>("complete");
    QTest::newRow("incomplete") << false;
    QTest::newRow("complete") << true;
  }
  void captureRestoreAllThirteen() {
    QFETCH(bool, complete);
    Fixture fixture(complete);
    const auto preferences = requested();
    const auto revision = fixture.columns->editRevision();
    QVERIFY(revision);
    QVERIFY(fixture.columns->applyPreferences(preferences, *revision));
    QCOMPARE(fixture.columns->editRevision(), revision);
    QVERIFY(equalCaptured(*fixture.columns, preferences));
    auto* header = fixture.table->horizontalHeader();
    QCOMPARE(header->sectionResizeMode(0), QHeaderView::Stretch);
    for (int logical = 0; logical < 13; ++logical) {
      const auto index = static_cast<std::size_t>(logical);
      QCOMPARE(fixture.table->isColumnHidden(logical), !preferences.columns[index].visible);
      QCOMPARE(header->logicalIndex(logical), 12 - logical);
      QVERIFY(choice(fixture, complete, logical));
      QCOMPARE(choice(fixture, complete, logical)->isChecked(), preferences.columns[index].visible);
    }
    QCOMPARE(header->sortIndicatorSection(), 12);
    QCOMPARE(header->sortIndicatorOrder(), Qt::DescendingOrder);
    Fixture restored(complete);
    QVERIFY(restored.columns->applyPreferences(*fixture.columns->capturePreferences(), *restored.columns->editRevision()));
    QVERIFY(equalCaptured(*restored.columns, preferences));
  }

  void captureIsReadOnlyIncludingHiddenManualWidth() {
    Fixture fixture;
    auto* header = fixture.table->horizontalHeader();
    header->resizeSection(11, 331);
    header->hideSection(11);
    header->resizeSection(11, 407); // A hidden resize must override the old visible width.
    QCOMPARE(header->sectionSize(11), 0);
    const auto state = header->saveState();
    const auto revision = fixture.columns->editRevision();
    QSignalSpy moved(header, &QHeaderView::sectionMoved);
    QSignalSpy resized(header, &QHeaderView::sectionResized);
    QSignalSpy sorted(header, &QHeaderView::sortIndicatorChanged);
    QSignalSpy reset(&fixture.model, &QAbstractItemModel::modelReset);
    const auto captured = fixture.columns->capturePreferences();
    QVERIFY(captured);
    QCOMPARE(captured->columns[11].width, std::optional<int>{407});
    QVERIFY(!captured->columns[11].visible);
    QVERIFY(!captured->columns[0].width);
    QCOMPARE(header->saveState(), state);
    QCOMPARE(fixture.columns->editRevision(), revision);
    QCOMPARE(moved.count(), 0);
    QCOMPARE(resized.count(), 0);
    QCOMPARE(sorted.count(), 0);
    QCOMPARE(reset.count(), 0);
  }

  void hiddenWidthsRoundtripAndChooserUnhidesRememberedSize() {
    Fixture fixture;
    auto preferences = requested();
    for (auto& column : preferences.columns) { column.visible = false; }
    preferences.columns[8].width = 719;
    QVERIFY(fixture.columns->applyPreferences(preferences, *fixture.columns->editRevision()));
    QVERIFY(equalCaptured(*fixture.columns, preferences));
    for (int logical = 0; logical < 13; ++logical) { QVERIFY(fixture.table->isColumnHidden(logical)); }
    auto* hash = choice(fixture, false, 8);
    QVERIFY(hash);
    const auto revision = fixture.columns->editRevision();
    hash->trigger();
    QVERIFY(!fixture.table->isColumnHidden(8));
    QCOMPARE(fixture.table->horizontalHeader()->sectionSize(8), 719);
    QVERIFY(fixture.columns->editRevision() > revision);
    QVERIFY(fixture.table->isColumnHidden(0));
  }

  void nameStretchWidthIsNeverSampled() {
    Fixture fixture;
    fixture.table->resize(18000, 240);
    QCoreApplication::processEvents();
    QVERIFY(fixture.table->horizontalHeader()->sectionSize(0) > 8192);
    const auto capture = fixture.columns->capturePreferences();
    QVERIFY(capture);
    QVERIFY(!capture->columns[0].width);
    QVERIFY(fixture.columns->applyPreferences(*capture, *fixture.columns->editRevision()));
    QCOMPARE(fixture.table->horizontalHeader()->sectionResizeMode(0), QHeaderView::Stretch);
  }

  void resetKeepsOriginalConstructionBaseline() {
    Fixture fixture;
    const auto baseline = fixture.columns->capturePreferences();
    QVERIFY(baseline);
    QVERIFY(fixture.columns->applyPreferences(requested(), *fixture.columns->editRevision()));
    fixture.columns->reset();
    const auto after = fixture.columns->capturePreferences();
    QVERIFY(after);
    QCOMPARE(after->columns, baseline->columns);
    QCOMPARE(after->order, baseline->order);
    // Existing Reset deliberately retains the current sort rather than inventing a baseline sort.
    QCOMPARE(after->sortId, std::string{"saved-selected-done"});
    QVERIFY(!after->ascending);
  }

  void invalidPreferencesAreAtomic_data() {
    QTest::addColumn<int>("invalid");
    QTest::newRow("name-width") << 0;
    QTest::newRow("missing-width") << 1;
    QTest::newRow("width-below-minimum") << 2;
    QTest::newRow("width-above-limit") << 3;
    QTest::newRow("duplicate-order") << 4;
    QTest::newRow("unknown-order") << 5;
    QTest::newRow("unknown-sort") << 6;
  }
  void invalidPreferencesAreAtomic() {
    QFETCH(int, invalid);
    Fixture fixture;
    auto preferences = requested();
    switch (invalid) {
      case 0: preferences.columns[0].width = 100; break;
      case 1: preferences.columns[9].width.reset(); break;
      case 2: preferences.columns[1].width = 15; break;
      case 3: preferences.columns[12].width = 8193; break;
      case 4: preferences.order[1] = preferences.order[0]; break;
      case 5: preferences.order[5] = "plugin-column"; break;
      default: preferences.sortId = "status"; break; // Java identifier is not the private native state ID.
    }
    const auto state = fixture.table->horizontalHeader()->saveState();
    const auto revision = fixture.columns->editRevision();
    QVERIFY(!fixture.columns->applyPreferences(preferences, *revision));
    QCOMPARE(fixture.table->horizontalHeader()->saveState(), state);
    QCOMPARE(fixture.columns->editRevision(), revision);
  }

  void exactWidthBoundariesAndUncapturableWidths() {
    Fixture fixture;
    auto preferences = requested();
    preferences.columns[1].width = 16;
    preferences.columns[12].width = 8192;
    QVERIFY(fixture.columns->applyPreferences(preferences, *fixture.columns->editRevision()));
    QVERIFY(equalCaptured(*fixture.columns, preferences));
    auto* header = fixture.table->horizontalHeader();
    header->hideSection(11);
    header->resizeSection(11, 8193);
    QVERIFY(!fixture.columns->capturePreferences());
    header->resizeSection(11, 16);
    QVERIFY(fixture.columns->capturePreferences());
    header->setMinimumSectionSize(0);
    header->resizeSection(11, 15);
    QVERIFY(!fixture.columns->capturePreferences());
  }

  void staleRevisionRejectsAndProgrammaticApplyDoesNotEdit() {
    Fixture fixture;
    const auto old = fixture.columns->editRevision();
    fixture.table->horizontalHeader()->resizeSection(1, 301);
    QVERIFY(fixture.columns->editRevision() != old);
    const auto before = fixture.columns->capturePreferences();
    QVERIFY(before);
    QVERIFY(!fixture.columns->applyPreferences(requested(), *old));
    QVERIFY(equalCaptured(*fixture.columns, *before));
    const auto current = fixture.columns->editRevision();
    QVERIFY(fixture.columns->applyPreferences(requested(), *current));
    QCOMPARE(fixture.columns->editRevision(), current);
  }

  void preservesModelSelectionAndPaneIndependence() {
    Fixture fixture;
    Fixture other(true);
    fixture.table->selectRow(1);
    const QPersistentModelIndex selected = fixture.proxy.mapToSource(fixture.table->currentIndex());
    QVERIFY(selected.isValid());
    const auto untouched = other.columns->capturePreferences();
    auto* const model = fixture.table->model();
    QSignalSpy resets(&fixture.model, &QAbstractItemModel::modelReset);
    QVERIFY(fixture.columns->applyPreferences(requested(), *fixture.columns->editRevision()));
    QCOMPARE(fixture.table->model(), model);
    QCOMPARE(fixture.proxy.mapToSource(fixture.table->currentIndex()), QModelIndex(selected));
    QCOMPARE(fixture.table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(resets.count(), 0);
    QVERIFY(untouched && equalCaptured(*other.columns, *untouched));
  }

  void unsupportedHeaderCountsFailClosed_data() {
    QTest::addColumn<int>("count");
    QTest::newRow("empty") << 0;
    QTest::newRow("missing") << 12;
    QTest::newRow("extra") << 14;
  }
  void unsupportedHeaderCountsFailClosed() {
    QFETCH(int, count);
    Fixture fixture;
    fixture.model.setColumnCount(count);
    QVERIFY(!fixture.columns->capturePreferences());
    QVERIFY(!fixture.columns->applyPreferences(requested(), *fixture.columns->editRevision()));
  }

  void falsePredicateRejectsBeforeAnyChange() {
    Fixture fixture;
    const auto state = fixture.table->horizontalHeader()->saveState();
    int calls = 0;
    QVERIFY(!fixture.columns->applyPreferences(requested(), *fixture.columns->editRevision(), [&] { ++calls; return false; }));
    QCOMPARE(calls, 1);
    QCOMPARE(fixture.table->horizontalHeader()->saveState(), state);
  }

  void predicateResetReturningTrueStillAborts() {
    Fixture fixture;
    const auto baseline = fixture.columns->capturePreferences();
    QVERIFY(baseline);
    const auto revision = fixture.columns->editRevision();
    QVERIFY(!fixture.columns->applyPreferences(requested(), *revision, [&] { fixture.columns->reset(); return true; }));
    QVERIFY(equalCaptured(*fixture.columns, *baseline));
    QVERIFY(fixture.columns->editRevision() > revision);
  }

  void predicateDeletesControllerReturningTrueStillAborts() {
    Fixture fixture;
    auto* columns = fixture.columns.data();
    const auto revision = columns->editRevision();
    QVERIFY(!columns->applyPreferences(requested(), *revision, [&] { delete columns; return true; }));
    QVERIFY(!fixture.columns);
    QVERIFY(fixture.table);
  }

  void predicateDeletesTableReturningTrueStillAborts() {
    Fixture fixture;
    auto* columns = fixture.columns.data();
    const auto revision = columns->editRevision();
    QVERIFY(!columns->applyPreferences(requested(), *revision, [&] { fixture.table.reset(); return true; }));
    QVERIFY(!fixture.columns);
    QVERIFY(!fixture.table);
  }

  void directHeaderResetWinsRemainingChanges() {
    Fixture fixture;
    const auto baseline = fixture.columns->capturePreferences();
    QVERIFY(baseline);
    bool reset = false;
    auto* header = fixture.table->horizontalHeader();
    connect(header, &QHeaderView::sectionMoved, fixture.table.get(), [&](int, int, int) {
      if (!reset) { reset = true; fixture.columns->reset(); }
    });
    QVERIFY(!fixture.columns->applyPreferences(requested(), *fixture.columns->editRevision()));
    QVERIFY(reset);
    QVERIFY(equalCaptured(*fixture.columns, *baseline));
  }

  void directResizeDeletesTableSafely() {
    Fixture fixture;
    auto preferences = *fixture.columns->capturePreferences();
    preferences.columns[1].width = 339;
    bool deleted = false;
    connect(fixture.table->horizontalHeader(), &QHeaderView::sectionResized, this, [&](int logical, int, int) {
      if (logical == 1 && !deleted) { deleted = true; fixture.table.reset(); }
    });
    auto* columns = fixture.columns.data();
    QVERIFY(!columns->applyPreferences(preferences, *columns->editRevision()));
    QVERIFY(deleted);
    QVERIFY(!fixture.columns);
  }
};

QTEST_MAIN(ClassicPreferencesColumnsTests)
#include "classic_preferences_columns_tests.moc"

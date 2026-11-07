#include <QPixmap>
#include "classic_sort_header.hpp"
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
#include <QTabWidget>
#include <QProgressBar>
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

class SortHeaderProbe final : public ui::ClassicSortHeader {
public:
  using ui::ClassicSortHeader::ClassicSortHeader;
  QStyleOptionHeader option(int column)const{QStyleOptionHeader value;initStyleOption(&value);initStyleOptionForIndex(&value,column);return value;}
};
void configure(QTableView& table, ui::LibraryModel& model, bool complete, bool customOrder = false) {
  table.setHorizontalHeader(new SortHeaderProbe(&table));
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
  for (int logical = 10; logical < ui::LibraryModel::Columns; ++logical) { table.setColumnHidden(logical, true); }
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
  void everyColumnInBothPanesMatchesAscendingAndDescendingArrow(){
    Tables fixture;auto rows=sample(2);
    for(int n=0;n<2;++n){auto copy=rows->items.back();copy.key=copy.hash=QStringLiteral("%1").arg(5+n,40,16,QChar('0')).toUpper();copy.name=QStringLiteral("Owned complete %1").arg(n);rows->items.push_back(copy);}
    const qint64 sizes[]{1073741824LL,512,3145728,1099511627776LL,1024,32505856};
    for(std::size_t n=0;n<rows->items.size();++n){auto& item=rows->items[n];const bool complete=n>=3;item.bytes=sizes[n];item.completed=complete?*item.bytes:*item.bytes/2;item.assumedComplete=complete;item.received=static_cast<qint64>(n*31);item.sent=static_cast<qint64>((7-n)*17);item.savedPosition=static_cast<qint64>(9-n);item.savedSelected=ui::SavedSelectedValues{.bytes=item.bytes,.remaining=complete?0:*item.bytes/2,.percentThousandths=complete?1000:500};item.savePath=QStringLiteral("owned/path/%1").arg(n);item.trackerName=QStringLiteral("owned-%1.invalid").arg(n);}
    QVERIFY(fixture.model.apply(rows));
    for(int n=0;n<fixture.model.rowCount();++n){const auto target=fixture.model.targetAt(n);QVERIFY(target);QVERIFY(fixture.model.apply(ui::Telemetry{*target,*rows->items[static_cast<std::size_t>(n)].completed,n*13,(7-n)*11,rows->items[static_cast<std::size_t>(n)].state}));}
    for(auto* table:{&fixture.incomplete,&fixture.complete}){
      QCOMPARE(table->model()->rowCount(),3);auto* header=dynamic_cast<SortHeaderProbe*>(table->horizontalHeader());QVERIFY(header);
      for(int column=0;column<ui::LibraryModel::Columns;++column){for(const auto order:{Qt::AscendingOrder,Qt::DescendingOrder}){
        table->sortByColumn(column,order);QCOMPARE(header->sortIndicatorOrder(),order);QCOMPARE(header->option(column).sortIndicator,order==Qt::AscendingOrder?QStyleOptionHeader::SortUp:QStyleOptionHeader::SortDown);
        for(int row=1;row<table->model()->rowCount();++row){const auto a=table->model()->index(row-1,column).data(Qt::UserRole),b=table->model()->index(row,column).data(Qt::UserRole);QVERIFY(a.isValid()&&b.isValid());
          const int comparison=a.metaType().id()==QMetaType::QString?QString::compare(a.toString(),b.toString(),proxy(*table)->sortCaseSensitivity()):(a.toDouble()<b.toDouble()?-1:a.toDouble()>b.toDouble()?1:0);
          QVERIFY(order==Qt::AscendingOrder?comparison<=0:comparison>=0);
        }
      }}
    }
    for(auto* table:{&fixture.incomplete,&fixture.complete}){
      table->show();table->setColumnHidden(2,false);table->sortByColumn(2,Qt::AscendingOrder);QCoreApplication::processEvents();auto* header=table->horizontalHeader();QVERIFY(header->sectionsClickable());
      const QPoint point(header->sectionViewportPosition(2)+header->sectionSize(2)/2,header->height()/2);
      QTest::mouseClick(header->viewport(),Qt::LeftButton,Qt::NoModifier,point);QCOMPARE(header->sortIndicatorOrder(),Qt::DescendingOrder);
      QTest::mouseClick(header->viewport(),Qt::LeftButton,Qt::NoModifier,point);QCOMPARE(header->sortIndicatorOrder(),Qt::AscendingOrder);
    }
    const auto evidence=qEnvironmentVariable("BIGLYBTC_UI_EVIDENCE_ROOT");
    if(!evidence.isEmpty()){
      for(auto* table:{&fixture.incomplete,&fixture.complete}){table->show();table->setColumnHidden(2,false);for(const auto order:{Qt::AscendingOrder,Qt::DescendingOrder}){table->sortByColumn(2,order);QCoreApplication::processEvents();QVERIFY(table->horizontalHeader()->grab().save(evidence+'/'+(table==&fixture.complete?QStringLiteral("complete"):QStringLiteral("incomplete"))+QStringLiteral("-size-")+(order==Qt::AscendingOrder?QStringLiteral("ascending"):QStringLiteral("descending"))+QStringLiteral(".png")));}}
    }
    ui::LibraryWindow window;QVERIFY(dynamic_cast<ui::ClassicSortHeader*>(window.incompleteTable().horizontalHeader()));QVERIFY(dynamic_cast<ui::ClassicSortHeader*>(window.completeTable().horizontalHeader()));
  }
  void detachedDetailsOpenByDoubleClickStayScopedAndCloseSafely(){
    ui::LibraryWindow window;auto rows=sample();rows->inlineDetails=false;rows->startupDone=3;rows->startupTotal=10;
    window.resize(1200,900);window.setPresentation(rows);window.setBusy(false);window.show();QCoreApplication::processEvents();
    auto* tabs=window.findChild<QTabWidget*>("classic-main-tabs");auto* inspector=window.findChild<QTabWidget*>("torrent-details");auto* progress=window.findChild<QProgressBar*>("classic-startup-progress");QVERIFY(tabs&&inspector&&progress);
    auto* library=tabs->currentWidget();QVERIFY(!inspector->isVisible());QVERIFY(progress->isVisible());QCOMPARE(progress->value(),3);QCOMPARE(progress->maximum(),10);
    QSignalSpy requests(&window,&ui::LibraryWindow::detailsRequested);
    auto& table=window.incompleteTable();const auto index=table.model()->index(0,0);QVERIFY(index.isValid());const auto point=table.visualRect(index).center();
    QTest::mouseClick(table.viewport(),Qt::LeftButton,Qt::NoModifier,point);QCOMPARE(requests.size(),0);
    const auto firstHash=table.model()->index(0,8).data().toString();
    QTest::mouseDClick(table.viewport(),Qt::LeftButton,Qt::NoModifier,point);QCoreApplication::processEvents();
    QCOMPARE(tabs->count(),2);QCOMPARE(tabs->currentWidget()->property("BiglyBTC.torrentDetailsHash").toString(),firstHash);QCOMPARE(inspector->parentWidget(),tabs->currentWidget());QVERIFY(inspector->isVisible());
    QCOMPARE(requests.size(),0);inspector->setCurrentIndex(1);QCOMPARE(requests.size(),1);
    tabs->setCurrentWidget(library);QCoreApplication::processEvents();QVERIFY(!inspector->isVisible());
    auto& complete=window.completeTable();const auto otherIndex=complete.model()->index(0,0);const auto otherPoint=complete.visualRect(otherIndex).center();
    QTest::mouseClick(complete.viewport(),Qt::LeftButton,Qt::NoModifier,otherPoint);QTest::mouseDClick(complete.viewport(),Qt::LeftButton,Qt::NoModifier,otherPoint);QCoreApplication::processEvents();
    QCOMPARE(tabs->count(),3);const auto secondHash=tabs->currentWidget()->property("BiglyBTC.torrentDetailsHash").toString();QVERIFY(secondHash!=firstHash);
    tabs->setCurrentIndex(1);QCOMPARE(tabs->currentWidget()->property("BiglyBTC.torrentDetailsHash").toString(),firstHash);
    QPointer<QTabWidget> retained(inspector);const int closing=tabs->currentIndex();QVERIFY(QMetaObject::invokeMethod(tabs,"tabCloseRequested",Qt::DirectConnection,Q_ARG(int,closing)));QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(retained);QCOMPARE(tabs->count(),2);
    tabs->setCurrentWidget(library);QVERIFY(!inspector->isVisible());
    auto done=std::make_shared<ui::LibraryPresentation>(*rows);done->revision=2;done->startupDone=10;window.setPresentation(done);QVERIFY(!progress->isVisible());QVERIFY(!inspector->isVisible());
  }
  void humanSizesKeepNumericRolesAndOriginalUnitChoices(){
    QCOMPARE(ui::transferUptime(366LL*86400+3661),QStringLiteral("1y 1d 1h 1m 1s"));
    QCOMPARE(ui::transferUptime(60),QStringLiteral("0d 0h 1m 0s"));
    ui::LibraryModel model;auto rows=sample();rows->items[0].bytes=1099511627776LL;QVERIFY(model.apply(rows));
    QCOMPARE(model.index(0,2).data(Qt::UserRole).toLongLong(),qint64{1099511627776LL});QVERIFY(model.index(0,2).data().toString().contains("TB"));QVERIFY(!model.headerData(2,Qt::Horizontal,Qt::DisplayRole).toString().contains("bytes"));
    rows->revision=2;rows->units.siLabels=true;rows->units.rateBits=true;QVERIFY(model.apply(rows));QVERIFY(model.index(0,2).data().toString().contains("TiB"));
    const auto target=model.targetAt(0);QVERIFY(target);QVERIFY(model.apply(ui::Telemetry{*target,0,1024,1024,"Downloading"}));QCOMPARE(model.index(0,5).data(Qt::UserRole).toLongLong(),qint64{1024});QVERIFY(model.index(0,5).data().toString().contains("Kibit/s"));
  }
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
    QCOMPARE(table.model()->columnCount(), ui::LibraryModel::Columns);
    for (int logical = 0; logical < table.model()->columnCount(); ++logical) {
      auto* action = columnAction(table, complete, logical);
      QVERIFY(action);
      QVERIFY(action->isCheckable());
      QVERIFY(action->isEnabled());
      QCOMPARE(action->text(), fixture.model.headerData(logical, Qt::Horizontal, Qt::DisplayRole).toString());
      QCOMPARE(action->isChecked(), !table.isColumnHidden(logical));
      const bool hidden = logical == 8 || logical >= 10 || (complete && logical == 4);
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
    const int originalOrder[]{0, 6, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14};
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
      QCOMPARE(table.isColumnHidden(logical), logical == 8 || logical >= 10);
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
    QTRY_COMPARE(table.horizontalHeader()->hiddenSectionCount(), 6);
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
    QCOMPARE(window.incompleteTable().horizontalHeader()->hiddenSectionCount(), 6);
    QCOMPARE(window.completeTable().horizontalHeader()->hiddenSectionCount(), 7);
  }
};

QTEST_MAIN(ColumnTests)
#include "column_tests.moc"

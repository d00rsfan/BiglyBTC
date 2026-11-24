// SPDX-License-Identifier: GPL-2.0-or-later
#include "swarm_discoveries_page.hpp"
#include "library_window.hpp"
#include "activity_centre.hpp"
#include <QTabWidget>
#include <QStatusBar>
#include <QTableWidget>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <QTest>
#include <QSignalSpy>
#include <QTimer>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QHeaderView>
#include <QTableView>
#include <QPushButton>
#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QScrollBar>
using namespace biglybtc;
class SwarmPageTests final:public QObject {
 Q_OBJECT
private slots:
 void completedSwarmTagLookupUsesTheCurrentSnapshotOnce() {
  ui::LibraryWindow window;
  ui::SwarmViewSnapshot view;
  view.generation=1;
  view.sequence=1;
  view.tagLookup=ui::SwarmTagLookupView{1,QByteArray(20,'p'),"Looking up generated tags",{},true};
  window.setSwarmSnapshot(view);
  const auto name=QStringLiteral("swarm-tag-lookup-result");
  QVERIFY(!window.findChild<QMessageBox*>(name));
  ++view.sequence;
  view.tagLookup->running=false;
  view.tagLookup->status=QStringLiteral("Lookup finished");
  view.tagLookup->names={QStringLiteral("<fixture>"),QStringLiteral("Second")};
  window.setSwarmSnapshot(view);
  auto* result=window.findChild<QMessageBox*>(name);
  QVERIFY(result);
  QCOMPARE(result->textFormat(),Qt::PlainText);
  QCOMPARE(result->text(),QStringLiteral("<fixture>\nSecond"));
  ++view.sequence;
  window.setSwarmSnapshot(view);
  QCOMPARE(window.findChildren<QMessageBox*>(name).size(),qsizetype{1});
  auto stale=view;
  stale.sequence=1;
  stale.tagLookup->names={QStringLiteral("Stale")};
  window.setSwarmSnapshot(stale);
  QCOMPARE(result->text(),QStringLiteral("<fixture>\nSecond"));
  result->accept();
  QTRY_VERIFY(!window.findChild<QMessageBox*>(name));
 }
 void libraryTagScanDialogUsesTheCurrentProfile() {
  ui::LibraryWindow window;
  window.beginColumnLayoutSession(1);
  auto* action=window.findChild<QAction*>("classic-swarm-discoveries");
  QVERIFY(action);
  action->trigger();
  auto* page=window.findChild<ui::SwarmDiscoveriesPage*>();
  QVERIFY(page);
  auto* open=page->findChild<QPushButton*>("swarm-tag-discovery-open");
  QVERIFY(open);
  QVERIFY(!open->isEnabled());

  ui::SwarmViewSnapshot view;
  view.generation=1;
  view.sequence=1;
  view.profile=std::make_shared<services::related_content::Snapshot>();
  window.setSwarmSnapshot(view);
  QVERIFY(open->isEnabled());
  open->click();
  const auto dialogName=QStringLiteral("swarm-tag-discovery");
  auto* dialog=page->findChild<QDialog*>(dialogName);
  QVERIFY(dialog);
  auto* start=dialog->findChild<QPushButton*>("swarm-tag-scan-start");
  auto* cancel=dialog->findChild<QPushButton*>("swarm-tag-scan-cancel");
  auto* table=dialog->findChild<QTableView*>("swarm-tag-scan-findings");
  auto* filter=dialog->findChild<QLineEdit*>("swarm-tag-scan-filter");
  auto* apply=dialog->findChild<QPushButton*>("swarm-tag-scan-apply");
  auto* feedback=dialog->findChild<QLabel*>("swarm-tag-application-status");
  QVERIFY(start&&cancel&&table&&filter&&apply&&feedback);
  QVERIFY(!apply->isEnabled());
  QVERIFY(start->isEnabled());
  QVERIFY(!cancel->isEnabled());
  QSignalSpy scans(&window,&ui::LibraryWindow::swarmTagScanRequested);
  QSignalSpy cancels(&window,&ui::LibraryWindow::swarmCancelTagScanRequested);
  start->click();
  QCOMPARE(scans.size(),1);

  auto scan=std::make_shared<services::related_content::TagScanSnapshot>();
  scan->request=1;
  scan->total=2;
  scan->finished=1;
  scan->running=true;
  scan->findings=std::make_shared<const std::vector<services::related_content::TagScanFinding>>(
    std::initializer_list<services::related_content::TagScanFinding>{
      {std::string(20,'a'),"Generated title","<b>Example</b>",1784887200000},
      {std::string(20,'b'),"Generated other","Other",1784887200001}});
  view.tagScan=scan;
  ++view.sequence;
  window.setSwarmSnapshot(view);
  QVERIFY(!start->isEnabled());
  QVERIFY(cancel->isEnabled());
  QCOMPARE(table->model()->rowCount(),2);
  QCOMPARE(table->model()->index(0,0).data().toString(),QStringLiteral("<b>Example</b>"));
  QCOMPARE(table->model()->index(0,1).data().toString(),QStringLiteral("Generated title"));
  QCOMPARE(table->model()->index(0,2).data().toString(),QStringLiteral("Public"));
  QSignalSpy requests(&window,&ui::LibraryWindow::swarmTagApplicationRequested);
  table->selectionModel()->select(table->model()->index(1,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);
  table->selectionModel()->select(table->model()->index(0,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);
  QVERIFY(apply->isEnabled());
  apply->click();
  QCOMPARE(requests.size(),1);
  const auto args=requests.front();
  QCOMPARE(args[0].toULongLong(),qulonglong(1));
  using Selections=services::related_content::TagScanSelections;
  const auto rows=qvariant_cast<Selections>(args[1]);
  QCOMPARE(rows.size(),std::size_t{2});
  QVERIFY(scan->contains(rows[0])&&scan->contains(rows[1]));
  QCOMPARE(rows[0].hash,std::string(20,'a'));
  QCOMPARE(rows[0].tag,std::string("<b>Example</b>"));
  QCOMPARE(rows[0].foundOn,std::int64_t{1784887200000});
  QCOMPARE(rows[1].hash,std::string(20,'b'));

  auto updated=std::make_shared<services::related_content::TagScanSnapshot>(*scan);
  auto findings=std::make_shared<std::vector<services::related_content::TagScanFinding>>(*scan->findings);
  findings->insert(findings->begin(),{std::string(20,'c'),"Generated incoming","Incoming",1784887200002});
  updated->findings=findings;
  view.tagScan=updated;
  ++view.sequence;
  window.setSwarmSnapshot(view);
  QCOMPARE(table->selectionModel()->selectedRows().size(),2);
  QVERIFY(!table->selectionModel()->isRowSelected(0,{}));
  QVERIFY(table->selectionModel()->isRowSelected(1,{})&&table->selectionModel()->isRowSelected(2,{}));
  window.setSwarmTagApplicationStatus(2,QStringLiteral("Foreign result"));
  QVERIFY(feedback->isHidden());
  window.setSwarmTagApplicationStatus(1,QStringLiteral("<b>Saved</b>"));
  QCOMPARE(feedback->textFormat(),Qt::PlainText);
  QCOMPARE(feedback->text(),QStringLiteral("<b>Saved</b>"));
  window.setBusy(true);
  QVERIFY(!apply->isEnabled());
  window.setBusy(false);
  QVERIFY(apply->isEnabled());
  filter->setText(QStringLiteral("missing"));
  QCOMPARE(table->model()->rowCount(),0);
  filter->setText(QStringLiteral("generated"));
  QCOMPARE(table->model()->rowCount(),3);

  auto stale=view;
  --stale.sequence;
  stale.tagScan.reset();
  window.setSwarmSnapshot(stale);
  QCOMPARE(table->model()->rowCount(),3);
  cancel->click();
  QCOMPARE(cancels.size(),1);
  ++view.generation;
  view.sequence=1;
  view.tagScan.reset();
  window.setSwarmSnapshot(view);
  QTRY_VERIFY(!page->findChild<QDialog*>(dialogName));
  QCOMPARE(cancels.size(),1);
  open->click();
  dialog=page->findChild<QDialog*>(dialogName);
  QVERIFY(dialog);
  QCOMPARE(dialog->findChild<QTableView*>("swarm-tag-scan-findings")->model()->rowCount(),0);
 }

 void notificationPageRetiresWithItsProfileSession(){ui::LibraryWindow window;window.beginColumnLayoutSession(7);auto* action=window.findChild<QAction*>("classic-activities");QVERIFY(action);QSignalSpy requests(&window,&ui::LibraryWindow::activityCommandRequested);action->trigger();auto* page=window.findChild<ui::ActivityCentre*>();QVERIFY(page);ui::ActivityView view;view.session=7;view.rows.push_back({{"Generated old source",QByteArray(32,'s')},"Generated previous profile notification",{},QDateTime::currentDateTime(),false,false,false});window.setActivities(view);QCOMPARE(page->findChild<QTableWidget*>("activity-table")->rowCount(),1);window.beginColumnLayoutSession(8);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!window.findChild<ui::ActivityCentre*>());action->trigger();page=window.findChild<ui::ActivityCentre*>();QVERIFY(page);QCOMPARE(page->findChild<QTableWidget*>("activity-table")->rowCount(),0);QVERIFY(!requests.empty());QCOMPARE(requests.back()[0].toULongLong(),qulonglong(8));}

 void settingsPreserveAnImportedLargeMaximum(){ui::SwarmDiscoveriesPage page;auto profile=std::make_shared<services::related_content::Snapshot>();profile->settings.maximum=2000000;profile->writable=true;ui::SwarmViewSnapshot view;view.generation=1;view.sequence=1;view.profile=profile;page.setSnapshot(view);auto* settings=page.findChild<QPushButton*>("swarm-settings");QVERIFY(settings);int observed=-1;QTimer::singleShot(0,&page,[&]{if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){if(auto* maximum=dialog->findChild<QSpinBox*>("swarm-maximum")){observed=maximum->value();}dialog->reject();}});settings->click();QCOMPARE(observed,2000000);}

 void restorationProgressUsesTheRealSessionAndDoesNotRegress(){
  ui::LibraryWindow window;
  window.beginColumnLayoutSession(7);
  auto* status=window.statusBar()->findChild<QLabel*>("classic-client-status");
  QVERIFY(status);
  auto snapshot=std::make_shared<ui::LibraryPresentation>();
  snapshot->revision=1;snapshot->startupDone=75;snapshot->startupTotal=100;
  window.setPresentation(snapshot);
  QVERIFY(status->text().startsWith(QStringLiteral("Restoring transfers 75 / 100")));
  window.setMessage(QStringLiteral("Saving settings"));
  QVERIFY(status->text().contains(QStringLiteral("Saving settings")));
  window.setStartupProgress(7,100,100);
  QCOMPARE(status->text(),QStringLiteral("Saving settings"));
  snapshot=std::make_shared<ui::LibraryPresentation>(*snapshot);
  snapshot->revision=2;window.setPresentation(snapshot);
  QVERIFY(!status->text().contains(QStringLiteral("Restoring transfers")));
  window.beginColumnLayoutSession(8);
  window.setStartupProgress(7,1,100);
  QVERIFY(!status->text().contains(QStringLiteral("Restoring transfers")));
  window.setStartupProgress(8,1,10);
  QVERIFY(status->text().startsWith(QStringLiteral("Restoring transfers 1 / 10")));
  window.enableSessionCloseHandshake();window.setBusy(false);
  QSignalSpy close(&window,&ui::LibraryWindow::sessionCloseRequested);
  window.close();
  QCOMPARE(close.size(),1);
  window.setMessage(QStringLiteral("Closing: stopped 2 / 5 torrents"));
  window.setStartupProgress(8,3,10);
  QCOMPARE(status->text(),QStringLiteral("Closing: stopped 2 / 5 torrents"));
 }

 void discoveriesTabCanCloseAndReopen(){ui::LibraryWindow window;auto* action=window.findChild<QAction*>("classic-swarm-discoveries");if(!action){for(auto* candidate:window.findChildren<QAction*>()){if(candidate->text().contains("Swarm Discoveries")){action=candidate;break;}}}QVERIFY(action);action->trigger();auto* page=window.findChild<ui::SwarmDiscoveriesPage*>();QVERIFY(page);auto* tabs=qobject_cast<QTabWidget*>(page->parentWidget()->parentWidget());QVERIFY(tabs);const int index=tabs->indexOf(page);QVERIFY(index>=0);emit tabs->tabCloseRequested(index);QVERIFY(tabs->indexOf(page)<0);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);action->trigger();QVERIFY(window.findChild<ui::SwarmDiscoveriesPage*>());}
 void largeSavedDiscoveriesUseVirtualCells(){ui::SwarmDiscoveriesPage page;auto snapshot=std::make_shared<services::related_content::Snapshot>();snapshot->settings.sources={"*"};snapshot->settings.uiEnabled=true;for(int i=0;i<100005;++i){services::related_content::Item item;item.key=std::to_string(i);item.title="Generated discovery";item.size=i;item.networks=1;snapshot->items.push_back(std::move(item));}ui::SwarmViewSnapshot view;view.generation=1;view.sequence=1;view.profile=std::move(snapshot);
#ifdef __GLIBC__
 const auto before=mallinfo2();const auto allocatedBefore=before.uordblks+before.hblkhd;
#endif
 page.setSnapshot(view);
#ifdef __GLIBC__
 const auto after=mallinfo2();QVERIFY2(after.uordblks+after.hblkhd<=allocatedBefore+96U*1024U*1024U,"Large discovery projection must stay below the per-cell allocation budget");
#endif
 auto* table=page.findChild<QTableView*>("swarm-table");QVERIFY(table);QCOMPARE(table->model()->rowCount(),100005);QCOMPARE(table->model()->index(0,2).data().toString(),QStringLiteral("Generated discovery"));}
 void savedActivitiesKeepSourceReferencesAndDisableUnknownCallbacks(){ui::ActivityCentre page;ui::ActivityView view;view.session=7;view.writable=true;ui::ActivityRowView known{{"Public: Generated room",QByteArray(32,'s')},"Generated notice","Generated room",QDateTime::currentDateTime(),false,false,true};view.rows={known,{{"Generated opaque",QByteArray(32,'o')},"Generated opaque notice",{},QDateTime::currentDateTime(),false,false,false}};page.setSnapshot(view);QSignalSpy open(&page,&ui::ActivityCentre::viewRequested),change(&page,&ui::ActivityCentre::command);auto* table=page.findChild<QTableWidget*>("activity-table");QVERIFY(table);table->selectRow(1);page.findChild<QPushButton*>("activity-view")->click();QCOMPARE(open.size(),0);table->selectRow(0);page.findChild<QPushButton*>("activity-view")->click();QCOMPARE(open.size(),1);QCOMPARE(open[0][0].toULongLong(),qulonglong(7));page.findChild<QPushButton*>("activity-read")->click();QCOMPARE(change.size(),1);QCOMPARE(change[0][1].toString(),QStringLiteral("read"));if(qEnvironmentVariableIsSet("BIGLYBTC_ACTIVITY_SCREENSHOT")){page.resize(900,440);page.show();QTest::qWait(30);QVERIFY(page.grab().save(qEnvironmentVariable("BIGLYBTC_ACTIVITY_SCREENSHOT")));}}

 void publicSearchCancelResultsAndSavedViewAreDistinct(){
  ui::SwarmDiscoveriesPage page;page.resize(700,450);page.show();auto* search=page.findChild<QPushButton*>("swarm-search");auto* cancel=page.findChild<QPushButton*>("swarm-cancel-search");auto* term=page.findChild<QLineEdit*>("swarm-search-term");auto* table=page.findChild<QTableView*>("swarm-table");QVERIFY(search&&cancel&&term&&table);QVERIFY(!search->isEnabled()&&!cancel->isEnabled());auto profile=std::make_shared<services::related_content::Snapshot>();profile->settings.sources={"*"};services::related_content::Item saved;saved.hash=std::string(20,'A');saved.key="saved";saved.title="Generated saved";profile->items.push_back(saved);ui::SwarmViewSnapshot view;view.generation=1;view.sequence=1;view.profile=profile;page.setSnapshot(view);QCOMPARE(table->model()->rowCount(),1);
  QSignalSpy requests(&page,&ui::SwarmDiscoveriesPage::searchRequested),cancels(&page,&ui::SwarmDiscoveriesPage::cancelSearchRequested),reads(&page,&ui::SwarmDiscoveriesPage::readRequested);term->setText("tag:generated+classics | Штучний");QTest::keyClick(term,Qt::Key_Return);QCOMPARE(requests.size(),1);QCOMPARE(requests[0][0].toString(),term->text());
  auto progress=std::make_shared<services::related_content::SearchProgress>();progress->id=1;progress->term=term->text().toStdString();progress->status="Generated searching";progress->running=true;auto result=saved;result.key="remote";result.hash=std::string(20,'R');result.title="Generated remote";result.randoms.resize(20);progress->results={result};view.search=progress;view.sequence=2;page.setSnapshot(view);QCOMPARE(table->model()->rowCount(),1);QCOMPARE(table->model()->index(0,2).data().toString(),QString("Generated remote"));QCOMPARE(table->model()->index(0,1).data(Qt::UserRole).toLongLong(),qint64(5));QVERIFY(cancel->isEnabled());if(qEnvironmentVariableIsSet("BIGLYBTC_SEARCH_SCREENSHOT")){QCoreApplication::processEvents();page.grab().save(qEnvironmentVariable("BIGLYBTC_SEARCH_SCREENSHOT"));}cancel->click();QCOMPARE(cancels.size(),1);table->selectRow(0);QTest::mouseDClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(table->model()->index(0,2)).center());QCOMPARE(reads.size(),0);
  bool checked=false;QTimer::singleShot(0,&page,[&]{auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QVERIFY(menu);for(const auto* action:menu->actions()){if(action->text()=="Mark read"||action->text()=="Mark unread"||action->text()=="Remove from discoveries"){QVERIFY(!action->isEnabled());checked=true;}}menu->close();});emit table->customContextMenuRequested(QPoint(5,5));QVERIFY(checked);
  auto finished=std::make_shared<services::related_content::SearchProgress>(*progress);finished->running=false;view.search=finished;view.sequence=3;page.setSnapshot(view);QVERIFY(!cancel->isEnabled());for(auto* button:page.findChildren<QPushButton*>()){if(button->text()=="All discoveries"){button->click();break;}}QCOMPARE(table->model()->index(0,2).data().toString(),QString("Generated saved"));view.sequence=4;page.setSnapshot(view);QCOMPARE(table->model()->index(0,2).data().toString(),QString("Generated saved"));QVERIFY(profile->items[0].key=="saved");
 }
 void originalColumnsNumericSortingFiltersAndBoundedSettings(){
  ui::SwarmDiscoveriesPage page;auto snapshot=std::make_shared<services::related_content::Snapshot>();snapshot->settings.sources={"*"};snapshot->tableLayout="d15:Column.rc_titleli1ei0ei270ei1ei1ee14:Column.rc_sizeli1ei1ei90ei1ei1ee10:SortColumn7:rc_size14:unknown-layout4:keepe";
  for(int i=0;i<2;++i){services::related_content::Item item;item.title=i?"Generated small":"Generated large";item.key=std::to_string(i);item.size=i?17:12345;item.hash=std::string(20,char('A'+i));item.randoms={1,2};item.unread=i==0;item.relatedTo={std::string(20,'P')};snapshot->items.push_back(item);}snapshot->unread=1;
  ui::SwarmViewSnapshot view;view.generation=1;view.sequence=1;view.profile=snapshot;view.status="Generated ready";page.setSnapshot(view);page.resize(1000,500);page.show();
  auto* table=page.findChild<QTableView*>("swarm-table");QVERIFY(table);QCOMPARE(table->model()->rowCount(),2);QCOMPARE(table->horizontalHeader()->visualIndex(2),0);QCOMPARE(table->columnWidth(2),270);QCOMPARE(table->model()->index(0,2).data().toString(),QString("Generated small"));QCOMPARE(table->horizontalHeader()->sortIndicatorOrder(),Qt::AscendingOrder);
  auto* unread=page.findChild<QCheckBox*>("swarm-unread-only");unread->setChecked(true);QCOMPARE(table->model()->rowCount(),1);unread->setChecked(false);auto* filter=page.findChild<QLineEdit*>("swarm-filter");filter->setText("small");QCOMPARE(table->model()->rowCount(),1);filter->clear();page.filterRelated(QByteArray(20,'X'));QCOMPARE(table->model()->rowCount(),0);page.filterRelated({});QCOMPARE(table->model()->rowCount(),2);
  QSignalSpy layouts(&page,&ui::SwarmDiscoveriesPage::layoutRequested);table->setColumnWidth(2,299);QTRY_COMPARE(layouts.size(),1);const auto raw=layouts.front().front().toByteArray();auto parsed=core::Document::parse(raw.toStdString());QVERIFY(parsed);QCOMPARE(std::string(parsed->find("unknown-layout")->bytes),std::string("keep"));
  QSignalSpy settings(&page,&ui::SwarmDiscoveriesPage::settingsRequested);QTimer::singleShot(0,&page,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);auto* maximum=dialog->findChild<QSpinBox*>("swarm-maximum");QVERIFY(maximum);QCOMPARE(maximum->value(),500);maximum->setValue(731);dialog->accept();});page.findChild<QPushButton*>("swarm-settings")->click();QCOMPARE(settings.size(),1);const auto saved=qvariant_cast<services::related_content::Settings>(settings.front().front());QCOMPARE(saved.maximum,731);
  if(qEnvironmentVariableIsSet("BIGLYBTC_SWARM_SCREENSHOT")){page.grab().save(qEnvironmentVariable("BIGLYBTC_SWARM_SCREENSHOT"));}
  auto same=view;same.sequence=2;page.setSnapshot(same);QCOMPARE(table->columnWidth(2),299); // Counter-only refresh preserves local table choice.
 }
};
QTEST_MAIN(SwarmPageTests)
#include "swarm_discoveries_page_tests.moc"

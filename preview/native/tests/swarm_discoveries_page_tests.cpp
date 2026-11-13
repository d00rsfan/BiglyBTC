// SPDX-License-Identifier: GPL-2.0-or-later
#include "swarm_discoveries_page.hpp"
#include <QTest>
#include <QSignalSpy>
#include <QTimer>
#include <QLineEdit>
#include <QCheckBox>
#include <QHeaderView>
#include <QTableView>
#include <QPushButton>
#include <QApplication>
#include <QDialog>
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

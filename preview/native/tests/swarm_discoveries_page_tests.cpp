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
using namespace biglybtc;
class SwarmPageTests final:public QObject {
 Q_OBJECT
private slots:
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

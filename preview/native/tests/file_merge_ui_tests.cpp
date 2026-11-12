// SPDX-License-Identifier: GPL-2.0-or-later
#include "statistics_page.hpp"
#include "file_merge_view.hpp"
#include <QTabWidget>
#include <QTableWidget>
#include <QScrollBar>
#include <QtTest>
#include <QPixmap>
using namespace biglybtc::ui;
class FileMergeUiTests final:public QObject {
 Q_OBJECT
private slots:
 void appendedTabAndNavigation(){StatisticsNavigation nav;nav.section=11;QVERIFY(validStatisticsNavigation(nav));StatisticsPage page(nullptr,nav);QCOMPARE(page.tabs().count(),12);QCOMPARE(page.tabs().tabText(10),QStringLiteral("I2P"));QCOMPARE(page.tabs().tabText(11),QStringLiteral("Swarm Merge"));QCOMPARE(page.tabs().currentIndex(),11);page.resize(900,450);page.show();QCoreApplication::processEvents();QVERIFY(page.minimumHeight()<450);page.resize(900,350);QCOMPARE(page.height(),350);nav.section=12;QVERIFY(!validStatisticsNavigation(nav));}
 void boundedPassiveTable(){FileMergeView page;page.resize(700,320);page.show();auto packet=std::make_shared<FileMergePacket>();packet->session=3;packet->enabled=true;packet->state=QStringLiteral("Generated merge");for(int n=0;n<100;++n){packet->rows.push_back({QStringLiteral("Source %1").arg(n),QStringLiteral("Target %1").arg(n),QStringLiteral("Waiting"),0,1,n,0,0});}page.setPresentation(packet);QCoreApplication::processEvents();auto* table=page.findChild<QTableWidget*>();QVERIFY(table);QCOMPARE(table->rowCount(),100);table->setCurrentCell(5,0);table->verticalScrollBar()->setValue(50);table->horizontalScrollBar()->setValue(100);const auto top=table->verticalScrollBar()->value(),left=table->horizontalScrollBar()->value();auto next=std::make_shared<FileMergePacket>(*packet);next->rows[0].submitted=1000;page.setPresentation(next);QCOMPARE(table->verticalScrollBar()->value(),top);QCOMPARE(table->horizontalScrollBar()->value(),left);auto stale=std::make_shared<FileMergePacket>();stale->session=2;page.setPresentation(stale);QCOMPARE(table->rowCount(),100);if(const auto capture=qEnvironmentVariable("BIGLYBTC_MERGE_SCREENSHOT");!capture.isEmpty()){page.resize(1280,650);table->verticalScrollBar()->setValue(0);table->horizontalScrollBar()->setValue(0);QCoreApplication::processEvents();QVERIFY(page.grab().save(capture));}}
};
QTEST_MAIN(FileMergeUiTests)
#include "file_merge_ui_tests.moc"

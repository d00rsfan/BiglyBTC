// SPDX-License-Identifier: GPL-2.0-or-later
#include "swarm_rating_presentation.hpp"
#include "swarm_discoveries_page.hpp"
#include <QApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QTableView>
#include <QScrollBar>
#include <QSignalSpy>
#include <QtTest>
using namespace biglybtc;
namespace {
QByteArray hex16(std::u16string_view text){QByteArray out;constexpr char hex[]="0123456789abcdef";for(const auto value:text){for(int bit=12;bit>=0;bit-=4){out+=hex[(value>>bit)&15];}}return out;}
}
class SwarmRatingTests final:public QObject {
 Q_OBJECT
private slots:
 void exactOriginalRefreshBodyMatchesCompletedTextAndComments(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_RCM_RATING_JAVA")){QSKIP("Original aercm source oracle not configured");}
  using Rating=plugins::rating::RatingData;std::vector<std::vector<Rating>> cases{{},{Rating::create(5,u"",u""),Rating::create(4,u"",u" x ")},{Rating::create(5,u"",u"x"),Rating::create(4,u"",u"x"),Rating::create(4,u"",u"x"),Rating::create(4,u"",u"x"),Rating::create(4,u"",u"x")}};
  const std::array<std::u16string,6> comments{u" \t\r\n",u"\u00a0kept\u00a0",u"\ufeffkept",u"<b>plain & text</b>",u"Generated \U0001f30d",std::u16string{char16_t(0xd800)}};
  for(unsigned sample=0;sample<160;++sample){std::vector<Rating> rows;for(unsigned i=0;i<1+sample%16;++i){rows.push_back(Rating::create(int((sample+i*3)%16)-5,u"Generated",comments[(sample+i)%comments.size()]));}cases.push_back(std::move(rows));}
  cases.push_back({Rating{3,u"Generated",std::nullopt}});QByteArray input;for(const auto& rows:cases){if(rows.empty()){input+="EMPTY";}else{bool first=true;for(const auto& row:rows){if(!first){input+=',';}first=false;input+=QByteArray::number(row.score)+':';input+=row.comment?hex16(*row.comment):QByteArray("-");}}input+='\n';}
  QProcess java;java.start(qEnvironmentVariable("BIGLYBTC_RCM_RATING_JAVA"),{"-cp",qEnvironmentVariable("BIGLYBTC_RCM_RATING_CLASSPATH"),"RcmRatingColumnOracle"});QVERIFY(java.waitForStarted(5000));java.write(input);java.closeWriteChannel();QVERIFY(java.waitForFinished(10000));QVERIFY2(java.exitCode()==0,java.readAllStandardError().constData());auto lines=java.readAllStandardOutput().split('\n');if(!lines.empty()&&lines.back().isEmpty()){lines.pop_back();}QCOMPARE(lines.size(),qsizetype(cases.size()));for(std::size_t i=0;i<cases.size();++i){const auto native=ui::presentSwarmRatings(cases[i]);const auto actual=native?hex16(native->text.toStdU16String())+'\t'+hex16(native->tooltip.toStdU16String()):QByteArray("ERROR");QCOMPARE(actual,lines[qsizetype(i)]);}
 }
 void lookupsRequireClickAndRatingUpdatesKeepRowsSelectionAndScroll(){
  ui::SwarmDiscoveriesPage page;page.resize(680,330);page.show();QSignalSpy requested(&page,&ui::SwarmDiscoveriesPage::ratingRequested);auto profile=std::make_shared<services::related_content::Snapshot>();profile->settings.enabled=true;profile->settings.uiEnabled=true;profile->settings.activeOnly=false;profile->settings.sources={"*"};for(unsigned n=0;n<80;++n){services::related_content::Item item;item.key="fixture-"+std::to_string(n);item.hash=std::string(20,'x');item.hash[0]=char(n);item.title="Generated "+std::to_string(n);item.networks=1;item.unread=true;profile->items.push_back(std::move(item));}ui::SwarmViewSnapshot view;view.generation=7;view.sequence=1;view.profile=profile;page.setSnapshot(view);QCoreApplication::processEvents();auto* table=page.findChild<QTableView*>("swarm-table");QVERIFY(table);QCOMPARE(table->model()->rowCount(),80);QCOMPARE(requested.size(),0);const auto index=table->model()->index(2,9);table->selectRow(2);const auto selected=table->selectionModel()->selectedRows().front().data(Qt::UserRole+1);QVERIFY(QMetaObject::invokeMethod(table,"clicked",Qt::DirectConnection,Q_ARG(QModelIndex,index)));QCOMPARE(requested.size(),1);QCOMPARE(requested[0][0].toULongLong(),quint64{7});const auto hash=requested[0][1].toByteArray();QCOMPARE(hash.size(),20);table->horizontalScrollBar()->setValue(table->horizontalScrollBar()->maximum());table->verticalScrollBar()->setValue(20);const int horizontal=table->horizontalScrollBar()->value(),vertical=table->verticalScrollBar()->value();QSignalSpy resets(table->model(),&QAbstractItemModel::modelReset);auto ratings=std::make_shared<ui::SwarmRatingValues>();ratings->insert(hash,{QStringLiteral("4.1"),QStringLiteral("Generated <script>plain</script>"),false});view.ratings=ratings;++view.sequence;page.setSnapshot(view);QCoreApplication::processEvents();QCOMPARE(resets.size(),0);QCOMPARE(table->horizontalScrollBar()->value(),horizontal);QCOMPARE(table->verticalScrollBar()->value(),vertical);QCOMPARE(table->selectionModel()->selectedRows().front().data(Qt::UserRole+1),selected);QCOMPARE(table->model()->index(2,9).data().toString(),QStringLiteral("4.1"));QVERIFY(table->model()->index(2,9).data(Qt::ToolTipRole).toString().contains(QStringLiteral("&lt;script&gt;")));QVERIFY(QMetaObject::invokeMethod(table,"clicked",Qt::DirectConnection,Q_ARG(QModelIndex,table->model()->index(2,9))));QCOMPARE(requested.size(),1);
 }
};
QTEST_MAIN(SwarmRatingTests)
#include "swarm_rating_tests.moc"

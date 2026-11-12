// SPDX-License-Identifier: GPL-2.0-or-later
#include "all_peers_page.hpp"
#include "library_window.hpp"
#include "tracker_status_text.hpp"
#include "classic_status_bar.hpp"
#include <QToolButton>
#include <QTabWidget>
#include <QLabel>
#include <QAction>
#include <QHeaderView>
#include <QLineEdit>
#include <QSignalSpy>
#include <QScrollBar>
#include <QScrollArea>
#include <QSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QtTest>
using namespace biglybtc;
namespace {
ui::PeerRow peer(QString key,QString hash,QString address,int rate){ui::PeerRow r;r.key=key;r.hash=hash;r.name="Fixture "+hash;r.address=address;r.localAddress="127.0.0.1";r.localInterface="loopback";r.port=40000;r.localPort=41000;r.client="Owned peer";r.state=3;r.upRate=rate;r.received=qint64{1}<<40;r.sent=1024;r.protocol="TCP";r.progressPpm=750000;r.availability=QByteArray(128,2);return r;}
}
class AllPeersUiTests final:public QObject {
 Q_OBJECT
private slots:
 void restoresOriginalColumnsNumericOrderAndSessionScope(){
  ui::AllPeersPage page(10);auto packet=std::make_shared<ui::PeerPacket>();packet->session=10;packet->sequence=1;packet->available=true;packet->rows={peer("a","A","10.0.0.20",9000000),peer("b","B","10.0.0.3",180000),peer("c","C","10.0.0.20",1230),peer("d","A","2001:db8::1",0)};
  std::expected<core::Document,core::ParseError> source=core::Document::parse("de");
  auto table=core::Document::parse("de");QVERIFY(table);QVERIFY(table->replaceValue("Column.name","li1ei0ei250ei0ei1ee"));QVERIFY(table->replaceValue("Column.ip","li1ei1ei239ei0ei1ee"));QVERIFY(table->replaceValue("Column.uploadspeed","li1ei2ei65ei0ei0ee"));QVERIFY(table->replaceValue("Column.T","li0ei3ei17ei0ei0ee"));QVERIFY(table->replaceValue("SortColumns","l11:uploadspeede"));
  source=core::Document::parse("d16:Table.MyTorrents"+table->serialize()+"e");QVERIFY(source);auto saved=core::readColumnLayouts(*source);QVERIFY(saved);packet->layout=std::make_shared<const core::SavedTableLayout>(std::move(saved->incomplete));page.setSnapshot(packet);
  auto* model=page.model();auto* view=page.table();auto* proxy=qobject_cast<QSortFilterProxyModel*>(view->model());QVERIFY(proxy);QCOMPARE(model->rowCount(),4);QCOMPARE(view->horizontalHeader()->sortIndicatorOrder(),Qt::DescendingOrder);QCOMPARE(view->horizontalHeader()->sortIndicatorSection(),ui::AllPeersModel::columnFor("uploadspeed"));QCOMPARE(view->columnWidth(ui::AllPeersModel::columnFor("ip")),239);QVERIFY(view->isColumnHidden(ui::AllPeersModel::columnFor("T")));QCOMPARE(proxy->data(proxy->index(0,ui::AllPeersModel::columnFor("uploadspeed")),ui::AllPeersModel::SortRole).toLongLong(),qint64{9000000});
  view->sortByColumn(ui::AllPeersModel::columnFor("ip"),Qt::AscendingOrder);QCOMPARE(proxy->data(proxy->index(0,ui::AllPeersModel::columnFor("ip"))).toString(),QString("10.0.0.3"));QCOMPARE(model->data(model->index(0,ui::AllPeersModel::columnFor("download")),ui::AllPeersModel::SortRole).toLongLong(),qint64{1}<<40);QVERIFY(model->data(model->index(0,ui::AllPeersModel::columnFor("uploadspeed"))).toString().contains("MiB/s"));QCOMPARE(model->data(model->index(3,ui::AllPeersModel::columnFor("uploadspeed"))).toString(),QString("0 B/s"));
  const auto row=proxy->mapToSource(proxy->index(0,0)).row();view->selectRow(0);const auto selected=model->row(row)->key;auto next=std::make_shared<ui::PeerPacket>(*packet);next->sequence=2;next->rows[0].upRate=100;page.setSnapshot(next);QCOMPARE(model->packet()->sequence,quint64{2});const auto chosen=proxy->mapToSource(view->selectionModel()->selectedRows().front()).row();QCOMPARE(model->row(chosen)->key,selected);
  auto stale=std::make_shared<ui::PeerPacket>(*packet);stale->session=9;stale->sequence=999;page.setSnapshot(stale);QCOMPARE(model->packet()->session,quint64{10});page.setSnapshot(packet);QCOMPARE(model->packet()->sequence,quint64{2});
  auto* filter=page.findChild<QLineEdit*>();QVERIFY(filter);filter->setText("fixture b");QCOMPARE(proxy->rowCount(),1);page.invalidateSession();QCOMPARE(model->rowCount(),0);
 }
 void completeLibraryRefreshIncludesObservedRates(){
  ui::LibraryModel model;auto first=std::make_shared<ui::LibraryPresentation>();first->revision=1;ui::LibraryItem row;row.key="fixture";row.hash="A";row.name="Fixture";row.state="Seeding";row.bytes=1024;row.completed=1024;row.providerAdmission=1;row.providerGeneration=1;row.liveDownloadRate=0;row.liveUploadRate=65536;first->items.push_back(row);QVERIFY(model.apply(first));QCOMPARE(model.data(model.index(0,5),Qt::UserRole).toLongLong(),qint64{65536});
  auto next=std::make_shared<ui::LibraryPresentation>(*first);next->revision=2;next->items[0].state="Forced Seeding";next->items[0].liveUploadRate=131072;QVERIFY(model.apply(next));QCOMPARE(model.data(model.index(0,5),Qt::UserRole).toLongLong(),qint64{131072});QVERIFY(model.data(model.index(0,5),Qt::DisplayRole).toString().contains("/s"));
  auto stopped=std::make_shared<ui::LibraryPresentation>(*next);stopped->revision=3;stopped->items[0].state="Stopped";stopped->items[0].providerAdmission=0;stopped->items[0].liveUploadRate.reset();stopped->items[0].liveDownloadRate.reset();QVERIFY(model.apply(stopped));QVERIFY(!model.data(model.index(0,5),Qt::UserRole).isValid());
  auto bad=std::make_shared<ui::LibraryPresentation>(*stopped);bad->revision=4;bad->items[0].liveUploadRate=-1;QVERIFY(!model.apply(bad));
 }
 void trackerStatusShowsActualReasonAndAnnounceOrScrapeData(){
  ui::TrackerStatusFacts facts;facts.live=true;facts.outcome=3;facts.error="400 Bad Request | HTTP 400 response body: <html>\nOwned error body";auto value=ui::presentTrackerStatus(facts);QCOMPARE(value.value("text").toString(),QString("Error: 400 Bad Request"));QVERIFY(value.value("detail").toString().contains("Owned error body"));
  ui::LibraryModel model;auto packet=std::make_shared<ui::LibraryPresentation>();packet->revision=1;ui::LibraryItem row;row.key="tracker";row.hash="A";row.name="Owned torrent";row.state="Seeding";row.originalValues[1]=value;packet->items.push_back(row);QVERIFY(model.apply(packet));QVERIFY(model.data(model.index(0,16),Qt::ToolTipRole).toString().contains("&lt;html&gt;"));
  facts.outcome=2;facts.error.clear();facts.peers=1;QCOMPARE(ui::presentTrackerStatus(facts).value("text").toString(),QString("OK (1 peer)"));facts.peers=0;QVERIFY(ui::presentTrackerStatus(facts).value("text").toString().contains("0 peers"));facts.peers=-1;QCOMPARE(ui::presentTrackerStatus(facts).value("text").toString(),QString("OK"));
  facts.outcome=0;facts.live=false;facts.queued=true;facts.scrapeSeeds=12;facts.scrapePeers=3;const auto scrape=ui::presentTrackerStatus(facts);QVERIFY(scrape.value("text").toString().contains("Scrape OK"));QVERIFY(scrape.value("detail").toString().contains("12"));facts.scrapeSeeds.reset();facts.scrapePeers.reset();QCOMPARE(ui::presentTrackerStatus(facts).value("text").toString(),QString("Queued"));
  facts.live=true;facts.outcome=3;facts.error="No matching local route for tracker announce; request was not sent";QCOMPARE(ui::presentTrackerStatus(facts).value("text").toString(),QString("No route for tracker announce"));facts.outcome=0;facts.queued=false;facts.configured=false;QCOMPARE(ui::presentTrackerStatus(facts).value("text").toString(),QString("No tracker configured"));
 }
 void passiveTorrentUpdatesPreserveBothHorizontalScrollPositions(){
  ui::LibraryWindow window;window.resize(700,600);window.beginColumnLayoutSession(1);auto snapshot=std::make_shared<ui::LibraryPresentation>();snapshot->revision=1;snapshot->inlineDetails=false;
  for(int i=0;i<2;++i){ui::LibraryItem row;row.key=QString::number(i);row.hash=QString::number(i);row.name="Generated torrent "+QString::number(i);row.state=i?"Seeding":"Downloading";row.assumedComplete=i!=0;row.bytes=1024;row.completed=i?1024:100;row.liveDownloadRate=10;row.liveUploadRate=20;snapshot->items.push_back(row);}window.setPresentation(snapshot);window.show();QTest::qWait(20);
  auto* leech=window.findChild<QTableView*>("incomplete-torrents");auto* seed=window.findChild<QTableView*>("complete-torrents");QVERIFY(leech&&seed);
  for(auto* table:{leech,seed}){table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);for(int i=0;i<table->model()->columnCount();++i){table->setColumnHidden(i,false);table->setColumnWidth(i,100);}table->setCurrentIndex(table->model()->index(0,0));}
  QTest::qWait(20);QVERIFY(leech->horizontalScrollBar()->maximum()>200);QVERIFY(seed->horizontalScrollBar()->maximum()>200);leech->horizontalScrollBar()->setValue(200);seed->horizontalScrollBar()->setValue(200);
  for(int step=2;step<6;++step){auto next=std::make_shared<ui::LibraryPresentation>(*snapshot);next->revision=quint64(step);if(step%2==0){std::reverse(next->items.begin(),next->items.end());}window.setPresentation(next);QCoreApplication::processEvents();QCOMPARE(leech->horizontalScrollBar()->value(),200);QCOMPARE(seed->horizontalScrollBar()->value(),200);window.setTelemetry({{QStringLiteral("0"),quint64(step)},100,1024,2048,QStringLiteral("Downloading")});QCoreApplication::processEvents();QCOMPARE(leech->horizontalScrollBar()->value(),200);QCOMPARE(seed->horizontalScrollBar()->value(),200);}
  window.close();
 }
 void passiveTorrentUpdatesNeverRevealOffscreenCurrentRow(){
  ui::LibraryWindow window;window.resize(760,700);window.beginColumnLayoutSession(1);
  auto snapshot=std::make_shared<ui::LibraryPresentation>();snapshot->revision=1;snapshot->inlineDetails=false;
  for(int pane=0;pane<2;++pane){for(int i=0;i<100;++i){ui::LibraryItem row;row.key=row.hash=QString::number(pane)+QString::number(i).rightJustified(3,'0');row.name="Generated "+row.hash;row.state=pane?"Seeding":"Downloading";row.assumedComplete=pane!=0;row.bytes=1024;row.completed=pane?1024:100;row.liveDownloadRate=10;row.liveUploadRate=20;snapshot->items.push_back(row);}}
  window.setPresentation(snapshot);window.show();QTest::qWait(20);
  for(const auto* name:{"incomplete-torrents","complete-torrents"}){for(int chosen:{0,90}){
   auto* table=window.findChild<QTableView*>(name);QVERIFY(table);table->sortByColumn(0,Qt::AscendingOrder);QTest::qWait(10);
   const auto index=table->model()->index(chosen,0);table->scrollTo(index);QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(index).center());
   const auto key=table->currentIndex().data(ui::LibraryModel::KeyRole).toString();QVERIFY(!key.isEmpty());
   auto* vertical=table->verticalScrollBar();QVERIFY(vertical->maximum()>20);vertical->setValue(20);QTest::qWait(10);
   QVERIFY(!table->viewport()->rect().intersects(table->visualRect(table->currentIndex())));
   const int position=vertical->value();QSignalSpy movements(vertical,&QScrollBar::valueChanged);
   int exposedMoves=0;const auto observer=connect(vertical,&QScrollBar::valueChanged,table,[&](int value){if(table->updatesEnabled()&&value!=position){++exposedMoves;}});
   window.setTelemetry({{key,snapshot->revision},100,2048,4096,QStringLiteral("Downloading")});QTest::qWait(10);
   QCOMPARE(vertical->value(),position);QCOMPARE(movements.size(),0);QCOMPARE(table->currentIndex().data(ui::LibraryModel::KeyRole).toString(),key);
   window.setDetails({{key,snapshot->revision},QString{},1024,{}});QTest::qWait(10);
   QCOMPARE(vertical->value(),position);QCOMPARE(movements.size(),0);
   auto next=std::make_shared<ui::LibraryPresentation>(*snapshot);next->revision++;window.setPresentation(next);snapshot=next;QTest::qWait(10);
   QCOMPARE(vertical->value(),position);QCOMPARE(movements.size(),0);QCOMPARE(table->currentIndex().data(ui::LibraryModel::KeyRole).toString(),key);
   auto reset=std::make_shared<ui::LibraryPresentation>(*snapshot);reset->revision++;std::reverse(reset->items.begin(),reset->items.end());window.setPresentation(reset);snapshot=reset;QTest::qWait(10);
   QCOMPARE(vertical->value(),position);QCOMPARE(exposedMoves,0);QCOMPARE(table->currentIndex().data(ui::LibraryModel::KeyRole).toString(),key);
   disconnect(observer);QVERIFY(table->hasAutoScroll());QVERIFY(table->updatesEnabled());table->setFocus();QTest::keyClick(table,Qt::Key_End,Qt::ControlModifier);QTest::qWait(10);QVERIFY(vertical->value()>position);
  }}
  window.close();
 }
 void passivePeerUpdatesNeverRevealOffscreenCurrentRow(){
  ui::AllPeersPage page(20);page.resize(760,450);auto packet=std::make_shared<ui::PeerPacket>();packet->session=20;packet->sequence=1;packet->available=true;
  for(int i=0;i<100;++i){const auto key=QString::number(i).rightJustified(3,'0');packet->rows.push_back(peer(key,key,"10.0.0.1",100+i));}page.setSnapshot(packet);page.show();auto* table=page.table();table->sortByColumn(ui::AllPeersModel::columnFor("name"),Qt::AscendingOrder);QTest::qWait(20);
  table->selectRow(0);table->selectionModel()->select(table->model()->index(1,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);QTest::qWait(10);auto* vertical=table->verticalScrollBar();QVERIFY(vertical->maximum()>20);vertical->setValue(20);QTest::qWait(10);QVERIFY(!table->viewport()->rect().intersects(table->visualRect(table->currentIndex())));
  const int position=vertical->value();int exposedMoves=0;const auto observer=connect(vertical,&QScrollBar::valueChanged,table,[&](int value){if(table->updatesEnabled()&&value!=position){++exposedMoves;}});
  for(int sequence=2;sequence<6;++sequence){auto next=std::make_shared<ui::PeerPacket>(*packet);next->sequence=sequence;if(sequence%2==0){std::reverse(next->rows.begin(),next->rows.end());}page.setSnapshot(next);packet=next;QTest::qWait(10);QCOMPARE(vertical->value(),position);QCOMPARE(exposedMoves,0);const auto rows=table->selectionModel()->selectedRows();QCOMPARE(rows.size(),2);QStringList names;for(const auto& index:rows){names.push_back(index.data().toString());}names.sort();QCOMPARE(names,QStringList({"Fixture 000","Fixture 001"}));}
  disconnect(observer);QVERIFY(table->hasAutoScroll());QVERIFY(table->updatesEnabled());table->setFocus();QTest::keyClick(table,Qt::Key_End,Qt::ControlModifier);QTest::qWait(10);QVERIFY(vertical->value()>position);page.close();
 }
 void classicStatusSettingsPreserveOriginalFlagsAndUnknownValues(){
  auto doc=core::Document::parse("de");QVERIFY(doc);QVERIFY(doc->replaceValue("Status Area Show SR","i0e"));QVERIFY(doc->replaceValue("Status Area Show NAT","i0e"));QVERIFY(doc->replaceValue("Status Area Show IPF","i0e"));QVERIFY(doc->replaceValue("Status Area Show NI","i1e"));QVERIFY(doc->replaceValue("unknown.status.preference","4:kept"));const auto before=doc->serialize();
  const auto settings=ui::readClassicStatusSettings(&*doc);QVERIFY(!settings.shareRatio&&!settings.nat&&!settings.ipFilter&&settings.networkInfo&&settings.dht);QCOMPARE(doc->serialize(),before);
  ui::ClassicStatusBar bar;bar.resize(1400,40);bar.setSettings(settings,{});bar.beginSession(1);bar.show();QTest::qWait(10);
  QVERIFY(bar.findChild<QToolButton*>("status-share-ratio")->isHidden());QVERIFY(bar.findChild<QToolButton*>("status-nat")->isHidden());QVERIFY(bar.findChild<QToolButton*>("status-ip-filter")->isHidden());QVERIFY(!bar.findChild<QToolButton*>("status-network")->isHidden());
  bar.setSettings({},{});QVERIFY(bar.findChild<QToolButton*>("status-ip-filter")->text().contains(QString::fromUtf8("—")));QVERIFY(bar.findChild<QToolButton*>("status-upload")->text().contains(QString::fromUtf8("—")));bar.close();
 }
 void classicStatusUsesLivePacketsWithoutOpeningStatistics(){
  ui::LibraryWindow window;window.resize(1400,650);window.beginColumnLayoutSession(1);auto library=std::make_shared<ui::LibraryPresentation>();library->revision=1;library->inlineDetails=false;library->units.siLabels=true;library->statusBar.networkInfo=true;
  for(int pane=0;pane<2;++pane){ui::LibraryItem row;row.key=row.hash=QString::number(pane);row.name=pane?"Generated seed":"Generated download";row.liveDownloadRate=pane?0:3072;row.liveUploadRate=pane?129024:0;row.state=pane?"Seeding":"Downloading";row.assumedComplete=pane!=0;row.bytes=1024;row.completed=pane?1024:100;library->items.push_back(row);}window.setPresentation(library);window.show();QTest::qWait(10);
  auto* bar=window.findChild<ui::ClassicStatusBar*>();QVERIFY(bar);QVERIFY(!window.findChild<QWidget*>("classic-statistics-page"));
  auto session=std::make_shared<ui::NativeSessionPacket>();session->session=1;session->applicationRun=1;session->epoch=1;session->sequence=1;session->state=ui::NativeSessionState::Active;session->ownerElapsedNanoseconds=1000000000;core::EngineStatistics sample;sample.elapsedNanoseconds=1000000000;sample.counters={4096,131072,3072,129024};session->sample=sample;
  core::SourceRateValues rates;rates.payloadReceived=3072;rates.protocolReceived=1024;rates.payloadSent=129024;rates.protocolSent=2048;rates.smoothedReceived=4096;rates.smoothedSent=131072;rates.smoothing=core::sourceSmoothingPolicy(60);session->sourceRates=rates;session->rateLimits.upload={core::RateLimitState::Limited,131072};session->rateLimits.download={core::RateLimitState::Unlimited,0};window.setNativeSession(session);QVERIFY(window.nativeSessionSnapshot());
  auto runtime=std::make_shared<ui::RuntimeMetricsPacket>();runtime->session=1;runtime->epoch=1;runtime->sequence=1;runtime->mainlineObserved=true;runtime->mainlineState=1;runtime->mainlineLookups=4;runtime->mainlineAnnounces=2;runtime->mainlinePeers=3;
  auto metrics=std::make_shared<core::RuntimeMetrics>();metrics->sequence=1;metrics->metrics={{"net.has_incoming_connections",core::RuntimeMetricType::Gauge,core::RuntimeMetricGroup::Net,1},{"peer.num_banned_peers",core::RuntimeMetricType::Counter,core::RuntimeMetricGroup::Peer,2}};runtime->snapshot=metrics;
  auto network=std::make_shared<services::network_identity::Snapshot>();network->current=true;network->families[0].address="203.0.113.1";network->families[0].asn=services::network_identity::Asn{"64500","Example operator","203.0.113.0/24"};runtime->network=network;window.setRuntimeMetrics(runtime);
  auto overall=std::make_shared<ui::OverallPacket>();overall->session=1;overall->sequence=1;overall->available=true;overall->values[static_cast<std::size_t>(core::SavedStatistic::Downloaded)]=2048;overall->values[static_cast<std::size_t>(core::SavedStatistic::Uploaded)]=4096;window.setOverall(overall);QTest::qWait(10);
  QVERIFY(bar->findChild<QToolButton*>("status-upload")->text().contains("128"));QVERIFY(bar->findChild<QToolButton*>("status-download")->text().contains("4.0"));QVERIFY(bar->findChild<QToolButton*>("status-upload")->text().contains("KiB/s"));QVERIFY(bar->findChild<QToolButton*>("status-mainline")->text().contains("active"));QVERIFY(bar->findChild<QToolButton*>("status-nat")->text().contains("incoming"));QVERIFY(bar->findChild<QToolButton*>("status-network")->toolTip().contains("Example operator"));QVERIFY(bar->findChild<QToolButton*>("status-share-ratio")->text().contains("2.000"));
  QVERIFY(bar->findChild<QToolButton*>("status-ip-filter")->text().contains(QString::fromUtf8("—")));QVERIFY(bar->findChild<QToolButton*>("status-ip-filter")->toolTip().contains("2"));QVERIFY(window.findChild<QToolButton*>("chat-unread-indicator"));QVERIFY(window.findChild<QToolButton*>("azureus-dht-indicator"));
  const auto position=[&](const char* id){return window.findChild<QToolButton*>(id)->mapTo(bar,QPoint{}).x();};QVERIFY(position("status-mainline")<position("azureus-dht-indicator"));QVERIFY(position("azureus-dht-indicator")<position("chat-unread-indicator"));QVERIFY(position("chat-unread-indicator")<position("status-download"));
  window.setMessage(QStringLiteral("Ready"));
  const auto capture=qEnvironmentVariable("BIGLYBTC_STATUSBAR_CAPTURE");if(!capture.isEmpty()){QVERIFY(window.grab().save(capture));}
  window.beginColumnLayoutSession(2);window.setNativeSession(session);window.setRuntimeMetrics(runtime);QVERIFY(bar->findChild<QToolButton*>("status-upload")->text().contains(QString::fromUtf8("—")));QVERIFY(!bar->findChild<QToolButton*>("status-network")->toolTip().contains("Example operator"));window.close();
 }
 void classicStatusButtonsNavigateToDetails(){
  ui::LibraryWindow window;window.resize(1400,650);window.beginColumnLayoutSession(1);window.show();auto* bar=window.findChild<ui::ClassicStatusBar*>();QVERIFY(bar);QSignalSpy clicks(bar,&ui::ClassicStatusBar::statisticsRequested);bar->findChild<QToolButton*>("status-download")->click();QCOMPARE(clicks.size(),1);auto* tabs=window.findChild<QTabWidget*>("statistics-tabs");QVERIFY(tabs);QCOMPARE(tabs->currentWidget()->objectName(),QString("statistics-transfers"));bar->findChild<QToolButton*>("status-mainline")->click();QCOMPARE(tabs->currentWidget()->objectName(),QString("statistics-all-metrics"));window.close();
 }
 void torrentPropertiesHaveAllCoreTabsAndRejectStalePackets(){
  QTabWidget tabs;auto* general=new QWidget(&tabs);new QFormLayout(general);tabs.addTab(general,"General");tabs.addTab(new QWidget(&tabs),"Files");ui::TorrentDetailPages pages(&tabs);QCOMPARE(tabs.count(),15);tabs.resize(900,450);tabs.show();
  ui::TorrentDetailInterest interest;interest.session=4;interest.request=1;interest.hash=QString(40,'A');interest.section=engine::InspectionSection::Trackers;pages.beginInterest(interest);
  auto packet=std::make_shared<ui::TorrentDetailPacket>();packet->interest=interest;packet->sequence=1;packet->actual.hash=interest.hash.toStdString();packet->actual.retained=true;
  engine::TrackerInspection ok;ok.url="http://127.0.0.1/announce";ok.local="127.0.0.1:1234";ok.state=2;ok.peers=12;packet->actual.trackers.push_back(ok);auto error=ok;error.local="[::1]:1234";error.state=3;error.message="No matching local route";packet->actual.trackers.push_back(error);
  QVERIFY(pages.apply(packet));auto* table=tabs.findChild<QTableView*>("torrent-trackers");QVERIFY(table);QCOMPARE(table->model()->rowCount(),2);bool reply=false,failed=false;for(int row=0;row<2;++row){reply|=table->model()->index(row,4).data().toString()=="Announce OK";failed|=table->model()->index(row,4).data().toString()=="Error / skipped";}QVERIFY(reply&&failed);
  if(const auto destination=qEnvironmentVariable("BIGLYBTC_DETAIL_SCREENSHOT");!destination.isEmpty()){tabs.setCurrentIndex(2);QCoreApplication::processEvents();QVERIFY(tabs.grab().save(destination));}
  auto stale=std::make_shared<ui::TorrentDetailPacket>(*packet);stale->interest.request=0;stale->sequence=100;stale->actual.trackers.clear();QVERIFY(!pages.apply(stale));QCOMPARE(table->model()->rowCount(),2);QVERIFY(!pages.apply(packet));
  interest.hash=QString(40,'B');interest.request=2;pages.beginInterest(interest);QCOMPARE(table->model()->rowCount(),0);QVERIFY(!pages.apply(packet));
  for(int n=0;n<tabs.count();++n){QVERIFY(tabs.isTabEnabled(n));tabs.setCurrentIndex(n);tabs.resize(900,450);QCoreApplication::processEvents();QVERIFY(tabs.minimumSizeHint().height()<450);}
 }
 void torrentOptionsPreserveDraftUntilAcknowledgedAndUseSeparateTransactions(){
  QTabWidget tabs;auto* general=new QWidget(&tabs);new QFormLayout(general);tabs.addTab(general,"General");tabs.addTab(new QWidget(&tabs),"Files");ui::TorrentDetailPages pages(&tabs);ui::TorrentDetailInterest interest;interest.session=1;interest.request=1;interest.hash=QString(40,'A');interest.section=engine::InspectionSection::Options;pages.beginInterest(interest);
  auto packet=std::make_shared<ui::TorrentDetailPacket>();packet->interest=interest;packet->sequence=1;packet->actual.hash=interest.hash.toStdString();packet->optionsKnown=true;packet->optionsWritable=true;packet->savedOptions={100,200,30,4};QVERIFY(pages.apply(packet));auto* down=tabs.findChild<QSpinBox*>("torrent-download-limit");QVERIFY(down);QCOMPARE(down->value(),100);down->setValue(333);
  auto next=std::make_shared<ui::TorrentDetailPacket>(*packet);next->sequence=2;QVERIFY(pages.apply(next));QCOMPARE(down->value(),333);pages.optionsFinished({});next->sequence=3;next->savedOptions[0]=333;QVERIFY(pages.apply(next));QCOMPARE(down->value(),333);pages.setOptionsWritable(true);QSignalSpy commands(&pages,&ui::TorrentDetailPages::optionsRequested);for(auto* button:tabs.findChildren<QPushButton*>()){if(button->text()=="Apply rate limits"){button->click();}}
  QCOMPARE(commands.size(),1);QVERIFY(commands.front()[0].toBool());QCOMPARE(commands.front()[1].toInt(),333);QCOMPARE(commands.front()[2].toInt(),200);
 }
 void originalFileIndicesDriveLiveProgressAndHumanUnits(){
  ui::FileModel model;ui::Target target{QString(40,'A'),1};model.select(target);auto files=std::make_shared<ui::FileDetails>();files->target=target;files->items={{2,"generated.bin",1048576,0,false,false},{0,"padding",16384,0,true,true}};QVERIFY(model.apply(files));model.setLiveProgress(target.hash,{0,0,524288});QCOMPARE(model.columnCount(),8);QCOMPARE(model.data(model.index(0,5),Qt::UserRole).toLongLong(),qint64{524288});QCOMPARE(model.data(model.index(0,6),Qt::UserRole).toDouble(),50.);QVERIFY(model.data(model.index(0,1),Qt::DisplayRole).toString()==ui::transferBytes(1048576));model.setLiveProgress(QString(40,'B'),{});QCOMPARE(model.data(model.index(0,6),Qt::UserRole).toDouble(),50.);model.setLiveProgress(target.hash,{});QVERIFY(!model.data(model.index(0,6),Qt::UserRole).isValid());
 }
 void windowCanShrinkAfterStatisticsAndWindowStateChanges(){
  ui::LibraryWindow window;window.beginColumnLayoutSession(1);auto packet=std::make_shared<ui::LibraryPresentation>();packet->revision=1;packet->inlineDetails=false;
  for(int i=0;i<2;++i){ui::LibraryItem row;row.key=row.hash=QString::number(i);row.name="Generated torrent";row.bytes=1024;row.completed=i?1024:100;row.assumedComplete=i!=0;row.state=i?"Seeding":"Downloading";packet->items.push_back(row);}window.setPresentation(packet);window.resize(1300,650);window.show();QTest::qWait(20);
  window.findChild<QToolButton*>("status-download")->click();QTest::qWait(20);auto* torrents=window.findChild<QAction*>("classic-my-torrents");QVERIFY(torrents);torrents->trigger();QTest::qWait(20);
  for(int turn=0;turn<3;++turn){if(turn==1){window.showFullScreen();}else{window.showMaximized();}QTest::qWait(10);window.showNormal();QTest::qWait(10);window.resize(1300,550);QTest::qWait(20);QVERIFY2(window.height()<=550,qPrintable(QString("height=%1 minimum=%2 hint=%3").arg(window.height()).arg(window.minimumHeight()).arg(window.minimumSizeHint().height())));auto* status=window.statusBar();QVERIFY(status->isVisible());QVERIFY(status->mapTo(&window,status->rect().bottomRight()).y()<window.height());}
  window.findChild<QToolButton*>("status-download")->click();window.resize(1300,550);QTest::qWait(20);QVERIFY(window.height()<=550);auto* workspace=window.findChild<QScrollArea*>("statistics-workspace-scroll");QVERIFY(workspace);QVERIFY(workspace->verticalScrollBar()->maximum()>0);QVERIFY(window.statusBar()->mapTo(&window,window.statusBar()->rect().bottomRight()).y()<window.height());
  window.close();
 }
 void classicActionSubscribesOnlyToVisibleView(){
  ui::LibraryWindow window;QSignalSpy requests(&window,&ui::LibraryWindow::peersObservationChanged);window.beginColumnLayoutSession(1);window.show();auto* action=window.findChild<QAction*>("classic-all-peers");QVERIFY(action);action->trigger();QTRY_VERIFY_WITH_TIMEOUT(window.findChild<ui::AllPeersPage*>()!=nullptr,1000);QTRY_VERIFY_WITH_TIMEOUT(!requests.empty()&&requests.last().at(0).toBool(),1500);
  auto* torrents=window.findChild<QAction*>("classic-my-torrents");QVERIFY(torrents);torrents->trigger();QTRY_VERIFY_WITH_TIMEOUT(!requests.last().at(0).toBool(),1500);window.close();
 }
};
QTEST_MAIN(AllPeersUiTests)
#include "all_peers_ui_tests.moc"

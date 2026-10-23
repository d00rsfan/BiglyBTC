#include "country_statistics_presentation.hpp"
#include "country_statistics_view.hpp"
#include <QAbstractItemModel>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QtTest>
#include <algorithm>
#include <functional>
#include <limits>

using namespace biglybtc;
namespace {
ui::CountryStatisticsPresentation packet(quint64 sequence=1,quint64 ledger=11) {
  auto source=std::make_shared<core::CountryStatistics>();
  source->identity={42,7,2};source->sequence=ledger;source->providerAvailable=true;
  source->retainedConnections=4;source->includedConnections=4;source->rowCount=3;
  auto& local=source->rows[0];local.kind=core::CountryRowKind::Local;local.recordedPeers=1;local.rateObservedPeers=1;
  local.observedDownloadRate=0;local.observedUploadRate=0;local.partial=false;
  local.observedDownloadBytes=0;local.observedUploadBytes=0;local.downloadBytePeers=1;local.uploadBytePeers=1;
  auto& country=source->rows[1];country.kind=core::CountryRowKind::Country;country.code={'U','S',0};
  constexpr char name[]="<b>Example country</b>";std::copy_n(name,sizeof(name),country.name.begin());
  country.recordedPeers=2;country.rateObservedPeers=1;country.rateMissingPeers=1;
  country.observedDownloadRate=100;country.observedUploadRate=9;
  auto& unknown=source->rows[2];unknown.kind=core::CountryRowKind::Unknown;unknown.recordedPeers=1;unknown.rateMissingPeers=1;
  auto result=std::make_shared<ui::CountryStatisticsPacket>();result->session=7;result->epoch=2;result->sequence=sequence;result->snapshot=source;return result;
}
ui::CountryStatisticsPresentation changed(const ui::CountryStatisticsPresentation& original,
    const std::function<void(ui::CountryStatisticsPacket&,core::CountryStatistics&)>& edit) {
  auto out=std::make_shared<ui::CountryStatisticsPacket>(*original);
  auto data=std::make_shared<core::CountryStatistics>(*out->snapshot);out->snapshot=data;edit(*out,*data);return out;
}
QTableView* table(ui::CountryStatisticsView& view){return view.findChild<QTableView*>(QStringLiteral("country-table"));}
QLabel* status(ui::CountryStatisticsView& view){return view.findChild<QLabel*>(QStringLiteral("country-status"));}
QAbstractItemModel* source(ui::CountryStatisticsView& view){return static_cast<QSortFilterProxyModel*>(table(view)->model())->sourceModel();}
int findRow(ui::CountryStatisticsView& view,const QString& text){
  auto* model=table(view)->model();for(int i=0;i<model->rowCount();++i){if(model->index(i,0).data().toString()==text){return i;}}return -1;
}
class EventObserver final:public QObject {
public:std::function<void()> callback;
protected:bool eventFilter(QObject* object,QEvent* event)override{QPointer<QObject> watched(object);if(event->type()==QEvent::ToolTipChange&&callback){auto once=std::move(callback);once();}return watched.isNull();}
};
void show(ui::CountryStatisticsView& view){view.resize(1250,480);view.show();QCoreApplication::processEvents();}
}
class CountryStatisticsViewTests final:public QObject {
  Q_OBJECT
private slots:
  void validFixturesAndAbsentOwner(){
    auto p=packet();QVERIFY(core::validCountryStatistics(*p->snapshot));QVERIFY(ui::acceptCountryStatistics({},p,7));
    const auto empty=changed(packet(),[](auto&,auto& data){data.sequence=0;data.rowCount=0;data.retainedConnections=0;data.includedConnections=0;});
    QVERIFY(core::validCountryStatistics(*empty->snapshot));QVERIFY(ui::acceptCountryStatistics({},empty,7));
    ui::CountryStatisticsView view;QVERIFY(!view.beginSession(0));QVERIFY(view.beginSession(7));QVERIFY(!view.beginSession(7));show(view);
    QCOMPARE(table(view)->model()->rowCount(),0);QVERIFY(status(view)->text().contains(QStringLiteral("unavailable")));
    auto unavailable=std::make_shared<ui::CountryStatisticsPacket>();unavailable->session=7;unavailable->sequence=1;
    QVERIFY(view.setPresentation(unavailable));QCOMPARE(table(view)->model()->rowCount(),0);
    QVERIFY(view.setPresentation(packet(2)));QCOMPARE(table(view)->model()->rowCount(),3);
  }
  void identityAndSequenceFences_data(){
    QTest::addColumn<int>("mutation");
    for(int i=0;i<12;++i){const auto name=QByteArray::number(i);QTest::newRow(name.constData())<<i;}
  }
  void identityAndSequenceFences(){
    QFETCH(int,mutation);const auto old=packet(4);auto incoming=changed(packet(5),[mutation](auto& p,auto& s){
      switch(mutation){case 0:p.session=8;break;case 1:p.sequence=4;break;case 2:p.sequence=0;break;
        case 3:p.epoch=1;s.identity.engineEpoch=1;break;case 4:s.identity.applicationRun=43;break;
        case 5:s.identity.profileSession=8;break;case 6:s.identity.engineEpoch=3;break;
        case 7:s.sequence=10;break;case 8:s.tailUncertain=false;break;case 9:s.rowCount=core::MaximumCountryRows+1;break;
        case 10:s.rows[1].observedUploadRate=-1;break;case 11:p.snapshot.reset();p.epoch=0;break;}
    });
    QVERIFY(!ui::acceptCountryStatistics(old,incoming,7));
    ui::CountryStatisticsView view;QVERIFY(view.beginSession(7));QVERIFY(view.setPresentation(old));
    QVERIFY(!view.setPresentation(incoming));QCOMPARE(view.presentation().get(),old.get());
  }
  void equalLedgerRevisionAllowsProviderReclassification(){
    const auto old=packet(1,90);
    const auto next=changed(packet(2,90),[](auto&,auto& s){
      s.providerAvailable=false;s.rows[1].kind=core::CountryRowKind::Unavailable;s.rows[1].code={};s.rows[1].name={};
      s.rows[2].kind=core::CountryRowKind::Local;
      // Keep distinct valid groups: merge the old local and unknown records.
      s.rows[0].recordedPeers=2;s.rows[0].rateMissingPeers=1;s.rows[0].partial=true;s.rowCount=2;
    });
    QVERIFY(core::validCountryStatistics(*next->snapshot));QVERIFY(ui::acceptCountryStatistics(old,next,7));
    ui::CountryStatisticsView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(old));QVERIFY(view.setPresentation(next));
    QCOMPARE(table(view)->model()->rowCount(),2);QVERIFY(status(view)->text().contains(QStringLiteral("provider unavailable")));
    QVERIFY(findRow(view,QStringLiteral("Local"))>=0);QVERIFY(findRow(view,QStringLiteral("Country unavailable"))>=0);
  }
  void newEpochMayRestartLedgerButNotPublicationSequence(){
    const auto old=packet(80,400);
    const auto next=changed(packet(81,1),[](auto& p,auto& s){p.epoch=3;s.identity.engineEpoch=3;});
    QVERIFY(ui::acceptCountryStatistics(old,next,7));
    const auto stale=changed(next,[](auto& p,auto&){p.sequence=1;});QVERIFY(!ui::acceptCountryStatistics(old,stale,7));
    ui::CountryStatisticsView view;QVERIFY(view.beginSession(7));QVERIFY(view.setPresentation(old));QVERIFY(view.beginSession(8));
    QVERIFY(!view.setPresentation(next));QVERIFY(!view.presentation());
  }
  void knownZeroMissingAndPlainCountryText(){
    ui::CountryStatisticsView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet()));
    auto* model=table(view)->model();const auto local=findRow(view,QStringLiteral("Local"));const auto unknown=findRow(view,QStringLiteral("Unknown country"));
    QCOMPARE(model->columnCount(),7);QVERIFY(local>=0&&unknown>=0);QCOMPARE(model->index(local,2).data().toString(),QStringLiteral("0"));
    QVERIFY(model->index(local,2).data(Qt::UserRole).isValid());QCOMPARE(model->index(local,2).data(Qt::UserRole).toLongLong(),0);
    QCOMPARE(model->index(unknown,2).data().toString(),QStringLiteral("Unavailable"));QVERIFY(!model->index(unknown,2).data(Qt::UserRole).isValid());
    QCOMPARE(model->index(local,5).data(Qt::UserRole).toLongLong(),0);QVERIFY(model->index(local,5).data(Qt::UserRole).isValid());
    QVERIFY(!model->index(unknown,5).data(Qt::UserRole).isValid());
    const auto country=findRow(view,QStringLiteral("US — <b>Example country</b>"));QVERIFY(country>=0);
    QCOMPARE(model->index(country,1).data(Qt::UserRole).toULongLong(),1ULL); // one observed, two retained records
    QCOMPARE(status(view)->textFormat(),Qt::PlainText);QVERIFY(model->index(country,4).data().toString().contains(QStringLiteral("Partial")));
  }
  void numericSortFilterAndSelectionSurviveUpdate(){
    ui::CountryStatisticsView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet()));
    auto* grid=table(view);grid->sortByColumn(2,Qt::AscendingOrder);QCOMPARE(grid->model()->index(0,2).data(Qt::UserRole).toLongLong(),0);
    QCOMPARE(grid->model()->index(1,2).data(Qt::UserRole).toLongLong(),100);QVERIFY(!grid->model()->index(2,2).data(Qt::UserRole).isValid());
    auto* filter=view.findChild<QLineEdit*>(QStringLiteral("country-filter"));filter->setText(QStringLiteral("eXamPle"));
    QTRY_COMPARE(grid->model()->rowCount(),1);grid->setCurrentIndex(grid->model()->index(0,0));
    const auto key=grid->currentIndex().data(Qt::UserRole+1);QVERIFY(view.setPresentation(packet(2)));
    QCOMPARE(filter->text(),QStringLiteral("eXamPle"));QCOMPARE(grid->model()->rowCount(),1);QCOMPARE(grid->currentIndex().data(Qt::UserRole+1),key);
    QCOMPARE(grid->horizontalHeader()->sortIndicatorSection(),2);
  }
  void closedObservationRetainsActualRates(){
    ui::CountryStatisticsView view;QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(packet()));
    const auto last=changed(packet(2),[](auto& p,auto&){p.current=false;});QVERIFY(view.setPresentation(last));
    QVERIFY(status(view)->text().startsWith(QStringLiteral("Last observed")));
    const auto row=findRow(view,QStringLiteral("US — <b>Example country</b>"));QVERIFY(row>=0);
    QCOMPARE(table(view)->model()->index(row,2).data(Qt::UserRole).toLongLong(),100);
    QVERIFY(table(view)->model()->index(row,4).data().toString().contains(QStringLiteral("Last observed")));
  }
  void hiddenAndMinimizedCacheWithoutModelMutation(){
    ui::CountryStatisticsView view;QVERIFY(view.beginSession(7));QVERIFY(view.setPresentation(packet()));
    QCOMPARE(table(view)->model()->rowCount(),0);show(view);QTRY_COMPARE(table(view)->model()->rowCount(),3);
    const auto old=status(view)->text();view.hide();auto last=changed(packet(2),[](auto& p,auto&){p.current=false;});
    QVERIFY(view.setPresentation(last));QCOMPARE(status(view)->text(),old);show(view);QTRY_VERIFY(status(view)->text().startsWith(QStringLiteral("Last observed")));
    view.showMinimized();QCoreApplication::processEvents();QVERIFY(view.isMinimized());
    const auto rendered=status(view)->text();QVERIFY(view.setPresentation(packet(3)));QCOMPARE(status(view)->text(),rendered);
    view.showNormal();QTRY_VERIFY(!status(view)->text().startsWith(QStringLiteral("Last observed")));
  }
  void modelResetRebindCannotPublishOldStatus(){
    ui::CountryStatisticsView view;QVERIFY(view.beginSession(7));show(view);bool fired=false;
    const auto newer=changed(packet(2),[](auto& p,auto&){p.current=false;});
    connect(source(view),&QAbstractItemModel::modelAboutToBeReset,&view,[&]{if(!fired){fired=true;QVERIFY(view.setPresentation(newer));}});
    QVERIFY(view.setPresentation(packet()));QVERIFY(fired);QTRY_COMPARE(view.presentation().get(),newer.get());
    QTRY_VERIFY(status(view)->text().startsWith(QStringLiteral("Last observed")));
  }
  void tooltipRebindCannotPublishOldAccessibleText(){
    ui::CountryStatisticsView view;QVERIFY(view.beginSession(7));show(view);status(view)->setToolTip(QString{});EventObserver observer;status(view)->installEventFilter(&observer);
    const auto newer=changed(packet(2),[](auto& p,auto&){p.current=false;});bool fired=false;
    observer.callback=[&]{fired=true;QVERIFY(view.setPresentation(newer));};
    QVERIFY(view.setPresentation(packet()));QVERIFY(fired);QTRY_VERIFY(status(view)->accessibleDescription().startsWith(QStringLiteral("Last observed")));
    QCOMPARE(view.presentation().get(),newer.get());
  }
  void retiredRowsKeepBytePrefixesButNoCurrentRates(){
    ui::CountryStatisticsView view;QVERIFY(view.beginSession(7));show(view);
    const auto retired=changed(packet(),[](auto&,auto& data){
      data.retiredConnections=1;data.includedConnections=3;auto& local=data.rows[0];
      local.retiredPeers=1;local.rateObservedPeers=0;local.observedDownloadRate.reset();local.observedUploadRate.reset();
      local.observedDownloadBytes=12345;local.observedUploadBytes=678;local.partial=true;
    });
    QVERIFY(view.setPresentation(retired));const auto row=findRow(view,QStringLiteral("Local"));QVERIFY(row>=0);auto* model=table(view)->model();
    QVERIFY(!model->index(row,2).data(Qt::UserRole).isValid());QCOMPARE(model->index(row,5).data(Qt::UserRole).toLongLong(),12345);
    QCOMPARE(model->index(row,6).data(Qt::UserRole).toLongLong(),678);QVERIFY(model->index(row,5).data(Qt::ToolTipRole).toString().contains(QStringLiteral("1 retired")));
  }
  void modelResetMayDeleteOwner(){
    QPointer<ui::CountryStatisticsView> view=new ui::CountryStatisticsView;QVERIFY(view->beginSession(7));show(*view);
    auto* model=source(*view);connect(model,&QAbstractItemModel::modelAboutToBeReset,model,[&]{delete view.data();});
    QVERIFY(view->setPresentation(packet()));QVERIFY(view.isNull());
  }
  void tooltipMayDeleteOwner(){
    QPointer<ui::CountryStatisticsView> view=new ui::CountryStatisticsView;QVERIFY(view->beginSession(7));show(*view);
    status(*view)->setToolTip(QString{});EventObserver observer;status(*view)->installEventFilter(&observer);observer.callback=[&]{delete view.data();};
    QVERIFY(view->setPresentation(packet()));QVERIFY(view.isNull());
  }
};
QTEST_MAIN(CountryStatisticsViewTests)
#include "country_statistics_view_tests.moc"

#include "manual_tag_view.hpp"

#include <QAbstractItemModel>
#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QtTest>

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace biglybtc;
template<class T> T& child(QObject& parent,const char* id) {
  auto* result=parent.findChild<T*>(QString::fromLatin1(id));
  if(!result){throw std::runtime_error("Missing manual tag control");}return *result;
}
std::string bytes(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string list(std::initializer_list<std::string> values){std::string out="l";for(const auto& value:values){out+=value;}return out+'e';}
std::string dict(std::initializer_list<std::pair<std::string,std::string>> fields){
  std::vector<std::pair<std::string,std::string>> sorted(fields);std::sort(sorted.begin(),sorted.end());std::string out="d";
  for(const auto& [key,value]:sorted){out+=bytes(key)+value;}return out+'e';
}
constexpr std::string_view A="AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQT",B="77777777777777777777777777777777";
core::TorrentHash hash(bool second=false){
  std::string raw(20,'\xff');if(!second){for(std::size_t i=0;i<raw.size();++i){raw[i]=static_cast<char>(i);}}
  auto value=core::TorrentHash::parse(raw);if(!value){throw std::runtime_error("Fixture hash invalid");}return *value;
}
std::string fixture(){return dict({{"3",dict({
  {"1",dict({{"n",bytes("Alpha")},{"o",list({bytes(A)})}})},
  {"2",dict({{"n",bytes("Beta")},{"o",list({bytes(B)})}})},
  {"3",dict({{"n",bytes("Unresolved")},{"o",list({bytes("not-a-normal-hash")})}})}})}});}
auto membership(quint64 session=7,quint64 library=11,quint64 tags=13,std::string raw=fixture()){
  auto parsed=core::readSavedDownloadTags(raw);if(!parsed){throw std::runtime_error("Fixture document invalid");}
  const std::array hashes{hash(),hash(true)};auto result=core::resolveManualTagMembership(session,library,tags,*parsed,hashes);
  if(!result){throw std::runtime_error("Fixture membership invalid");}return *result;
}
auto packet(quint64 sequence=1,quint64 session=7,quint64 epoch=1,std::shared_ptr<const core::ManualTagMembership> member={}){
  auto value=std::make_shared<ui::ManualTagStatisticsPacket>();value->session=session;value->epoch=epoch;value->sequence=sequence;
  value->membership=member?std::move(member):membership(session);value->libraryRevision=value->membership->libraryRevision();
  value->tagRevision=value->membership->tagRevision();value->readState=ui::ManualTagReadState::Ready;
  value->traffic={core::ManualTagTrafficState::Active,core::TargetTraffic{hash(),101,23,80,20},true,true};return value;
}
auto missing(quint64 sequence,quint64 session=7,quint64 epoch=1,quint64 library=11,quint64 tags=14){
  auto value=std::make_shared<ui::ManualTagStatisticsPacket>();value->session=session;value->epoch=epoch;value->sequence=sequence;
  value->libraryRevision=library;value->tagRevision=tags;value->readState=ui::ManualTagReadState::Missing;return value;
}
QTableView& table(QWidget& view){return child<QTableView>(view,"manual-tag-table");}
QLabel& status(QWidget& view){return child<QLabel>(view,"manual-tag-status");}
QPushButton& refresh(QWidget& view){return child<QPushButton>(view,"manual-tag-refresh");}
void show(QWidget& view){view.resize(1150,650);view.show();QCoreApplication::processEvents();}
int rowFor(QWidget& view,const QString& name){
  const auto* model=table(view).model();for(int i=0;i<model->rowCount();++i){if(model->index(i,0).data().toString()==name){return i;}}return -1;
}
QVariant value(QWidget& view,const QString& name,int column,int role=Qt::DisplayRole){
  const auto row=rowFor(view,name);if(row<0){throw std::runtime_error("Expected manual tag missing");}return table(view).model()->index(row,column).data(role);
}
class TooltipHook final:public QObject{
public:
  std::function<void()> callback;bool armed=true;
  bool eventFilter(QObject*,QEvent* event)override{
    if(armed && event->type()==QEvent::ToolTipChange){armed=false;callback();return true;}return false;
  }
};
}

class ManualTagViewTests final:public QObject{
  Q_OBJECT
private slots:
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
  void observedCombinedRatesZeroAndUnknownHaveExactRoles(){
    ui::ManualTagStatisticsView view;QVERIFY(view.beginSession(7));show(view);auto input=packet();QVERIFY(view.setPresentation(input));
    QCOMPARE(table(view).model()->rowCount(),3);QCOMPARE(table(view).model()->columnCount(),6);
    QCOMPARE(value(view,"Alpha",3,Qt::UserRole).toLongLong(),101LL);QCOMPARE(value(view,"Alpha",4,Qt::UserRole).toLongLong(),23LL);
    QCOMPARE(value(view,"Beta",3,Qt::UserRole).toLongLong(),0LL);QVERIFY(!value(view,"Unresolved",3,Qt::UserRole).isValid());
    QCOMPARE(value(view,"Unresolved",3).toString(),QStringLiteral("Unavailable"));
    QCOMPARE(value(view,"Alpha",0,Qt::UserRole+1).toLongLong(),12884901889LL);
    QCOMPARE(value(view,"Alpha",0,Qt::UserRole+2).toByteArray(),QByteArray("Alpha"));
    QCOMPARE(value(view,"Alpha",2,Qt::UserRole+3).toULongLong(),1ULL);QCOMPARE(value(view,"Unresolved",2,Qt::UserRole+4).toULongLong(),1ULL);
    QVERIFY(status(view).text().contains("Observed native target"));
  }
  void maximumRateIsCopiedAndNumericSortFilterAndUidSelectionSurviveRefresh(){
    ui::ManualTagStatisticsView view;view.beginSession(7);show(view);const auto join=membership();auto first=packet(1,7,1,join);
    first->traffic.target->downloadRate=std::numeric_limits<std::int64_t>::max();QVERIFY(view.setPresentation(first));
    table(view).sortByColumn(3,Qt::DescendingOrder);QCoreApplication::processEvents();
    QCOMPARE(table(view).model()->index(0,0).data().toString(),QStringLiteral("Unresolved")); // Unknown sorts after numeric in ascending, first in descending.
    QCOMPARE(value(view,"Alpha",3,Qt::UserRole).toLongLong(),std::numeric_limits<qint64>::max());
    const int row=rowFor(view,"Alpha");table(view).setCurrentIndex(table(view).model()->index(row,0));
    table(view).sortByColumn(1,Qt::AscendingOrder);auto next=packet(2,7,1,join);QVERIFY(view.setPresentation(next));
    QCOMPARE(table(view).currentIndex().data(Qt::UserRole+1).toLongLong(),12884901889LL);
    child<QLineEdit>(view,"manual-tag-filter").setText("aLp");QTRY_COMPARE(table(view).model()->rowCount(),1);
    QCOMPARE(table(view).model()->index(0,0).data().toString(),QStringLiteral("Alpha"));
    auto third=packet(3,7,1,join);QVERIFY(view.setPresentation(third));QCOMPARE(table(view).model()->rowCount(),1);
    QCOMPARE(child<QLineEdit>(view,"manual-tag-filter").text(),QStringLiteral("aLp"));
    QCOMPARE(table(view).horizontalHeader()->sortIndicatorSection(),1);
  }
  void lastObservedAndPartialCoverageNeverBecomeCurrentOtherTagZero(){
    ui::ManualTagStatisticsView view;view.beginSession(7);show(view);const auto join=membership();auto first=packet(1,7,1,join);
    first->traffic.current=false;QVERIFY(view.setPresentation(first));QCOMPARE(value(view,"Alpha",5).toString(),QStringLiteral("Last observed"));
    QVERIFY(status(view).text().contains("Last observed"));auto next=packet(2,7,1,join);next->traffic.completeOwnedCoverage=false;
    QVERIFY(view.setPresentation(next));QVERIFY(!value(view,"Beta",3,Qt::UserRole).isValid());QCOMPARE(value(view,"Alpha",3,Qt::UserRole).toLongLong(),101LL);
  }
  void missingEmptyAbsentSectionAndFailureAreDistinctPlainText(){
    ui::ManualTagStatisticsView view;view.beginSession(7);show(view);QVERIFY(view.setPresentation(missing(1)));QCOMPARE(table(view).model()->rowCount(),0);
    QVERIFY(status(view).text().contains("No saved manual tag configuration"));
    auto empty=packet(2,7,1,membership(7,11,15,dict({{"3","de"}})));QVERIFY(view.setPresentation(empty));
    QVERIFY(status(view).text().contains("No visible saved manual-download tags"));
    auto absent=packet(3,7,1,membership(7,11,16,"de"));QVERIFY(view.setPresentation(absent));
    QVERIFY(status(view).text().contains("no manual-download tags section"));
    auto failed=missing(4,7,1,11,17);failed->readState=ui::ManualTagReadState::Failed;failed->failure="<b>owned failure</b>";
    QVERIFY(view.setPresentation(failed));QCOMPARE(status(view).textFormat(),Qt::PlainText);QVERIFY(status(view).text().contains(failed->failure));QCOMPARE(table(view).model()->rowCount(),0);
  }
  void binaryNamesAndHiddenPreferencesDoNotCreateUnsafeOrHiddenRows(){
    const auto raw=dict({{"3",dict({{"1",dict({{"n",bytes("<b>literal</b>")},{"o",list({bytes(A)})}})},
      {"2",dict({{"n",bytes(std::string("a\0b",3))},{"o","le"}})},
      {"3",dict({{"n",bytes("Hidden")},{"o","le"},{"c",dict({{"vis","i0e"}})}})}})}});
    ui::ManualTagStatisticsView view;view.beginSession(7);show(view);QVERIFY(view.setPresentation(packet(1,7,1,membership(7,11,13,raw))));
    QCOMPARE(table(view).model()->rowCount(),2);QVERIFY(rowFor(view,"Hidden")<0);QVERIFY(rowFor(view,"<b>literal</b>")>=0);
    QCOMPARE(value(view,"Tag with binary saved name",0,Qt::UserRole+2).toByteArray(),QByteArray("a\0b",3));
    QVERIFY(value(view,"<b>literal</b>",0,Qt::ToolTipRole).toString().contains("3c 62 3e"));
    QVERIFY(!value(view,"<b>literal</b>",0,Qt::ToolTipRole).toString().contains("<b>"));
  }
  void sharedAdmissionRejectsBadIdentityShapeAndGlobalSequence(){
    const auto first=packet();QVERIFY(ui::acceptManualTagStatistics({},first,7));
    auto checkRejected=[&](auto edit){auto next=std::make_shared<ui::ManualTagStatisticsPacket>(*first);next->sequence=2;edit(*next);QVERIFY(!ui::acceptManualTagStatistics(first,next,7));};
    checkRejected([](auto& x){x.session=8;});checkRejected([](auto& x){x.sequence=1;});checkRejected([](auto& x){x.libraryRevision=0;});
    checkRejected([](auto& x){x.tagRevision=12;});checkRejected([](auto& x){x.epoch=0;});checkRejected([](auto& x){x.membership.reset();});
    checkRejected([](auto& x){x.failure="unexpected";});checkRejected([](auto& x){x.traffic.target->payloadDownloadRate=102;});
    checkRejected([](auto& x){x.readState=static_cast<ui::ManualTagReadState>(99);});
    checkRejected([](auto& x){x.membership=membership();}); // Equal revisions require exact sealed join reuse.
    auto higher=packet(2,7,2,first->membership);QVERIFY(ui::acceptManualTagStatistics(first,higher,7));
    higher->sequence=1;QVERIFY(!ui::acceptManualTagStatistics(first,higher,7));
    auto foreign=packet(2,7,1,first->membership);auto h=core::TorrentHash::parse(std::string(20,'\x7f'));QVERIFY(h.has_value());foreign->traffic.target->hash=*h;
    QVERIFY(!ui::acceptManualTagStatistics(first,foreign,7));
  }
  void watermarksAcrossMissingAndFailedPreventMembershipRollback(){
    ui::ManualTagStatisticsView view;view.beginSession(7);const auto first=packet();QVERIFY(view.setPresentation(first));
    const auto gone=missing(2,7,1,12,14);QVERIFY(view.setPresentation(gone));auto stale=packet(3);QVERIFY(!view.setPresentation(stale));
    auto failed=missing(3,7,1,12,15);failed->readState=ui::ManualTagReadState::Failed;QVERIFY(view.setPresentation(failed));
    auto old=packet(4,7,1,membership(7,12,14));QVERIFY(!view.setPresentation(old));
    auto fresh=packet(4,7,1,membership(7,12,15));QVERIFY(view.setPresentation(fresh));
    QVERIFY(view.beginSession(8));QVERIFY(!view.beginSession(8));QVERIFY(!view.beginSession(0));QVERIFY(!view.setPresentation(packet(99)));
    QVERIFY(view.setPresentation(packet(1,8,0,membership(8)) )==false); // Active target requires an actual owner epoch.
    auto metadata=packet(1,8,0,membership(8));metadata->traffic={};QVERIFY(view.setPresentation(metadata));
  }
  void hiddenPacketsAndNewProfileDoNotMutateModelsUntilShown(){
    ui::ManualTagStatisticsView view;view.beginSession(7);auto first=packet();QVERIFY(view.setPresentation(first));
    QCOMPARE(table(view).model()->rowCount(),0);QCOMPARE(status(view).text(),QStringLiteral("Manual tag data unavailable"));show(view);
    QTRY_COMPARE(table(view).model()->rowCount(),3);const auto oldText=status(view).text();view.hide();QCoreApplication::processEvents();
    QVERIFY(view.setPresentation(missing(2)));QCOMPARE(table(view).model()->rowCount(),3);QCOMPARE(status(view).text(),oldText);
    QVERIFY(view.beginSession(8));QCOMPARE(table(view).model()->rowCount(),3);show(view);QTRY_COMPARE(table(view).model()->rowCount(),0);
    QVERIFY(!view.presentation());QVERIFY(status(view).text().contains("unavailable"));
  }
  void minimizedPacketsRenderOnlyLatestAfterRestore(){
    ui::ManualTagStatisticsView view;view.beginSession(7);show(view);const auto join=membership();QVERIFY(view.setPresentation(packet(1,7,1,join)));
    const auto text=status(view).text();view.showMinimized();QTRY_VERIFY(view.isMinimized());QVERIFY(view.setPresentation(missing(2)));
    QCoreApplication::processEvents();QCOMPARE(table(view).model()->rowCount(),3);QCOMPARE(status(view).text(),text);
    auto latest=packet(3,7,1,membership(7,11,14));latest->traffic.current=false;QVERIFY(view.setPresentation(latest));
    view.showNormal();QTRY_VERIFY(!view.isMinimized());QTRY_VERIFY(status(view).text().contains("Last observed"));QCOMPARE(table(view).model()->rowCount(),3);
  }
  void initialReadIsLazyOnceAndPacketUpdatesDoNotRead(){
    ui::ManualTagStatisticsView view;QSignalSpy reads(&view,&ui::ManualTagStatisticsView::readRequested);
    show(view);QCoreApplication::processEvents();QCOMPARE(reads.count(),0);view.hide();QVERIFY(view.beginSession(7));
    QCoreApplication::processEvents();QCOMPARE(reads.count(),0);show(view);QTRY_COMPARE(reads.count(),1);QCOMPARE(reads[0][0].toULongLong(),7ULL);
    const auto join=membership();QVERIFY(view.setPresentation(packet(1,7,1,join)));QVERIFY(view.setPresentation(packet(2,7,1,join)));
    view.hide();show(view);QCoreApplication::processEvents();QCOMPARE(reads.count(),1);refresh(view).click();QTRY_COMPARE(reads.count(),2);
  }
  void explicitHiddenReadDefersUntilVisibleAndSessionResetRequestsAgain(){
    ui::ManualTagStatisticsView view;view.beginSession(7);QSignalSpy reads(&view,&ui::ManualTagStatisticsView::readRequested);show(view);QTRY_COMPARE(reads.count(),1);
    view.hide();refresh(view).click();QCoreApplication::processEvents();QCOMPARE(reads.count(),1);show(view);QTRY_COMPARE(reads.count(),2);
    QVERIFY(view.beginSession(8));QTRY_COMPARE(reads.count(),3);QCOMPARE(reads[2][0].toULongLong(),8ULL);
  }
  void initialReadWhileMinimizedRemainsPendingUntilRestore(){
    ui::ManualTagStatisticsView view;QSignalSpy reads(&view,&ui::ManualTagStatisticsView::readRequested);view.resize(1100,650);view.showMinimized();
    QTRY_VERIFY(view.isMinimized());view.beginSession(7);QCoreApplication::processEvents();QCOMPARE(reads.count(),0);
    view.showNormal();QTRY_VERIFY(!view.isMinimized());QTRY_COMPARE(reads.count(),1);QCOMPARE(reads[0][0].toULongLong(),7ULL);
  }
  void directReadCallbackMayRebindOrDeleteView(){
    auto* view=new ui::ManualTagStatisticsView;QPointer<ui::ManualTagStatisticsView> guard(view);QVector<quint64> sessions;
    connect(view,&ui::ManualTagStatisticsView::readRequested,view,[&](quint64 session){sessions.push_back(session);if(session==7){view->beginSession(8);}else{delete view;}});
    view->beginSession(7);show(*view);QTRY_VERIFY(guard.isNull());QCOMPARE(sessions,QVector<quint64>({7,8}));
  }
  void modelResetReentryPublishesOnlyNewerPacket(){
    ui::ManualTagStatisticsView view;view.beginSession(7);show(view);const auto join=membership();auto first=packet(1,7,1,join),newer=packet(2,7,1,join);
    newer->traffic.current=false;
    auto* proxy=qobject_cast<QSortFilterProxyModel*>(table(view).model());QVERIFY(proxy);bool once=true,accepted=false;
    connect(proxy->sourceModel(),&QAbstractItemModel::modelAboutToBeReset,&view,[&]{if(once){once=false;accepted=view.setPresentation(newer);}});
    QVERIFY(view.setPresentation(first));QVERIFY(accepted);QTRY_VERIFY(status(view).text().contains("Last observed"));QCOMPARE(view.presentation(),ui::ManualTagStatisticsPresentation(newer));
  }
  void tooltipReentryProfileResetAndDeletionAreGuarded(){
    auto* view=new ui::ManualTagStatisticsView;QPointer<ui::ManualTagStatisticsView> guard(view);view->beginSession(7);show(*view);
    TooltipHook reset;reset.callback=[&]{view->beginSession(8);};status(*view).installEventFilter(&reset);
    QVERIFY(view->setPresentation(packet()));QTRY_VERIFY(!view->presentation());QTRY_VERIFY(status(*view).text().contains("unavailable"));
    TooltipHook remove;remove.callback=[&]{delete view;};status(*view).setToolTip({});status(*view).installEventFilter(&remove);
    const auto accepted=view->setPresentation(packet(1,8,1,membership(8)));QVERIFY(accepted);QVERIFY(guard.isNull());
  }
  void childDeletionDuringRefreshDoesNotLeaveStaleAccess(){
    ui::ManualTagStatisticsView view;view.beginSession(7);show(view);QPointer<QLabel> label(&status(view));
    TooltipHook remove;remove.callback=[&]{delete label.data();};label->installEventFilter(&remove);
    QVERIFY(view.setPresentation(packet()));QVERIFY(label.isNull());QVERIFY(view.setPresentation(missing(2)));QCoreApplication::processEvents();
  }
};
QTEST_MAIN(ManualTagViewTests)
#include "manual_tag_view_tests.moc"

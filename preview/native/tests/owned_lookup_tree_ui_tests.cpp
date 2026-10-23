#include "owned_lookup_tree_widget.hpp"
#include "azureus_statistics_view.hpp"
#include "owned_azureus_service.hpp"
#include "owned_ddb_fixture.hpp"
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPixmap>
#include <QTableView>
#include <QTimer>
#include <QTreeView>
#include <QtTest>
#include <functional>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace {
using namespace biglybtc;namespace control=services::azureus_control;using namespace std::chrono_literals;
template<class T>T& child(QObject& owner,const char* name){auto* result=owner.findChild<T*>(QString::fromLatin1(name));if(!result){throw std::runtime_error("Missing lookup tree control");}return *result;}
void show(QWidget& widget){widget.resize(1150,900);widget.show();QCoreApplication::processEvents();}
struct Fixture{
  test::OwnedDdbPeer root{"127.0.0.2"},leaf{"127.0.0.3"};
  std::unique_ptr<app::OwnedAzureusService> service;control::NodeId target{};
  Fixture(){root.contacts({leaf.endpoint()});leaf.values({{std::string("generated\0tree",14)}});target.fill(0x3a);
    app::AzureusServiceOptions options;options.enabled=true;auto created=app::OwnedAzureusService::create(options);if(!created){throw std::runtime_error("Actual owned DDB service failed to start");}service=std::move(*created);}
  bool allow(services::azureus_udp::Peer peer){const auto request=service->allowPeer(peer);if(!request){return false;}
    return QTest::qWaitFor([&]{const auto observation=service->observation();return observation.lastResult&&observation.lastResult->request==*request&&!observation.lastResult->error&&!observation.lastResult->transportFailure;},3000);}
  bool ready(){return allow(root.endpoint())&&allow(leaf.endpoint());}
  std::shared_ptr<const control::LookupSnapshot> lookup()const{return service->observation().lookupSnapshot;}
  auto packet(quint64 sequence=1,quint64 session=7)const{const auto observed=service->observation();if(!observed.snapshot){throw std::runtime_error("Missing actual transport observation");}
    auto result=std::make_shared<ui::AzureusStatisticsPacket>();result->session=session;result->sequence=sequence;result->state=ui::AzureusStatisticsState::Observed;
    result->snapshot=observed.snapshot;result->routerSnapshot=observed.routerSnapshot;result->lookupSnapshot=observed.lookupSnapshot;result->current=observed.current;return result;}
};
void optionalCapture(QWidget& widget){const auto raw=qEnvironmentVariable("BIGLYBTC_DDB_CAPTURE_DIRECTORY");if(raw.isEmpty()){return;}const QFileInfo info(raw);const auto path=info.absoluteFilePath();struct stat st{};
  if(!info.isAbsolute()||info.isSymLink()||info.canonicalFilePath()!=path||::lstat(path.toLocal8Bit().constData(),&st)!=0||!S_ISDIR(st.st_mode)||st.st_uid!=::geteuid()||(st.st_mode&0077)!=0){throw std::runtime_error("DDB capture directory must be owned canonical private directory");}
  QCoreApplication::processEvents();QFile output(path+QStringLiteral("/actual-ddb-lookup-tree.png"));if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)||!output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)||!widget.grab().save(&output,"PNG")||!output.flush()){throw std::runtime_error("Exclusive DDB capture failed");}}
class Hook final:public QObject{public:std::function<void()> callback;bool eventFilter(QObject* object,QEvent* event)override{QPointer<QObject> guard(object);if(event->type()==QEvent::ToolTipChange&&callback){auto once=std::move(callback);once();}return guard.isNull();}};
}
class OwnedLookupTreeUiTests final:public QObject{
  Q_OBJECT
private slots:
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
  void actualRecursiveGetBranchesAndAcceptedBytesFeedPassiveTreeAndRawTimeline(){
    Fixture fixture;QVERIFY(fixture.ready());ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Operations);QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(fixture.packet()));
    auto& lookup=child<ui::OwnedLookupTreeWidget>(view,"azureus-lookup-tree-view");auto& tree=child<QTreeView>(view,"azureus-lookup-tree");QVERIFY(!lookup.snapshot());QCOMPARE(tree.model()->rowCount(),0);
    QCOMPARE(fixture.root.received(),std::size_t{0});QCOMPARE(fixture.leaf.received(),std::size_t{0});
    const auto request=fixture.service->lookupValue(fixture.root.endpoint(),fixture.target);QVERIFY(request);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.lookup()&&control::lookupTerminal(fixture.lookup()->state),5000);const auto actual=fixture.lookup();QVERIFY(actual);QCOMPARE(actual->state,control::LookupState::Complete);
    QCOMPARE(actual->nodes.size(),std::size_t{2});QCOMPARE(actual->results.size(),std::size_t{1});QCOMPARE(fixture.root.received(),std::size_t{1});QCOMPARE(fixture.leaf.received(),std::size_t{1});QVERIFY(fixture.root.valid()&&fixture.leaf.valid());
    const auto packet=fixture.packet(2);QVERIFY(view.setPresentation(packet));QVERIFY(lookup.snapshot()==packet->lookupSnapshot);QCOMPARE(tree.model()->rowCount(),1);
    const auto root=tree.model()->index(0,0);QCOMPARE(root.data(Qt::UserRole).toULongLong(),static_cast<qulonglong>(actual->nodes[0].id));QCOMPARE(tree.model()->rowCount(root),1);
    const auto leaf=tree.model()->index(0,0,root);QCOMPARE(tree.model()->parent(leaf),root);QCOMPARE(leaf.data(Qt::UserRole).toULongLong(),static_cast<qulonglong>(actual->nodes[1].id));
    QCOMPARE(actual->nodes[1].parent,actual->nodes[0].id);QCOMPARE(tree.model()->index(0,3,root).data(Qt::UserRole).toULongLong(),1ULL);
    QVERIFY(tree.model()->index(0,1,root).data().toString().endsWith(QLatin1Char(':')+QString::number(fixture.leaf.endpoint().port)));
    tree.setExpanded(root,true);tree.setCurrentIndex(leaf);auto& details=child<QPlainTextEdit>(view,"azureus-lookup-tree-details");
    QTRY_VERIFY(details.toPlainText().contains(QStringLiteral("Hex: 67656e6572617465640074726565")));QVERIFY(details.toPlainText().contains(QStringLiteral("14 bytes")));
    QVERIFY(details.toPlainText().contains(QStringLiteral("Target ")+QString::fromLatin1(QByteArray(20,':').toHex())));QVERIFY(details.isReadOnly());
    QCOMPARE(child<QTableView>(view,"azureus-statistics-table").model()->rowCount(),static_cast<int>(packet->snapshot->events().size()));
    QVERIFY(child<QWidget>(view,"azureus-operation-timeline").accessibleDescription().contains(QStringLiteral("FIND_VALUE")));
    QVERIFY(child<QLabel>(view,"azureus-lookup-tree-scope").toolTip().contains(QStringLiteral("not geographic paths")));QVERIFY(view.findChildren<QTimer*>().isEmpty());
    optionalCapture(view);const auto before=fixture.root.received();QCoreApplication::processEvents();QCOMPARE(fixture.root.received(),before);
    QVERIFY(fixture.service->close());const auto closed=fixture.packet(3);QVERIFY(!closed->current);QVERIFY(view.setPresentation(closed));QVERIFY(lookup.snapshot());QVERIFY(child<QLabel>(view,"azureus-lookup-tree-status").text().contains(QStringLiteral("Last observed.")));
  }
  void actualCancellationKeepsDispatchedChildDistinctFromAcknowledgement(){
    Fixture fixture;fixture.leaf.silent();QVERIFY(fixture.ready());const auto request=fixture.service->lookupValue(fixture.root.endpoint(),fixture.target,0,32,5s);QVERIFY(request);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.leaf.received(),std::size_t{1},3000);QTRY_VERIFY_WITH_TIMEOUT(fixture.lookup()&&fixture.lookup()->nodes.size()==2&&fixture.lookup()->nodes[1].state==control::LookupNodeState::Sent&&fixture.lookup()->nodes[1].ticket,3000);
    const auto pending=fixture.packet();ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Operations);QVERIFY(view.beginSession(7));show(view);QVERIFY(view.setPresentation(pending));
    auto& tree=child<QTreeView>(view,"azureus-lookup-tree");const auto root=tree.model()->index(0,0);QCOMPARE(tree.model()->rowCount(root),1);QVERIFY(tree.model()->index(0,4,root).data(Qt::UserRole).toULongLong()>0);
    const auto cancelled=fixture.service->cancel(*request);QVERIFY(cancelled);QTRY_VERIFY_WITH_TIMEOUT(fixture.lookup()&&fixture.lookup()->state==control::LookupState::Cancelled,3000);
    const auto terminal=fixture.packet(2);QVERIFY(view.setPresentation(terminal));const auto latestRoot=tree.model()->index(0,0);
    QVERIFY(tree.model()->index(0,2,latestRoot).data().toString().contains(QStringLiteral("Cancellation acknowledged")));QVERIFY(fixture.lookup()->results.empty());QVERIFY(fixture.service->close());
  }
  void hiddenUpdatesProfilesAndMalformedSnapshotsCannotReplaceAcceptedFacts(){
    Fixture fixture;QVERIFY(fixture.ready());QVERIFY(fixture.service->lookupValue(fixture.root.endpoint(),fixture.target));QTRY_VERIFY_WITH_TIMEOUT(fixture.lookup()&&control::lookupTerminal(fixture.lookup()->state),5000);
    QCOMPARE(fixture.lookup()->state,control::LookupState::Complete);QCOMPARE(fixture.lookup()->nodes.size(),std::size_t{2});
    ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Operations);QVERIFY(view.beginSession(7));show(view);const auto packet=fixture.packet();QVERIFY(view.setPresentation(packet));
    auto& lookup=child<ui::OwnedLookupTreeWidget>(view,"azureus-lookup-tree-view");auto& status=child<QLabel>(view,"azureus-lookup-tree-status");const auto previous=status.text();
    auto malformed=std::make_shared<control::LookupSnapshot>(*packet->lookupSnapshot);malformed->nodes[1].parent=malformed->nodes[1].id;QVERIFY(!lookup.setSnapshot(malformed,true,7));QVERIFY(lookup.snapshot()==packet->lookupSnapshot);
    view.hide();auto retained=std::make_shared<ui::AzureusStatisticsPacket>(*packet);retained->sequence=2;retained->current=false;QVERIFY(view.setPresentation(retained));QCOMPARE(status.text(),previous);
    show(view);QTRY_VERIFY(status.text().contains(QStringLiteral("Last observed.")));view.showMinimized();QTRY_VERIFY(view.isMinimized());auto current=std::make_shared<ui::AzureusStatisticsPacket>(*packet);current->sequence=3;QVERIFY(view.setPresentation(current));
    QVERIFY(status.text().contains(QStringLiteral("Last observed.")));view.showNormal();QTRY_VERIFY(!status.text().contains(QStringLiteral("Last observed.")));
    view.hide();QVERIFY(view.beginSession(8));QVERIFY(!lookup.snapshot());QCOMPARE(child<QTreeView>(view,"azureus-lookup-tree").model()->rowCount(),0);QVERIFY(!child<QPlainTextEdit>(view,"azureus-lookup-tree-details").accessibleDescription().contains(QStringLiteral("Hex:")));QVERIFY(!view.setPresentation(current));
    QVERIFY(view.setPresentation(fixture.packet(1,8)));QVERIFY(!lookup.snapshot());show(view);QTRY_VERIFY(lookup.snapshot());
    auto failed=std::make_shared<ui::AzureusStatisticsPacket>(*view.presentation());failed->sequence=2;failed->lookupSnapshot.reset();QVERIFY(view.setPresentation(failed));QVERIFY(!lookup.snapshot());QCOMPARE(child<QTreeView>(view,"azureus-lookup-tree").model()->rowCount(),0);
    QVERIFY(status.text().contains(QStringLiteral("unavailable; no branches inferred")));QVERIFY(fixture.service->close());
  }
  void statusAndSelectedResultCallbacksMayRebindOrDeleteTheParent(){
    Fixture fixture;QVERIFY(fixture.ready());QVERIFY(fixture.service->lookupValue(fixture.root.endpoint(),fixture.target));QTRY_VERIFY_WITH_TIMEOUT(fixture.lookup()&&control::lookupTerminal(fixture.lookup()->state),5000);
    QCOMPARE(fixture.lookup()->state,control::LookupState::Complete);QCOMPARE(fixture.lookup()->nodes.size(),std::size_t{2});
    ui::AzureusStatisticsView view(ui::AzureusStatisticsSection::Operations);QVERIFY(view.beginSession(7));show(view);
    Hook rebind;rebind.callback=[&]{QVERIFY(view.beginSession(8));QVERIFY(view.setPresentation(fixture.packet(1,8)));};
    child<QLabel>(view,"azureus-lookup-tree-status").setToolTip({});child<QLabel>(view,"azureus-lookup-tree-status").installEventFilter(&rebind);QVERIFY(view.setPresentation(fixture.packet()));
    QTRY_VERIFY(view.presentation()&&view.presentation()->session==8);QTRY_VERIFY(child<ui::OwnedLookupTreeWidget>(view,"azureus-lookup-tree-view").snapshot());QVERIFY(!rebind.callback);
    QPointer<ui::AzureusStatisticsView> victim=new ui::AzureusStatisticsView(ui::AzureusStatisticsSection::Operations);QVERIFY(victim->beginSession(7));show(*victim);QVERIFY(victim->setPresentation(fixture.packet(2)));
    auto& tree=child<QTreeView>(*victim,"azureus-lookup-tree");const auto root=tree.model()->index(0,0);tree.setExpanded(root,true);
    Hook remove;bool deleted=false;remove.callback=[&]{deleted=true;delete victim.data();};child<QPlainTextEdit>(*victim,"azureus-lookup-tree-details").installEventFilter(&remove);
    tree.setCurrentIndex(tree.model()->index(0,0,root));QVERIFY(victim);QTRY_VERIFY(deleted);QVERIFY(victim.isNull());QVERIFY(fixture.service->close());
  }
};
QTEST_MAIN(OwnedLookupTreeUiTests)
#include "owned_lookup_tree_ui_tests.moc"

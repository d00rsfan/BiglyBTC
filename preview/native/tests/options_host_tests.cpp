#include "session_bridge.hpp"
#include "library_window.hpp"
#include "options_dialog.hpp"
#include "profile.hpp"
#if defined(BIGLYBTC_SOCKS4_HOST_QUALIFIED_TESTS)
#include "network_policy.hpp"
#endif

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
using namespace biglybtc;
using Id = core::SettingId;
void write(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) { throw std::runtime_error("Cannot write owned settings fixture"); }
}
QByteArray read(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) { throw std::runtime_error("Cannot read owned settings fixture"); }
  return file.readAll();
}
struct Fixture {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-options-host-XXXXXX")};
  QString profile = root.path() + QStringLiteral("/profile");
  Fixture() {
    if (!root.isValid() || !QDir().mkdir(profile)) { throw std::runtime_error("Cannot create owned profile"); }
    write(profile + QStringLiteral("/downloads.config"), "d9:downloadslee");
    write(config(), "d7:unknownli7e3:rawee");
  }
  QString config() const { return profile + QStringLiteral("/biglybt.config"); }
};
ui::OptionsPresentation last(const QSignalSpy& spy) { return qvariant_cast<ui::OptionsPresentation>(spy.last().at(0)); }
ui::OptionsSavePresentation saved(const QSignalSpy& spy) { return qvariant_cast<ui::OptionsSavePresentation>(spy.last().at(0)); }
const core::SettingValue& value(const ui::OptionsPresentation& packet, Id id) {
  for (const auto& field : packet->snapshot->fields) { if (field.id == id && field.value) { return *field.value; } }
  throw std::runtime_error("Missing settings value");
}
}

class OptionsHostTests : public QObject {
  Q_OBJECT
private slots:
  void savesSparseOriginalValuesAndReturnsFreshRevision() {
    Fixture fixture; app::SessionBridge bridge;
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady);
    QSignalSpy reads(&bridge,&app::SessionBridge::optionsReady), writes(&bridge,&app::SessionBridge::optionsSaved);
    QVERIFY(bridge.open(fixture.profile.toStdString(),0));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000); QCOMPARE(sessions.size(),1);
    const auto session = sessions[0][0].toULongLong();
    const auto before = read(fixture.config());
    QVERIFY(bridge.loadOptions({session,1})); QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);
    auto snapshot=last(reads); QVERIFY(snapshot->snapshot); QVERIFY(ui::validOptionsValues(*snapshot->snapshot));
    QCOMPARE(read(fixture.config()),before); QVERIFY(!QFile::exists(fixture.config()+QStringLiteral(".bak")));
    QTRY_VERIFY(!bridge.busy());
    ui::OptionsSubmission change{session,2,snapshot->revision,{{Id::UploadLimit,std::int64_t{23}},{Id::MaxActive,std::int64_t{6}}}};
    QVERIFY(bridge.saveOptions(change)); QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);
    const auto result=saved(writes); QVERIFY2(result->saved,qPrintable(result->failure));
    QVERIFY(result->restartRequired); QVERIFY(result->snapshot->revision > snapshot->revision);
    QCOMPARE(std::get<std::int64_t>(value(result->snapshot,Id::UploadLimit)),23);
    QVERIFY(read(fixture.config()).contains("7:unknownli7e3:rawe"));
    QCOMPARE(read(fixture.config()+QStringLiteral(".bak")),before);
    QTRY_VERIFY(!bridge.busy());
    QVERIFY(bridge.saveOptions(change)); QTRY_COMPARE_WITH_TIMEOUT(writes.size(),2,10000);
    QVERIFY(!saved(writes)->saved); QVERIFY(!saved(writes)->failure.isEmpty());
  }
  void externalEditAndIncompatibleNetworkRejectBeforePublication() {
    Fixture fixture; app::SessionBridge bridge;
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady);
    QSignalSpy reads(&bridge,&app::SessionBridge::optionsReady), writes(&bridge,&app::SessionBridge::optionsSaved);
    QVERIFY(bridge.open(fixture.profile.toStdString(),0)); QTRY_VERIFY(!bridge.busy());
    const auto session=sessions[0][0].toULongLong();
    QVERIFY(bridge.loadOptions({session,1})); QTRY_COMPARE(reads.size(),1); QTRY_VERIFY(!bridge.busy());
    auto revision=last(reads)->revision;
    write(fixture.config(),"d7:unknownli9e3:rawee"); const auto external=read(fixture.config());
    QVERIFY(bridge.saveOptions({session,2,revision,{{Id::UploadLimit,std::int64_t{21}}}}));
    QTRY_COMPARE(writes.size(),1); QVERIFY(!saved(writes)->saved); QCOMPARE(read(fixture.config()),external);
    QVERIFY(!QFile::exists(fixture.config()+QStringLiteral(".bak"))); QTRY_VERIFY(!bridge.busy());
    QVERIFY(bridge.loadOptions({session,3})); QTRY_COMPARE(reads.size(),2); QTRY_VERIFY(!bridge.busy());
    revision=last(reads)->revision;
    QVERIFY(bridge.saveOptions({session,4,revision,{{Id::TcpPort,std::int64_t{18001}},{Id::UploadLimit,std::int64_t{22}}}}));
    QTRY_COMPARE(writes.size(),2); QVERIFY(!saved(writes)->saved); QCOMPARE(read(fixture.config()),external);
    QVERIFY(!QFile::exists(fixture.config()+QStringLiteral(".bak")));
  }
  void wrongSessionAndInvalidValuesHaveNoSideEffect() {
    Fixture fixture; app::SessionBridge bridge;
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady);
    QVERIFY(bridge.open(fixture.profile.toStdString(),0)); QTRY_VERIFY(!bridge.busy());
    const auto session=sessions[0][0].toULongLong();const auto before=read(fixture.config());
    QVERIFY(!bridge.loadOptions({session+1,1}));
    QVERIFY(!bridge.saveOptions({session+1,2,1,{{Id::UploadLimit,std::int64_t{10}}}}));
    QVERIFY(!bridge.saveOptions({session,3,1,{{Id::UploadLimit,std::string{"wrong type"}}}}));
    QVERIFY(!bridge.saveOptions({session,4,1,{{static_cast<Id>(999),true}}}));
    QCOMPARE(read(fixture.config()),before); QVERIFY(!bridge.busy());
  }
#if defined(BIGLYBTC_SOCKS4_HOST_QUALIFIED_TESTS)
  void verifiedSocksCapabilitySurvivesFailedStartupAndOptionsRepair() {
    // Required actual-image proof, no fake capability or skip. The malformed
    // TCP port fails before session/network creation; saving never restarts it.
    QVERIFY2(engine::verifiedSocks4Capability(), "Qualified SOCKS4 Host test requires actual source/image admission");
    Fixture fixture;
    const auto inventory=read(fixture.profile+QStringLiteral("/downloads.config"));
    QByteArray config="d7:unknownli7e3:rawe";
    const auto string=[&](const char* key,const QByteArray& value){
      const QByteArray name(key);config+=QByteArray::number(name.size())+':'+name;
      config+=QByteArray::number(value.size())+':'+value;
    };
    const auto integer=[&](const char* key,int value){
      const QByteArray name(key);config+=QByteArray::number(name.size())+':'+name+'i'+QByteArray::number(value)+'e';
    };
    integer("TCP.Listen.Port",70000);integer("TCP.Listen.Port.Enable",0);
    integer("peercontrol.udp.public.enable",0);integer("Server Enable UDP",0);
    integer("Peer Source Selection Default.Tracker",0);integer("Peer Source Selection Default.DHT",0);
    integer("Peer Source Selection Default.PeerExchange",0);integer("Plugin.DHT.dht.enabled",0);
    integer("Proxy.Data.Enable",1);string("Proxy.Data.SOCKS.version","V4a");
    string("Proxy.Host","127.0.0.1");string("Proxy.Port","1080");string("Proxy.Username","owned-user");
    config+='e';write(fixture.config(),config);
    app::SessionBridge bridge;
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),
        messages(&bridge,&app::SessionBridge::messageReady),native(&bridge,&app::SessionBridge::nativeSessionReady),
        reads(&bridge,&app::SessionBridge::optionsReady),writes(&bridge,&app::SessionBridge::optionsSaved),
        closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(fixture.profile.toStdString()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1&&!messages.empty(),10000);
    bool badPort=false;for(const auto& item:messages){badPort=badPort||item[0].toString().contains(QStringLiteral("TCP.Listen.Port"));}
    QVERIFY(badPort);QCOMPARE(native.size(),0);QCOMPARE(read(fixture.config()),config);
    QVERIFY(!QFile::exists(fixture.config()+QStringLiteral(".bak")));
    const auto session=sessions.first()[0].toULongLong();QVERIFY(session);
    QVERIFY(bridge.loadOptions({session,1}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    const auto snapshot=last(reads);QVERIFY(snapshot&&snapshot->snapshot);QVERIFY(ui::validOptionsValues(*snapshot->snapshot));
    bool invalidPort=false;for(const auto& field:snapshot->snapshot->fields){
      if(field.id==Id::TcpPort){invalidPort=field.state==ui::OptionsFieldState::OutOfRange;}
    }
    QVERIFY(invalidPort);QCOMPARE(std::get<std::string>(value(snapshot,Id::PeerProxyVersion)),std::string("V4a"));
    QVERIFY(bridge.saveOptions({session,2,snapshot->revision,{{Id::TcpPort,std::int64_t{6881}}}}));
    QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    const auto repair=saved(writes);QVERIFY2(repair->saved,qPrintable(repair->failure));QVERIFY(repair->restartRequired);
    QVERIFY(repair->snapshot&&repair->snapshot->revision>snapshot->revision);
    QCOMPARE(read(fixture.config()+QStringLiteral(".bak")),config);
    QVERIFY(bridge.loadOptions({session,3}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),2,10000);QTRY_VERIFY(!bridge.busy());
    const auto fresh=last(reads);QVERIFY(fresh&&fresh->snapshot);
    QCOMPARE(std::get<std::int64_t>(value(fresh,Id::TcpPort)),std::int64_t{6881});
    QCOMPARE(std::get<std::string>(value(fresh,Id::PeerProxyVersion)),std::string("V4a"));
    // Still no owner: a second ordinary sparse edit must reuse the retained
    // proof rather than depend on a successful policy/engine assignment.
    QVERIFY(bridge.saveOptions({session,4,fresh->revision,{{Id::UploadLimit,std::int64_t{23}}}}));
    QTRY_COMPARE_WITH_TIMEOUT(writes.size(),2,10000);QTRY_VERIFY(!bridge.busy());
    const auto second=saved(writes);QVERIFY2(second->saved,qPrintable(second->failure));QVERIFY(second->restartRequired);
    QCOMPARE(std::get<std::int64_t>(value(second->snapshot,Id::UploadLimit)),std::int64_t{23});
    QVERIFY(read(fixture.config()).contains("7:unknownli7e3:rawe"));QCOMPARE(native.size(),0);
    QCOMPARE(read(fixture.profile+QStringLiteral("/downloads.config")),inventory);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);
    QVERIFY(closed.first()[1].toBool());QVERIFY(closed.first()[2].toBool());
    auto released=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(released);
    const auto document=core::Document::parse(read(fixture.config()).toStdString());QVERIFY(document);
    const auto* proxy=document->find("Proxy.Username");QVERIFY(proxy&&proxy->type==core::Node::Type::Bytes);
    QCOMPARE(proxy->bytes,std::string("owned-user"));QCOMPARE(native.size(),0);
  }
#endif
  void toolsMenuOpensBoundDialogAndUsesCompatibleSave() {
    Fixture fixture;app::SessionBridge bridge;ui::LibraryWindow window;
    connect(&bridge,&app::SessionBridge::snapshotReady,&window,&ui::LibraryWindow::setPresentation);
    connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,&window,&ui::LibraryWindow::beginColumnLayoutSession);
    connect(&bridge,&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);
    connect(&bridge,&app::SessionBridge::optionsReady,&window,&ui::LibraryWindow::setOptions);
    connect(&bridge,&app::SessionBridge::optionsSaved,&window,&ui::LibraryWindow::setOptionsSaved);
    connect(&window,&ui::LibraryWindow::optionsRequested,&bridge,[&](ui::OptionsRequest request){if(!bridge.loadOptions(request)){window.rejectOptionsRequest(request);}});
    connect(&window,&ui::LibraryWindow::optionsSaveRequested,&bridge,[&](ui::OptionsSubmission request){if(!bridge.saveOptions(request)){window.rejectOptionsSave(request);}});
    QVERIFY(bridge.open(fixture.profile.toStdString(),0));QTRY_VERIFY(!bridge.busy());
    auto* action=window.findChild<QAction*>(QStringLiteral("classic-options"));QVERIFY(action);QVERIFY(action->isEnabled());
    window.show();action->trigger();
    auto* dialog=window.findChild<ui::OptionsDialog*>();QVERIFY(dialog);
    QTRY_VERIFY_WITH_TIMEOUT(dialog->revision()!=0,10000);QVERIFY(!dialog->dirty());
    QCOMPARE(dialog->session()>0,true); QVERIFY(window.close());
    QVERIFY(!window.isVisible());
  }
};
QTEST_MAIN(OptionsHostTests)
#include "options_host_tests.moc"

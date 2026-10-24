#include "session_bridge.hpp"
#include "library_window.hpp"
#include "options_dialog.hpp"
#include "profile.hpp"

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

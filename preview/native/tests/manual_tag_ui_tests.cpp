// SPDX-License-Identifier: GPL-2.0-or-later
// Presentation/controller tests only; packets grant no profile/live authority.
#include "library_window.hpp"
#include "manual_tag_editor.hpp"
#include "biglybtc/saved_download_tag_edits.hpp"
#include <QPlainTextEdit>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
namespace ui=biglybtc::ui;namespace core=biglybtc::core;
namespace {
std::string b(std::string_view x){return std::to_string(x.size())+':'+std::string(x);}
std::string d(std::initializer_list<std::pair<std::string,std::string>> rows){std::string out="d";for(const auto& [k,v]:rows){out+=b(k)+v;}return out+'e';}
const QString Hash=QStringLiteral("000102030405060708090A0B0C0D0E0F10111213");
std::string tags(bool assigned=false){return d({{"3",d({{"1",d({{"n",b("Movies")},{"o",assigned?"l32:AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQTe":"le"},{"p",assigned?"ld1:ai123eee":"le"}})}})}});}
ui::Presentation library(quint64 revision){auto p=std::make_shared<ui::LibraryPresentation>();p->revision=revision;ui::LibraryItem row;row.key=Hash;row.hash=Hash;row.name=QStringLiteral("A torrent");row.state=QStringLiteral("Stopped");row.bytes=123;row.completed=0;p->items.push_back(row);return p;}
ui::ManualTagCommand take(QSignalSpy& spy){if(spy.isEmpty()){qFatal("Missing actual UI command");}return qvariant_cast<ui::ManualTagCommand>(spy.takeFirst().front());}
ui::ManualTagCommandPresentation loaded(const ui::ManualTagCommand& command,quint64 generation=40,quint64 revision=9,std::string wire=tags(),ui::ManualTagCommandStatus status=ui::ManualTagCommandStatus::Loaded){
  auto parsed=core::readSavedDownloadTags(wire);if(!parsed){qFatal("Invalid owned UI fixture");}auto p=std::make_shared<ui::ManualTagCommandPacket>();p->session=command.session;p->request=command.request;p->kind=command.kind;p->generation=generation;p->libraryRevision=revision;p->status=status;p->readState=wire=="de"?ui::ManualTagReadState::Missing:ui::ManualTagReadState::Ready;p->writable=true;p->changed=status==ui::ManualTagCommandStatus::Saved;p->tags=std::make_shared<const core::SavedDownloadTags>(std::move(*parsed));return p;
}
ui::ManualTagManagerDialog* open(ui::LibraryWindow& window){auto* a=window.findChild<QAction*>(QStringLiteral("classic-manual-tags"));if(!a){qFatal("Missing manager action");}a->trigger();return window.findChild<ui::ManualTagManagerDialog*>();}
void setup(ui::LibraryWindow& window){window.setOfflinePreview(false);window.beginColumnLayoutSession(7);window.setPresentation(library(9));}
}
class ManualTagUiTests final:public QObject {
  Q_OBJECT
private slots:
  void firstConstraintOnSavedMembersRequiresOriginalUserConfirmation(){
    ui::ManualTagCommand request;request.session=7;request.request=1;
    ui::ManualTagManagerDialog dialog(7);dialog.setPresentation(loaded(request,40,9,tags(true)));dialog.show();QSignalSpy commands(&dialog,&ui::ManualTagManagerDialog::commandRequested);
    auto* expression=dialog.findChild<QPlainTextEdit*>(QStringLiteral("manual-tag-constraint-expression"));auto* save=dialog.findChild<QPushButton*>(QStringLiteral("manual-tag-constraint-save"));QVERIFY(expression&&save);expression->setPlainText(QStringLiteral("true"));
    const auto respond=[&](QMessageBox::StandardButton answer){
      bool clicked=false;QTimer response,watchdog;response.setSingleShot(true);watchdog.setSingleShot(true);
      connect(&response,&QTimer::timeout,&dialog,[&]{
        auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if(!box||box->parentWidget()!=&dialog||box->standardButtons()!=(QMessageBox::Ok|QMessageBox::Cancel)){return;}
        if(auto* button=box->button(answer);button&&button->isEnabled()){clicked=true;button->click();}
      });
      connect(&watchdog,&QTimer::timeout,&dialog,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());box&&box->parentWidget()==&dialog){box->reject();}});
      response.start(0);watchdog.start(1500);save->click();response.stop();watchdog.stop();return clicked;
    };
    QVERIFY(save->isEnabled());QVERIFY(respond(QMessageBox::Cancel));QVERIFY(commands.isEmpty());
    QVERIFY(save->isEnabled());QVERIFY(respond(QMessageBox::Ok));QCOMPARE(commands.count(),1);QCOMPARE(take(commands).kind,ui::ManualTagCommandKind::SetConstraint);
  }
  void constraintIntentKeepsPendingSourceAndBusyLifecycle(){
    ui::LibraryWindow window;setup(window);QSignalSpy commands(&window,&ui::LibraryWindow::manualTagCommandRequested);
    const QPointer<ui::ManualTagManagerDialog> dialog(open(window));QVERIFY(dialog);window.setManualTagCommandFinished(loaded(take(commands)));
    auto* expression=dialog->findChild<QPlainTextEdit*>(QStringLiteral("manual-tag-constraint-expression"));
    auto* options=dialog->findChild<QLineEdit*>(QStringLiteral("manual-tag-constraint-options"));
    auto* enabled=dialog->findChild<QCheckBox*>(QStringLiteral("manual-tag-constraint-enabled"));
    auto* save=dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-constraint-save"));QVERIFY(expression&&options&&enabled&&save&&save->isEnabled());
    expression->setPlainText(QStringLiteral("  size>0  "));options->setText(QStringLiteral("am=1;future;"));save->click();
    const auto request=take(commands);QCOMPARE(request.kind,ui::ManualTagCommandKind::SetConstraint);QCOMPARE(request.tagId,1);QCOMPARE(request.generation,quint64(40));
    QCOMPARE(request.constraintExpression,QStringLiteral("size>0"));QCOMPARE(request.constraintOptions,QStringLiteral("am=1;future;"));QVERIFY(request.constraintEnabled);
    window.setBusy(true);QVERIFY(dialog&&dialog->isVisible());QVERIFY(!save->isEnabled());QCOMPARE(expression->toPlainText(),QStringLiteral("  size>0  "));
    window.setBusy(false);QVERIFY(!save->isEnabled());
    auto changed=core::editSavedDownloadTags(tags(),core::SavedTagSetConstraint{1,"size>0","am=1;future;",true});QVERIFY(changed);
    auto packet=std::make_shared<ui::ManualTagCommandPacket>(*loaded(request,41,9,changed->value.source->originalBytes(),ui::ManualTagCommandStatus::Saved));packet->constraintStatus=QStringLiteral("Configured; current facts required");
    auto stale=std::make_shared<ui::ManualTagCommandPacket>(*packet);++stale->request;window.setManualTagCommandFinished(stale);QVERIFY(!save->isEnabled());
    window.setManualTagCommandFinished(packet);QVERIFY(save->isEnabled());QCOMPARE(expression->toPlainText(),QStringLiteral("size>0"));QCOMPARE(options->text(),QStringLiteral("am=1;future;"));
    QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("manual-tag-status"))->text().contains(QStringLiteral("current facts")));
    window.close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!dialog);window.setManualTagCommandFinished(packet);QVERIFY(!window.findChild<ui::ManualTagManagerDialog*>());
  }
  void workerBusyKeepsPendingDialogUntilActualResult(){
    ui::LibraryWindow window;setup(window);QSignalSpy commands(&window,&ui::LibraryWindow::manualTagCommandRequested);
    const QPointer<ui::ManualTagManagerDialog> dialog(open(window));QVERIFY(dialog);const auto reload=take(commands);
    window.setBusy(true);QVERIFY(dialog&&dialog->isVisible());
    window.setManualTagCommandFinished(loaded(reload));QVERIFY(dialog&&dialog->isVisible());
    auto* create=dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-new"));
    auto* name=dialog->findChild<QLineEdit*>(QStringLiteral("manual-tag-name"));
    auto* table=dialog->findChild<QTableWidget*>(QStringLiteral("manual-tag-table"));QVERIFY(create&&name&&table);
    QCOMPARE(table->rowCount(),1);QVERIFY(!create->isEnabled());
    window.setBusy(false);QVERIFY(dialog&&dialog->isVisible());name->setText(QStringLiteral("Series"));QVERIFY(create->isEnabled());
    create->click();QCOMPARE(commands.size(),1);const auto edit=take(commands);QCOMPARE(edit.kind,ui::ManualTagCommandKind::Create);QCOMPARE(edit.generation,quint64(40));
    window.setBusy(true);QVERIFY(dialog&&dialog->isVisible());QVERIFY(!create->isEnabled());QCOMPARE(table->rowCount(),1);
    window.setBusy(false);QVERIFY(dialog&&dialog->isVisible());QVERIFY(!create->isEnabled()); // Still awaiting this exact worker result.
    const auto next=d({{"3",d({{"1",d({{"n",b("Movies")}})},{"3",d({{"n",b("Series")}})}})}});
    window.setManualTagCommandFinished(loaded(edit,41,9,next,ui::ManualTagCommandStatus::Saved));
    QVERIFY(dialog&&dialog->isVisible());QCOMPARE(table->rowCount(),2);QVERIFY(create->isEnabled());QVERIFY(commands.isEmpty());
    QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("manual-tag-status"))->text().contains(QStringLiteral("Saved")));
    window.close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!dialog);
    window.setManualTagCommandFinished(loaded(edit,99));QVERIFY(!window.findChild<ui::ManualTagManagerDialog*>());
  }
  void managerCommandsUseActualResultAndKeepClassicShortcuts(){
    ui::LibraryWindow window;setup(window);QSignalSpy commands(&window,&ui::LibraryWindow::manualTagCommandRequested);auto* dialog=open(window);QVERIFY(dialog);QCOMPARE(commands.size(),1);const auto request=take(commands);QCOMPARE(request.kind,ui::ManualTagCommandKind::Reload);QCOMPARE(request.session,quint64(7));QVERIFY(request.request);QCOMPARE(request.generation,quint64(0));
    window.setManualTagCommandFinished(loaded(request));auto* name=dialog->findChild<QLineEdit*>(QStringLiteral("manual-tag-name"));auto* create=dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-new"));QVERIFY(name&&create);name->setText(QStringLiteral("  Series  "));QVERIFY(create->isEnabled());create->click();QCOMPARE(commands.size(),1);const auto submitted=take(commands);QCOMPARE(submitted.kind,ui::ManualTagCommandKind::Create);QCOMPARE(submitted.name,QStringLiteral("Series"));QCOMPARE(submitted.generation,quint64(40));QVERIFY(submitted.request>request.request);QVERIFY(!create->isEnabled());
    auto* table=dialog->findChild<QTableWidget*>(QStringLiteral("manual-tag-table"));QCOMPARE(table->rowCount(),1); // No invented row before durable reply.
    const auto next=d({{"3",d({{"1",d({{"n",b("Movies")}})},{"3",d({{"n",b("Series")}})}})}});window.setManualTagCommandFinished(loaded(submitted,41,9,next,ui::ManualTagCommandStatus::Saved));QCOMPARE(table->rowCount(),2);QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("manual-tag-status"))->text().contains(QStringLiteral("Saved")));
    auto* myTorrents=window.findChild<QAction*>(QStringLiteral("classic-my-torrents"));QVERIFY(myTorrents);QCOMPARE(myTorrents->shortcut(),QKeySequence(QStringLiteral("Ctrl+1")));QVERIFY(window.findChild<QAction*>(QStringLiteral("classic-statistics")));
  }
  void stalePacketsAndSourceFailureCannotEnableEdits(){
    ui::LibraryWindow window;setup(window);QSignalSpy commands(&window,&ui::LibraryWindow::manualTagCommandRequested);auto* dialog=open(window);QVERIFY(dialog);const auto request=take(commands);auto wrong=std::make_shared<ui::ManualTagCommandPacket>(*loaded(request));wrong->session=8;window.setManualTagCommandFinished(wrong);QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-new"))->isEnabled());wrong->session=7;wrong->request++;window.setManualTagCommandFinished(wrong);QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-new"))->isEnabled());window.setManualTagCommandFinished(loaded(request));
    auto* rename=dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-rename"));auto* name=dialog->findChild<QLineEdit*>(QStringLiteral("manual-tag-name"));name->setText(QStringLiteral("New name"));rename->click();const auto edit=take(commands);auto failed=std::make_shared<ui::ManualTagCommandPacket>();failed->session=edit.session;failed->request=edit.request;failed->kind=edit.kind;failed->status=ui::ManualTagCommandStatus::Rejected;failed->failure=QStringLiteral("Source changed");window.setManualTagCommandFinished(failed);QVERIFY(!rename->isEnabled());QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("manual-tag-status"))->text().contains(QStringLiteral("Rejected")));QVERIFY(dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-reload"))->isEnabled());
    window.beginColumnLayoutSession(8);window.setManualTagCommandFinished(loaded(edit,90));QVERIFY(!dialog->isVisible());
  }
  void membershipMenuBindsSelectionSourceAndAsyncCheckbox(){
    ui::LibraryWindow window;setup(window);QSignalSpy commands(&window,&ui::LibraryWindow::manualTagCommandRequested);auto* dialog=open(window);QVERIFY(dialog);window.setManualTagCommandFinished(loaded(take(commands)));auto* menu=window.findChild<QMenu*>(QStringLiteral("classic-torrent-tags"));QVERIFY(menu);QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));auto* member=menu->findChild<QAction*>(QStringLiteral("manual-tag-member-1"));QVERIFY(member&&member->isEnabled()&&!member->isChecked());member->trigger();QCOMPARE(commands.size(),1);const auto assign=take(commands);QCOMPARE(assign.kind,ui::ManualTagCommandKind::AddMember);QCOMPARE(assign.member.hash,Hash);QCOMPARE(assign.member.generation,quint64(9));QCOMPARE(assign.generation,quint64(40));QVERIFY(!member->isChecked());
    window.setManualTagCommandFinished(loaded(assign,41,9,tags(true),ui::ManualTagCommandStatus::Saved));QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));member=menu->findChild<QAction*>(QStringLiteral("manual-tag-member-1"));QVERIFY(member&&member->isChecked());window.setPresentation(library(10));member->trigger();QVERIFY(commands.isEmpty()); // Captured Target is retired.
    QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));member=menu->findChild<QAction*>(QStringLiteral("manual-tag-member-1"));QVERIFY(member&&!member->isEnabled());
  }
  void malformedRecoveryAndExactAutomationDirectionsAreReadonly(){
    ui::ManualTagCommand request;request.session=7;request.request=1;auto packet=std::make_shared<ui::ManualTagCommandPacket>(*loaded(request));packet->writable=false;ui::ManualTagManagerDialog dialog(7);dialog.setPresentation(packet);QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("manual-tag-rename"))->isEnabled());QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("manual-tag-new"))->isEnabled());
    const auto malformed=d({{"3",d({{"1",d({{"n",b("name")},{"o",b("bad")}})}})}});auto values=ui::manualTagEditorValues(loaded(request,40,9,malformed));QVERIFY(values&&!values->rows[0].editable);
    const auto keys=d({{"3",d({{"1",d({{"n",b("tag.local.key")},{"c",d({{"pp.constraint","l4:true5:am=1;e"}})}})}})}});values=ui::manualTagEditorValues(loaded(request,40,9,keys));QVERIFY(values&&values->rows[0].label.contains(QStringLiteral("localization key")));QVERIFY(!values->rows[0].canAddMember&&values->rows[0].canRemoveMember);
    const auto disabled=d({{"3",d({{"1",d({{"n",b("manual")},{"c",d({{"pp.constraint","l4:truee"},{"pp.enabled.constraint","i0e"}})}})}})}});values=ui::manualTagEditorValues(loaded(request,40,9,disabled));QVERIFY(values&&values->rows[0].canAddMember&&values->rows[0].canRemoveMember);
  }
  void deleteDeclineCancelAndCloseDoNotForgeResults(){
    ui::LibraryWindow window;setup(window);QSignalSpy commands(&window,&ui::LibraryWindow::manualTagCommandRequested);auto* dialog=open(window);QVERIFY(dialog);window.setManualTagCommandFinished(loaded(take(commands)));auto* remove=dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-delete"));QVERIFY(remove&&remove->isEnabled());QTimer::singleShot(0,[]{for(auto* widget:QApplication::topLevelWidgets()){if(auto* box=qobject_cast<QMessageBox*>(widget)){box->done(QMessageBox::No);}}});remove->click();QVERIFY(commands.isEmpty());
    dialog->findChild<QLineEdit*>(QStringLiteral("manual-tag-name"))->setText(QStringLiteral("new"));dialog->findChild<QPushButton*>(QStringLiteral("manual-tag-rename"))->click();const auto request=take(commands);auto cancelled=std::make_shared<ui::ManualTagCommandPacket>();cancelled->session=request.session;cancelled->request=request.request;cancelled->kind=request.kind;cancelled->status=ui::ManualTagCommandStatus::Cancelled;cancelled->failure=QStringLiteral("Cancelled before publication");window.setManualTagCommandFinished(cancelled);QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("manual-tag-status"))->text().contains(QStringLiteral("Cancelled")));QCOMPARE(dialog->findChild<QTableWidget*>(QStringLiteral("manual-tag-table"))->item(0,0)->text(),QStringLiteral("Movies"));
    dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);window.setManualTagCommandFinished(loaded(request,99));QVERIFY(!window.findChild<ui::ManualTagManagerDialog*>());
  }
  void queuedDestructionKeepsIntentOwningAndViewGuarded(){
    auto* window=new ui::LibraryWindow;setup(*window);QObject context;int dispatched=0;
    const QPointer<ui::LibraryWindow> view(window);
    connect(window,&ui::LibraryWindow::manualTagCommandRequested,&context,[view,&dispatched](ui::ManualTagCommand){if(view){++dispatched;}},Qt::QueuedConnection);
    QVERIFY(open(*window));delete window;QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCOMPARE(dispatched,0);
  }
  void managerCapture(){
    ui::ManualTagCommand request;request.session=7;request.request=1;ui::ManualTagManagerDialog dialog(7);dialog.setPresentation(loaded(request));dialog.show();QVERIFY(dialog.isVisible());
    const auto path=qEnvironmentVariable("BIGLYBTC_MANUAL_TAG_CAPTURE");if(!path.isEmpty()){QVERIFY(QFileInfo(path).isAbsolute());QVERIFY(!QFileInfo::exists(path));QVERIFY(dialog.grab().save(path));}
    QVERIFY(dialog.findChild<QTableWidget*>(QStringLiteral("manual-tag-table"))->rowCount()==1);
  }
};
QTEST_MAIN(ManualTagUiTests)
#include "manual_tag_ui_tests.moc"

#include "torrent_intake_dialog.hpp"
#include "library_window.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QPixmap>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableView>
#include <QtTest>
#include <memory>

using namespace biglybtc;
namespace {
// Explicit synthetic GUI packets: no file access or producer qualification.
ui::TorrentIntakePresentation packet(ui::TorrentIntakeRequest request,quint64 revision=1) {
  auto value=std::make_shared<ui::TorrentIntakePacket>();value->source=std::move(request);value->revision=revision;
  value->hash=QString(40,QLatin1Char('a'));value->name=QStringLiteral("bundle");
  value->files={{0,QStringLiteral("first.bin"),100,false},{1,QStringLiteral("Sub/second.bin"),200,false},{2,QStringLiteral(".pad/0"),0,true}};
  return value;
}
void inputs(ui::TorrentIntakeDialog& dialog) {
  dialog.findChild<QLineEdit*>(QStringLiteral("import-source"))->setText(QStringLiteral("/tmp/biglybtc-synthetic-preview.torrent"));
  dialog.findChild<QLineEdit*>(QStringLiteral("import-destination"))->setText(QStringLiteral("/tmp/biglybtc-synthetic-destination"));
}
ui::TorrentIntakeRequest request(const QSignalSpy& spy){return qvariant_cast<ui::TorrentIntakeRequest>(spy.last()[0]);}
ui::TorrentIntakeSubmission submission(const QSignalSpy& spy){return qvariant_cast<ui::TorrentIntakeSubmission>(spy.last()[0]);}
void empty(ui::LibraryWindow& window) {
  auto rows=std::make_shared<ui::LibraryPresentation>();rows->revision=1;window.setPresentation(rows);window.beginColumnLayoutSession(77);
}
}
class TorrentIntakeDialogTests final:public QObject {
  Q_OBJECT
private slots:

  void verifiedSeedingModeHoldsStartAndRequiresEveryRealFile() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();
    QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted);
    auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();
    auto frame=std::make_shared<ui::TorrentIntakePacket>(*packet(request(reads)));frame->files.pop_back();dialog.setPreview(frame);
    auto* table=dialog.findChild<QTableView*>(QStringLiteral("import-file-table"));QTRY_COMPARE(table->model()->rowCount(),2);
    auto* mode=dialog.findChild<QComboBox*>(QStringLiteral("import-start-mode"));auto* add=dialog.findChild<QPushButton*>(QStringLiteral("import-confirm"));
    QTRY_VERIFY(add->isEnabled());mode->setCurrentIndex(mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::VerifySeeding)));
    dialog.accept();QCOMPARE(writes.size(),1);const auto sent=submission(writes);QVERIFY(sent.verifyExisting&&!sent.start&&!sent.forceStart&&!sent.paused);
    dialog.rejectSubmission(sent);QTRY_VERIFY(add->isEnabled());QCOMPARE(mode->currentData().toInt(),static_cast<int>(ui::TorrentIntakeStartMode::VerifySeeding));
    QPointer<QLabel> status=dialog.findChild<QLabel*>(QStringLiteral("import-file-status"));QPointer<QPushButton> confirm=add;
    QTRY_VERIFY(status&&status->text().contains(QStringLiteral("Read-only")));
    // render() publishes text before Qt processes the resulting LayoutRequest.
    // Wait for the wrapped text to fit above the buttons at the unchanged size.
    QTRY_VERIFY(status&&confirm&&status->heightForWidth(status->width())>0
        &&status->height()>=status->heightForWidth(status->width())
        &&dialog.rect().contains(status->geometry())
        &&status->geometry().bottom()<confirm->mapTo(&dialog,QPoint(0,0)).y());
    const auto capture=qEnvironmentVariable("BIGLYBTC_CAPTURE_DIR");if(!capture.isEmpty()){QDir output(capture);QVERIFY(output.mkpath("."));QVERIFY(dialog.grab().save(output.filePath(QStringLiteral("torrent-intake-verified-seeding.png"))));}
    QVERIFY(table->model()->setData(table->model()->index(0,0),Qt::Unchecked,Qt::CheckStateRole));dialog.accept();QCOMPARE(writes.size(),1);
    QVERIFY(dialog.beginSession(78));QCOMPARE(mode->currentData().toInt(),static_cast<int>(ui::TorrentIntakeStartMode::Queued));QVERIFY(!dialog.preview());
  }

  void saveNameDraftPreservesDefaultAndCustomLeafAcrossFailure() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();
    QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted);
    auto* name=dialog.findChild<QLineEdit*>(QStringLiteral("import-save-name"));auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QVERIFY(name&&load);
    QTRY_VERIFY(load->isEnabled());load->click();dialog.setPreview(packet(request(reads)));auto* table=dialog.findChild<QTableView*>(QStringLiteral("import-file-table"));QVERIFY(table);QTRY_COMPARE(table->model()->rowCount(),3);
    auto* add=dialog.findChild<QPushButton*>(QStringLiteral("import-confirm"));QVERIFY(add);QTRY_VERIFY(name->isEnabled()&&add->isEnabled());QVERIFY(name->text().isEmpty());QCOMPARE(name->placeholderText(),QStringLiteral("bundle"));
    dialog.accept();QCOMPARE(writes.size(),1);QVERIFY(!submission(writes).saveName);dialog.rejectSubmission(submission(writes));QTRY_VERIFY(name->isEnabled());
    const auto custom=QString::fromUtf8("saved-\xd0\xbf\xd0\xb0\xd0\xbf\xd0\xba\xd0\xb0");name->setText(custom);QTRY_VERIFY(add->isEnabled());dialog.accept();QCOMPARE(writes.size(),2);const auto sent=submission(writes);QVERIFY(sent.saveName&&*sent.saveName==custom);QTRY_VERIFY(!name->isEnabled());
    auto failed=std::make_shared<ui::TorrentIntakeResultPacket>();failed->session=sent.session;failed->request=sent.request;failed->revision=sent.revision;failed->failure=QStringLiteral("Owned destination conflict");dialog.setResult(failed);QTRY_VERIFY(name->isEnabled());QCOMPARE(name->text(),custom);
    const auto capture=qEnvironmentVariable("BIGLYBTC_CAPTURE_DIR");if(!capture.isEmpty()){QDir directory(capture);QVERIFY(directory.mkpath(QStringLiteral(".")));QVERIFY(dialog.grab().save(directory.filePath(QStringLiteral("torrent-intake-save-name.png"))));}
    for(const auto& invalid:{QStringLiteral("../escape"),QStringLiteral("part\\leaf"),QStringLiteral(".BiglyBTC.control"),QStringLiteral("   "),QString(128,QChar(0x044f))}){name->setText(invalid);QTRY_VERIFY(!add->isEnabled());dialog.accept();QCOMPARE(writes.size(),2);}
    name->setText(QStringLiteral("bundle"));QTRY_VERIFY(add->isEnabled());dialog.setBusy(true);QTRY_VERIFY(!name->isEnabled());dialog.accept();QCOMPARE(writes.size(),2);dialog.setBusy(false);QTRY_VERIFY(name->isEnabled());dialog.accept();QCOMPARE(writes.size(),3);QVERIFY(submission(writes).saveName&&*submission(writes).saveName==QStringLiteral("bundle"));dialog.rejectSubmission(submission(writes));
    QVERIFY(dialog.beginSession(78));QVERIFY(name->text().isEmpty());QVERIFY(!dialog.preview());dialog.accept();QCOMPARE(writes.size(),3);
  }
  void saveNameResetYieldsToRebindDeletionAndMissingChild() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));auto* name=dialog.findChild<QLineEdit*>(QStringLiteral("import-save-name"));auto* destination=dialog.findChild<QLineEdit*>(QStringLiteral("import-destination"));QVERIFY(name&&destination);name->setText(QStringLiteral("old-name"));bool rebound=false;
    connect(name,&QLineEdit::textChanged,&dialog,[&](const QString& text){if(text.isEmpty()&&!rebound){rebound=true;QVERIFY(dialog.beginSession(79));}});
    QVERIFY(!dialog.beginSession(78,QStringLiteral("/old78")));QVERIFY(rebound);QVERIFY(name->text().isEmpty());QVERIFY(destination->text().isEmpty());QVERIFY(!dialog.beginSession(78));
    QPointer<ui::TorrentIntakeDialog> deleted=new ui::TorrentIntakeDialog;QVERIFY(deleted->beginSession(90));auto* text=deleted->findChild<QLineEdit*>(QStringLiteral("import-save-name"));text->setText(QStringLiteral("old"));connect(text,&QLineEdit::textChanged,this,[&]{delete deleted.data();});QVERIFY(!deleted->beginSession(91));QVERIFY(deleted.isNull());QCoreApplication::processEvents();
    ui::TorrentIntakeDialog intact;QVERIFY(intact.beginSession(101));inputs(intact);intact.show();QSignalSpy reads(&intact,&ui::TorrentIntakeDialog::previewRequested),writes(&intact,&ui::TorrentIntakeDialog::submitted);auto* load=intact.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();intact.setPreview(packet(request(reads)));auto* table=intact.findChild<QTableView*>(QStringLiteral("import-file-table"));QVERIFY(table);QTRY_COMPARE(table->model()->rowCount(),3);auto* add=intact.findChild<QPushButton*>(QStringLiteral("import-confirm"));QTRY_VERIFY(add->isEnabled());
    delete intact.findChild<QLineEdit*>(QStringLiteral("import-save-name"));intact.accept();QCOMPARE(writes.size(),0);QCoreApplication::processEvents();QCOMPARE(writes.size(),0);
  }
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
  void trackerDraftPreservesDefaultAndExplicitTiersOrClearAcrossFailure() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();
    QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted);
    auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();
    auto source=std::make_shared<ui::TorrentIntakePacket>(*packet(request(reads)));
    source->trackers=core::TorrentIntakeTrackerFields{"http://keep.invalid/announce",{{"https://other.invalid/a"}}};dialog.setPreview(source);
    auto* table=dialog.findChild<QTableView*>(QStringLiteral("import-file-table"));QVERIFY(table);QTRY_COMPARE(table->model()->rowCount(),3);
    auto* edit=dialog.findChild<QCheckBox*>(QStringLiteral("import-edit-trackers"));auto* text=dialog.findChild<QPlainTextEdit*>(QStringLiteral("import-trackers"));auto* add=dialog.findChild<QPushButton*>(QStringLiteral("import-confirm"));QVERIFY(edit&&text&&add);
    QTRY_VERIFY(add->isEnabled());QVERIFY(!edit->isChecked());QVERIFY(text->isReadOnly());QCOMPARE(text->toPlainText(),QStringLiteral("http://keep.invalid/announce\n\nhttps://other.invalid/a"));
    add->click();QCOMPARE(writes.size(),1);QVERIFY(!submission(writes).trackers);dialog.rejectSubmission(submission(writes));QTRY_VERIFY(add->isEnabled());
    edit->setChecked(true);text->setPlainText(QStringLiteral("https://new.invalid/a\nhttps://new.invalid/a\n\nudp://127.0.0.1:9/a"));QTRY_VERIFY(add->isEnabled());add->click();QCOMPARE(writes.size(),2);
    const core::TorrentIntakeTrackerTiers expected{{"https://new.invalid/a","https://new.invalid/a"},{"udp://127.0.0.1:9/a"}};
    QVERIFY(submission(writes).trackers&&*submission(writes).trackers==expected);const auto draft=text->toPlainText();
    auto failure=std::make_shared<ui::TorrentIntakeResultPacket>();const auto sent=submission(writes);failure->session=sent.session;failure->request=sent.request;failure->revision=sent.revision;failure->failure=QStringLiteral("Owned inventory conflict");dialog.setResult(failure);
    QTRY_VERIFY(add->isEnabled());QVERIFY(edit->isChecked());QCOMPARE(text->toPlainText(),draft);
    const auto capture=qEnvironmentVariable("BIGLYBTC_CAPTURE_DIR");if(!capture.isEmpty()){QDir directory(capture);QVERIFY(directory.mkpath(QStringLiteral(".")));QVERIFY(dialog.grab().save(directory.filePath(QStringLiteral("torrent-intake-tracker-tiers.png"))));}
    text->clear();QTRY_VERIFY(add->isEnabled());add->click();QCOMPARE(writes.size(),3);QVERIFY(submission(writes).trackers&&submission(writes).trackers->empty());
    dialog.rejectSubmission(submission(writes));QTRY_VERIFY(add->isEnabled());edit->setChecked(false);QTRY_VERIFY(add->isEnabled());add->click();QCOMPARE(writes.size(),4);QVERIFY(!submission(writes).trackers);
  }
  void unrepresentableSourceTrackersRemainImportableButNeverBecomeEditableEmpty() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();
    QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted);
    auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();
    auto source=std::make_shared<ui::TorrentIntakePacket>(*packet(request(reads)));source->trackers.reset();QVERIFY(ui::validTorrentIntake(source));dialog.setPreview(source);
    auto* table=dialog.findChild<QTableView*>(QStringLiteral("import-file-table"));QVERIFY(table);QTRY_COMPARE(table->model()->rowCount(),3);
    auto* edit=dialog.findChild<QCheckBox*>(QStringLiteral("import-edit-trackers"));auto* text=dialog.findChild<QPlainTextEdit*>(QStringLiteral("import-trackers"));auto* add=dialog.findChild<QPushButton*>(QStringLiteral("import-confirm"));QVERIFY(edit&&text&&add);
    QTRY_VERIFY(add->isEnabled());QVERIFY(!edit->isEnabled());QVERIFY(!text->isEnabled());QVERIFY(text->isReadOnly());
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("import-file-status"))->text().contains(QStringLiteral("kept unchanged")));
    add->click();QCOMPARE(writes.size(),1);QVERIFY(!submission(writes).trackers);dialog.rejectSubmission(submission(writes));QTRY_VERIFY(add->isEnabled());
    edit->setChecked(true);QTRY_VERIFY(!add->isEnabled());dialog.accept();QCOMPARE(writes.size(),1); // Programmatic misuse cannot grant edit authority.
  }
  void malformedTrackerDraftAndDeletedEditorCannotSubmit() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted);
    auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();dialog.setPreview(packet(request(reads)));
    auto* edit=dialog.findChild<QCheckBox*>(QStringLiteral("import-edit-trackers"));auto* text=dialog.findChild<QPlainTextEdit*>(QStringLiteral("import-trackers"));auto* add=dialog.findChild<QPushButton*>(QStringLiteral("import-confirm"));QVERIFY(edit&&text&&add);QTRY_VERIFY(add->isEnabled());edit->setChecked(true);
    for(const auto& value:{QStringLiteral("file:///tmp/wrong"),QStringLiteral("http://bad host/a"),QString(65537,u'x')}){text->setPlainText(value);QTRY_VERIFY(!add->isEnabled());dialog.accept();QCOMPARE(writes.size(),0);}
    text->setPlainText(QStringLiteral("https://valid.invalid/a"));QTRY_VERIFY(add->isEnabled());delete text;dialog.accept();QCOMPARE(writes.size(),0);QCoreApplication::processEvents();QCOMPARE(writes.size(),0);
  }
  void trackerPrefillAndProfileResetYieldToReentrantBindingOrDeletion() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted);
    auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();
    auto source=std::make_shared<ui::TorrentIntakePacket>(*packet(request(reads)));source->trackers->announce="https://old.invalid/a";
    auto* text=dialog.findChild<QPlainTextEdit*>(QStringLiteral("import-trackers"));QVERIFY(text);bool rebound=false;
    auto connection=connect(text,&QPlainTextEdit::textChanged,&dialog,[&]{if(!rebound){rebound=true;QVERIFY(dialog.beginSession(78));}});
    dialog.setPreview(source);QVERIFY(rebound);QVERIFY(!dialog.preview());QVERIFY(text->toPlainText().isEmpty());dialog.accept();QCOMPARE(writes.size(),0);disconnect(connection);
    QPointer<ui::TorrentIntakeDialog> deleted=new ui::TorrentIntakeDialog;QVERIFY(deleted->beginSession(90));inputs(*deleted);deleted->show();QSignalSpy reads2(deleted.data(),&ui::TorrentIntakeDialog::previewRequested);
    auto* load2=deleted->findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load2->isEnabled());load2->click();auto source2=std::make_shared<ui::TorrentIntakePacket>(*packet(request(reads2)));source2->trackers->announce="https://old.invalid/a";
    auto* text2=deleted->findChild<QPlainTextEdit*>(QStringLiteral("import-trackers"));connect(text2,&QPlainTextEdit::textChanged,this,[&]{delete deleted.data();});deleted->setPreview(source2);QVERIFY(deleted.isNull());QCoreApplication::processEvents();
  }
  void sparseWantedChoicesPaddingAndFailedCommitRetainDraft() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();
    QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted);
    auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();QCOMPARE(reads.size(),1);
    const auto source=packet(request(reads));dialog.setPreview(source);
    auto* table=dialog.findChild<QTableView*>(QStringLiteral("import-file-table"));QVERIFY(table);
    QTRY_COMPARE(table->model()->rowCount(),3);auto* model=table->model();
    QVERIFY(!model->setData(model->index(2,0),Qt::Checked,Qt::CheckStateRole));
    QVERIFY(model->setData(model->index(1,0),Qt::Unchecked,Qt::CheckStateRole));
    table->selectionModel()->select(model->index(0,0),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
    dialog.findChild<QComboBox*>(QStringLiteral("import-file-priority"))->setCurrentIndex(1);
    dialog.findChild<QPushButton*>(QStringLiteral("import-apply-priority"))->click();
    auto* add=dialog.findChild<QPushButton*>(QStringLiteral("import-confirm"));QTRY_VERIFY(add->isEnabled());add->click();QCOMPARE(writes.size(),1);
    auto change=submission(writes);QVERIFY(change.start);QVERIFY((change.files==std::vector<compat::FilePolicy>{{false,1},{true,0},{true,0}}));
    QCOMPARE(change.session,quint64{77});QCOMPARE(change.request,source->source.request);QCOMPARE(change.revision,source->revision);
    QVERIFY(!dialog.close());QVERIFY(dialog.pending()); // Accepted commit cannot be abandoned by closing its form.
    auto failed=std::make_shared<ui::TorrentIntakeResultPacket>();failed->session=77;failed->request=change.request;failed->revision=change.revision;failed->failure=QStringLiteral("Owned simulated save conflict");
    dialog.setResult(failed);QTRY_VERIFY(add->isEnabled());
    QCOMPARE(model->data(model->index(1,0),Qt::CheckStateRole).toInt(),int(Qt::Unchecked));
    QCOMPARE(model->data(model->index(0,3),Qt::DisplayRole).toString(),QStringLiteral("High"));
    add->click();QCOMPARE(writes.size(),2);QVERIFY(submission(writes).files==change.files);
    auto success=std::make_shared<ui::TorrentIntakeResultPacket>(*failed);success->imported=true;success->failure.clear();dialog.setResult(success);
    QCOMPARE(dialog.result(),int(QDialog::Accepted));QVERIFY(!dialog.pending());
    QCOMPARE(source->files[1].bytes,qint64{200}); // Immutable preview was not edited to manufacture the choices.
  }
  void perTorrentLimitsAreExactRetainedDraftAndResetOnNewProfile(){
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();
    QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted);
    auto* down=dialog.findChild<QSpinBox*>(QStringLiteral("import-download-limit"));auto* up=dialog.findChild<QSpinBox*>(QStringLiteral("import-upload-limit"));QVERIFY(down&&up);
    QCOMPARE(down->value(),0);QCOMPARE(up->value(),0);QCOMPARE(down->maximum(),static_cast<int>(core::MaximumTorrentIntakeRate/1024));
    auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();dialog.setPreview(packet(request(reads)));
    auto* table=dialog.findChild<QTableView*>(QStringLiteral("import-file-table"));QVERIFY(table);
    QTRY_COMPARE(table->model()->rowCount(),3); // Wait for queued preview publication before clicking.
    auto* add=dialog.findChild<QPushButton*>(QStringLiteral("import-confirm"));QTRY_VERIFY(add->isEnabled());down->setValue(47);up->setValue(23);
    add->click();QCOMPARE(writes.size(),1);const auto sent=submission(writes);
    QCOMPARE(sent.limits.downloadBytesPerSecond,std::int64_t{48128});QCOMPARE(sent.limits.uploadBytesPerSecond,std::int64_t{23552});
    QTRY_VERIFY(!down->isEnabled()&&!up->isEnabled());
    dialog.rejectSubmission(sent);QTRY_VERIFY(down->isEnabled()&&up->isEnabled());QCOMPARE(down->value(),47);QCOMPARE(up->value(),23);
    dialog.setBusy(true);QTRY_VERIFY(!down->isEnabled()&&!up->isEnabled());dialog.accept();QCOMPARE(writes.size(),1);
    dialog.setBusy(false);QTRY_VERIFY(down->isEnabled()&&up->isEnabled());down->setValue(0);up->setValue(0);dialog.accept();QCOMPARE(writes.size(),2);
    const auto unlimited=submission(writes);QVERIFY(unlimited.limits==core::TorrentIntakeLimits{});dialog.rejectSubmission(unlimited);
    down->setValue(7);up->setValue(9);QVERIFY(dialog.beginSession(78));QCOMPARE(down->value(),0);QCOMPARE(up->value(),0);QVERIFY(!dialog.preview());
    dialog.accept();QCOMPARE(writes.size(),2);
  }
  void sequentialIntentHasDefaultDraftBusyAndProfileLifetime() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();
    QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted);
    auto* sequential=dialog.findChild<QCheckBox*>(QStringLiteral("import-sequential"));QVERIFY(sequential);QVERIFY(!sequential->isChecked());
    auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();dialog.setPreview(packet(request(reads)));
    auto* add=dialog.findChild<QPushButton*>(QStringLiteral("import-confirm"));QTRY_VERIFY(add->isEnabled());sequential->setChecked(true);
    auto* down=dialog.findChild<QSpinBox*>(QStringLiteral("import-download-limit"));auto* up=dialog.findChild<QSpinBox*>(QStringLiteral("import-upload-limit"));QVERIFY(down&&up);
    down->setValue(47);up->setValue(23);QCoreApplication::processEvents();
    const auto capture=qEnvironmentVariable("BIGLYBTC_CAPTURE_DIR");
    if(!capture.isEmpty()){QDir directory(capture);QVERIFY(directory.mkpath(QStringLiteral(".")));QVERIFY(dialog.grab().save(directory.filePath(QStringLiteral("torrent-intake-sequential-limits.png"))));}
    dialog.setBusy(true);QTRY_VERIFY(!sequential->isEnabled());dialog.accept();QCOMPARE(writes.size(),0);
    dialog.setBusy(false);QTRY_VERIFY(sequential->isEnabled());dialog.accept();QCOMPARE(writes.size(),1);const auto sent=submission(writes);QVERIFY(sent.sequentialDownload);
    QCOMPARE(sent.limits.downloadBytesPerSecond,std::int64_t{48128});QCOMPARE(sent.limits.uploadBytesPerSecond,std::int64_t{23552});
    QTRY_VERIFY(!sequential->isEnabled());dialog.rejectSubmission(sent);QTRY_VERIFY(sequential->isEnabled());QVERIFY(sequential->isChecked());
    sequential->setChecked(false);dialog.accept();QCOMPARE(writes.size(),2);const auto ordinary=submission(writes);QVERIFY(!ordinary.sequentialDownload);dialog.rejectSubmission(ordinary);
    sequential->setChecked(true);QVERIFY(dialog.beginSession(78));QVERIFY(!sequential->isChecked());QVERIFY(!dialog.preview());
    dialog.accept();QCOMPARE(writes.size(),2);
  }
  void sequentialProfileResetCannotPrefillAnOvertakingProfile() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));
    auto* sequential=dialog.findChild<QCheckBox*>(QStringLiteral("import-sequential"));auto* destination=dialog.findChild<QLineEdit*>(QStringLiteral("import-destination"));
    QVERIFY(sequential&&destination);QVERIFY(destination->text().isEmpty());sequential->setChecked(true);bool rebound=false;
    connect(sequential,&QCheckBox::toggled,&dialog,[&](bool checked){if(!checked){rebound=dialog.beginSession(79);}});
    QVERIFY(!dialog.beginSession(78,QStringLiteral("/old78")));QVERIFY(rebound);QVERIFY(destination->text().isEmpty());
    QVERIFY(!dialog.beginSession(78)); // The actual newer session remains bound.
    dialog.findChild<QLineEdit*>(QStringLiteral("import-source"))->setText(QStringLiteral("/tmp/biglybtc-profile79.torrent"));dialog.show();
    QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested);auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));
    QTRY_VERIFY(load->isEnabled());load->click();QCOMPARE(reads.size(),1);QCOMPARE(request(reads).session,quint64{79});QVERIFY(destination->text().isEmpty());
  }
  void sequentialProfileResetAllowsDeletionFromToggledCallback() {
    QPointer<ui::TorrentIntakeDialog> dialog=new ui::TorrentIntakeDialog;QVERIFY(dialog->beginSession(77));
    auto* sequential=dialog->findChild<QCheckBox*>(QStringLiteral("import-sequential"));QVERIFY(sequential);sequential->setChecked(true);
    connect(sequential,&QCheckBox::toggled,sequential,[&]{delete dialog.data();});
    QVERIFY(!dialog->beginSession(78));QVERIFY(dialog.isNull());
  }
  void forcedStartRequiresSelectedStartAndKeepsFailedDraft(){
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted);
    auto* mode=dialog.findChild<QComboBox*>(QStringLiteral("import-start-mode"));QVERIFY(mode);QCOMPARE(mode->count(),5);QCOMPARE(mode->currentData().toInt(),static_cast<int>(ui::TorrentIntakeStartMode::Queued));
    QVERIFY(!dialog.findChild<QCheckBox*>(QStringLiteral("import-start")));QVERIFY(!dialog.findChild<QCheckBox*>(QStringLiteral("import-force-start")));
    auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();dialog.setPreview(packet(request(reads)));
    auto* publishedTable=dialog.findChild<QTableView*>(QStringLiteral("import-file-table"));QVERIFY(publishedTable);QTRY_COMPARE(publishedTable->model()->rowCount(),3);QTRY_VERIFY(mode->isEnabled());
    dialog.accept();QCOMPARE(writes.size(),1);QVERIFY(submission(writes).start&&!submission(writes).forceStart&&!submission(writes).paused);dialog.rejectSubmission(submission(writes));QTRY_VERIFY(mode->isEnabled());
    mode->setCurrentIndex(mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::Forced)));dialog.accept();QCOMPARE(writes.size(),2);const auto forced=submission(writes);QVERIFY(forced.start&&forced.forceStart&&!forced.paused);dialog.rejectSubmission(forced);QTRY_VERIFY(mode->isEnabled());QCOMPARE(mode->currentData().toInt(),static_cast<int>(ui::TorrentIntakeStartMode::Forced));
    mode->setCurrentIndex(mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::Stopped)));dialog.accept();QCOMPARE(writes.size(),3);const auto stopped=submission(writes);QVERIFY(!stopped.start&&!stopped.forceStart&&!stopped.paused);dialog.rejectSubmission(stopped);
    mode->setCurrentIndex(mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::Paused)));dialog.accept();QCOMPARE(writes.size(),4);const auto paused=submission(writes);QVERIFY(!paused.start&&!paused.forceStart&&paused.paused);dialog.rejectSubmission(paused);QCOMPARE(mode->currentData().toInt(),static_cast<int>(ui::TorrentIntakeStartMode::Paused));
    dialog.setBusy(true);QTRY_VERIFY(!mode->isEnabled());dialog.accept();QCOMPARE(writes.size(),4);dialog.setBusy(false);QTRY_VERIFY(mode->isEnabled());
    mode->addItem(QStringLiteral("Unsupported test mode"),999);mode->setCurrentIndex(4);dialog.accept();QCOMPARE(writes.size(),4);mode->setCurrentIndex(mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::Paused)));
    auto* table=dialog.findChild<QTableView*>(QStringLiteral("import-file-table"));QVERIFY(table);for(int index:{0,1}){QVERIFY(table->model()->setData(table->model()->index(index,0),Qt::Unchecked,Qt::CheckStateRole));}
    QTRY_VERIFY(!mode->isEnabled());dialog.accept();QCOMPARE(writes.size(),5);const auto skipped=submission(writes);QVERIFY(!skipped.start&&!skipped.forceStart&&!skipped.paused);dialog.rejectSubmission(skipped);
    QVERIFY(dialog.beginSession(78));QCOMPARE(mode->currentData().toInt(),static_cast<int>(ui::TorrentIntakeStartMode::Queued));QVERIFY(!dialog.preview());dialog.accept();QCOMPARE(writes.size(),5);
  }
  void forcedStartResetCannotWriteIntoOvertakingProfile(){
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));auto* mode=dialog.findChild<QComboBox*>(QStringLiteral("import-start-mode"));auto* destination=dialog.findChild<QLineEdit*>(QStringLiteral("import-destination"));QVERIFY(mode&&destination);mode->setCurrentIndex(mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::Forced)));bool rebound=false;
    connect(mode,&QComboBox::currentIndexChanged,&dialog,[&](int index){if(index==0){rebound=dialog.beginSession(79);}});
    QVERIFY(!dialog.beginSession(78,QStringLiteral("/old78")));QVERIFY(rebound);QVERIFY(destination->text().isEmpty());QVERIFY(!dialog.beginSession(78));
  }
  void startModeResetForwardsExactOrderAndPreservesBlockedState(){
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));auto* mode=dialog.findChild<QComboBox*>(QStringLiteral("import-start-mode"));QVERIFY(mode);
    mode->setCurrentIndex(mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::Paused)));QStringList notifications;
    connect(mode,&QComboBox::currentIndexChanged,this,[&](int index){notifications.push_back(QStringLiteral("index:%1").arg(index));});
    connect(mode,&QComboBox::currentTextChanged,this,[&](const QString& text){notifications.push_back(QStringLiteral("text:")+text);});
    QVERIFY(dialog.beginSession(78));QCOMPARE(notifications,QStringList({QStringLiteral("index:0"),QStringLiteral("text:Queued")}));QVERIFY(!mode->signalsBlocked());
    mode->blockSignals(true);mode->setCurrentIndex(mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::Paused)));QVERIFY(dialog.beginSession(79));QVERIFY(mode->signalsBlocked());QCOMPARE(notifications.size(),2);
  }
  void startModeForwardedTextMayRebindOrDeleteOwnerAndIndexMayDeleteChild(){
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));auto* mode=dialog.findChild<QComboBox*>(QStringLiteral("import-start-mode"));QVERIFY(mode);mode->setCurrentIndex(mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::Paused)));
    bool rebound=false;connect(mode,&QComboBox::currentTextChanged,&dialog,[&](const QString&){rebound=dialog.beginSession(79);});QVERIFY(!dialog.beginSession(78,QStringLiteral("/old78")));QVERIFY(rebound);QVERIFY(dialog.findChild<QLineEdit*>(QStringLiteral("import-destination"))->text().isEmpty());
    QPointer<ui::TorrentIntakeDialog> deleted=new ui::TorrentIntakeDialog;QVERIFY(deleted->beginSession(90));auto* mode2=deleted->findChild<QComboBox*>(QStringLiteral("import-start-mode"));QVERIFY(mode2);mode2->setCurrentIndex(mode2->findData(static_cast<int>(ui::TorrentIntakeStartMode::Paused)));
    connect(mode2,&QComboBox::currentTextChanged,this,[&](const QString&){delete deleted.data();});QVERIFY(!deleted->beginSession(91));QVERIFY(deleted.isNull());
    ui::TorrentIntakeDialog child;QVERIFY(child.beginSession(100));QPointer<QComboBox> mode3=child.findChild<QComboBox*>(QStringLiteral("import-start-mode"));QVERIFY(mode3);mode3->setCurrentIndex(mode3->findData(static_cast<int>(ui::TorrentIntakeStartMode::Paused)));
    connect(mode3.data(),&QComboBox::currentIndexChanged,this,[&](int){delete mode3.data();});QVERIFY(!child.beginSession(101));QVERIFY(mode3.isNull());
  }
  void forcedStartResetAndChildDeletionCannotDispatch(){
    QPointer<ui::TorrentIntakeDialog> dialog=new ui::TorrentIntakeDialog;QVERIFY(dialog->beginSession(77));auto* mode=dialog->findChild<QComboBox*>(QStringLiteral("import-start-mode"));QVERIFY(mode);mode->setCurrentIndex(mode->findData(static_cast<int>(ui::TorrentIntakeStartMode::Paused)));
    connect(mode,&QComboBox::currentIndexChanged,mode,[&]{delete dialog.data();});QVERIFY(!dialog->beginSession(78));QVERIFY(dialog.isNull());
    ui::TorrentIntakeDialog intact;QVERIFY(intact.beginSession(80));inputs(intact);intact.show();QSignalSpy reads(&intact,&ui::TorrentIntakeDialog::previewRequested),writes(&intact,&ui::TorrentIntakeDialog::submitted);
    auto* load=intact.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();intact.setPreview(packet(request(reads)));
    auto* table=intact.findChild<QTableView*>(QStringLiteral("import-file-table"));QVERIFY(table);QTRY_COMPARE(table->model()->rowCount(),3);
    auto* add=intact.findChild<QPushButton*>(QStringLiteral("import-confirm"));QTRY_VERIFY(add->isEnabled());
    delete intact.findChild<QComboBox*>(QStringLiteral("import-start-mode"));intact.accept();QCOMPARE(writes.size(),0);QCoreApplication::processEvents();QCOMPARE(writes.size(),0);
  }
  void staleSourcesMalformedIndicesAndCancelledPreviewsCannotSubmit() {
    ui::TorrentIntakeDialog dialog;QVERIFY(dialog.beginSession(77));inputs(dialog);dialog.show();
    QSignalSpy reads(&dialog,&ui::TorrentIntakeDialog::previewRequested),writes(&dialog,&ui::TorrentIntakeDialog::submitted),cancelled(&dialog,&ui::TorrentIntakeDialog::previewCancelled);
    auto* load=dialog.findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();
    const auto old=request(reads);dialog.findChild<QLineEdit*>(QStringLiteral("import-source"))->setText(QStringLiteral("/tmp/biglybtc-other.torrent"));
    dialog.setPreview(packet(old));dialog.accept();QCOMPARE(writes.size(),0);QVERIFY(!dialog.preview());
    QTRY_VERIFY(load->isEnabled());load->click();auto malformed=std::make_shared<ui::TorrentIntakePacket>(*packet(request(reads)));malformed->files[1].index=0;
    dialog.setPreview(malformed);dialog.accept();QCOMPARE(writes.size(),0);QVERIFY(!dialog.preview());
    QTRY_VERIFY(load->isEnabled());load->click();const auto cancelledRead=request(reads);dialog.reject();
    QCOMPARE(cancelled.size(),1);QVERIFY(qvariant_cast<ui::TorrentIntakeRequest>(cancelled[0][0])==cancelledRead);
    dialog.setPreview(packet(cancelledRead));QVERIFY(!dialog.preview());QCOMPARE(writes.size(),0);
    QVERIFY(dialog.beginSession(78));dialog.setPreview(packet(old));QVERIFY(!dialog.preview());
  }
  void allSkippedIsExplicitStoppedAndSubmissionMayDeleteDialog() {
    auto owner=std::make_unique<ui::TorrentIntakeDialog>();QPointer<ui::TorrentIntakeDialog> dialog=owner.get();QVERIFY(dialog->beginSession(77));inputs(*dialog);dialog->show();
    QSignalSpy reads(dialog.data(),&ui::TorrentIntakeDialog::previewRequested);
    auto* load=dialog->findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();dialog->setPreview(packet(request(reads)));
    auto* table=dialog->findChild<QTableView*>(QStringLiteral("import-file-table"));QTRY_COMPARE(table->model()->rowCount(),3);
    for(int n=0;n<2;++n){QVERIFY(table->model()->setData(table->model()->index(n,0),Qt::Unchecked,Qt::CheckStateRole));}
    int calls=0;bool stopped=false;
    connect(dialog.data(),&ui::TorrentIntakeDialog::submitted,this,[&](ui::TorrentIntakeSubmission value){++calls;stopped=!value.start;dialog->accept();owner.reset();});
    dialog->accept();QVERIFY(dialog.isNull());QCOMPARE(calls,1);QVERIFY(stopped);QCoreApplication::processEvents();QCOMPARE(calls,1);
  }
  void windowUsesTaggedPreviewAndRejectsMissingWorkerReplyWithoutLosingChoices() {
    ui::LibraryWindow window;empty(window);window.show();
    QSignalSpy reads(&window,&ui::LibraryWindow::torrentIntakeRequested),writes(&window,&ui::LibraryWindow::torrentIntakeSubmitted),legacy(&window,&ui::LibraryWindow::torrentImportRequested);
    auto* open=window.findChild<QAction*>(QStringLiteral("classic-open"));QVERIFY(open);QTRY_VERIFY(open->isEnabled());open->trigger();
    auto* dialog=window.findChild<ui::TorrentIntakeDialog*>();QVERIFY(dialog);inputs(*dialog);
    auto* load=dialog->findChild<QPushButton*>(QStringLiteral("import-load-files"));QTRY_VERIFY(load->isEnabled());load->click();QCOMPARE(reads.size(),1);QCOMPARE(legacy.size(),0);
    window.setTorrentIntake(packet(request(reads),9));auto* table=dialog->findChild<QTableView*>(QStringLiteral("import-file-table"));QTRY_COMPARE(table->model()->rowCount(),3);
    QVERIFY(table->model()->setData(table->model()->index(1,0),Qt::Unchecked,Qt::CheckStateRole));dialog->accept();QCOMPARE(writes.size(),1);
    window.setBusy(true);window.setBusy(false);QTRY_VERIFY(!dialog->pending());
    QCOMPARE(table->model()->data(table->model()->index(1,0),Qt::CheckStateRole).toInt(),int(Qt::Unchecked));
    dialog->accept();QCOMPARE(writes.size(),2);auto result=std::make_shared<ui::TorrentIntakeResultPacket>();const auto sent=submission(writes);
    result->session=sent.session;result->request=sent.request;result->revision=sent.revision;result->imported=true;
    window.setTorrentIntakeFinished(result);QTRY_VERIFY(!dialog->isVisible());
    window.beginColumnLayoutSession(78);window.setTorrentIntake(packet(request(reads),10));QVERIFY(!dialog->preview());
    QCOMPARE(legacy.size(),0);QVERIFY(window.close());
  }
};
QTEST_MAIN(TorrentIntakeDialogTests)
#include "torrent_intake_dialog_tests.moc"

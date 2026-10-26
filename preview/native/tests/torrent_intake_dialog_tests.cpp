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
#include <QPointer>
#include <QPushButton>
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
  void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
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

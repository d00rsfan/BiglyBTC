// SPDX-License-Identifier: GPL-2.0-or-later
#include "library_window.hpp"
#include "publication_controls.hpp"
#include "publication_review_dialog.hpp"
#include "saved_service_pages.hpp"
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTableView>
#include <QtTest>
using namespace biglybtc::ui;
class PublicationUiTests final:public QObject {
 Q_OBJECT
private slots:
 void sharedCancellationPrecedesReentryAndOldPacketsStayStale(){
  QTableView table;QStandardItemModel model(2,1);table.setModel(&model);model.setData(model.index(0,0),9,SavedSharesModel::SourceIndexRole);model.setData(model.index(1,0),4,SavedSharesModel::SourceIndexRole);model.setData(model.index(0,0),QStringLiteral("Zulu"));model.setData(model.index(1,0),QStringLiteral("Alpha"));PublicationControls controls(PublicationKind::Shares,&table);controls.beginSession(1,10);controls.setAvailable(true,false);QSignalSpy reads(&controls,&PublicationControls::catalogRequested),commands(&controls,&PublicationControls::commandRequested);controls.refresh();QVERIFY(!reads.empty());auto key=qvariant_cast<PublicationCatalogKey>(reads.last()[0]);key.request=11;controls.setCatalogPending(key);auto catalog=std::make_shared<PublicationCatalogPacket>();catalog->key=key;catalog->generation=11;catalog->normal=true;catalog->listenerRunning=true;catalog->rows={{1,9,true,false,false,true,{}},{2,4,true,false,false,true,{}}};controls.setCatalog(catalog);model.sort(0,Qt::AscendingOrder);table.selectRow(0);controls.findChild<QPushButton*>("share-remove")->click();QCOMPARE(commands.size(),1);auto command=qvariant_cast<PublicationCommand>(commands.last()[0]);QCOMPARE(command.row,quint64{2});command.key.request=12;controls.setPending(command);bool stoppedFirst=false;QObject::connect(&controls,&PublicationControls::cancelRequested,&controls,[&](PublicationCommandKey){stoppedFirst=command.receiver->stop_requested();controls.beginSession(2,20);});controls.findChild<QPushButton*>("publication-cancel")->click();QVERIFY(stoppedFirst);QCOMPARE(controls.session(),quint64{2});auto old=std::make_shared<PublicationPacket>();old->key=command.key;old->state=PublicationState::Completed;old->committed=true;old->message=QStringLiteral("stale result");controls.setResult(old);QVERIFY(!controls.findChild<QLabel*>("publication-status")->text().contains("stale result"));QVERIFY(!controls.findChild<QPushButton*>("share-remove")->isEnabled());
 }
 void reviewPlaintextReentryAndWindowTeardown(){
  PublicationReviewDialog dialog;auto first=std::make_shared<PublicationPacket>();first->key={1,2,3,4,PublicationKind::Shares,PublicationAction::ReviewFile};first->state=PublicationState::ReviewReady;first->review=PublicationReview{1,QStringLiteral("Actual <b>literal</b> source"),QStringLiteral("<img src='file:///untrusted'> & literal"),QStringLiteral("Create")};auto second=std::make_shared<PublicationPacket>(*first);second->key.request=5;second->review->revision=2;QVERIFY(dialog.beginReview(first));dialog.open();QCOMPARE(dialog.findChild<QPlainTextEdit*>()->toPlainText(),first->review->details);QObject::connect(&dialog,&PublicationReviewDialog::cancelled,&dialog,[&](PublicationCommandKey){QVERIFY(dialog.beginReview(second));dialog.open();});dialog.reject();QVERIFY(dialog.frame()==second&&dialog.isVisible());QSignalSpy confirmed(&dialog,&PublicationReviewDialog::confirmed);dialog.findChild<QPushButton*>("publication-review-confirm")->click();QCOMPARE(confirmed.size(),1);QCOMPARE(qvariant_cast<PublicationConfirmation>(confirmed.last()[0]).key,second->key);QVERIFY(!dialog.findChild<QPushButton*>("publication-review-confirm")->isEnabled());
  auto window=std::make_unique<LibraryWindow>();window->setOfflinePreview(false);window->beginColumnLayoutSession(70);QSignalSpy catalog(window.get(),&LibraryWindow::publicationCatalogRequested),commands(window.get(),&LibraryWindow::publicationRequested);window->findChild<QAction*>("classic-my-tracker")->trigger();QTRY_VERIFY(!catalog.empty());auto actual=qvariant_cast<PublicationCatalogKey>(catalog.last()[0]);auto packet=std::make_shared<PublicationCatalogPacket>();packet->key=actual;packet->generation=actual.request;packet->normal=true;window->setPublicationCatalog(packet);window->publicationControls(PublicationKind::Tracker)->findChild<QPushButton*>("tracker-configure-http")->click();QCOMPARE(commands.size(),1);const auto command=qvariant_cast<PublicationCommand>(commands.last()[0]);auto review=std::make_shared<PublicationPacket>(*first);review->key=command.key;window->setPublicationResult(review);QVERIFY(window->findChild<PublicationReviewDialog*>());window.reset();QVERIFY(command.receiver->stop_requested());
 }
};
QTEST_MAIN(PublicationUiTests)
#include "publication_ui_tests.moc"

// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_magnet_dialog.hpp"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>
using namespace biglybtc::ui;
namespace {
std::shared_ptr<MetasearchDownloadPacket> review(){auto p=std::make_shared<MetasearchDownloadPacket>();p->key={MetasearchQueryKey{1,2,3,4},{5,6},7};p->state=MetasearchDownloadState::MagnetReviewReady;p->magnetReview=MetasearchMagnetReview{8,QStringLiteral("v1:59505bfa6ed5a481d5c08ceef3e8303abcb13657"),QStringLiteral("Name <b>literal</b>"),QStringLiteral("Source <img src=x>"),QStringLiteral("query <a>literal</a>")};return p;}
}
class MetasearchMagnetUiTests final:public QObject {
 Q_OBJECT
private slots:
 void plaintextReviewSubmitsOnlyBoundKeyRevisionAndPublicDestination(){
  MetasearchMagnetDialog dialog;auto packet=review();QVERIFY(dialog.beginReview(packet,QStringLiteral("/reviewed/destination")));QSignalSpy commands(&dialog,&MetasearchMagnetDialog::submitted);
  for(const auto* name:{"metasearch-magnet-identity","metasearch-magnet-name","metasearch-magnet-source","metasearch-magnet-query","metasearch-magnet-status"}){auto* label=dialog.findChild<QLabel*>(name);QVERIFY(label);QCOMPARE(label->textFormat(),Qt::PlainText);}QVERIFY(dialog.findChild<QLabel*>("metasearch-magnet-name")->text().contains("<b>literal</b>"));
  auto* network=dialog.findChild<QComboBox*>("metasearch-magnet-network");network->addItem(QStringLiteral("Unavailable I2P"),static_cast<int>(TorrentIntakeNetwork::I2p));network->setCurrentIndex(1);QVERIFY(!dialog.findChild<QPushButton*>("metasearch-magnet-confirm")->isEnabled());dialog.findChild<QPushButton*>("metasearch-magnet-confirm")->click();QCOMPARE(commands.size(),0);network->setCurrentIndex(0);dialog.findChild<QPushButton*>("metasearch-magnet-confirm")->click();QCOMPARE(commands.size(),1);const auto command=qvariant_cast<MetasearchMagnetSubmission>(commands.last()[0]);QCOMPARE(command.key,packet->key);QCOMPARE(command.revision,quint64{8});QCOMPARE(command.destination,QStringLiteral("/reviewed/destination"));QCOMPARE(command.network,TorrentIntakeNetwork::Public);QVERIFY(dialog.pending());
  auto stale=command;stale.revision=7;dialog.refuse(stale);QVERIFY(dialog.pending());dialog.refuse(command);QVERIFY(!dialog.pending());QSignalSpy cancelled(&dialog,&MetasearchMagnetDialog::cancelled);dialog.reject();QCOMPARE(cancelled.size(),1);QCOMPARE(qvariant_cast<MetasearchDownloadKey>(cancelled.last()[0]),packet->key);QVERIFY(!dialog.review());
 }
 void submissionAndCancellationSignalReentryCannotReuseAnOldReview(){
  auto* dialog=new MetasearchMagnetDialog;QPointer<MetasearchMagnetDialog> alive(dialog);auto packet=review();QVERIFY(dialog->beginReview(packet,QStringLiteral("/reviewed/destination")));QObject::connect(dialog,&MetasearchMagnetDialog::submitted,dialog,[&](const MetasearchMagnetSubmission& command){QCOMPARE(command.key,packet->key);dialog->invalidate();});dialog->findChild<QPushButton*>("metasearch-magnet-confirm")->click();QVERIFY(alive);QVERIFY(!dialog->review());auto next=review();next->magnetReview->revision=9;QVERIFY(dialog->beginReview(next,QStringLiteral("/next")));auto replacement=review();replacement->key.request=99;replacement->magnetReview->revision=10;const auto reentry=QObject::connect(dialog,&MetasearchMagnetDialog::cancelled,dialog,[&](auto){QVERIFY(dialog->beginReview(replacement,QStringLiteral("/replacement")));dialog->open();});dialog->reject();QVERIFY(alive);QCOMPARE(dialog->review(),MetasearchDownloadPresentation(replacement));QVERIFY(dialog->isVisible());QObject::disconnect(reentry);QObject::connect(dialog,&MetasearchMagnetDialog::cancelled,dialog,[&](auto){delete dialog;});dialog->reject();QVERIFY(!alive);
 }
};
QTEST_MAIN(MetasearchMagnetUiTests)
#include "metasearch_magnet_ui_tests.moc"

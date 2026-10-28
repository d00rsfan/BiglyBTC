// SPDX-License-Identifier: GPL-2.0-or-later
#include "ownership.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>
using biglybtc::services::tor::OwnershipHandshake;
namespace {
const QByteArray challenge="250 AUTHCHALLENGE SERVERHASH=9AC6D0EF401195DCFCBDCBC6E2876DA2DDB4319D4D92242C4E402C450BA848C8 SERVERNONCE=1F1E1D1C1B1A191817161514131211100F0E0D0C0B0A09080706050403020100";
QByteArray nonce(){QByteArray result(32,'\0');for(int i=0;i<32;++i){result[i]=static_cast<char>(i);}return result;}
class ScriptPeer final:public QObject {
public:
  explicit ScriptPeer(QJsonObject row):row_(std::move(row)),server(this){
    connect(&server,&QTcpServer::newConnection,this,[this]{
      auto* client=server.nextPendingConnection();client->setReadBufferSize(4096);
      connect(client,&QTcpSocket::readyRead,client,[this,client]{
        while(client->canReadLine()){
          commands.push_back(client->readLine());const auto count=commands.size();
          if(count==1){client->write(challenge+"\r\n");}
          else if(count==2){client->write("250 OK\r\n");}
          else if(count==3 || count==4){
            const auto rows=row_["lines_hex"].toArray();client->write(QByteArray::fromHex(rows[count-3].toString().toLatin1())+"\r\n");
          }else if(count==5){
            const auto rows=row_["lines_hex"].toArray();
            for(qsizetype i=2;i<rows.size();++i){client->write(QByteArray::fromHex(rows[i].toString().toLatin1())+"\r\n");}
            if(row_["source_status"].toString()=="REJECT"){client->disconnectFromHost();}
          }
        }
      });
    });
    listening=server.listen(QHostAddress::LocalHost,0);
  }
  QJsonObject row_;QTcpServer server;QList<QByteArray> commands;bool listening=false;
};
}
class OwnershipSourceTests final:public QObject {
  Q_OBJECT
private slots:
  void initTestCase(){
    QFile file(QStringLiteral(BIGLYBTC_TOR_OWNERSHIP_FIXTURES));QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonParseError error;const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    QCOMPARE(error.error,QJsonParseError::NoError);QVERIFY(document.isArray());cases_=document.array();QCOMPARE(cases_.size(),93);
  }
  void frozenSourceRows_data(){
    QTest::addColumn<int>("index");
    for(qsizetype i=0;i<cases_.size();++i){QTest::newRow(qPrintable(cases_[i].toObject()["label"].toString()))<<static_cast<int>(i);}
  }
  void frozenSourceRows(){
    QFETCH(int,index);const auto row=cases_[index].toObject();ScriptPeer peer(row);QVERIFY(peer.listening);
    OwnershipHandshake setup;QSignalSpy ready(&setup,&OwnershipHandshake::ready),failed(&setup,&OwnershipHandshake::failed);
    QVERIFY(setup.beginOwnedPeer(peer.server.serverPort(),QByteArray(32,'\0'),nonce(),1000));
    const bool policy=row["native_policy"].toString()!="same";
    if(row["source_status"].toString()=="OK" && !policy){
      QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,3000);QCOMPARE(failed.count(),0);
      QCOMPARE(setup.state(),OwnershipHandshake::State::Ready);
      QCOMPARE(setup.rawVersionInfo(),QByteArray::fromHex(row["source_info_hex"].toString().toLatin1()));
    }else{
      QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,3000);QCOMPARE(ready.count(),0);
      QCOMPARE(setup.state(),OwnershipHandshake::State::Failed);
    }
    QVERIFY(!setup.deadlineActive());QCOMPARE(peer.commands.size(),2+row["source_command_count"].toInt());
    QByteArray actual;for(qsizetype i=2;i<peer.commands.size();++i){if(i>2){actual+='|';}auto command=peer.commands[i];QVERIFY(command.endsWith("\r\n"));command.chop(2);actual+=command;}
    QCOMPARE(actual,QByteArray::fromHex(row["source_commands_hex"].toString().toLatin1()));setup.stop();
  }
private:QJsonArray cases_;
};
QTEST_GUILESS_MAIN(OwnershipSourceTests)
#include "ownership_source_tests.moc"

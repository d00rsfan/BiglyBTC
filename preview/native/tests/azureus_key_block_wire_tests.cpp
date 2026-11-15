// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_key_block/key_block.hpp"
#include <QTest>
#include <QFile>
namespace block=biglybtc::services::azureus_key_block;
block::udp::EncodeFields header(const block::udp::Header& h){block::udp::EncodeFields f;f.kind=h.kind;f.localVersion=55;f.remoteVersion=h.version;f.network=h.network;f.action=h.action;f.transaction=h.transaction;f.instance=h.instance;f.connection=h.connection;f.now=h.time;f.originator=h.originator;f.flags=h.flags;f.flags2=h.flags2;return f;}
class AzureusKeyBlockWireTests final:public QObject{Q_OBJECT
private slots:
 void actualOriginalPacketsRoundtrip(){
  QFile file(QString::fromUtf8(KEY_BLOCK_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));unsigned accepted=0,oversized=0;
  while(!file.atEnd()){const auto fields=file.readLine().trimmed().split('\t');QCOMPARE(fields.size(),3);const auto kind=fields[1]=="request"?block::udp::Kind::Request:block::udp::Kind::Reply;const auto raw=QByteArray::fromHex(fields[2]);std::string wire(raw.constData(),raw.size());block::udp::Context context;context.now=1000;auto decoded=block::decode(kind,wire,context);
   if(wire.size()>block::udp::maximumPacketBytes){QVERIFY(!decoded);QCOMPARE(decoded.error().code,block::ErrorCode::Limit);++oversized;continue;}
   QVERIFY2(decoded,fields[0].constData());QCOMPARE(decoded->consumed(),wire.size());QVERIFY(decoded->trailing().empty());const auto f=header(decoded->header().fields());auto encoded=kind==block::udp::Kind::Request?block::encodeRequest({f,std::get<block::RequestBody>(decoded->body())}):block::encodeReply({f});QVERIFY(encoded);QCOMPARE(*encoded,wire);
   for(std::size_t n=0;n<wire.size();++n){QVERIFY(!block::decode(kind,wire.substr(0,n),context));}auto extra=block::decode(kind,wire+"tail",context);QVERIFY(extra);QCOMPARE(extra->trailing(),std::string_view("tail"));++accepted;
  }QCOMPARE(accepted,12u);QCOMPARE(oversized,4u);
 }
 void declaredSizesAndCorrelationFieldsDoNotExpandAuthority(){
  block::RequestFields f;f.header.connection=INT64_MIN;f.body={-17,std::string(28,'k'),std::string(128,'s')};auto encoded=block::encodeRequest(f);QVERIFY(encoded);block::udp::Context context;auto decoded=block::decode(block::udp::Kind::Request,*encoded,context);QVERIFY(decoded);QCOMPARE(std::get<block::RequestBody>(decoded->body()),f.body);
  auto wire=*encoded;const auto at=decoded->header().consumed();wire[at+4]=char(255);QVERIFY(!block::decode(block::udp::Kind::Request,wire,context));wire=*encoded;wire[at+5+28]=char(255);wire[at+6+28]=char(255);QVERIFY(!block::decode(block::udp::Kind::Request,wire,context));context.network=1;QVERIFY(!block::decode(block::udp::Kind::Request,*encoded,context));
  f.body.request.resize(256);QVERIFY(!block::encodeRequest(f));f.body.request.resize(28);f.body.signature.resize(65536);QVERIFY(!block::encodeRequest(f));f.body.signature.resize(8192);QVERIFY(!block::encodeRequest(f));f.body.signature.clear();f.header.action=1024;QVERIFY(!block::encodeRequest(f));block::ReplyFields reply;reply.header.connection=INT64_MIN;reply.header.action=1025;QVERIFY(!block::encodeReply(reply));
 }
};
QTEST_GUILESS_MAIN(AzureusKeyBlockWireTests)
#include "azureus_key_block_wire_tests.moc"

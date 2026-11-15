// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_key_block/policy.hpp"
#include <QTest>
#include <QFile>
#include <map>
namespace block=biglybtc::services::azureus_key_block;
std::string unhex(const QByteArray& bytes){return QByteArray::fromHex(bytes).toStdString();}
class AzureusKeyBlockPolicyTests final:public QObject{Q_OBJECT
private slots:
 void actualOriginalPrecedenceAndSignatures(){
  QFile file(QString::fromUtf8(KEY_BLOCK_POLICY_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));std::string modulus,exponent;std::map<QByteArray,std::optional<block::Record>> states;unsigned operations=0,signatures=0;
  while(!file.atEnd()){const auto f=file.readLine().trimmed().split('\t');if(f[0]=="test-key"){modulus=f[1].toStdString();exponent=f[2].toStdString();continue;}QVERIFY(!modulus.empty());
   if(f[0]=="signature"){const auto request=unhex(f[1]),signature=unhex(f[2]);QCOMPARE(block::verifySignature(request,signature,{modulus,exponent}),f[3]=="1");QVERIFY(!block::verifyOfficial(request,signature));++signatures;continue;}
   QCOMPARE(f.size(),12);auto& previous=states[f[0]];block::Record candidate{unhex(f[5]),unhex(f[6]),1000,f[2]=="1"};QCOMPARE(candidate.created(),f[4].toInt());QCOMPARE(candidate.add(),f[3]=="1");const auto decision=block::decide(previous?&*previous:nullptr,candidate);const bool verified=!decision.verify||block::verifySignature(candidate.request,candidate.signature,{modulus,exponent});QCOMPARE(decision.returnBlocked&&verified,f[7]=="1");if(decision.replace&&verified){previous=candidate;}
   QCOMPARE(previous&&previous->add(),f[8]=="1");QCOMPARE(previous?QByteArray::fromStdString(previous->request).toHex():QByteArray("-"),f[9]);QCOMPARE(previous&&previous->direct,f[10]=="1");QCOMPARE(previous&&previous->direct?1:0,f[11].toInt());++operations;
  }QCOMPARE(operations,40u);QCOMPARE(signatures,3u);
 }
 void malformedRecordsNeverBecomeMutationPlans(){
  for(unsigned n:{0u,8u,256u}){block::Record candidate{std::string(n,'x'),"",0,true};QCOMPARE(block::decide(nullptr,candidate),block::Decision{});}QVERIFY(!block::verifySignature("payload","signature",{"nothex","10001"}));QVERIFY(!block::verifySignature("payload","signature",{"00","10001"}));QVERIFY(!block::verifySignature("payload","",block::officialPublicKey()));QVERIFY(!block::verifyOfficial(std::string(256,'x'),std::string(192,'s')));
 }
};
QTEST_GUILESS_MAIN(AzureusKeyBlockPolicyTests)
#include "azureus_key_block_policy_tests.moc"

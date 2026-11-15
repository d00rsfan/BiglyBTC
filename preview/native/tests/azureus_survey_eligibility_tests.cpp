// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_db_runtime/survey_eligibility.hpp"
#include <QTest>
#include <QFile>
#include <array>
#include <vector>
namespace survey=biglybtc::services::azureus_db_runtime::survey;
class AzureusSurveyEligibilityTests final:public QObject {
 Q_OBJECT
private slots:
 void actualOriginalEligibilityClocksAndRandomConsumption(){
  QFile file(QString::fromUtf8(SURVEY_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));unsigned cases=0;while(!file.atEnd()){const auto f=file.readLine().trimmed().split('\t');QCOMPARE(f.size(),12);survey::Observation o;o.wall=f[2].toLongLong();o.monotonic=f[3].toLongLong();if(f[4]!="-"){o.previous=f[4].toLongLong();}o.jitter=f[5].toInt();std::vector<survey::Value> values;if(f[6]!="-"){for(const auto& raw:f[6].split(',')){const auto pair=raw.split(':');QCOMPARE(pair.size(),2);values.push_back({pair[1].toLongLong(),std::bit_cast<std::int8_t>(std::uint8_t(pair[0].toInt()))});}}
   const auto decision=survey::eligibility(std::int8_t(f[1].toInt()),values,o);QVERIFY2(decision,f[0].constData());QCOMPARE(decision->eligible,bool(f[7].toInt()));std::optional<std::int64_t> next;if(f[8]!="-"){next=f[8].toLongLong();}QCOMPARE(decision->next,next);QCOMPARE(decision->monotonicObserved,bool(f[9].toInt()));QCOMPARE(decision->wallObserved,bool(f[10].toInt()));QCOMPARE(decision->jitterConsumed,bool(f[11].toInt()));++cases;
  }QCOMPARE(cases,24u);
 }
 void replicationNibblesAndInvalidInputsRemainBounded(){
  for(unsigned i=0;i<256;++i){auto value=std::bit_cast<std::int8_t>(std::uint8_t(i));QCOMPARE(survey::factor(value),std::uint8_t(i==255?255:i&15));QCOMPARE(survey::frequency(value),std::uint8_t(i==255?255:i>>4));}
  const std::vector<survey::Value> tooMany(257);QVERIFY(!survey::eligibility(1,tooMany,{}));const std::array values{survey::Value{0,17}};survey::Observation o;o.jitter=900000;QVERIFY(!survey::eligibility(1,values,o));o.jitter=-900001;QVERIFY(!survey::eligibility(1,values,o));o.jitter=-900000;QVERIFY(survey::eligibility(1,values,o));o.jitter=899999;QVERIFY(survey::eligibility(1,values,o));o.previous=1;o.jitter=INT32_MAX;auto ignored=survey::eligibility(1,values,o);QVERIFY(ignored);QVERIFY(!ignored->jitterConsumed);
 }
};
QTEST_GUILESS_MAIN(AzureusSurveyEligibilityTests)
#include "azureus_survey_eligibility_tests.moc"

// SPDX-License-Identifier: GPL-2.0-only
#include "rating_stars.hpp"
#include <QCryptographicHash>
#include <QProcess>
#include <QTest>
#include <bit>
#include <array>
#include <cmath>
#include <limits>
using namespace biglybtc;
QByteArray starDigest(const QImage& image){QByteArray bytes;bytes.reserve(image.width()*image.height()*4);for(int y=0;y<image.height();++y){for(int x=0;x<image.width();++x){const auto pixel=image.pixel(x,y);bytes+=char(qAlpha(pixel));bytes+=char(qRed(pixel));bytes+=char(qGreen(pixel));bytes+=char(qBlue(pixel));}}return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();}
class RatingStarsTests final:public QObject{
 Q_OBJECT
private slots:
 void pixelsMatchOriginalMethodAndRealSwtScaling(){
  const auto java=qEnvironmentVariable("BIGLYBTC_RATING_STARS_JAVA");if(java.isEmpty()){QSKIP("Original SWT pixel oracle not configured");}QProcess process;process.start(java,{"-cp",qEnvironmentVariable("BIGLYBTC_RATING_STARS_CLASSPATH"),"RatingStarsOracle",qEnvironmentVariable("BIGLYBTC_RATING_STARS_ASSETS")});QVERIFY(process.waitForStarted());unsigned cases=0;auto compare=[&](float stars,float personal,bool dark,int width){const auto image=ui::ratingStarsForCell(stars,personal,dark,width);QVERIFY(!image.isNull());const auto input=QByteArray::number(std::bit_cast<quint32>(stars),16)+' '+QByteArray::number(std::bit_cast<quint32>(personal),16)+' '+(dark?"1":"0")+' '+QByteArray::number(width);process.write(input+'\n');process.waitForBytesWritten(1000);if(!process.canReadLine()){process.waitForReadyRead(3000);}const auto actual=process.readLine().trimmed();const auto expected=QByteArray::number(image.width())+' '+QByteArray::number(image.height())+' '+starDigest(image);QVERIFY2(actual==expected,(input+" original="+actual+" native="+expected).constData());++cases;};
  const std::array<float,16> values{-std::numeric_limits<float>::infinity(),-100,-1,-0.0f,0,0.076923072f,0.07692308f,1,1.9999f,2.5f,3.3333333f,4.9999995f,5,5.001f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};for(float stars:values){for(float personal:values){for(bool dark:{false,true}){for(int width:{1,2,7,32,64,65,69,130}){compare(stars,personal,dark,width);if(QTest::currentTestFailed()){return;}}}}}for(int width=1;width<=64;++width){for(bool dark:{false,true}){compare(2.5f,3.0f,dark,width);if(QTest::currentTestFailed()){return;}}}QCOMPARE(cases,4224U);process.closeWriteChannel();QVERIFY(process.waitForFinished());QCOMPARE(process.exitCode(),0);
 }
 void middleClickCalculationMatchesTheOriginalSelectedBlock(){
  const auto java=qEnvironmentVariable("BIGLYBTC_RATING_STARS_JAVA");if(java.isEmpty()){QSKIP("Original click oracle not configured");}QProcess process;process.start(java,{"-cp",qEnvironmentVariable("BIGLYBTC_RATING_STARS_CLASSPATH"),"RatingStarsOracle",qEnvironmentVariable("BIGLYBTC_RATING_STARS_ASSETS")});QVERIFY(process.waitForStarted());unsigned cases=0;for(int width=1;width<=130;++width){for(int x:{-150,-66,-1,0,1,7,31,64,65,129,300}){process.write("CLICK "+QByteArray::number(width)+' '+QByteArray::number(x)+'\n');process.waitForBytesWritten(1000);if(!process.canReadLine()){process.waitForReadyRead(3000);}const auto expected=ui::ratingScoreAt(width,x);QCOMPARE(process.readLine().trimmed(),expected?QByteArray::number(*expected):QByteArray("NONE"));++cases;}}QCOMPARE(cases,1430U);process.closeWriteChannel();QVERIFY(process.waitForFinished());QCOMPARE(process.exitCode(),0);
 }
 void cellPresentationPreservesOriginalFallbackTieAndTooltip(){
  ui::RatingCellFacts facts;facts.personal=4;auto value=ui::presentRatingCell(facts,false);QCOMPARE(value.value("stars").toFloat(),4.0f);QCOMPARE(value.value("sort").toFloat(),4.0f);QCOMPARE(value.value("text").toString(),QString("4.0/5.0"));QCOMPARE(value.value("detail").toString(),QString("4.0 (no ratings yet)\nYour rating is 4.0"));facts.average=4.25f;facts.count=20;facts.comments=3;value=ui::presentRatingCell(facts,false);QCOMPARE(value.value("sort").toFloat(),4.252f);QCOMPARE(value.value("stars").toFloat(),4.252f);QCOMPARE(value.value("text").toString(),QString("4.2/5.0"));QCOMPARE(value.value("detail").toString(),QString("4.2 (based on 20 ratings)\n3 comment(s)\nYour rating is 4.0"));QCOMPARE(ui::presentRatingCell(facts,true).value("sort").toFloat(),4.0f);facts.enabled=false;value=ui::presentRatingCell(facts,false);QVERIFY(value.value("text").toString().isEmpty());QVERIFY(value.value("detail").toString().isEmpty());QVERIFY(!value.value("enabled").toBool());QVERIFY(ui::ratingStarsForCell(3,4,false,0).isNull());QCOMPARE(ui::ratingStarsForCell(3,4,false,130).size(),QSize(65,15));
 }
};
QTEST_GUILESS_MAIN(RatingStarsTests)
#include "rating_stars_tests.moc"

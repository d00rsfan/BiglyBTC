#include "statistics_page.hpp"
#include <QApplication>
#include <QLabel>
#include <QTabWidget>
#include <QtTest>
#include <array>
#include <stdexcept>
namespace {
using namespace biglybtc;
const std::array<const char*,6> ids{"receive-rate","send-rate","payload-receive-rate","payload-send-rate","protocol-receive-rate","protocol-send-rate"};
QLabel& field(ui::StatisticsPage& p,std::size_t i){auto* label=p.findChild<QLabel*>(QStringLiteral("statistics-engine-")+QString::fromLatin1(ids[i]));if(!label){throw std::runtime_error("Missing engine rate");}return *label;}
std::shared_ptr<ui::EngineStatisticsPacket> observed(){auto p=std::make_shared<ui::EngineStatisticsPacket>();p->session=1;p->operation=1;p->sequence=2;p->target={QStringLiteral("ENGINE"),1};p->state=ui::EngineStatisticsState::Observed;p->sample=core::EngineStatistics{{1000,700,800,500},2000000000LL,core::EngineRates{100,80,70,50,30,30}};return p;}
void show(ui::StatisticsPage& p){p.tabs().setCurrentIndex(1);p.resize(900,1000);p.show();QCoreApplication::processEvents();}
}
class EngineRatesViewTests final : public QObject {
  Q_OBJECT
private slots:
void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
void allSixMeasuredRatesHaveExactValuesAndRemainDistinctFromSavedTotals(){ui::StatisticsPage p;p.beginRequest(1,1);show(p);p.setEngineStatistics(observed());const std::array<int,6> expected{100,80,70,50,30,30};for(std::size_t i=0;i<ids.size();++i){QCOMPARE(field(p,i).text(),QStringLiteral("%1 bytes/s").arg(expected[i]));QVERIFY(field(p,i).toolTip().contains(QStringLiteral("interval")));}auto* saved=p.findChild<QLabel*>(QStringLiteral("statistics-saved-downloaded"));QVERIFY(saved);QCOMPARE(saved->text(),QStringLiteral("Unavailable"));}
void firstObservationDoesNotInventAnyOfSixRates(){ui::StatisticsPage p;p.beginRequest(1,1);show(p);auto value=observed();value->sample->elapsedNanoseconds=0;value->sample->rates.reset();p.setEngineStatistics(value);for(std::size_t i=0;i<ids.size();++i){QCOMPARE(field(p,i).text(),QStringLiteral("Unavailable"));QVERIFY(field(p,i).toolTip().contains(QStringLiteral("actual engine interval")));}}
void hiddenPageDefersAndStalePacketsCannotReplaceRates(){ui::StatisticsPage p;p.beginRequest(1,1);p.setEngineStatistics(observed());for(std::size_t i=0;i<ids.size();++i){QCOMPARE(field(p,i).text(),QStringLiteral("Unavailable"));}show(p);QCOMPARE(field(p,2).text(),QStringLiteral("70 bytes/s"));auto stale=observed();stale->sample->rates=core::EngineRates{90,70,60,40,30,30};p.setEngineStatistics(stale);QCOMPARE(field(p,2).text(),QStringLiteral("70 bytes/s"));}
void terminalPacketRetainsLastActualRatesAndNewSessionClearsThem(){ui::StatisticsPage p;p.beginRequest(1,1);show(p);auto value=observed();p.setEngineStatistics(value);auto ended=std::make_shared<ui::EngineStatisticsPacket>(*value);ended->sequence=3;ended->state=ui::EngineStatisticsState::Ended;p.setEngineStatistics(ended);QCOMPARE(field(p,2).text(),QStringLiteral("70 bytes/s"));p.beginRequest(2,1);for(std::size_t i=0;i<ids.size();++i){QCOMPARE(field(p,i).text(),QStringLiteral("Unavailable"));}}
};
QTEST_MAIN(EngineRatesViewTests)
#include "engine_rates_view_tests.moc"

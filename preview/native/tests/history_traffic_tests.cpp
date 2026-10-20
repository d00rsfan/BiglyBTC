#include "saved_history_plot.hpp"
#include "saved_history_traffic.hpp"

#include <QApplication>
#include <QtTest>

#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
using namespace biglybtc;
using Traffic = ui::SavedHistoryTraffic;
constexpr core::HistoryDate Date{2024, 2, 29}, End{2024, 3, 2};
constexpr std::array<Traffic, 3> Kinds{Traffic::Payload, Traffic::Protocol, Traffic::Dht};
constexpr std::array<std::size_t, 3> Down{3, 2, 5}, Up{1, 0, 4};
constexpr core::HistoryCounters First{101, 202, 303, 404, 505, 606}, Second{-7, -11, -13, -17, -19, -23}, Total{94, 191, 290, 387, 486, 583};
std::shared_ptr<core::SavedHistoryDay> day(core::HistoryDate date = Date) {
  auto value = std::make_shared<core::SavedHistoryDay>(); value->date = date; value->observations = 2;
  value->hours[8] = First; value->hours[17] = Second; value->totals = Total; return value;
}
std::shared_ptr<core::SavedHistoryWeek> week(bool populated = true) {
  const auto dates = core::savedHistoryWeekDates(End); if (!dates) { throw std::runtime_error("Finite week invalid"); }
  auto value = std::make_shared<core::SavedHistoryWeek>(); value->endingDate = End;
  for (std::size_t i = 0; i < 7; ++i) { value->days[i].date = (*dates)[i]; if (populated) { value->days[i].snapshot = *day((*dates)[i]); } }
  return value;
}
bool unknown(const ui::SavedHistoryPlotPresentation& value, std::size_t size) {
  if (!value || value->buckets().size() != size) { return false; }
  for (const auto& b : value->buckets()) { if (b.downloaded || b.uploaded) { return false; } } return true;
}
} // namespace

class HistoryTrafficTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen")); QApplication::setQuitOnLastWindowClosed(false);
    static_assert(ui::validSavedHistoryTraffic(Traffic::Payload)); static_assert(ui::validSavedHistoryTraffic(Traffic::Protocol));
    static_assert(ui::validSavedHistoryTraffic(Traffic::Dht)); static_assert(!ui::validSavedHistoryTraffic(static_cast<Traffic>(3)));
  }
  void allNineProjectionsUseExactSixCounterIndicesAndFixedUtcShape() {
    const auto source = week();
    for (std::size_t k = 0; k < Kinds.size(); ++k) {
      const auto daily = ui::savedHistoryDayPlot(Date, day(), Kinds[k]); const auto seven = ui::savedHistoryWeekPlot(End, source, Kinds[k]);
      const auto hours = ui::savedHistoryWeekHourlyPlot(End, source, Kinds[k]); QVERIFY(daily && seven && hours);
      QCOMPARE(daily->traffic(), Kinds[k]); QCOMPARE(seven->traffic(), Kinds[k]); QCOMPARE(hours->traffic(), Kinds[k]);
      QCOMPARE(daily->buckets().size(), std::size_t{24}); QCOMPARE(seven->buckets().size(), std::size_t{7}); QCOMPARE(hours->buckets().size(), std::size_t{168});
      QCOMPARE(daily->buckets()[8].downloaded, std::optional<std::int64_t>{First[Down[k]]}); QCOMPARE(daily->buckets()[8].uploaded, std::optional<std::int64_t>{First[Up[k]]});
      QCOMPARE(daily->buckets()[17].downloaded, std::optional<std::int64_t>{Second[Down[k]]}); QCOMPARE(daily->buckets()[17].uploaded, std::optional<std::int64_t>{Second[Up[k]]});
      QVERIFY(!daily->buckets()[0].downloaded); QCOMPARE(daily->minimum(), Second[Down[k]]); QCOMPARE(daily->maximum(), First[Down[k]]);
      for (std::size_t i = 0; i < 7; ++i) {
        QCOMPARE(seven->buckets()[i].date, source->days[i].date); QVERIFY(!seven->buckets()[i].hour);
        QCOMPARE(seven->buckets()[i].downloaded, std::optional<std::int64_t>{Total[Down[k]]}); QCOMPARE(seven->buckets()[i].uploaded, std::optional<std::int64_t>{Total[Up[k]]});
        for (unsigned h = 0; h < 24; ++h) {
          const auto& b = hours->buckets()[i * 24 + h]; QCOMPARE(b.date, source->days[i].date); QCOMPARE(b.hour, std::optional<unsigned>{h});
          if (h == 8 || h == 17) { const auto& expected = h == 8 ? First : Second; QCOMPARE(b.downloaded, std::optional<std::int64_t>{expected[Down[k]]}); QCOMPARE(b.uploaded, std::optional<std::int64_t>{expected[Up[k]]}); }
          else { QVERIFY(!b.downloaded && !b.uploaded); }
        }
      }
      QVERIFY(seven->buckets()[0].downloaded != hours->buckets()[8].downloaded);
    }
  }
  void defaultArgumentsKeepPayloadAndOldSourceIndices() {
    QCOMPARE(ui::savedHistoryDayPlot(Date, day())->traffic(), Traffic::Payload);
    QCOMPARE(ui::savedHistoryDayPlot(Date, day())->buckets()[8].downloaded, std::optional<std::int64_t>{404});
    QCOMPARE(ui::savedHistoryWeekPlot(End, week())->buckets()[0].uploaded, std::optional<std::int64_t>{191});
    QCOMPARE(ui::savedHistoryWeekHourlyPlot(End, week())->buckets()[8].uploaded, std::optional<std::int64_t>{202});
  }
  void emptyMissingAndRecordedZeroStayDistinctForEveryTraffic() {
    for (const auto kind : Kinds) {
      auto source = week(false); core::SavedHistoryDay empty; empty.date = source->days[1].date; source->days[1].snapshot = empty;
      auto zero = day(source->days[2].date); zero->hours = {}; zero->hours[0] = core::HistoryCounters{}; zero->totals = core::HistoryCounters{}; zero->observations = 1; source->days[2].snapshot = *zero;
      const auto daily = ui::savedHistoryDayPlot(zero->date, zero, kind), seven = ui::savedHistoryWeekPlot(End, source, kind), hours = ui::savedHistoryWeekHourlyPlot(End, source, kind);
      QVERIFY(!seven->buckets()[0].downloaded && !seven->buckets()[1].downloaded); QCOMPARE(seven->buckets()[2].downloaded, std::optional<std::int64_t>{0});
      QVERIFY(!hours->buckets()[24].downloaded); QCOMPARE(hours->buckets()[48].uploaded, std::optional<std::int64_t>{0}); QVERIFY(!hours->buckets()[49].uploaded);
      QCOMPARE(daily->buckets()[0].downloaded, std::optional<std::int64_t>{0}); QVERIFY(daily->point(0, false, {0, 0, 400, 80})); QVERIFY(!daily->point(1, false, {0, 0, 400, 80}));
      QVERIFY(unknown(ui::savedHistoryDayPlot(Date, {}, kind), 24)); QVERIFY(unknown(ui::savedHistoryWeekPlot(End, {}, kind), 7)); QVERIFY(unknown(ui::savedHistoryWeekHourlyPlot(End, {}, kind), 168));
    }
  }
  void signedExtremesAreExactAndFiniteInEveryModeAndTraffic() {
    for (std::size_t k = 0; k < Kinds.size(); ++k) {
      auto source = week(false); auto extreme = day(source->days[0].date); core::HistoryCounters raw{};
      raw[Down[k]] = std::numeric_limits<std::int64_t>::min(); raw[Up[k]] = std::numeric_limits<std::int64_t>::max();
      extreme->hours = {}; extreme->hours[0] = raw; extreme->totals = raw; extreme->observations = 1; source->days[0].snapshot = *extreme;
      const std::array frames{ui::savedHistoryDayPlot(extreme->date, extreme, Kinds[k]), ui::savedHistoryWeekPlot(End, source, Kinds[k]), ui::savedHistoryWeekHourlyPlot(End, source, Kinds[k])};
      for (const auto& f : frames) {
        QVERIFY(f); QCOMPARE(f->minimum(), std::numeric_limits<std::int64_t>::min()); QCOMPARE(f->maximum(), std::numeric_limits<std::int64_t>::max());
        QCOMPARE(f->buckets()[0].downloaded, std::optional<std::int64_t>{std::numeric_limits<std::int64_t>::min()});
        for (const bool uploaded : {false, true}) { const auto point = f->point(0, uploaded, {5, 7, 900, 100}); QVERIFY(point); QVERIFY(std::isfinite(point->x()) && std::isfinite(point->y())); }
        QVERIFY(f->bucketToolTip(0).contains(QStringLiteral("-9223372036854775808 bytes"))); QVERIFY(f->bucketToolTip(0).contains(QStringLiteral("9223372036854775807 bytes")));
      }
    }
  }
  void invalidTrafficOrRequestedDateNeverRetargetsAWidget() {
    ui::SavedHistoryPlot plot; plot.setDay(Date, day()); const auto old = plot.presentation();
    for (const int bad : {-1, 3, 99}) {
      const auto kind = static_cast<Traffic>(bad); QVERIFY(!ui::validSavedHistoryTraffic(kind));
      QVERIFY(!ui::savedHistoryDayPlot(Date, day(), kind)); QVERIFY(!ui::savedHistoryWeekPlot(End, week(), kind)); QVERIFY(!ui::savedHistoryWeekHourlyPlot(End, week(), kind));
      plot.setDay(Date, day(), kind); QCOMPARE(plot.presentation(), old); plot.setWeek(End, week(), kind); QCOMPARE(plot.presentation(), old); plot.setWeekHourly(End, week(), kind); QCOMPARE(plot.presentation(), old);
    }
    for (const auto kind : Kinds) { QVERIFY(!ui::savedHistoryDayPlot({2023, 2, 29}, day(), kind)); QVERIFY(!ui::savedHistoryWeekPlot({1970, 1, 6}, week(), kind)); QVERIFY(!ui::savedHistoryWeekHourlyPlot({1970, 1, 6}, week(), kind)); }
  }
  void mismatchedSourceDatesProduceUnknownsForEveryTraffic() {
    for (const auto kind : Kinds) {
      auto daily = day(); daily->date = End; QVERIFY(unknown(ui::savedHistoryDayPlot(Date, daily, kind), 24));
      for (int bad = 0; bad < 3; ++bad) {
        auto source = week(); if (bad == 0) { source->endingDate = Date; } else if (bad == 1) { source->days[0].date = End; } else { source->days[0].snapshot->date = End; }
        const auto seven = ui::savedHistoryWeekPlot(End, source, kind), hourly = ui::savedHistoryWeekHourlyPlot(End, source, kind);
        QVERIFY(unknown(seven, 7)); QVERIFY(unknown(hourly, 168)); QCOMPARE(seven->traffic(), kind); QCOMPARE(hourly->traffic(), kind);
        QCOMPARE(hourly->bucketLabel(0), QStringLiteral("2024-02-25 00:00 UTC")); QCOMPARE(hourly->bucketLabel(167), QStringLiteral("2024-03-02 23:00 UTC"));
      }
    }
  }
  void framesOwnCopiedScalarsAndOldTrafficFramesNeverChange() {
    for (std::size_t k = 0; k < Kinds.size(); ++k) {
      auto d = day(); auto w = week(); std::weak_ptr<core::SavedHistoryDay> weakDay = d; std::weak_ptr<core::SavedHistoryWeek> weakWeek = w;
      const auto daily = ui::savedHistoryDayPlot(Date, d, Kinds[k]), seven = ui::savedHistoryWeekPlot(End, w, Kinds[k]), hourly = ui::savedHistoryWeekHourlyPlot(End, w, Kinds[k]);
      d->hours[8] = core::HistoryCounters{}; w->days[0].snapshot->hours[8] = core::HistoryCounters{}; w->days[0].snapshot->totals = core::HistoryCounters{};
      d.reset(); w.reset(); QVERIFY(weakDay.expired() && weakWeek.expired());
      QCOMPARE(daily->buckets()[8].downloaded, std::optional<std::int64_t>{First[Down[k]]}); QCOMPARE(seven->buckets()[0].uploaded, std::optional<std::int64_t>{Total[Up[k]]});
      QCOMPARE(hourly->buckets()[8].uploaded, std::optional<std::int64_t>{First[Up[k]]}); QCOMPARE(hourly->traffic(), Kinds[k]);
    }
  }
  void renderedTooltipAndAccessibleNameIdentifyExactChosenTraffic() {
    const std::array<QString, 3> names{QStringLiteral("Payload"), QStringLiteral("Protocol"), QStringLiteral("DHT")};
    ui::SavedHistoryPlot plot; plot.resize(900, 220); plot.show();
    for (std::size_t k = 0; k < Kinds.size(); ++k) {
      plot.setDay(Date, day(), Kinds[k]); const auto tip = plot.presentation()->bucketToolTip(8);
      QVERIFY(tip.contains(names[k], Qt::CaseInsensitive)); QVERIFY(tip.contains(QString::number(First[Down[k]]) + QStringLiteral(" bytes")));
      QVERIFY(tip.contains(QString::number(First[Up[k]]) + QStringLiteral(" bytes"))); QVERIFY(plot.accessibleName().contains(names[k], Qt::CaseInsensitive));
      QVERIFY(!plot.accessibleName().contains(QStringLiteral("bytes/s"))); QVERIFY(!tip.contains(QLatin1Char('<')));
    }
  }
};
QTEST_MAIN(HistoryTrafficTests)
#include "history_traffic_tests.moc"

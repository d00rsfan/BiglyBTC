#include "saved_history_plot.hpp"

#include <QLocale>
#include <QtTest>

#include <cmath>
#include <limits>
#include <memory>

namespace {
using namespace biglybtc;
constexpr core::HistoryDate Date{2024, 2, 29}, End{2024, 3, 2};
std::shared_ptr<core::SavedHistoryDay> day() {
  auto value = std::make_shared<core::SavedHistoryDay>(); value->date = Date; return value;
}
std::shared_ptr<core::SavedHistoryWeek> week() {
  auto value = std::make_shared<core::SavedHistoryWeek>(); value->endingDate = End;
  const auto dates = core::savedHistoryWeekDates(End);
  for (std::size_t i = 0; i < 7; ++i) { value->days[i].date = (*dates)[i]; }
  return value;
}
void known(core::SavedHistoryDay& value, unsigned hour, std::int64_t down, std::int64_t up) {
  value.hours[hour] = core::HistoryCounters{777, up, 999, down, 555, 666};
}
bool finiteInside(const QPointF& p, const QRectF& r) {
  return std::isfinite(p.x()) && std::isfinite(p.y())
      && p.x() >= r.left() && p.x() <= r.right() && p.y() >= r.top() && p.y() <= r.bottom();
}
} // namespace

class SavedHistoryPlotDataTests final : public QObject {
  Q_OBJECT
private slots:
  void missingDayHasTwentyFourUnknownUTCLabels() {
    const auto frame = ui::savedHistoryDayPlot(Date, {}); QVERIFY(frame);
    QCOMPARE(frame->mode(), ui::SavedHistoryPlotMode::Day); QCOMPARE(frame->endingDate(), Date);
    QCOMPARE(frame->buckets().size(), std::size_t{24}); QCOMPARE(frame->minimum(), std::int64_t{0}); QCOMPARE(frame->maximum(), std::int64_t{0});
    for (std::size_t i = 0; i < 24; ++i) {
      const auto& point = frame->buckets()[i]; QCOMPARE(point.date, Date); QCOMPARE(point.hour, std::optional<unsigned>{static_cast<unsigned>(i)});
      QVERIFY(!point.downloaded); QVERIFY(!point.uploaded); QVERIFY(!frame->point(i, false, {0, 0, 600, 100}));
      QVERIFY(frame->bucketToolTip(i).contains(QStringLiteral("Unknown")));
    }
    QCOMPARE(frame->bucketLabel(0), QStringLiteral("2024-02-29 00:00 UTC"));
    QCOMPARE(frame->bucketLabel(23), QStringLiteral("2024-02-29 23:00 UTC"));
  }
  void missingWeekHasSevenDatesAcrossLeapBoundary() {
    const auto frame = ui::savedHistoryWeekPlot(End, {}); QVERIFY(frame);
    QCOMPARE(frame->mode(), ui::SavedHistoryPlotMode::Week); QCOMPARE(frame->buckets().size(), std::size_t{7});
    QCOMPARE(frame->buckets().front().date, (core::HistoryDate{2024, 2, 25})); QCOMPARE(frame->buckets().back().date, End);
    QCOMPARE(frame->bucketLabel(4), QStringLiteral("2024-02-29 UTC"));
    for (const auto& bucket : frame->buckets()) { QVERIFY(!bucket.hour); QVERIFY(!bucket.downloaded); QVERIFY(!bucket.uploaded); }
  }
  void dayCopiesOnlyHourlyPayloadFieldsWithoutTotalsOrOtherCounters() {
    auto source = day(); known(*source, 3, -19, 7); source->totals = core::HistoryCounters{900, 800, 700, 600, 500, 400};
    const auto frame = ui::savedHistoryDayPlot(Date, source); QVERIFY(frame);
    QCOMPARE(frame->buckets()[3].downloaded, std::optional<std::int64_t>{-19});
    QCOMPARE(frame->buckets()[3].uploaded, std::optional<std::int64_t>{7});
    QVERIFY(!frame->buckets()[2].downloaded); QVERIFY(!frame->buckets()[4].downloaded);
    QCOMPARE(frame->minimum(), std::int64_t{-19}); QCOMPARE(frame->maximum(), std::int64_t{7});
  }
  void weekCopiesIndependentDailyTotalsNotHourlyValuesOrAWeekSum() {
    auto source = week();
    for (const std::size_t index : {1U, 5U}) {
      core::SavedHistoryDay member; member.date = source->days[index].date;
      member.totals = core::HistoryCounters{777, static_cast<std::int64_t>(index), 999, -static_cast<std::int64_t>(index), 555, 666};
      member.hours[0] = core::HistoryCounters{1, 300, 2, 400, 3, 4}; source->days[index].snapshot = member;
    }
    const auto frame = ui::savedHistoryWeekPlot(End, source); QVERIFY(frame);
    QCOMPARE(frame->buckets()[1].downloaded, std::optional<std::int64_t>{-1});
    QCOMPARE(frame->buckets()[5].downloaded, std::optional<std::int64_t>{-5});
    QCOMPARE(frame->buckets()[5].uploaded, std::optional<std::int64_t>{5});
    QCOMPARE(frame->minimum(), std::int64_t{-5}); QCOMPARE(frame->maximum(), std::int64_t{5});
    QCOMPARE(frame->buckets().size(), std::size_t{7});
  }
  void presentEmptyAndMissingRemainGapsWhileKnownZeroHasPoints() {
    auto source = week(); core::SavedHistoryDay empty; empty.date = source->days[1].date; source->days[1].snapshot = empty;
    core::SavedHistoryDay zero; zero.date = source->days[2].date; zero.totals = core::HistoryCounters{}; source->days[2].snapshot = zero;
    const auto frame = ui::savedHistoryWeekPlot(End, source); QVERIFY(frame);
    QVERIFY(!frame->buckets()[0].downloaded); QVERIFY(!frame->buckets()[1].downloaded);
    QCOMPARE(frame->buckets()[2].downloaded, std::optional<std::int64_t>{0});
    QVERIFY(!frame->point(0, false, {0, 0, 100, 100})); QVERIFY(!frame->point(1, true, {0, 0, 100, 100}));
    const auto point = frame->point(2, false, {0, 0, 100, 100}); QVERIFY(point); QCOMPARE(point->y(), qreal{50});
    QVERIFY(frame->bucketToolTip(0).contains(QStringLiteral("Unknown")));
    QVERIFY(frame->bucketToolTip(2).contains(QStringLiteral("0 bytes")));
  }
  void boundsAlwaysIncludeZeroAndBothDirections_data() {
    QTest::addColumn<qint64>("down"); QTest::addColumn<qint64>("up"); QTest::addColumn<qint64>("low"); QTest::addColumn<qint64>("high");
    QTest::newRow("positive") << qint64{4} << qint64{9} << qint64{0} << qint64{9};
    QTest::newRow("negative") << qint64{-4} << qint64{-9} << qint64{-9} << qint64{0};
    QTest::newRow("mixed") << qint64{-4} << qint64{9} << qint64{-4} << qint64{9};
    QTest::newRow("actual-zero") << qint64{0} << qint64{0} << qint64{0} << qint64{0};
  }
  void boundsAlwaysIncludeZeroAndBothDirections() {
    QFETCH(qint64, down); QFETCH(qint64, up); QFETCH(qint64, low); QFETCH(qint64, high);
    auto source = day(); known(*source, 12, down, up); const auto frame = ui::savedHistoryDayPlot(Date, source);
    QCOMPARE(frame->minimum(), low); QCOMPARE(frame->maximum(), high);
    const QRectF area{10, 20, 600, 140};
    for (const bool upload : {false, true}) { const auto p = frame->point(12, upload, area); QVERIFY(p); QVERIFY(finiteInside(*p, area)); }
  }
  void fullSignedDomainUsesFiniteCoordinatesWithoutIntegerOverflow() {
    auto source = day(); const auto low = std::numeric_limits<std::int64_t>::min(), high = std::numeric_limits<std::int64_t>::max();
    known(*source, 0, high, low); known(*source, 23, low, high); const auto frame = ui::savedHistoryDayPlot(Date, source);
    QCOMPARE(frame->minimum(), low); QCOMPARE(frame->maximum(), high); const QRectF area{30, 45, 900, 150};
    for (const std::size_t i : {0U, 23U}) { for (const bool upload : {false, true}) {
      const auto p = frame->point(i, upload, area); QVERIFY(p); QVERIFY(finiteInside(*p, area));
    } }
    QCOMPARE(frame->point(0, false, area)->y(), area.top()); QCOMPARE(frame->point(0, true, area)->y(), area.bottom());
    QCOMPARE(frame->point(23, false, area)->x(), area.right());
    QCOMPARE(frame->bucketToolTip(0), QStringLiteral("2024-02-29 00:00 UTC\nPayload downloaded: 9223372036854775807 bytes\nPayload uploaded: -9223372036854775808 bytes"));
  }
  void zeroGeometryNeverCreatesUnknownObservations() {
    auto source = day(); known(*source, 11, 0, 0); const auto frame = ui::savedHistoryDayPlot(Date, source);
    const auto p = frame->point(11, false, {0, 0, 230, 100}); QVERIFY(p); QCOMPARE(p->x(), qreal{110}); QCOMPARE(p->y(), qreal{50});
    QVERIFY(!frame->point(10, false, {0, 0, 230, 100})); QVERIFY(!frame->point(12, true, {0, 0, 230, 100}));
  }
  void invalidCoordinatesAndIndicesHaveNoPoint() {
    auto source = day(); known(*source, 0, 5, 2); const auto frame = ui::savedHistoryDayPlot(Date, source);
    const auto nan = std::numeric_limits<qreal>::quiet_NaN(), inf = std::numeric_limits<qreal>::infinity();
    for (const auto area : {QRectF{0, 0, 0, 100}, QRectF{0, 0, 100, -1}, QRectF{nan, 0, 1, 1},
        QRectF{0, 0, inf, 1}, QRectF{0, 0, 1, nan}, QRectF{inf, 0, 1, 1}}) {
      QVERIFY(!frame->point(0, false, area));
    }
    QVERIFY(!frame->point(24, false, {0, 0, 100, 100}));
    QVERIFY(!frame->point(std::numeric_limits<std::size_t>::max(), true, {0, 0, 100, 100}));
    QVERIFY(frame->bucketLabel(24).isEmpty()); QVERIFY(frame->bucketToolTip(24).isEmpty());
  }
  void frameOwnsCopiedDayFactsAfterMutationAndDestruction() {
    auto source = day(); known(*source, 7, 19, -3); std::weak_ptr<const core::SavedHistoryDay> weak = source;
    const auto frame = ui::savedHistoryDayPlot(Date, source); known(*source, 7, 999, 888); source->date = End; source.reset();
    QVERIFY(weak.expired()); QCOMPARE(frame->buckets()[7].downloaded, std::optional<std::int64_t>{19});
    QCOMPARE(frame->buckets()[7].uploaded, std::optional<std::int64_t>{-3}); QCOMPARE(frame->buckets()[7].date, Date);
  }
  void frameOwnsCopiedWeekFactsAndOldFramesStayImmutable() {
    auto source = week(); core::SavedHistoryDay member; member.date = source->days[0].date; member.totals = core::HistoryCounters{1, 2, 3, 4, 5, 6};
    source->days[0].snapshot = member; std::weak_ptr<const core::SavedHistoryWeek> weak = source;
    const auto first = ui::savedHistoryWeekPlot(End, source); (*source->days[0].snapshot->totals)[3] = 99;
    const auto second = ui::savedHistoryWeekPlot(End, source); source.reset(); QVERIFY(weak.expired());
    QCOMPARE(first->buckets()[0].downloaded, std::optional<std::int64_t>{4});
    QCOMPARE(second->buckets()[0].downloaded, std::optional<std::int64_t>{99}); QVERIFY(first != second);
  }
  void mismatchedDayAndWeekDatesDoNotPlotMislabelledData() {
    auto source = day(); known(*source, 1, 100, 200); source->date = End;
    const auto daily = ui::savedHistoryDayPlot(Date, source);
    for (const auto& bucket : daily->buckets()) { QVERIFY(!bucket.downloaded); QVERIFY(!bucket.uploaded); }
    for (int kind = 0; kind < 3; ++kind) {
      auto value = week(); core::SavedHistoryDay member; member.date = value->days[3].date; member.totals = core::HistoryCounters{1, 2, 3, 4, 5, 6}; value->days[3].snapshot = member;
      if (kind == 0) { value->endingDate = Date; }
      if (kind == 1) { value->days[6].date = Date; }
      if (kind == 2) { value->days[3].snapshot->date = End; }
      const auto frame = ui::savedHistoryWeekPlot(End, value); QVERIFY(frame);
      for (const auto& bucket : frame->buckets()) { QVERIFY(!bucket.downloaded); QVERIFY(!bucket.uploaded); }
    }
  }
  void invalidRequestedDatesCannotCreateAFrame() {
    for (const auto date : {core::HistoryDate{1969, 1, 1}, core::HistoryDate{2024, 2, 30}, core::HistoryDate{10000, 1, 1}}) {
      QVERIFY(!ui::savedHistoryDayPlot(date, {})); QVERIFY(!ui::savedHistoryWeekPlot(date, {}));
    }
    QVERIFY(ui::savedHistoryDayPlot({1970, 1, 1}, {})); QVERIFY(!ui::savedHistoryWeekPlot({1970, 1, 6}, {}));
    QVERIFY(ui::savedHistoryWeekPlot({1970, 1, 7}, {})); QVERIFY(ui::savedHistoryWeekPlot({9999, 12, 31}, {}));
  }
  void labelsAreExactUTCAndIgnoreDisplayLocale() {
    const auto previous = QLocale(); struct Restore { QLocale locale; ~Restore() { QLocale::setDefault(locale); } } restore{previous};
    QLocale::setDefault(QLocale(QLocale::Arabic, QLocale::Egypt));
    const auto frame = ui::savedHistoryDayPlot(Date, {}); QCOMPARE(frame->bucketLabel(9), QStringLiteral("2024-02-29 09:00 UTC"));
    QVERIFY(!frame->bucketToolTip(9).contains(QStringLiteral("bytes/s"))); QVERIFY(!frame->bucketToolTip(9).contains(QLatin1Char('<')));
  }
};
QTEST_GUILESS_MAIN(SavedHistoryPlotDataTests)
#include "saved_history_plot_tests.moc"

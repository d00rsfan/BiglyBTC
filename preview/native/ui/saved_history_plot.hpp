#pragma once

#include <biglybtc/saved_history_week.hpp>

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <array>
#include <memory>
#include <span>

class QEvent;
class QMouseEvent;
class QPaintEvent;

namespace biglybtc::ui {

enum class SavedHistoryPlotMode { Day, Week };
struct SavedHistoryPlotBucket {
  core::HistoryDate date;
  std::optional<unsigned> hour;
  std::optional<std::int64_t> downloaded;
  std::optional<std::int64_t> uploaded;
};
class SavedHistoryPlotFrame;
using SavedHistoryPlotPresentation = std::shared_ptr<const SavedHistoryPlotFrame>;

// Bounded owned scalar projection only: no sums/rates, IO or Java equality.
// Invalid requested date/range returns null. A null or date-mismatched source
// gives24/7 unknown buckets. Facts never retain a caller's mutable DTO alias.
SavedHistoryPlotPresentation savedHistoryDayPlot(
    core::HistoryDate, std::shared_ptr<const core::SavedHistoryDay>);
SavedHistoryPlotPresentation savedHistoryWeekPlot(
    core::HistoryDate endingDate, std::shared_ptr<const core::SavedHistoryWeek>);

class SavedHistoryPlotFrame final {
public:
  SavedHistoryPlotMode mode() const noexcept { return mode_; }
  core::HistoryDate endingDate() const noexcept { return endingDate_; }
  std::span<const SavedHistoryPlotBucket> buckets() const noexcept { return {buckets_.data(), count_}; }
  std::int64_t minimum() const noexcept { return minimum_; }
  std::int64_t maximum() const noexcept { return maximum_; }
  QString bucketLabel(std::size_t index) const;
  QString bucketToolTip(std::size_t index) const;
  // The same finite coordinate path used by painting. Unknown/out-of-range
  // buckets or invalid rectangles have no point. uploaded=false selects down.
  std::optional<QPointF> point(std::size_t index, bool uploaded, QRectF area) const noexcept;
private:
  friend SavedHistoryPlotPresentation savedHistoryDayPlot(
      core::HistoryDate, std::shared_ptr<const core::SavedHistoryDay>);
  friend SavedHistoryPlotPresentation savedHistoryWeekPlot(
      core::HistoryDate, std::shared_ptr<const core::SavedHistoryWeek>);
  SavedHistoryPlotFrame(SavedHistoryPlotMode, core::HistoryDate, std::size_t);
  void finish() noexcept;
  SavedHistoryPlotMode mode_;
  core::HistoryDate endingDate_;
  std::array<SavedHistoryPlotBucket, 24> buckets_{};
  std::size_t count_;
  std::int64_t minimum_ = 0, maximum_ = 0;
};

// GUI-thread only. No source ownership outside the copied immutable facts, no
// timers or signals. Setters preserve a nested newer frame and guard synchronous
// tooltip/accessibility event callbacks that delete this widget.
class SavedHistoryPlot final : public QWidget {
  Q_OBJECT
public:
  explicit SavedHistoryPlot(QWidget* parent = nullptr);
  void setDay(core::HistoryDate, std::shared_ptr<const core::SavedHistoryDay>);
  void setWeek(core::HistoryDate endingDate, std::shared_ptr<const core::SavedHistoryWeek>);
  SavedHistoryPlotPresentation presentation() const noexcept { return frame_; }
  QRectF plotRect() const;
  QSize sizeHint() const override;
protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  bool event(QEvent*) override;
private:
  void replace(SavedHistoryPlotPresentation);
  bool bucketToolTip(qreal x);
  SavedHistoryPlotPresentation frame_;
};

} // namespace biglybtc::ui

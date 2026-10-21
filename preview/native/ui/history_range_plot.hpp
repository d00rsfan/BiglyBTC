#pragma once

#include "saved_history_traffic.hpp"

#include <biglybtc/saved_history_range.hpp>

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class QEvent;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

namespace biglybtc::ui {

struct HistoryRangePlotBucket {
  std::int64_t startMillis = 0, endMillis = 0;
  std::optional<std::int64_t> downloaded, uploaded;
};
class HistoryRangePlotFrame;
using HistoryRangePlotPresentation = std::shared_ptr<const HistoryRangePlotFrame>;

// Valid plan only; null or mismatched source yields unknown buckets. The frame
// owns bounded primitive copies, never an alias of the caller's mutable DTO.
HistoryRangePlotPresentation historyRangePlot(core::SavedHistoryRangePlan,
    std::shared_ptr<const core::SavedHistoryRange>, SavedHistoryTraffic = SavedHistoryTraffic::Payload);

class HistoryRangePlotFrame final {
public:
  const core::SavedHistoryRangePlan& plan() const noexcept { return plan_; }
  SavedHistoryTraffic traffic() const noexcept { return traffic_; }
  std::span<const HistoryRangePlotBucket> buckets() const noexcept { return buckets_; }
  std::int64_t minimum() const noexcept { return minimum_; }
  std::int64_t maximum() const noexcept { return maximum_; }
  QString bucketLabel(std::size_t) const;
  QString bucketToolTip(std::size_t) const;
  std::optional<QPointF> point(std::size_t, bool uploaded, QRectF) const noexcept;
private:
  friend HistoryRangePlotPresentation historyRangePlot(core::SavedHistoryRangePlan,
      std::shared_ptr<const core::SavedHistoryRange>, SavedHistoryTraffic);
  core::SavedHistoryRangePlan plan_;
  SavedHistoryTraffic traffic_ = SavedHistoryTraffic::Payload;
  std::vector<HistoryRangePlotBucket> buckets_;
  std::int64_t minimum_ = 0, maximum_ = 0;
};

// GUI-thread-only inspection. No IO, timers or invented samples. Every bucket,
// including unknown, is keyboard inspectable; only explicit Copy writes text.
class HistoryRangePlot final : public QWidget {
  Q_OBJECT
public:
  explicit HistoryRangePlot(QWidget* parent = nullptr);
  void setRange(core::SavedHistoryRangePlan, std::shared_ptr<const core::SavedHistoryRange>,
      SavedHistoryTraffic = SavedHistoryTraffic::Payload);
  void clear();
  HistoryRangePlotPresentation presentation() const noexcept { return frame_; }
  std::optional<std::size_t> selectedBucket() const noexcept { return selectedBucket_; }
  QRectF plotRect() const;
  QSize sizeHint() const override;
protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void focusInEvent(QFocusEvent*) override;
  void focusOutEvent(QFocusEvent*) override;
  bool event(QEvent*) override;
private:
  void replace(HistoryRangePlotPresentation);
  bool bucketToolTip(qreal);
  void selectBucket(std::size_t);
  bool advanceInteraction() noexcept;
  HistoryRangePlotPresentation frame_;
  std::optional<std::size_t> selectedBucket_;
  std::uint64_t interactionRevision_ = 0;
};

} // namespace biglybtc::ui

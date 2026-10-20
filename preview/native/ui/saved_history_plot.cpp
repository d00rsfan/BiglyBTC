#include "saved_history_plot.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>

#include <algorithm>
#include <cmath>

namespace biglybtc::ui {
namespace {
QString dateText(core::HistoryDate date) {
  return QStringLiteral("%1-%2-%3").arg(date.year, 4, 10, QLatin1Char('0'))
      .arg(date.month, 2, 10, QLatin1Char('0')).arg(date.day, 2, 10, QLatin1Char('0'));
}
QString amount(std::optional<std::int64_t> value) {
  return value ? QObject::tr("%1 bytes").arg(*value) : QObject::tr("Unknown");
}
QString description(const SavedHistoryPlotFrame& frame) {
  if (frame.mode() == SavedHistoryPlotMode::WeekHourly) {
    return QObject::tr("Saved hourly payload traffic in bytes, seven independent UTC days ending %1").arg(dateText(frame.endingDate()));
  }
  return frame.mode() == SavedHistoryPlotMode::Day
      ? QObject::tr("Saved hourly payload traffic in bytes for %1 UTC").arg(dateText(frame.endingDate()))
      : QObject::tr("Saved daily payload traffic in bytes, seven independent UTC days ending %1").arg(dateText(frame.endingDate()));
}
QString summary(const SavedHistoryPlotFrame& frame) {
  return description(frame) + QLatin1Char('\n')
      + QObject::tr("Hover over a time bucket for exact saved values. Unknown values are gaps.");
}
const std::array<QColor, 2> Colors{QColor(30, 115, 210), QColor(195, 95, 15)};
const std::array<Qt::PenStyle, 2> Styles{Qt::SolidLine, Qt::DashLine};
} // namespace

SavedHistoryPlotFrame::SavedHistoryPlotFrame(SavedHistoryPlotMode mode,
    core::HistoryDate endingDate, std::size_t count)
    : mode_(mode), endingDate_(endingDate), count_(count) {}

void SavedHistoryPlotFrame::finish() noexcept {
  for (const auto& bucket : buckets()) {
    for (const auto value : {bucket.downloaded, bucket.uploaded}) {
      if (value) { minimum_ = std::min(minimum_, *value); maximum_ = std::max(maximum_, *value); }
    }
  }
}

SavedHistoryPlotPresentation savedHistoryDayPlot(core::HistoryDate date,
    std::shared_ptr<const core::SavedHistoryDay> source) {
  if (!core::validHistoryDate(date)) { return {}; }
  auto frame = std::shared_ptr<SavedHistoryPlotFrame>(new SavedHistoryPlotFrame(SavedHistoryPlotMode::Day, date, 24));
  for (std::size_t i = 0; i < frame->count_; ++i) {
    auto& bucket = frame->buckets_[i]; bucket.date = date; bucket.hour = static_cast<unsigned>(i);
    if (source && source->date == date && source->hours[i]) {
      bucket.downloaded = (*source->hours[i])[3]; bucket.uploaded = (*source->hours[i])[1];
    }
  }
  frame->finish(); return frame;
}

SavedHistoryPlotPresentation savedHistoryWeekPlot(core::HistoryDate end,
    std::shared_ptr<const core::SavedHistoryWeek> source) {
  const auto dates = core::savedHistoryWeekDates(end); if (!dates) { return {}; }
  bool matching = source && source->endingDate == end;
  if (matching) {
    for (std::size_t i = 0; i < dates->size(); ++i) {
      const auto& row = source->days[i];
      if (row.date != (*dates)[i] || (row.snapshot && row.snapshot->date != row.date)) { matching = false; break; }
    }
  }
  auto frame = std::shared_ptr<SavedHistoryPlotFrame>(new SavedHistoryPlotFrame(SavedHistoryPlotMode::Week, end, 7));
  for (std::size_t i = 0; i < frame->count_; ++i) {
    auto& bucket = frame->buckets_[i]; bucket.date = (*dates)[i];
    if (matching && source->days[i].snapshot && source->days[i].snapshot->totals) {
      bucket.downloaded = (*source->days[i].snapshot->totals)[3];
      bucket.uploaded = (*source->days[i].snapshot->totals)[1];
    }
  }
  frame->finish(); return frame;
}

SavedHistoryPlotPresentation savedHistoryWeekHourlyPlot(core::HistoryDate end,
    std::shared_ptr<const core::SavedHistoryWeek> source) {
  const auto dates = core::savedHistoryWeekDates(end); if (!dates) { return {}; }
  bool matching = source && source->endingDate == end;
  if (matching) {
    for (std::size_t i = 0; i < dates->size(); ++i) {
      const auto& row = source->days[i];
      if (row.date != (*dates)[i] || (row.snapshot && row.snapshot->date != row.date)) { matching = false; break; }
    }
  }
  auto frame = std::shared_ptr<SavedHistoryPlotFrame>(new SavedHistoryPlotFrame(SavedHistoryPlotMode::WeekHourly, end, 168));
  for (std::size_t day = 0; day < dates->size(); ++day) {
    for (unsigned hour = 0; hour < 24; ++hour) {
      auto& bucket = frame->buckets_[day * 24 + hour]; bucket.date = (*dates)[day]; bucket.hour = hour;
      if (matching && source->days[day].snapshot && source->days[day].snapshot->hours[hour]) {
        bucket.downloaded = (*source->days[day].snapshot->hours[hour])[3];
        bucket.uploaded = (*source->days[day].snapshot->hours[hour])[1];
      }
    }
  }
  frame->finish(); return frame;
}

QString SavedHistoryPlotFrame::bucketLabel(std::size_t index) const {
  if (index >= count_) { return {}; }
  const auto& bucket = buckets_[index];
  return bucket.hour ? QStringLiteral("%1 %2:00 UTC").arg(dateText(bucket.date)).arg(*bucket.hour, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1 UTC").arg(dateText(bucket.date));
}
QString SavedHistoryPlotFrame::bucketToolTip(std::size_t index) const {
  if (index >= count_) { return {}; }
  const auto& bucket = buckets_[index];
  return bucketLabel(index) + QLatin1Char('\n')
      + QObject::tr("Payload downloaded: %1").arg(amount(bucket.downloaded)) + QLatin1Char('\n')
      + QObject::tr("Payload uploaded: %1").arg(amount(bucket.uploaded));
}
std::optional<QPointF> SavedHistoryPlotFrame::point(std::size_t index, bool uploaded, QRectF area) const noexcept {
  if (index >= count_ || !std::isfinite(area.left()) || !std::isfinite(area.top())
      || !std::isfinite(area.width()) || !std::isfinite(area.height())
      || !std::isfinite(area.right()) || !std::isfinite(area.bottom())
      || area.width() <= 0 || area.height() <= 0) { return {}; }
  const auto value = uploaded ? buckets_[index].uploaded : buckets_[index].downloaded;
  if (!value) { return {}; }
  // Convert BEFORE subtraction: INT64_MAX - INT64_MIN must never occur in an
  // integer expression. The entire signed domain is finite in long double.
  long double low = static_cast<long double>(minimum_), high = static_cast<long double>(maximum_);
  if (low == high) { low = -1; high = 1; } // Display geometry for actual zero, not an invented observation.
  const auto fraction = (high - static_cast<long double>(*value)) / (high - low);
  const auto x = area.left() + area.width() * (static_cast<qreal>(index) / static_cast<qreal>(count_ - 1));
  const auto y = area.top() + area.height() * static_cast<qreal>(fraction);
  if (!std::isfinite(x) || !std::isfinite(y)) { return {}; }
  return QPointF{x, y};
}

SavedHistoryPlot::SavedHistoryPlot(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true); setMinimumHeight(180); setMaximumHeight(260);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  setAccessibleName(tr("Saved payload traffic in bytes"));
}
QSize SavedHistoryPlot::sizeHint() const { return {640, 220}; }
void SavedHistoryPlot::setDay(core::HistoryDate date, std::shared_ptr<const core::SavedHistoryDay> source) {
  QPointer<SavedHistoryPlot> guard(this); const auto before = frame_;
  auto next = savedHistoryDayPlot(date, std::move(source));
  if (guard && guard->frame_ == before && next) { guard->replace(std::move(next)); }
}
void SavedHistoryPlot::setWeek(core::HistoryDate end, std::shared_ptr<const core::SavedHistoryWeek> source) {
  QPointer<SavedHistoryPlot> guard(this); const auto before = frame_;
  auto next = savedHistoryWeekPlot(end, std::move(source));
  if (guard && guard->frame_ == before && next) { guard->replace(std::move(next)); }
}
void SavedHistoryPlot::setWeekHourly(core::HistoryDate end, std::shared_ptr<const core::SavedHistoryWeek> source) {
  QPointer<SavedHistoryPlot> guard(this); const auto before = frame_;
  auto next = savedHistoryWeekHourlyPlot(end, std::move(source));
  if (guard && guard->frame_ == before && next) { guard->replace(std::move(next)); }
}

void SavedHistoryPlot::replace(SavedHistoryPlotPresentation next) {
  QPointer<SavedHistoryPlot> guard(this);
  const auto name = description(*next), tip = summary(*next);
  if (!guard) { return; }
  frame_ = next;
  const auto current = [&] { return guard && guard->frame_ == next; };
  guard->setAccessibleName(name);
  if (!current()) { return; }
  guard->setAccessibleDescription(tr("Payload downloaded uses a solid blue line. Payload uploaded uses a dashed orange line. Values are signed bytes; unknown buckets are not connected."));
  if (!current()) { return; }
  guard->setToolTip(tip);
  if (current()) { guard->update(); }
}
QRectF SavedHistoryPlot::plotRect() const {
  const auto frame = frame_;
  const auto minimum = frame ? frame->minimum() : 0, maximum = frame ? frame->maximum() : 0;
  const auto metrics = fontMetrics();
  const int textWidth = std::max(metrics.horizontalAdvance(tr("%1 bytes").arg(minimum)),
      metrics.horizontalAdvance(tr("%1 bytes").arg(maximum)));
  const qreal left = std::min(static_cast<qreal>(textWidth + 12), std::max(qreal{20}, static_cast<qreal>(width()) * 0.36));
  return QRectF(rect()).adjusted(left, 48, -12, -30);
}
bool SavedHistoryPlot::bucketToolTip(qreal x) {
  QPointer<SavedHistoryPlot> guard(this); const auto frame = frame_;
  QString tip = tr("No saved history selected");
  if (frame) {
    const auto area = plotRect(); std::size_t index = 0;
    if (std::isfinite(x) && area.width() > 0) {
      const auto fraction = std::clamp((x - area.left()) / area.width(), qreal{0}, qreal{1});
      index = static_cast<std::size_t>(std::round(fraction * static_cast<qreal>(frame->buckets().size() - 1)));
    }
    tip = frame->bucketToolTip(index);
  }
  if (!guard || guard->frame_ != frame) { return false; }
  guard->setToolTip(tip);
  return guard && guard->frame_ == frame;
}
void SavedHistoryPlot::mouseMoveEvent(QMouseEvent* event) {
  QPointer<SavedHistoryPlot> guard(this);
  if (bucketToolTip(event->position().x()) && guard) { guard->QWidget::mouseMoveEvent(event); }
}
bool SavedHistoryPlot::event(QEvent* event) {
  if (event->type() == QEvent::ToolTip) {
    const auto* help = static_cast<QHelpEvent*>(event);
    if (!bucketToolTip(help->pos().x())) { return true; }
  }
  return QWidget::event(event);
}
void SavedHistoryPlot::paintEvent(QPaintEvent*) {
  if (!isVisible() || window()->isMinimized()) { return; }
  const auto frame = frame_; QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), palette().base()); const auto area = plotRect(); const auto metrics = painter.fontMetrics();
  painter.setPen(palette().text().color());
  const auto title = frame ? description(*frame) : tr("No saved history selected");
  painter.drawText(QRect(8, 0, std::max(0, width() - 16), 22), Qt::AlignLeft | Qt::AlignVCenter,
      metrics.elidedText(title, Qt::ElideRight, std::max(0, width() - 16)));
  const std::array<QString, 2> names{tr("Payload downloaded"), tr("Payload uploaded")};
  const int half = std::max(0, (width() - 16) / 2);
  for (std::size_t series = 0; series < names.size(); ++series) {
    const int left = 8 + static_cast<int>(series) * half;
    painter.setPen(QPen(Colors[series], 2.0, Styles[series])); painter.drawLine(left, 33, left + std::min(24, half), 33);
    painter.setPen(palette().text().color());
    painter.drawText(QRect(left + 30, 22, std::max(0, half - 30), 22), Qt::AlignLeft | Qt::AlignVCenter,
        metrics.elidedText(names[series], Qt::ElideRight, std::max(0, half - 30)));
  }
  if (area.width() <= 0 || area.height() <= 0) { return; }
  painter.setPen(palette().mid().color()); painter.drawRect(area);
  if (!frame) { painter.setPen(palette().text().color()); painter.drawText(area, Qt::AlignCenter, tr("No saved payload observations")); return; }
  bool known = false;
  for (const auto& bucket : frame->buckets()) { known = known || bucket.downloaded.has_value() || bucket.uploaded.has_value(); }
  if (!known) { painter.setPen(palette().text().color()); painter.drawText(area, Qt::AlignCenter, tr("No saved payload observations")); }
  const auto axisLabel = [&](std::int64_t value, qreal y) {
    painter.setPen(palette().text().color()); const auto label = tr("%1 bytes").arg(value);
    painter.drawText(QRectF(0, y - 10, std::max(qreal{0}, area.left() - 6), 20), Qt::AlignRight | Qt::AlignVCenter,
        metrics.elidedText(label, Qt::ElideLeft, std::max(0, static_cast<int>(area.left()) - 6)));
  };
  if (frame->maximum() == frame->minimum()) { axisLabel(0, area.center().y()); }
  else { axisLabel(frame->maximum(), area.top()); axisLabel(frame->minimum(), area.bottom()); }
  const long double low = static_cast<long double>(frame->minimum()), high = static_cast<long double>(frame->maximum());
  if (known) {
    const auto zeroY = low == high ? area.center().y()
        : area.top() + area.height() * static_cast<qreal>(high / (high - low));
    painter.setPen(QPen(palette().mid().color(), 1.0, Qt::DotLine)); painter.drawLine(QPointF{area.left(), zeroY}, QPointF{area.right(), zeroY});
    painter.save(); painter.setClipRect(area.adjusted(-3, -3, 3, 3));
    for (std::size_t series = 0; series < Colors.size(); ++series) {
      painter.setPen(QPen(Colors[series], 2.0, Styles[series])); painter.setBrush(Colors[series]);
      for (std::size_t i = 0; i < frame->buckets().size(); ++i) {
        const auto point = frame->point(i, series == 1, area); if (!point) { continue; }
        if (i > 0) { if (const auto previous = frame->point(i - 1, series == 1, area)) { painter.drawLine(*previous, *point); } }
        painter.drawEllipse(*point, 2.5, 2.5);
      }
    }
    painter.restore();
  }
  painter.setPen(palette().text().color()); qreal previousRight = -1;
  for (std::size_t i = 0; i < frame->buckets().size(); ++i) {
    const auto& bucket = frame->buckets()[i];
    const bool hourlyWeek = frame->mode() == SavedHistoryPlotMode::WeekHourly;
    if (hourlyWeek && bucket.hour != 0 && i + 1 != frame->buckets().size()) { continue; }
    const auto label = hourlyWeek
        ? QStringLiteral("%1-%2 %3:00").arg(bucket.date.month, 2, 10, QLatin1Char('0'))
            .arg(bucket.date.day, 2, 10, QLatin1Char('0')).arg(*bucket.hour, 2, 10, QLatin1Char('0'))
        : bucket.hour ? QStringLiteral("%1:00").arg(*bucket.hour, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1-%2").arg(bucket.date.month, 2, 10, QLatin1Char('0')).arg(bucket.date.day, 2, 10, QLatin1Char('0'));
    const auto x = area.left() + area.width() * (static_cast<qreal>(i) / static_cast<qreal>(frame->buckets().size() - 1));
    const qreal labelWidth = metrics.horizontalAdvance(label);
    const auto left = std::clamp(x - labelWidth / 2, qreal{0}, std::max(qreal{0}, static_cast<qreal>(width()) - labelWidth));
    if (left < previousRight + 8) { continue; }
    painter.drawText(QRectF(left, area.bottom() + 4, labelWidth, 22), Qt::AlignHCenter | Qt::AlignVCenter, label);
    previousRight = left + labelWidth;
  }
}

} // namespace biglybtc::ui

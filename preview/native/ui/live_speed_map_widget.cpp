#include "live_speed_map_widget.hpp"
#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QShowEvent>
#include <algorithm>
#include <limits>
#include <utility>
namespace biglybtc::ui {
namespace {
QString number(const std::optional<std::int64_t>& value) { return value ? QString::number(*value) : QObject::tr("unavailable"); }
constexpr std::uint16_t DisplayCap = 1199;
}
LiveSpeedMapWidget::LiveSpeedMapWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("live-speed-map"));
  setAccessibleName(tr("Passive SpeedManager map from scoped rate and RTT observations"));
  setMouseTracking(true); setMinimumSize(240, 230);
}
bool LiveSpeedMapWidget::setSnapshot(std::shared_ptr<const core::PassiveSpeedSnapshot> source, bool current) {
  if (revision_ == std::numeric_limits<quint64>::max()
      || (source && source->history.size() > core::PassiveSpeedMapper::MaximumPoints)) { return false; }
  if (source == source_ && current == current_) { refresh(); return true; }
  ++revision_; source_ = std::move(source); current_ = current; dirty_ = true;
  maxUpload_ = maxDownload_ = 1; maxMetric_ = 0;
  if (source_) {
    for (const auto& p : source_->history) {
      maxUpload_ = std::max(maxUpload_, std::uint32_t{p.uploadBin} * 256);
      maxDownload_ = std::max(maxDownload_, std::uint32_t{p.downloadBin} * 256);
      maxMetric_ = std::max(maxMetric_, std::min(p.metric, DisplayCap));
    }
  }
  refresh(); return true;
}
QRectF LiveSpeedMapWidget::plotRect() const noexcept {
  const auto area = QRectF(rect()).adjusted(66, 38, -24, -55);
  return area.width() > 0 && area.height() > 0 ? area : QRectF{};
}
std::optional<QPointF> LiveSpeedMapWidget::point(std::size_t index) const noexcept {
  const auto area = plotRect();
  if (!source_ || index >= source_->history.size() || area.isEmpty()) { return {}; }
  const auto& p = source_->history[index];
  return QPointF(area.left() + static_cast<double>(static_cast<long double>(std::uint32_t{p.uploadBin} * 256) / maxUpload_) * area.width(),
      area.bottom() - static_cast<double>(static_cast<long double>(std::uint32_t{p.downloadBin} * 256) / maxDownload_) * area.height());
}
QString LiveSpeedMapWidget::pointDescription(std::size_t index) const {
  if (!source_ || index >= source_->history.size()) { return {}; }
  const auto& p = source_->history[index];
  return tr("Observed pair %1: input upload %2 bytes/s; download %3 bytes/s; RTT %4 ms; mapped upload %5 bytes/s / download %6 bytes/s; latency metric %7; source time %8 ms%9")
      .arg(index + 1).arg(number(p.input.uploadBytesPerSecond), number(p.input.downloadBytesPerSecond), number(p.input.rttMillis))
      .arg(std::uint32_t{p.uploadBin} * 256).arg(std::uint32_t{p.downloadBin} * 256).arg(p.metric).arg(p.input.observedMillis)
      .arg(current_ && source_->state == core::PassiveSpeedState::Ready ? QString{} : tr("; last observed"));
}
void LiveSpeedMapWidget::refresh() {
  if (!dirty_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<LiveSpeedMapWidget> guard(this); const auto revision = revision_;
  const auto valid = [&] { return guard && guard->revision_ == revision && guard->isVisible() && !guard->window()->isMinimized(); };
  const auto description = !source_ ? tr("Live mapper observation unavailable; no points inferred")
      : source_->history.empty() ? tr("No paired rate/RTT observations; no points inferred")
      : tr("%1 actual paired map points%2. Upload/download are mapped combined bytes/s. Color is a variance/latency metric, not raw RTT. No route, probe or speed adjustment is started here.")
          .arg(source_->history.size()).arg(current_ && source_->state == core::PassiveSpeedState::Ready ? QString{} : tr("; last observed"));
  setToolTip(description); if (!valid()) { return; }
  setAccessibleDescription(description); if (!valid()) { return; }
  dirty_ = false; update();
}
void LiveSpeedMapWidget::paintEvent(QPaintEvent*) {
  if (!isVisible() || window()->isMinimized()) { return; }
  QPainter painter(this); painter.fillRect(rect(), palette().base()); painter.setPen(palette().text().color());
  painter.drawText(QRectF(8, 2, std::max(0, width() - 16), 28), Qt::AlignCenter,
      current_ && source_ && source_->state == core::PassiveSpeedState::Ready ? tr("Passive live SpeedManager map") : tr("Last-observed passive SpeedManager map"));
  const auto area = plotRect(); if (area.isEmpty()) { return; }
  painter.setPen(palette().mid().color()); painter.drawRect(area); painter.setPen(palette().text().color());
  painter.drawText(QRectF(area.left(), area.bottom() + 3, area.width(), 22), Qt::AlignCenter, tr("Mapped upload (bytes/s)"));
  painter.save(); painter.translate(17, area.center().y()); painter.rotate(-90);
  painter.drawText(QRectF(-area.height() / 2, -12, area.height(), 24), Qt::AlignCenter, tr("Mapped download (bytes/s)")); painter.restore();
  const auto source = source_;
  if (!source || source->history.empty()) {
    painter.drawText(area.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
        source ? tr("No paired observations") : tr("Live measurements unavailable")); return;
  }
  painter.setRenderHint(QPainter::Antialiasing);
  for (std::size_t i = 0; i < source->history.size(); ++i) {
    const auto p = point(i); if (!p) { continue; }
    const auto fraction = static_cast<double>(std::min(source->history[i].metric, DisplayCap)) / std::max<std::uint16_t>(1, maxMetric_);
    auto color = QColor::fromHsvF((1.0 - fraction) * (2.0 / 3.0), 0.8, 0.85);
    if (!current_ || source->state != core::PassiveSpeedState::Ready) { color.setAlpha(170); }
    painter.setPen(QPen(color, 1)); painter.setBrush(color); painter.drawEllipse(*p, 3.5, 3.5);
  }
  painter.setPen(palette().text().color());
  painter.drawText(QRectF(area.left(), area.bottom() + 25, area.width(), 20), Qt::AlignCenter,
      tr("Saved state is separate; latency metric cool 0 → warm %1").arg(maxMetric_));
}
void LiveSpeedMapWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!source_ || !isVisible() || window()->isMinimized()) { QWidget::mouseMoveEvent(event); return; }
  std::optional<std::size_t> nearest; double distance = 81;
  for (std::size_t i = 0; i < source_->history.size(); ++i) {
    const auto here = point(i); if (!here) { continue; }
    const auto x = here->x() - event->position().x(), y = here->y() - event->position().y();
    const auto squared = x * x + y * y; if (squared < distance) { distance = squared; nearest = i; }
  }
  if (!nearest) { QWidget::mouseMoveEvent(event); return; }
  setToolTip(pointDescription(*nearest)); // Final access: property callbacks may delete/rebind this widget.
}
void LiveSpeedMapWidget::showEvent(QShowEvent* event) {
  QPointer<LiveSpeedMapWidget> guard(this); QWidget::showEvent(event); if (guard) { refresh(); }
}
} // namespace biglybtc::ui

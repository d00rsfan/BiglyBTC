#include "saved_speed_map_widget.hpp"

#include <QAccessible>
#include <QColor>
#include <QFontMetrics>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QShowEvent>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace biglybtc::ui {
namespace {
constexpr std::uint16_t DisplayMetricCap = 1199;
QString raw(const std::optional<std::int64_t>& value) {
  return value ? QString::number(*value) : QObject::tr("unavailable");
}
QColor metricColor(std::uint16_t value, std::uint16_t maximum) {
  const auto capped = std::min(value, DisplayMetricCap);
  const auto scale = std::max<std::uint16_t>(1, std::min(maximum, DisplayMetricCap));
  const auto fraction = std::clamp(static_cast<double>(capped) / scale, 0.0, 1.0);
  return QColor::fromHsvF((1.0 - fraction) * (2.0 / 3.0), 0.8, 0.85);
}
}
SavedSpeedMapWidget::SavedSpeedMapWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("saved-speed-map"));
  setAccessibleName(tr("Last-saved SpeedManager upload/download latency map"));
  setFocusPolicy(Qt::StrongFocus); setMinimumSize(240, 220);
}
bool SavedSpeedMapWidget::setPresentation(SavedSpeedManagerPresentation packet, quint64 expectedSession) {
  if (expectedSession < session_ || revision_ == std::numeric_limits<quint64>::max()) { return false; }
  if (packet && !acceptSavedSpeedManager(expectedSession == session_ ? packet_ : SavedSpeedManagerPresentation{},
      packet, expectedSession)) {
    if (expectedSession == session_ && packet == packet_) { refresh(); return true; }
    return false;
  }
  if (packet == packet_ && expectedSession == session_) { refresh(); return true; }
  std::shared_ptr<const core::SavedSpeedMap> projected;
  std::optional<core::SavedSpeedMapError> error;
  if (packet && packet->state == SavedSpeedManagerState::Ready) {
    auto result = core::readSavedSpeedMap(packet->history->source);
    if (!result) { error = result.error(); }
    else {
      try { projected = std::make_shared<const core::SavedSpeedMap>(std::move(*result)); }
      catch (const std::bad_alloc&) { error = core::SavedSpeedMapError::Memory; }
    }
  }
  ++revision_; session_ = expectedSession; packet_ = std::move(packet);
  map_ = std::move(projected); error_ = error; selected_.reset(); dirty_ = true;
  refresh(); return true;
}
QRectF SavedSpeedMapWidget::plotRect() const noexcept {
  const QRectF area = QRectF(rect()).adjusted(64, 42, -24, -78);
  return area.width() > 0 && area.height() > 0 ? area : QRectF{};
}
std::optional<QPointF> SavedSpeedMapWidget::point(std::size_t record) const noexcept {
  const auto area = plotRect();
  if (!map_ || record >= map_->records.size() || !map_->records[record].point || area.isEmpty()) { return {}; }
  const auto& value = *map_->records[record].point;
  const auto maxX = std::max<std::uint32_t>(1, map_->maxUploadBytesPerSecond.value_or(0));
  const auto maxY = std::max<std::uint32_t>(1, map_->maxDownloadBytesPerSecond.value_or(0));
  return QPointF(area.left() + static_cast<double>(static_cast<long double>(value.uploadBytesPerSecond) / maxX) * area.width(),
      area.bottom() - static_cast<double>(static_cast<long double>(value.downloadBytesPerSecond) / maxY) * area.height());
}
QString SavedSpeedMapWidget::recordDescription(std::size_t record) const {
  if (!map_ || record >= map_->records.size()) { return {}; }
  const auto& value = map_->records[record];
  const auto stored = tr("Raw saved x %1 / y %2 / metric %3").arg(raw(value.rawX), raw(value.rawY), raw(value.rawMetric));
  if (!value.point) { return tr("Saved record %1: malformed fields; %2").arg(value.sourceIndex + 1).arg(stored); }
  const auto& p = *value.point;
  return tr("Saved record %1: upload %2 bytes/s; download %3 bytes/s; saved latency metric %4; %5%6")
      .arg(value.sourceIndex + 1).arg(p.uploadBytesPerSecond).arg(p.downloadBytesPerSecond).arg(p.metric).arg(stored)
      .arg(p.metricClamped ? tr("; source metric upper-clamped") : QString{});
}
QString SavedSpeedMapWidget::summary() const {
  if (!packet_) { return tr("Saved map unavailable"); }
  if (packet_->state == SavedSpeedManagerState::Missing) { return tr("No saved mapper history"); }
  if (packet_->state == SavedSpeedManagerState::Failed) { return tr("Saved map unavailable: read failed"); }
  if (error_) {
    return *error_ == core::SavedSpeedMapError::Limit ? tr("Saved map unavailable: projection limit")
        : *error_ == core::SavedSpeedMapError::Memory ? tr("Saved map unavailable: memory allocation failed")
        : tr("Saved map unavailable: invalid saved projection");
  }
  if (!map_ || map_->presence == core::SavedSpeedMapPresence::Missing) { return tr("Saved ping-map records were not recorded"); }
  if (map_->presence == core::SavedSpeedMapPresence::Malformed) { return tr("Saved ping-map list is malformed; values unavailable"); }
  if (map_->records.empty()) { return tr("Known empty saved ping map"); }
  return tr("Last-saved map: %1 valid records / %2 saved slots%3. Upload/download bytes/s; color is saved latency metric, not raw RTT or time.")
      .arg(map_->records.size() - map_->malformedRecords).arg(map_->records.size())
      .arg(map_->malformedRecords ? tr("; partial: %1 malformed slots").arg(map_->malformedRecords) : QString{});
}
QString SavedSpeedMapWidget::description() const {
  return selected_ ? recordDescription(*selected_) : summary();
}
void SavedSpeedMapWidget::queueRefresh() {
  if (queued_) { return; } queued_ = true; QPointer<SavedSpeedMapWidget> guard(this);
  QMetaObject::invokeMethod(this, [guard] { if (guard) { guard->queued_ = false; guard->refresh(); } }, Qt::QueuedConnection);
}
void SavedSpeedMapWidget::refresh() {
  if (!dirty_ || publishing_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<SavedSpeedMapWidget> guard(this); const auto revision = revision_;
  const auto current = [&] { return guard && guard->revision_ == revision && guard->isVisible() && !guard->window()->isMinimized(); };
  publishing_ = true;
  struct Finish {
    QPointer<SavedSpeedMapWidget> owner; quint64 revision;
    ~Finish() { if (owner) { owner->publishing_ = false;
      if (owner->dirty_ && owner->revision_ != revision) { owner->queueRefresh(); } } }
  } finish{guard, revision};
  const auto text = description(); setToolTip(text); if (!current()) { return; }
  const bool changed = accessibleDescription() != text;
  setAccessibleDescription(text); if (!current()) { return; }
  if (changed) { QAccessibleEvent event(this, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&event); if (!current()) { return; } }
  dirty_ = false; update();
}
void SavedSpeedMapWidget::select(std::size_t record) {
  if (!map_ || record >= map_->records.size() || revision_ == std::numeric_limits<quint64>::max()) { return; }
  ++revision_; selected_ = record; dirty_ = true; refresh();
}
void SavedSpeedMapWidget::keyPressEvent(QKeyEvent* event) {
  if (event->modifiers() != Qt::NoModifier || !isVisible() || window()->isMinimized() || !hasFocus()
      || !map_ || map_->records.empty()) { QWidget::keyPressEvent(event); return; }
  const auto last = map_->records.size() - 1;
  auto index = selected_.value_or(0);
  switch (event->key()) {
    case Qt::Key_Left: if (selected_ && index > 0) { --index; } break;
    case Qt::Key_Right: if (selected_ && index < last) { ++index; } break;
    case Qt::Key_Home: index = 0; break;
    case Qt::Key_End: index = last; break;
    default: QWidget::keyPressEvent(event); return;
  }
  event->accept(); select(index); // Final access: publication may delete/rebind this widget.
}
void SavedSpeedMapWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || event->modifiers() != Qt::NoModifier || !map_
      || !isVisible() || window()->isMinimized()) { QWidget::mousePressEvent(event); return; }
  std::optional<std::size_t> nearest; double distance = 81;
  for (std::size_t i = 0; i < map_->records.size(); ++i) {
    const auto here = point(i); if (!here) { continue; }
    const auto dx = here->x() - event->position().x(), dy = here->y() - event->position().y();
    const auto squared = dx * dx + dy * dy; if (squared < distance) { distance = squared; nearest = i; }
  }
  if (!nearest) { QWidget::mousePressEvent(event); return; }
  event->accept(); select(*nearest);
}
void SavedSpeedMapWidget::paintEvent(QPaintEvent*) {
  if (!isVisible() || window()->isMinimized()) { return; }
  QPainter painter(this); painter.fillRect(rect(), palette().base());
  painter.setPen(palette().text().color()); painter.drawText(QRectF(8, 4, width() - 16, 30), Qt::AlignCenter, tr("Last-saved SpeedManager map"));
  const auto area = plotRect(); if (area.isEmpty()) { return; }
  const auto source = map_; const auto selected = selected_;
  painter.setPen(palette().mid().color()); painter.drawRect(area);
  painter.setPen(palette().text().color());
  painter.drawText(QRectF(area.left(), area.bottom() + 4, area.width(), 22), Qt::AlignCenter, tr("Upload (bytes/s)"));
  painter.save(); painter.translate(18, area.center().y()); painter.rotate(-90);
  painter.drawText(QRectF(-area.height() / 2, -12, area.height(), 24), Qt::AlignCenter, tr("Download (bytes/s)")); painter.restore();
  painter.drawText(QPointF(area.left(), area.bottom() + 17), QStringLiteral("0"));
  painter.drawText(QRectF(area.right() - 110, area.bottom() + 5, 110, 20), Qt::AlignRight,
      source && source->maxUploadBytesPerSecond ? QString::number(*source->maxUploadBytesPerSecond) : QString{});
  painter.drawText(QRectF(area.left(), area.top() - 23, area.width(), 20), Qt::AlignLeft,
      source && source->maxDownloadBytesPerSecond ? QString::number(*source->maxDownloadBytesPerSecond) : QString{});
  if (source && source->maxMetric) {
    painter.setRenderHint(QPainter::Antialiasing);
    for (std::size_t i = 0; i < source->records.size(); ++i) {
      const auto here = point(i); if (!here) { continue; }
      const auto color = metricColor(source->records[i].point->metric, *source->maxMetric);
      painter.setPen(QPen(color, 1)); painter.setBrush(color); painter.drawEllipse(*here, 3.5, 3.5);
      if (selected == i) { painter.setPen(QPen(palette().text().color(), 2)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(*here, 6, 6); }
    }
    painter.setPen(palette().text().color());
    painter.drawText(QRectF(area.left(), area.bottom() + 26, area.width(), 20), Qt::AlignCenter,
        tr("Saved latency metric: cool 0 → warm %1 (higher values saturated)").arg(std::min(*source->maxMetric, DisplayMetricCap)));
  } else {
    painter.drawText(area.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, summary());
  }
  painter.setPen(palette().text().color());
  const auto caption = selected ? recordDescription(*selected) : summary();
  painter.drawText(QRectF(8, height() - 25, width() - 16, 22), Qt::AlignCenter,
      fontMetrics().elidedText(caption, Qt::ElideRight, std::max(0, width() - 16)));
  if (hasFocus()) { QStyleOptionFocusRect option; option.initFrom(this); option.rect = rect().adjusted(1, 1, -1, -1);
    style()->drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, this); }
}
void SavedSpeedMapWidget::showEvent(QShowEvent* event) {
  QPointer<SavedSpeedMapWidget> guard(this); QWidget::showEvent(event); if (guard) { refresh(); }
}
void SavedSpeedMapWidget::changeEvent(QEvent* event) {
  QPointer<SavedSpeedMapWidget> guard(this); QWidget::changeEvent(event);
  if (guard && event->type() == QEvent::WindowStateChange) { queueRefresh(); }
}
void SavedSpeedMapWidget::focusInEvent(QFocusEvent* event) {
  QPointer<SavedSpeedMapWidget> guard(this); QWidget::focusInEvent(event); if (guard) { update(); }
}
void SavedSpeedMapWidget::focusOutEvent(QFocusEvent* event) {
  QPointer<SavedSpeedMapWidget> guard(this); QWidget::focusOutEvent(event); if (guard) { update(); }
}
} // namespace biglybtc::ui

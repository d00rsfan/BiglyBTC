#include "history_range_plot.hpp"

#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QTimeZone>
#include <QEvent>
#include <QGuiApplication>
#include <QFontMetrics>
#include <QFocusEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStyle>
#include <QStyleOptionFocusRect>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace biglybtc::ui {
namespace {
QString utc(std::int64_t value) {
  return QDateTime::fromMSecsSinceEpoch(value, QTimeZone::utc()).toUTC().toString(Qt::ISODateWithMs);
}
QString amount(std::optional<std::int64_t> value) {
  return value ? QObject::tr("%1 bytes").arg(*value) : QObject::tr("Unknown");
}
QString trafficName(SavedHistoryTraffic traffic) {
  switch (traffic) {
    case SavedHistoryTraffic::Payload: return QObject::tr("Payload");
    case SavedHistoryTraffic::Protocol: return QObject::tr("Protocol");
    case SavedHistoryTraffic::Dht: return QObject::tr("DHT");
  }
  return {};
}
QString trafficNoun(SavedHistoryTraffic traffic) {
  switch (traffic) {
    case SavedHistoryTraffic::Payload: return QObject::tr("payload");
    case SavedHistoryTraffic::Protocol: return QObject::tr("protocol");
    case SavedHistoryTraffic::Dht: return QObject::tr("DHT");
  }
  return {};
}
std::array<std::size_t, 2> counterIndices(SavedHistoryTraffic traffic) {
  // download / upload, in the original six-component saved record order.
  if (traffic == SavedHistoryTraffic::Protocol) { return {2, 0}; }
  if (traffic == SavedHistoryTraffic::Dht) { return {5, 4}; }
  return {3, 1};
}
std::array<QString, 2> seriesNames(SavedHistoryTraffic traffic) {
  const auto name = trafficName(traffic);
  return {QObject::tr("%1 downloaded").arg(name), QObject::tr("%1 uploaded").arg(name)};
}
QString description(const HistoryRangePlotFrame& frame) {
  return QObject::tr("Saved rolling %1 traffic in bytes; UTC buckets").arg(trafficNoun(frame.traffic()));
}
QString summary(const HistoryRangePlotFrame& frame) {
  return description(frame) + QLatin1Char('\n')
      + QObject::tr("Inspect exact bucket intervals and signed saved values. Unknown values are gaps.");
}
const std::array<QColor, 2> Colors{QColor(30, 115, 210), QColor(195, 95, 15)};
const std::array<Qt::PenStyle, 2> Styles{Qt::SolidLine, Qt::DashLine};
bool validPlan(const core::SavedHistoryRangePlan& plan) {
  const auto expected = core::planSavedHistoryRange(plan.request,
      plan.request.period == core::HistoryPeriod::All ? std::optional{plan.startMillis} : std::nullopt);
  return expected && *expected == plan && plan.bucketCount > 0
      && plan.bucketCount <= core::SavedHistoryRangeMaximumBuckets;
}
} // namespace

HistoryRangePlotPresentation historyRangePlot(core::SavedHistoryRangePlan plan,
    std::shared_ptr<const core::SavedHistoryRange> source, SavedHistoryTraffic traffic) {
  if (!validPlan(plan) || !validSavedHistoryTraffic(traffic)) { return {}; }
  auto frame = std::shared_ptr<HistoryRangePlotFrame>(new HistoryRangePlotFrame);
  frame->plan_ = plan; frame->traffic_ = traffic;
  frame->buckets_.resize(plan.bucketCount);
  bool matching = source && source->plan == plan && source->buckets.size() == plan.bucketCount;
  std::int64_t start = plan.startMillis;
  for (std::size_t i = 0; i < plan.bucketCount; ++i) {
    // Regenerated bounded plan proves these nominal endpoints fit int64.
    const auto end = start + plan.bucketMillis;
    if (matching && (source->buckets[i].startMillis != start || source->buckets[i].endMillis != end)) { matching = false; }
    frame->buckets_[i].startMillis = start; frame->buckets_[i].endMillis = end;
    start = end;
  }
  const auto indices = counterIndices(traffic);
  for (std::size_t i = 0; i < plan.bucketCount; ++i) {
    auto& bucket = frame->buckets_[i];
    if (matching && source->buckets[i].totals) {
      bucket.downloaded = (*source->buckets[i].totals)[indices[0]];
      bucket.uploaded = (*source->buckets[i].totals)[indices[1]];
      for (const auto value : {bucket.downloaded, bucket.uploaded}) {
        frame->minimum_ = std::min(frame->minimum_, *value);
        frame->maximum_ = std::max(frame->maximum_, *value);
      }
    }
  }
  return frame;
}
QString HistoryRangePlotFrame::bucketLabel(std::size_t index) const {
  if (index >= buckets_.size()) { return {}; }
  const auto& bucket = buckets_[index];
  return QObject::tr("%1 — %2 UTC; nominal end exclusive").arg(utc(bucket.startMillis), utc(bucket.endMillis));
}
QString HistoryRangePlotFrame::bucketToolTip(std::size_t index) const {
  if (index >= buckets_.size()) { return {}; }
  const auto& bucket = buckets_[index]; const auto names = seriesNames(traffic_);
  const auto through = std::min(bucket.endMillis - 1, plan_.request.nowMillis);
  return bucketLabel(index) + QLatin1Char('\n')
      + QObject::tr("Query through %1 UTC (inclusive); epoch milliseconds [%2, %3]")
          .arg(utc(through)).arg(bucket.startMillis).arg(through) + QLatin1Char('\n')
      + QObject::tr("%1: %2").arg(names[0], amount(bucket.downloaded)) + QLatin1Char('\n')
      + QObject::tr("%1: %2").arg(names[1], amount(bucket.uploaded));
}
std::optional<QPointF> HistoryRangePlotFrame::point(std::size_t index, bool uploaded, QRectF area) const noexcept {
  if (index >= buckets_.size() || !std::isfinite(area.left()) || !std::isfinite(area.top())
      || !std::isfinite(area.width()) || !std::isfinite(area.height())
      || !std::isfinite(area.right()) || !std::isfinite(area.bottom())
      || area.width() <= 0 || area.height() <= 0) { return {}; }
  const auto value = uploaded ? buckets_[index].uploaded : buckets_[index].downloaded;
  if (!value) { return {}; }
  long double low = static_cast<long double>(minimum_), high = static_cast<long double>(maximum_);
  if (low == high) { low = -1; high = 1; }
  const auto fraction = (high - static_cast<long double>(*value)) / (high - low);
  const auto x = buckets_.size() == 1 ? area.center().x()
      : area.left() + area.width() * (static_cast<qreal>(index) / static_cast<qreal>(buckets_.size() - 1));
  const auto y = area.top() + area.height() * static_cast<qreal>(fraction);
  if (!std::isfinite(x) || !std::isfinite(y)) { return {}; }
  return QPointF{x, y};
}

HistoryRangePlot::HistoryRangePlot(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true); setMinimumHeight(180); setMaximumHeight(260);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName(tr("Saved payload traffic in bytes"));
  auto* copyAction = new QAction(tr("Copy selected values"), this);
  copyAction->setObjectName(QStringLiteral("saved-history-range-copy-selection"));
  copyAction->setShortcut(QKeySequence::Copy);
  copyAction->setShortcutContext(Qt::WidgetShortcut);
  copyAction->setEnabled(false);
  connect(copyAction, &QAction::triggered, this, [this] {
    QPointer<HistoryRangePlot> guard(this); const auto frame = frame_;
    const auto revision = interactionRevision_; const auto selection = selectedBucket_;
    if (!frame || !selection || *selection >= frame->buckets().size()) { return; }
    const auto text = frame->bucketToolTip(*selection);
    const auto current = [&] {
      return guard && guard->frame_ == frame && guard->interactionRevision_ == revision
          && guard->selectedBucket_ == selection;
    };
    if (!current()) { return; }
    QPointer<QClipboard> clipboard(QGuiApplication::clipboard());
    if (!clipboard || !current()) { return; }
    // Clipboard notifications may replace data, inspect another bucket, copy
    // again or delete this plot. Do no receiver work after publication.
    clipboard->setText(text, QClipboard::Clipboard);
  });
  setContextMenuPolicy(Qt::ActionsContextMenu);
  addAction(copyAction);
}
QSize HistoryRangePlot::sizeHint() const { return {640, 240}; }
void HistoryRangePlot::setRange(core::SavedHistoryRangePlan plan,
    std::shared_ptr<const core::SavedHistoryRange> source, SavedHistoryTraffic traffic) {
  QPointer<HistoryRangePlot> guard(this); const auto before = frame_;
  const auto revision = interactionRevision_;
  auto next = historyRangePlot(plan, std::move(source), traffic);
  if (guard && guard->frame_ == before && guard->interactionRevision_ == revision && next) { guard->replace(std::move(next)); }
}
void HistoryRangePlot::clear() {
  QPointer<HistoryRangePlot> guard(this);
  if (!advanceInteraction()) { return; }
  frame_.reset(); selectedBucket_.reset(); const auto revision = interactionRevision_;
  const auto current = [&] { return guard && !guard->frame_ && guard->interactionRevision_ == revision; };
  if (auto* action = findChild<QAction*>(QStringLiteral("saved-history-range-copy-selection"), Qt::FindDirectChildrenOnly)) {
    action->setEnabled(false); if (!current()) { return; }
  }
  setAccessibleName(tr("Saved rolling history in bytes")); if (!current()) { return; }
  setAccessibleDescription(tr("No saved query is loaded")); if (!current()) { return; }
  setToolTip(tr("No saved query is loaded")); if (current()) { guard->update(); }
}

bool HistoryRangePlot::advanceInteraction() noexcept {
  if (interactionRevision_ == std::numeric_limits<std::uint64_t>::max()) { return false; }
  ++interactionRevision_;
  return true;
}
void HistoryRangePlot::replace(HistoryRangePlotPresentation next) {
  QPointer<HistoryRangePlot> guard(this);
  const auto before = frame_; const auto beforeRevision = interactionRevision_;
  const auto name = description(*next), tip = summary(*next);
  const auto names = seriesNames(next->traffic());
  const auto explanation = tr("%1 uses a solid blue line. %2 uses a dashed orange line. Values are signed bytes; unknown buckets are not connected.").arg(names[0], names[1]);
  if (!guard || guard->frame_ != before || guard->interactionRevision_ != beforeRevision
      || !guard->advanceInteraction()) { return; }
  guard->frame_ = next; guard->selectedBucket_.reset();
  const auto revision = guard->interactionRevision_;
  const auto current = [&] { return guard && guard->frame_ == next && guard->interactionRevision_ == revision; };
  if (auto* copyAction = guard->findChild<QAction*>(QStringLiteral("saved-history-range-copy-selection"), Qt::FindDirectChildrenOnly)) {
    copyAction->setEnabled(false);
    if (!current()) { return; }
  }
  guard->setAccessibleName(name);
  if (!current()) { return; }
  guard->setAccessibleDescription(explanation);
  if (!current()) { return; }
  guard->setToolTip(tip);
  if (current()) { guard->update(); }
}
QRectF HistoryRangePlot::plotRect() const {
  const auto frame = frame_;
  const auto minimum = frame ? frame->minimum() : 0, maximum = frame ? frame->maximum() : 0;
  const auto metrics = fontMetrics();
  const int textWidth = std::max(metrics.horizontalAdvance(tr("%1 bytes").arg(minimum)),
      metrics.horizontalAdvance(tr("%1 bytes").arg(maximum)));
  const qreal left = std::min(static_cast<qreal>(textWidth + 12), std::max(qreal{20}, static_cast<qreal>(width()) * 0.36));
  return QRectF(rect()).adjusted(left, 48, -12, selectedBucket_ ? -54 : -30);
}
bool HistoryRangePlot::bucketToolTip(qreal x) {
  QPointer<HistoryRangePlot> guard(this); const auto frame = frame_;
  const auto revision = interactionRevision_;
  QString tip = tr("No saved history selected");
  if (frame) {
    const auto area = plotRect(); std::size_t index = 0;
    if (std::isfinite(x) && area.width() > 0) {
      const auto fraction = std::clamp((x - area.left()) / area.width(), qreal{0}, qreal{1});
      index = static_cast<std::size_t>(std::round(fraction * static_cast<qreal>(frame->buckets().size() - 1)));
    }
    tip = frame->bucketToolTip(index);
  }
  if (!guard || guard->frame_ != frame || guard->interactionRevision_ != revision) { return false; }
  guard->setToolTip(tip);
  return guard && guard->frame_ == frame && guard->interactionRevision_ == revision;
}
void HistoryRangePlot::mouseMoveEvent(QMouseEvent* event) {
  QPointer<HistoryRangePlot> guard(this);
  if (bucketToolTip(event->position().x()) && guard) { guard->QWidget::mouseMoveEvent(event); }
}
void HistoryRangePlot::selectBucket(std::size_t index) {
  QPointer<HistoryRangePlot> guard(this); const auto frame = frame_;
  const auto beforeRevision = interactionRevision_;
  if (!frame || index >= frame->buckets().size()) { return; }
  const auto name = description(*frame), text = frame->bucketToolTip(index);
  if (!guard || guard->frame_ != frame || guard->interactionRevision_ != beforeRevision
      || !guard->advanceInteraction()) { return; }
  guard->selectedBucket_ = index;
  const auto revision = guard->interactionRevision_;
  const auto current = [&] { return guard && guard->frame_ == frame && guard->interactionRevision_ == revision; };
  // Publish only after the key event is accepted and scalar state is committed.
  // Action and property callbacks may replace/delete us or inspect another
  // bucket against this very same immutable frame.
  if (auto* copyAction = guard->findChild<QAction*>(QStringLiteral("saved-history-range-copy-selection"), Qt::FindDirectChildrenOnly)) {
    copyAction->setEnabled(true);
    if (!current()) { return; }
  }
  // A key reentered from replacement's action-state notification may precede
  // that frame's name publication. Keep the canonical frame name current too.
  if (guard->accessibleName() != name) {
    guard->setAccessibleName(name);
    if (!current()) { return; }
  }
  guard->setToolTip(text);
  if (!current()) { return; }
  // Qt 6.12 emits DescriptionChanged here only when the text actually changes.
  guard->setAccessibleDescription(text);
  if (current()) { guard->update(); }
}
void HistoryRangePlot::keyPressEvent(QKeyEvent* event) {
  const bool navigation = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right
      || event->key() == Qt::Key_Home || event->key() == Qt::Key_End;
  if (!navigation || event->modifiers() != Qt::NoModifier || !isVisible()
      || window()->isMinimized() || !hasFocus() || !frame_) {
    QWidget::keyPressEvent(event); return;
  }
  const auto last = frame_->buckets().size() - 1;
  auto index = selectedBucket_.value_or(0);
  if (event->key() == Qt::Key_Home) { index = 0; }
  else if (event->key() == Qt::Key_End) { index = last; }
  else if (selectedBucket_) {
    if (event->key() == Qt::Key_Left && index > 0) { --index; }
    else if (event->key() == Qt::Key_Right && index < last) { ++index; }
  }
  event->accept();
  selectBucket(index);
}
void HistoryRangePlot::focusInEvent(QFocusEvent* event) {
  QPointer<HistoryRangePlot> guard(this); const auto frame = frame_;
  const auto revision = interactionRevision_;
  QWidget::focusInEvent(event);
  if (guard && guard->frame_ == frame && guard->interactionRevision_ == revision) { guard->update(); }
}
void HistoryRangePlot::focusOutEvent(QFocusEvent* event) {
  QPointer<HistoryRangePlot> guard(this); const auto frame = frame_;
  const auto revision = interactionRevision_;
  QWidget::focusOutEvent(event);
  if (guard && guard->frame_ == frame && guard->interactionRevision_ == revision) { guard->update(); }
}
bool HistoryRangePlot::event(QEvent* event) {
  if (event->type() == QEvent::ToolTip) {
    const auto* help = static_cast<QHelpEvent*>(event);
    if (!bucketToolTip(help->pos().x())) { return true; }
  }
  return QWidget::event(event);
}
void HistoryRangePlot::paintEvent(QPaintEvent*) {
  if (!isVisible() || window()->isMinimized()) { return; }
  const auto frame = frame_; const auto selection = selectedBucket_; QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), palette().base()); const auto area = plotRect(); const auto metrics = painter.fontMetrics();
  if (hasFocus()) {
    QStyleOptionFocusRect focus; focus.initFrom(this);
    focus.rect = rect().adjusted(1, 1, -2, -2); focus.backgroundColor = palette().base().color();
    style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
  }
  if (frame && selection && *selection < frame->buckets().size()) {
    auto caption = frame->bucketToolTip(*selection);
    caption.replace(QLatin1Char('\n'), QStringLiteral("  |  "));
    painter.setPen(palette().text().color());
    painter.drawText(QRect(8, height() - 24, std::max(0, width() - 16), 24), Qt::AlignLeft | Qt::AlignVCenter,
        metrics.elidedText(caption, Qt::ElideRight, std::max(0, width() - 16)));
  }
  painter.setPen(palette().text().color());
  const auto title = frame ? description(*frame) : tr("No saved history selected");
  painter.drawText(QRect(8, 0, std::max(0, width() - 16), 22), Qt::AlignLeft | Qt::AlignVCenter,
      metrics.elidedText(title, Qt::ElideRight, std::max(0, width() - 16)));
  const auto names = seriesNames(frame ? frame->traffic() : SavedHistoryTraffic::Payload);
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
  if (!frame) { painter.setPen(palette().text().color()); painter.drawText(area, Qt::AlignCenter, tr("No saved %1 observations").arg(trafficNoun(frame ? frame->traffic() : SavedHistoryTraffic::Payload))); return; }
  bool known = false;
  for (const auto& bucket : frame->buckets()) { known = known || bucket.downloaded.has_value() || bucket.uploaded.has_value(); }
  if (!known) { painter.setPen(palette().text().color()); painter.drawText(area, Qt::AlignCenter, tr("No saved %1 observations").arg(trafficNoun(frame ? frame->traffic() : SavedHistoryTraffic::Payload))); }
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
  if (hasFocus() && selection && *selection < frame->buckets().size()) {
    // X exists even for an unknown bucket. Unknown values deliberately have no
    // Y coordinate or point outline, and the series above still keep gaps.
    const auto x = frame->buckets().size() == 1 ? area.center().x()
        : area.left() + area.width() * (static_cast<qreal>(*selection)
            / static_cast<qreal>(frame->buckets().size() - 1));
    painter.save(); painter.setClipRect(area.adjusted(-6, -6, 6, 6));
    painter.setPen(QPen(palette().highlight().color(), 1.0, Qt::DashLine));
    painter.drawLine(QPointF{x, area.top()}, QPointF{x, area.bottom()});
    painter.setPen(QPen(palette().highlight().color(), 1.5)); painter.setBrush(Qt::NoBrush);
    for (const bool uploaded : {false, true}) {
      if (const auto point = frame->point(*selection, uploaded, area)) { painter.drawEllipse(*point, 5.0, 5.0); }
    }
    painter.restore();
  }
  painter.setPen(palette().text().color()); qreal previousRight = -1;
  for (std::size_t i = 0; i < frame->buckets().size(); ++i) {
    const auto& bucket = frame->buckets()[i];
    const auto stamp = QDateTime::fromMSecsSinceEpoch(bucket.startMillis, QTimeZone::utc());
    const auto label = stamp.toString(frame->plan().bucketMillis < 86400000
        ? QStringLiteral("MM-dd HH:mm") : QStringLiteral("yyyy-MM-dd"));
    const auto x = frame->buckets().size() == 1 ? area.center().x()
        : area.left() + area.width() * (static_cast<qreal>(i) / static_cast<qreal>(frame->buckets().size() - 1));
    const qreal labelWidth = metrics.horizontalAdvance(label);
    const auto left = std::clamp(x - labelWidth / 2, qreal{0}, std::max(qreal{0}, static_cast<qreal>(width()) - labelWidth));
    if (left < previousRight + 8) { continue; }
    painter.drawText(QRectF(left, area.bottom() + 4, labelWidth, 22), Qt::AlignHCenter | Qt::AlignVCenter, label);
    previousRight = left + labelWidth;
  }
}

} // namespace biglybtc::ui

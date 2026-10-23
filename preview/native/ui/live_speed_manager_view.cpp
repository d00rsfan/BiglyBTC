#include "live_speed_manager_view.hpp"
#include "live_speed_map_widget.hpp"
#include <QEvent>
#include <QLabel>
#include <QShowEvent>
#include <QVBoxLayout>
#include <limits>
#include <utility>
namespace biglybtc::ui {
namespace {
QString number(const std::optional<std::int64_t>& value) { return value ? QString::number(*value) : QObject::tr("unavailable"); }
QString limit(const std::optional<core::PassiveSpeedEstimate>& value, bool current) {
  if (!value) { return QObject::tr("Unavailable; requires consecutive qualified observations"); }
  return (current ? QObject::tr("Mapper policy estimate: ") : QObject::tr("Last-observed mapper policy estimate: "))
      + QObject::tr("%1 bytes/s; metric rating %2; %3 hits; owner time %4 ms")
          .arg(value->bytesPerSecond).arg(value->metricRating, 0, 'g', 6).arg(value->hits).arg(value->whenMillis);
}
QString persistent(const std::optional<core::PassiveSpeedEstimate>& value, bool fromSaved, bool current) {
  if (!value) { return QObject::tr("persistent policy unavailable"); }
  return QObject::tr("persistent policy %1 bytes/s (%2)").arg(value->bytesPerSecond)
      .arg(fromSaved ? QObject::tr("saved bias, not current measurement")
          : current ? QObject::tr("derived estimate") : QObject::tr("last observed"));
}
QString capacity(const std::optional<core::PassiveSpeedCapacity>& value, bool current) {
  if (!value) { return QObject::tr("Unavailable; no sustained/choke-derived capacity or saved seed"); }
  const auto& record = value->value;
  if (value->origin == core::PassiveCapacityOrigin::Saved || record.fromSaved) {
    return QObject::tr("Saved capacity seed: %1 bytes/s; %2; not a current measurement")
        .arg(record.bytesPerSecond).arg(record.estimateType == 1 ? QObject::tr("manual, read-only") : QObject::tr("saved source type %1").arg(record.estimateType, 0, 'g', 6));
  }
  const auto origin = value->origin == core::PassiveCapacityOrigin::Sustained ? QObject::tr("sustained-rate policy") : QObject::tr("choke-derived policy");
  return QObject::tr("%1: %2 bytes/s; %3; derived owner time %4 ms")
      .arg(current && value->current ? QObject::tr("Observed capacity result") : QObject::tr("Last-derived capacity result"))
      .arg(record.bytesPerSecond).arg(origin).arg(number(value->lastDerivedMillis));
}
QString reason(core::PassiveSpeedState state) {
  switch (state) {
    case core::PassiveSpeedState::Empty: return QObject::tr("waiting for real RTT/rate pairs");
    case core::PassiveSpeedState::Ready: return QObject::tr("qualified paired observations");
    case core::PassiveSpeedState::Gap: return QObject::tr("missing paired input");
    case core::PassiveSpeedState::Invalid: return QObject::tr("invalid paired input");
    case core::PassiveSpeedState::Clock: return QObject::tr("source clock discontinuity");
    case core::PassiveSpeedState::Overflow: return QObject::tr("source arithmetic limit");
    case core::PassiveSpeedState::Memory: return QObject::tr("optional allocation failed");
  }
  return QObject::tr("unavailable");
}
}
LiveSpeedManagerView::LiveSpeedManagerView(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("live-speed-manager-view")); auto* layout = new QVBoxLayout(this);
  auto* title = new QLabel(tr("Live passive mapper and capacity observations"), this); title->setTextFormat(Qt::PlainText); layout->addWidget(title);
  const std::array<const char*, 6> names{"live-speed-status", "live-speed-input", "live-speed-upload-limit", "live-speed-download-limit", "live-speed-upload-capacity", "live-speed-download-capacity"};
  for (std::size_t i = 0; i < facts_.size(); ++i) {
    facts_[i] = new QLabel(tr("Unavailable"), this); facts_[i]->setObjectName(QString::fromLatin1(names[i]));
    facts_[i]->setTextFormat(Qt::PlainText); facts_[i]->setWordWrap(true);
    facts_[i]->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard); layout->addWidget(facts_[i]);
  }
  plot_ = new LiveSpeedMapWidget(this); layout->addWidget(plot_);
  auto* scope = new QLabel(tr("Read-only, scoped rate/RTT policy observations. Mapper limits are not measured link capacity or configured limits. Capacity results retain their sustained/choke/saved provenance. No probe, public route, auto-speed adjustment or file read is started here. Saved history below is separate."), this);
  scope->setTextFormat(Qt::PlainText); scope->setWordWrap(true); layout->addWidget(scope);
}
bool LiveSpeedManagerView::beginSession(quint64 session) {
  if (!session || session <= session_ || revision_ == std::numeric_limits<quint64>::max()) { return false; }
  ++revision_; session_ = session; packet_.reset(); dirty_ = true; QPointer<LiveSpeedManagerView> guard(this);
  if (plot_) { plot_->setSnapshot({}, false); }
  if (guard && guard->session_ == session) { refresh(); }
  return true;
}
bool LiveSpeedManagerView::setPresentation(LiveSpeedManagerPresentation packet) {
  if (revision_ == std::numeric_limits<quint64>::max() || !acceptLiveSpeedManager(packet_, packet, session_)) { return false; }
  ++revision_; packet_ = std::move(packet); dirty_ = true; refresh(); return true;
}
void LiveSpeedManagerView::queueRefresh() {
  if (queued_) { return; } queued_ = true; QPointer<LiveSpeedManagerView> guard(this);
  QMetaObject::invokeMethod(this, [guard] { if (guard) { guard->queued_ = false; guard->refresh(); } }, Qt::QueuedConnection);
}
void LiveSpeedManagerView::refresh() {
  if (!dirty_ || rendering_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<LiveSpeedManagerView> guard(this); const auto revision = revision_; const auto packet = packet_; const auto targets = facts_; const auto plot = plot_;
  const auto current = [&] { return guard && guard->revision_ == revision && guard->packet_ == packet && guard->isVisible() && !guard->window()->isMinimized(); };
  rendering_ = true;
  struct Finish { QPointer<LiveSpeedManagerView> owner; quint64 revision;
    ~Finish() { if (owner) { owner->rendering_ = false; if (owner->dirty_ && owner->revision_ != revision) { owner->queueRefresh(); } } }
  } finish{guard, revision};
  std::array<QString, 6> values; values.fill(tr("Unavailable")); values[0] = tr("Live SpeedManager observation unavailable");
  const auto source = packet ? packet->snapshot : std::shared_ptr<const core::PassiveSpeedSnapshot>{};
  if (packet && !packet->failure.isEmpty()) { values[0] += tr(": %1").arg(packet->failure); }
  if (source) {
    const auto state = source->lastEventWasSpeedOnly
        ? source->history.empty() ? tr("rate-only observations; waiting for real RTT pairs") : tr("rate-only tick; last RTT map retained")
        : reason(source->state);
    values[0] = tr("%1: %2; %3 actual paired points retained / %4 gaps / %5 older points dropped")
        .arg(packet->current ? tr("Passive mapper") : tr("Last observed passive mapper"), state)
        .arg(source->history.size()).arg(source->gaps).arg(source->dropped);
    if (!packet->failure.isEmpty()) { values[0] += tr("; %1").arg(packet->failure); }
    if (source->lastEventWasSpeedOnly) {
      values[1] = tr("Latest accepted tick was rate-only; %1 actual rate-only ticks. No RTT or paired point inferred.").arg(source->speedOnlyAccepted);
    } else if (source->lastAttempt) {
      const auto& input = *source->lastAttempt;
      values[1] = tr("Last attempted pair: combined upload %1 / download %2 bytes/s; RTT %3 ms; owner time %4 ms")
          .arg(number(input.uploadBytesPerSecond), number(input.downloadBytesPerSecond), number(input.rttMillis)).arg(input.observedMillis);
    } else { values[1] = tr("No paired rate/RTT observation"); }
    const bool live = packet->current && source->state == core::PassiveSpeedState::Ready && !source->lastEventWasSpeedOnly;
    values[2] = tr("Upload mapper limit — %1; %2").arg(limit(source->upload, live), persistent(source->persistentUpload, source->persistentUploadFromSaved, live));
    values[3] = tr("Download mapper limit — %1; %2").arg(limit(source->download, live), persistent(source->persistentDownload, source->persistentDownloadFromSaved, live));
    values[4] = tr("Upload capacity — %1").arg(capacity(source->uploadCapacity, packet->current));
    values[5] = tr("Download capacity — %1").arg(capacity(source->downloadCapacity, packet->current));
  }
  for (std::size_t i = 0; i < targets.size(); ++i) {
    const auto target = targets[i]; if (!current() || !target) { return; }
    target->setText(values[i]); if (!current() || !target) { return; }
    target->setToolTip(values[i]); if (!current() || !target) { return; }
    target->setAccessibleDescription(values[i]); if (!current()) { return; }
  }
  if (!current() || !plot) { return; }
  plot->setSnapshot(source, packet && packet->current && source && source->state == core::PassiveSpeedState::Ready && !source->lastEventWasSpeedOnly);
  if (!current() || !plot) { return; }
  dirty_ = false;
}
void LiveSpeedManagerView::showEvent(QShowEvent* event) {
  QPointer<LiveSpeedManagerView> guard(this); QWidget::showEvent(event); if (!guard) { return; }
  if (observedWindow_ != window()) {
    if (observedWindow_) { observedWindow_->removeEventFilter(this); }
    observedWindow_ = window(); observedWindow_->installEventFilter(this);
  }
  queueRefresh();
}
bool LiveSpeedManagerView::eventFilter(QObject* object, QEvent* event) {
  if (object == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) { queueRefresh(); }
  return QWidget::eventFilter(object, event);
}
} // namespace biglybtc::ui

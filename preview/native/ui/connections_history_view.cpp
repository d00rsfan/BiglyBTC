#include "connections_history_view.hpp"
#include "runtime_service_summary.hpp"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QShowEvent>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <utility>

namespace biglybtc::ui {
namespace {
constexpr std::size_t index(ConnectionGauge value) { return static_cast<std::size_t>(value); }
QString number(const std::optional<std::int64_t>& value) {
  return value ? QString::number(*value) : QObject::tr("Unavailable");
}
struct Series { ConnectionGauge field; QColor color; Qt::PenStyle style; QString label; };
std::array<Series, 4> series(int mode) {
  const QColor blue(30, 100, 210), orange(210, 100, 20), green(30, 145, 80), purple(135, 65, 170);
  using G = ConnectionGauge;
  if (mode == 1) {
    return {{{G::InterestedInUs,blue,Qt::SolidLine,QObject::tr("Interested in us")},
      {G::InterestedInPeers,orange,Qt::DashLine,QObject::tr("We are interested")},
      {G::UploadRequestPeers,green,Qt::DotLine,QObject::tr("Upload requests")},
      {G::DownloadRequestPeers,purple,Qt::DashDotLine,QObject::tr("Download requests")}}};
  }
  if (mode == 2) {
    return {{{G::SendWaitingSockets,blue,Qt::SolidLine,QObject::tr("Send")},
      {G::ReceiveWaitingSockets,orange,Qt::DashLine,QObject::tr("Receive")},
      {G::Count,green,Qt::SolidLine,{}},{G::Count,purple,Qt::SolidLine,{}}}};
  }
  return {{{G::Connected,blue,Qt::SolidLine,QObject::tr("Connected")},
    {G::Connecting,orange,Qt::DashLine,QObject::tr("Connecting")},
    {G::Unchoked,green,Qt::DotLine,QObject::tr("Unchoked uploads")},
    {G::Count,purple,Qt::SolidLine,{}}}};
}
class Plot final : public QWidget {
public:
  explicit Plot(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("connections-history-plot"));
    setAccessibleName(tr("Observed connection counts over elapsed time"));
    setMinimumHeight(150);
  }
  void cacheFrame(ConnectionHistoryFrame frame, int mode) noexcept { frame_ = std::move(frame); mode_ = mode; }
protected:
  void paintEvent(QPaintEvent*) override {
    if (!isVisible() || window()->isMinimized()) { return; }
    const auto frame = frame_; const auto selected = series(mode_);
    QPainter painter(this); painter.fillRect(rect(),palette().base());
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area = QRectF(rect()).adjusted(64,38,-16,-31);
    if (area.width() <= 0 || area.height() <= 0) { return; }
    painter.setPen(palette().text().color()); painter.drawRect(area);
    const QString units = mode_ == 2 ? tr("Waiting sockets") : tr("Peers");
    painter.drawText(QPointF(5,14),units);
    qreal legendX = area.left();
    for (const auto& item : selected) {
      if (item.field == ConnectionGauge::Count) { continue; }
      painter.setPen(QPen(item.color,2,item.style)); painter.drawLine(QPointF(legendX,27),QPointF(legendX+16,27));
      painter.setPen(palette().text().color()); painter.drawText(QPointF(legendX+20,31),item.label);
      legendX += fontMetrics().horizontalAdvance(item.label) + 38;
    }
    bool known = false; std::int64_t minimum = 0, maximum = 0;
    for (std::size_t i = 0; i < frame.count; ++i) {
      for (const auto& item : selected) {
        if (item.field == ConnectionGauge::Count) { continue; }
        const auto value = frame.points[i].values[index(item.field)];
        if (value) { known = true; minimum = std::min(minimum,*value); maximum = std::max(maximum,*value); }
      }
    }
    if (!known) { painter.drawText(area,Qt::AlignCenter,tr("No observed counts for this chart")); return; }
    // Integer subtraction precedes conversion on the nonnegative owner clock.
    // Signed count extrema use long double subtraction to avoid int64 overflow.
    const auto first = frame.points[0].nanoseconds, last = frame.points[frame.count-1].nanoseconds;
    const auto elapsed = last-first;
    const auto range = static_cast<long double>(maximum)-static_cast<long double>(minimum);
    const auto position = [&](const ConnectionHistoryPoint& point, std::int64_t value) {
      const auto x = elapsed ? static_cast<long double>(point.nanoseconds-first)/elapsed : 0.5L;
      const auto y = range ? (static_cast<long double>(value)-minimum)/range : 0.5L;
      return QPointF(area.left()+static_cast<qreal>(x)*area.width(), area.bottom()-static_cast<qreal>(y)*area.height());
    };
    painter.drawText(QRectF(0,area.top()-8,60,18),Qt::AlignRight,QString::number(maximum));
    painter.drawText(QRectF(0,area.bottom()-9,60,18),Qt::AlignRight,QString::number(minimum));
    painter.save(); painter.setClipRect(area.adjusted(-2,-2,2,2));
    for (const auto& item : selected) {
      if (item.field == ConnectionGauge::Count) { continue; }
      painter.setPen(QPen(item.color,2,item.style)); painter.setBrush(item.color);
      for (std::size_t i = 0; i < frame.count; ++i) {
        const auto& point = frame.points[i]; const auto value = point.values[index(item.field)];
        if (!value) { continue; }
        const auto here = position(point,*value);
        if (i && point.connectedToPrevious) {
          const auto& old = frame.points[i-1]; const auto previous = old.values[index(item.field)];
          if (previous) { painter.drawLine(position(old,*previous),here); }
        }
        painter.drawEllipse(here,2,2);
      }
    }
    painter.restore(); painter.setPen(palette().text().color());
    painter.drawText(QPointF(area.left(),area.bottom()+22),tr("Observed elapsed: %1–%2 s")
      .arg(static_cast<double>(first)/1e9,0,'f',2).arg(static_cast<double>(last)/1e9,0,'f',2));
  }
private:
  ConnectionHistoryFrame frame_;
  int mode_ = 0;
};
}

ConnectionValues projectConnectionValues(const core::RuntimeMetrics* source) noexcept {
  ConnectionValues result{};
  const auto summary = projectRuntimeServiceSummary(source);
  using F = RuntimeServiceMetric;
  constexpr std::array<F,7> fields{F::ConnectedPeers,F::ConnectingPeers,F::UnchokedUploadPeers,
    F::UploadRequestPeers,F::DownloadRequestPeers,F::UploadBandwidthQueue,F::DownloadBandwidthQueue};
  constexpr std::array<ConnectionGauge,7> targets{ConnectionGauge::Connected,ConnectionGauge::Connecting,ConnectionGauge::Unchoked,
    ConnectionGauge::UploadRequestPeers,ConnectionGauge::DownloadRequestPeers,ConnectionGauge::SendWaitingSockets,ConnectionGauge::ReceiveWaitingSockets};
  for (std::size_t i = 0; i < fields.size(); ++i) { result[index(targets[i])] = summary.get(fields[i]); }
  if (!source || source->metrics.size() > core::MaximumRuntimeMetrics) { return result; }
  std::array<bool,2> seen{};
  constexpr std::array<std::string_view,2> names{"peer.num_peers_up_interested","peer.num_peers_down_interested"};
  for (const auto& metric : source->metrics) {
    for (std::size_t i = 0; i < names.size(); ++i) {
      if (metric.name != names[i]) { continue; }
      auto& value = result[index(i ? ConnectionGauge::InterestedInPeers : ConnectionGauge::InterestedInUs)];
      if (!seen[i] && metric.type == core::RuntimeMetricType::Gauge && metric.group == core::RuntimeMetricGroup::Peer) { value = metric.value; }
      else { value.reset(); }
      seen[i] = true;
    }
  }
  return result;
}
bool ConnectionHistory::observe(const core::RuntimeMetrics& source) noexcept {
  if (!source.sequence || source.metrics.size() > core::MaximumRuntimeMetrics || source.ownerElapsedNanoseconds < 0
      || source.sequence <= sequence_ || (sequence_ && source.ownerElapsedNanoseconds < nanoseconds_)) { return false; }
  ConnectionHistoryPoint point{source.sequence,source.ownerElapsedNanoseconds,projectConnectionValues(&source),
    connected_ && source.sequence-sequence_ == 1 && source.ownerElapsedNanoseconds > nanoseconds_};
  if (count_ < points_.size()) { points_[(begin_+count_)%points_.size()] = point; ++count_; }
  else { points_[begin_] = point; begin_ = (begin_+1)%points_.size(); }
  sequence_ = source.sequence; nanoseconds_ = source.ownerElapsedNanoseconds; connected_ = true;
  return true;
}
void ConnectionHistory::clear() noexcept {
  begin_ = count_ = 0; sequence_ = 0; nanoseconds_ = 0; connected_ = false;
}
ConnectionHistoryFrame ConnectionHistory::frame() const noexcept {
  ConnectionHistoryFrame result; result.count = count_;
  for (std::size_t i = 0; i < count_; ++i) { result.points[i] = points_[(begin_+i)%points_.size()]; }
  if (count_) { result.points[0].connectedToPrevious = false; }
  return result;
}

ConnectionsHistoryView::ConnectionsHistoryView(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("connections-history"));
  auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(3);
  interested_ = new QLabel(tr("Interested peers: unavailable"),this);
  interested_->setObjectName(QStringLiteral("runtime-peer-interested"));
  interested_->setTextFormat(Qt::PlainText); interested_->setWordWrap(true); layout->addWidget(interested_);
  auto* controls = new QHBoxLayout;
  auto* label = new QLabel(tr("History"),this); label->setTextFormat(Qt::PlainText); controls->addWidget(label);
  mode_ = new QComboBox(this); mode_->setObjectName(QStringLiteral("connections-history-mode"));
  mode_->setAccessibleName(tr("Connections history chart"));
  mode_->addItems({tr("Connections"),tr("Interested / requesting peers"),tr("Waiting sockets")});
  label->setBuddy(mode_); controls->addWidget(mode_); controls->addStretch(); layout->addLayout(controls);
  plot_ = new Plot(this); layout->addWidget(plot_);
  connect(mode_,&QComboBox::currentIndexChanged,this,[this] {
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) { return; }
    ++revision_; dirty_ = true;
    // Qt continues using the combo after its direct signal returns. A tooltip
    // listener may delete its owner, so render only after that setter returns.
    queueRefresh();
  });
}
void ConnectionsHistoryView::cache(const core::RuntimeMetrics* source, bool current, bool reset) noexcept {
  if (revision_ == std::numeric_limits<std::uint64_t>::max()) { return; }
  if (reset) { history_.clear(); }
  ++revision_; dirty_ = true; current_ = current;
  latest_ = projectConnectionValues(source);
  sequence_ = source ? source->sequence : 0; nanoseconds_ = source ? source->ownerElapsedNanoseconds : 0;
  if (source) { history_.observe(*source); } else { history_.gap(); }
  if (!current) { history_.gap(); }
}
void ConnectionsHistoryView::clear() noexcept { cache(nullptr,false,true); }
void ConnectionsHistoryView::queueRefresh() {
  if (queued_) { return; }
  queued_ = true; const QPointer<ConnectionsHistoryView> self(this);
  QMetaObject::invokeMethod(this,[self] { if (self) { self->queued_ = false; self->refresh(); } },Qt::QueuedConnection);
}
void ConnectionsHistoryView::refresh() {
  if (!dirty_ || rendering_ || !isVisible() || window()->isMinimized() || !interested_ || !mode_ || !plot_) { return; }
  const QPointer<ConnectionsHistoryView> self(this); const auto revision = revision_;
  const auto interested = interested_; const auto mode = mode_; const auto plot = plot_;
  const auto current = [&] { return self && self->revision_ == revision && interested && mode && plot
    && self->interested_ == interested && self->mode_ == mode && self->plot_ == plot
    && self->isVisible() && !self->window()->isMinimized(); };
  rendering_ = true;
  struct Finish { QPointer<ConnectionsHistoryView> self; std::uint64_t revision;
    ~Finish() { if (self) { self->rendering_ = false; if (self->dirty_ && self->revision_ != revision) { self->queueRefresh(); } } }
  } finish{self,revision};
  const auto label = tr("Interested peers (in us / we are interested): %1 / %2")
    .arg(number(latest_[index(ConnectionGauge::InterestedInUs)])).arg(number(latest_[index(ConnectionGauge::InterestedInPeers)]));
  const auto frame = history_.frame(); const int selected = mode->currentIndex();
  const auto stamp = sequence_ ? tr("%1. Accepted response %2 at %3 owner nanoseconds.")
    .arg(current_ ? tr("Current observation") : tr("Last observed")).arg(sequence_).arg(nanoseconds_)
    : frame.count ? tr("No current measurement; earlier actual observations are retained.")
    : tr("No observed connection measurements yet.");
  const auto tip = stamp + QLatin1Char(' ') + tr("Up to 600 actual observations; missing values leave gaps. Gauges may decrease. These are counts, not rates or bytes. Chart: %1")
    .arg(mode->currentText());
  interested->setText(label); if (!current()) { return; }
  interested->setToolTip(QStringLiteral("peer.num_peers_up_interested: peers interested in us; peer.num_peers_down_interested: peers we are interested in\n") + label);
  if (!current()) { return; }
  interested->setAccessibleDescription(label); if (!current()) { return; }
  static_cast<Plot*>(plot.data())->cacheFrame(frame,selected);
  plot->setToolTip(tip); if (!current()) { return; }
  QString description = tip;
  for (const auto& item : series(selected)) {
    if (item.field != ConnectionGauge::Count) { description += QLatin1Char('\n') + item.label + QStringLiteral(": ") + number(latest_[index(item.field)]); }
  }
  plot->setAccessibleDescription(description); if (!current()) { return; }
  plot->update(); dirty_ = false;
}
void ConnectionsHistoryView::showEvent(QShowEvent* event) {
  const QPointer<ConnectionsHistoryView> self(this); QWidget::showEvent(event); if (!self) { return; }
  if (observedWindow_ != window()) {
    if (observedWindow_) { observedWindow_->removeEventFilter(this); }
    observedWindow_ = window(); observedWindow_->installEventFilter(this);
  }
  queueRefresh();
}
bool ConnectionsHistoryView::eventFilter(QObject* object, QEvent* event) {
  if (object == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    const QPointer<ConnectionsHistoryView> self(this); const QPointer<QObject> watched(object);
    setUpdatesEnabled(!window()->isMinimized()); if (!self) { return watched.isNull(); }
    queueRefresh();
  }
  return QWidget::eventFilter(object,event);
}
} // namespace biglybtc::ui

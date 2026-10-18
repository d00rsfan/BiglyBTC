#include "engine_activity_view.hpp"

#include "engine_activity_history.hpp"
#include "engine_statistics_presentation.hpp"

#include <QEvent>
#include <QGridLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <utility>

namespace biglybtc::ui {
namespace {
QLabel* plainLabel(QString text, const QString& id, QWidget* parent) {
  auto* label = new QLabel(std::move(text), parent); label->setObjectName(id);
  label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  return label;
}
QString unavailable() { return QObject::tr("Unavailable"); }
QString readableElapsed(std::int64_t nanoseconds) {
  if (nanoseconds < 1000) { return QObject::tr("%1 ns").arg(nanoseconds); }
  const std::int64_t divisor = nanoseconds < 1000000 ? 1000 : nanoseconds < 1000000000 ? 1000000 : 1000000000;
  const auto unit = nanoseconds < 1000000 ? QStringLiteral("\u00B5s") : nanoseconds < 1000000000 ? QStringLiteral("ms") : QStringLiteral("s");
  auto whole = nanoseconds / divisor;
  // Display rounding only. Remainder<1e9, so the scaled numerator fits int64;
  // all integer-first observation coordinates and exact tooltip times stay intact.
  auto fraction = ((nanoseconds % divisor) * 1000 + divisor / 2) / divisor;
  if (fraction == 1000) { ++whole; fraction = 0; }
  auto digits = QString::number(fraction).rightJustified(3, QLatin1Char('0'));
  while (digits.endsWith(QLatin1Char('0'))) { digits.chop(1); }
  return QString::number(whole) + (digits.isEmpty() ? QString{} : QLatin1Char('.') + digits) + QLatin1Char(' ') + unit;
}
constexpr std::array<const char*, 6> RateIds{"received", "sent", "payload-received", "payload-sent", "protocol-received", "protocol-sent"};
std::array<std::int64_t, 3> values(const core::EngineRates& rates, bool sending) {
  return sending ? std::array{rates.sent, rates.payloadSent, rates.protocolSent}
                 : std::array{rates.received, rates.payloadReceived, rates.protocolReceived};
}
} // namespace

class EngineActivityView::Chart final : public QWidget {
public:
  explicit Chart(bool sending, QWidget* parent) : QWidget(parent), sending_(sending) {
    setObjectName(sending ? QStringLiteral("engine-activity-send-chart") : QStringLiteral("engine-activity-receive-chart"));
    setAccessibleName(sending ? tr("Observed engine send combined, payload and protocol traffic rates")
                              : tr("Observed engine receive combined, payload and protocol traffic rates"));
    setMinimumHeight(150); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }
  void cacheFrame(EngineActivityPresentation frame) { frame_ = std::move(frame); }
  void repaintFrame() {
    const auto frame = frame_; const auto points = frame ? frame->points() : std::span<const EngineActivityPoint>{};
    const auto tip = points.empty() ? tr("No actual measured engine rate observations")
        : tr("Observed elapsed: %1; span %2. Exact plotted range: %3 to %4 ns; span %5 ns.")
            .arg(readableElapsed(points.front().elapsedNanoseconds))
            .arg(readableElapsed(points.back().elapsedNanoseconds - points.front().elapsedNanoseconds))
            .arg(points.front().elapsedNanoseconds).arg(points.back().elapsedNanoseconds)
            .arg(points.back().elapsedNanoseconds - points.front().elapsedNanoseconds);
    QPointer<Chart> guard(this); setToolTip(tip);
    if (guard && guard->frame_ == frame) { guard->update(); }
  }
protected:
  void paintEvent(QPaintEvent*) override {
    if (!isVisible() || window()->isMinimized()) { return; }
    const auto frame = frame_;
    const auto points = frame ? frame->points() : std::span<const EngineActivityPoint>{};
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());
    std::int64_t maximum = 0;
    for (const auto& point : points) {
      const auto rate = values(point.rates, sending_);
      maximum = std::max({maximum, rate[0], rate[1], rate[2]});
    }
    const auto maximumLabel = tr("%1 bytes/s").arg(maximum);
    const int left = painter.fontMetrics().horizontalAdvance(maximumLabel) + 16;
    const QRectF area = QRectF(rect()).adjusted(left, 28, -16, -34);
    if (area.width() <= 0 || area.height() <= 0) { return; }
    painter.setPen(palette().text().color());
    painter.drawText(QRectF(0, 0, width(), 24), Qt::AlignLeft | Qt::AlignVCenter,
        sending_ ? tr("Send traffic") : tr("Receive traffic"));
    painter.drawText(QRectF(0, area.top() - 10, left - 8, 20), Qt::AlignRight | Qt::AlignVCenter, maximumLabel);
    painter.drawText(QRectF(0, area.bottom() - 10, left - 8, 20), Qt::AlignRight | Qt::AlignVCenter, tr("0 bytes/s"));
    painter.setPen(palette().mid().color()); painter.drawRect(area);
    if (points.empty()) {
      painter.setPen(palette().text().color());
      painter.drawText(area, Qt::AlignCenter, tr("No measured engine rate samples yet"));
      return;
    }
    // History guarantees ordered nonnegative int64 times. Subtract FIRST, then
    // convert the bounded nonnegative differences; close large times stay apart.
    const auto first = points.front().elapsedNanoseconds;
    const auto span = points.back().elapsedNanoseconds - first;
    const std::array<QColor, 3> colors{QColor(30, 125, 205), QColor(38, 150, 90), QColor(190, 95, 20)};
    const std::array<Qt::PenStyle, 3> styles{Qt::SolidLine, Qt::DashLine, Qt::DotLine};
    std::array<QPointF, EngineActivityHistory::MaximumPoints> run{};
    const auto scale = static_cast<qreal>(std::max<std::int64_t>(1, maximum));
    for (std::size_t series = 0; series < colors.size(); ++series) {
      painter.setPen(QPen(colors[series], 2.0, styles[series]));
      std::size_t count = 0;
      const auto flush = [&] {
        if (count == 1) { painter.drawPoint(run[0]); }
        else if (count > 1) { painter.drawPolyline(run.data(), static_cast<int>(count)); }
        count = 0;
      };
      for (const auto& point : points) {
        if (!point.connectedToPrevious) { flush(); }
        const auto elapsed = point.elapsedNanoseconds - first;
        const auto x = span > 0 ? area.left() + area.width() * (static_cast<qreal>(elapsed) / static_cast<qreal>(span)) : area.right();
        const auto rate = values(point.rates, sending_)[series];
        run[count++] = {x, area.bottom() - area.height() * (static_cast<qreal>(rate) / scale)};
      }
      flush();
    }
    painter.setPen(palette().text().color());
    painter.drawText(QRectF(left, height() - 29, width() - left - 16, 24), Qt::AlignLeft | Qt::AlignVCenter,
        tr("Elapsed: %1").arg(readableElapsed(first)));
    painter.drawText(QRectF(left, height() - 29, width() - left - 16, 24), Qt::AlignRight | Qt::AlignVCenter,
        tr("+%1").arg(readableElapsed(span)));
  }
private:
  const bool sending_;
  EngineActivityPresentation frame_{};
};

EngineActivityView::EngineActivityView(QWidget* parent) : QWidget(parent),
    name_(plainLabel(tr("No observed transfer engine"), QStringLiteral("engine-activity-name"), this)),
    state_(plainLabel(tr("No engine observations yet"), QStringLiteral("engine-activity-status"), this)),
    samples_(plainLabel(tr("No measured engine rate samples yet"), QStringLiteral("engine-activity-samples"), this)) {
  setObjectName(QStringLiteral("statistics-engine-activity-view"));
  setAccessibleName(tr("One owned transfer engine traffic history"));
  auto* layout = new QVBoxLayout(this); layout->addWidget(name_); layout->addWidget(state_);
  auto* rates = new QGridLayout;
  rates->addWidget(plainLabel(tr("Receive"), QStringLiteral("engine-activity-receive-heading"), this), 0, 1);
  rates->addWidget(plainLabel(tr("Send"), QStringLiteral("engine-activity-send-heading"), this), 0, 2);
  const std::array<QString, 3> titles{tr("Combined:"), tr("Payload:"), tr("Protocol:")};
  for (std::size_t i = 0; i < titles.size(); ++i) {
    rates->addWidget(plainLabel(titles[i], QStringLiteral("engine-activity-rate-heading-") + QString::number(i), this), static_cast<int>(i) + 1, 0);
  }
  for (std::size_t i = 0; i < rates_.size(); ++i) {
    rates_[i] = plainLabel(unavailable(), QStringLiteral("engine-activity-rate-") + QString::fromLatin1(RateIds[i]), this);
    rates->addWidget(rates_[i], static_cast<int>(i / 2) + 1, static_cast<int>(i % 2) + 1);
  }
  rates->setColumnStretch(1, 1); rates->setColumnStretch(2, 1);
  layout->addLayout(rates); layout->addWidget(samples_);
  layout->addWidget(plainLabel(tr("Combined: blue solid; payload: green dashed; protocol: orange dotted. Gaps remain disconnected."), QStringLiteral("engine-activity-legend"), this));
  charts_[0] = new Chart(false, this); charts_[1] = new Chart(true, this);
  layout->addWidget(charts_[0], 1); layout->addWidget(charts_[1], 1);
  layout->addWidget(plainLabel(tr("One experimental transfer engine only; peer payload + protocol, not IP/tracker/DHT or application-wide traffic. Up to 600 actual rate observations; no inferred zero, smoothing or historical reconstruction. Ended runs retain the last plotted observations, not exhaustive teardown totals."),
      QStringLiteral("engine-activity-scope"), this));
}
std::size_t EngineActivityView::sampleCount() const { return frame_ ? frame_->points().size() : 0; }
void EngineActivityView::setPresentation(EngineActivityPresentation frame) {
  if (!frame || !frame->latest()) { return; }
  const auto latest = frame->latest();
  if (frame_ && latest->session < frame_->latest()->session) { return; }
  const auto prior = frame_ && latest->session == frame_->latest()->session ? frame_->latest() : EngineStatisticsPresentation{};
  if (!acceptEngineStatistics(prior, latest, latest->session)) { return; }
  if (renderRevision_ == std::numeric_limits<quint64>::max()) { return; }
  ++renderRevision_; frame_ = std::move(frame);
  for (auto* chart : charts_) { chart->cacheFrame(frame_); } // Owned data only: no hidden Qt mutation/repaint.
  dirty_ = true; refresh();
}
void EngineActivityView::clear() {
  if (renderRevision_ != std::numeric_limits<quint64>::max()) { ++renderRevision_; }
  frame_.reset();
  for (auto* chart : charts_) { chart->cacheFrame({}); }
  dirty_ = true; refresh();
}
void EngineActivityView::refresh() {
  if (!dirty_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<EngineActivityView> guard(this); const auto frame = frame_; const auto revision = renderRevision_;
  const auto current = [&] { return guard && guard->frame_ == frame && guard->renderRevision_ == revision
      && guard->isVisible() && !guard->window()->isMinimized(); };
  const auto latest = frame ? frame->latest() : EngineStatisticsPresentation{};
  struct Fact { QPointer<QLabel> label; QString text; QString tip; };
  std::array<Fact, 9> facts{};
  facts[0] = {name_, latest ? tr("Transfer: %1").arg(latest->name.isEmpty() ? tr("(unnamed transfer)") : latest->name) : tr("No observed transfer engine"),
      latest ? tr("%1; generation %2; engine operation %3").arg(latest->target.hash).arg(latest->target.generation).arg(latest->operation) : QString{}};
  QString status = tr("No engine observations yet");
  if (latest) {
    switch (latest->state) {
      case EngineStatisticsState::Preparing: status = tr("Preparing; no engine rate observations yet"); break;
      case EngineStatisticsState::Observed: status = latest->sample->rates ? tr("Actual engine interval rates") : tr("Latest engine interval rate unavailable"); break;
      case EngineStatisticsState::Ended:
        status = latest->sample && latest->sample->rates ? tr("Ended; last observed engine interval rates")
            : latest->sample ? tr("Ended; latest engine interval rate unavailable") : tr("Ended; no engine rate observations"); break;
    }
  }
  facts[1] = {state_, status, latest && latest->sample ? tr("Last actual counter observation: %1 ns since first acceptance").arg(latest->sample->elapsedNanoseconds) : QString{}};
  facts[2] = {samples_, frame && !frame->points().empty() ? tr("%1 actual rate observations (maximum 600)").arg(frame->points().size()) : tr("No measured engine rate samples yet"),
      tr("Only known measured rates are plotted; disconnected observations are not joined")};
  const auto rates = latest && latest->sample ? latest->sample->rates : std::nullopt;
  const std::array<std::int64_t, 6> values = rates ? std::array{rates->received, rates->sent, rates->payloadReceived, rates->payloadSent, rates->protocolReceived, rates->protocolSent}
                                                       : std::array<std::int64_t, 6>{};
  for (std::size_t i = 0; i < rates_.size(); ++i) {
    facts[3 + i] = {rates_[i], rates ? tr("%1 bytes/s").arg(values[i]) : unavailable(),
        rates ? tr("%1 measured bytes per second; no smoothing").arg(values[i]) : tr("Latest observation has no measured interval rate; no zero is inferred")};
  }
  dirty_ = false;
  for (const auto& fact : facts) {
    if (!current() || !fact.label) { if (guard) { guard->dirty_ = true; } return; }
    fact.label->setText(fact.text);
    if (!current() || !fact.label) { if (guard) { guard->dirty_ = true; } return; }
    fact.label->setToolTip(fact.tip);
    if (!current()) { if (guard) { guard->dirty_ = true; } return; }
  }
  for (auto* chart : charts_) {
    QPointer<Chart> plot(chart);
    if (!current() || !plot) { if (guard) { guard->dirty_ = true; } return; }
    plot->repaintFrame();
    if (!current()) { if (guard) { guard->dirty_ = true; } return; }
  }
}
void EngineActivityView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (observedWindow_ != window()) {
    if (observedWindow_) { observedWindow_->removeEventFilter(this); }
    observedWindow_ = window(); observedWindow_->installEventFilter(this);
  }
  refresh();
}
bool EngineActivityView::eventFilter(QObject* object, QEvent* event) {
  if (object == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    QPointer<EngineActivityView> guard(this);
    QMetaObject::invokeMethod(this, [guard] { if (guard) { guard->refresh(); } }, Qt::QueuedConnection);
  }
  return QWidget::eventFilter(object, event);
}
} // namespace biglybtc::ui

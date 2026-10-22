#include "native_activity_view.hpp"

#include "native_activity_history.hpp"
#include "effective_rate_limit_text.hpp"

#include <QComboBox>
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
QString modeTitle(NativeActivityRateMode mode) {
  switch(mode){
    case NativeActivityRateMode::Source10s:return QObject::tr("10-second rates");
    case NativeActivityRateMode::Interval:return QObject::tr("Measured interval rates");
    case NativeActivityRateMode::Smoothed:return QObject::tr("Configured smoothed combined rates");
  }
  return QObject::tr("Unavailable rates");
}
NativeActivitySeries values(const NativeActivityPoint& point,NativeActivityRateMode mode,bool sending) {
  return nativeActivityRates(point.rates,point.sourceRates,mode,sending);
}
} // namespace

class NativeActivityView::Chart final : public QWidget {
public:
  explicit Chart(bool sending, QWidget* parent) : QWidget(parent), sending_(sending) {
    setObjectName(sending ? QStringLiteral("native-activity-send-chart") : QStringLiteral("native-activity-receive-chart"));
    setAccessibleName(sending ? tr("Observed native session send combined, payload and protocol traffic rates")
                              : tr("Observed native session receive combined, payload and protocol traffic rates"));
    setMinimumHeight(150); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }
  void cacheFrame(NativeActivityPresentation frame) { frame_ = std::move(frame); }
  void cacheMode(NativeActivityRateMode mode) { mode_=mode; }
  void repaintFrame() {
    const auto frame = frame_; const auto mode=mode_; const auto points = frame ? frame->points() : std::span<const NativeActivityPoint>{};
    const auto tip = points.empty() ? tr("No actual measured native session rate observations")
        : tr("Observed elapsed: %1; span %2. Exact plotted range: %3 to %4 ns; span %5 ns.")
            .arg(readableElapsed(points.front().elapsedNanoseconds))
            .arg(readableElapsed(points.back().elapsedNanoseconds - points.front().elapsedNanoseconds))
            .arg(points.front().elapsedNanoseconds).arg(points.back().elapsedNanoseconds)
            .arg(points.back().elapsedNanoseconds - points.front().elapsedNanoseconds);
    QPointer<Chart> guard(this); setToolTip(modeTitle(mode)+QStringLiteral(". ")+tip);
    if (guard && guard->frame_ == frame && guard->mode_==mode) { guard->update(); }
  }
protected:
  void paintEvent(QPaintEvent*) override {
    if (!isVisible() || window()->isMinimized()) { return; }
    const auto frame = frame_;
    const auto points = frame ? frame->points() : std::span<const NativeActivityPoint>{};
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());
    std::int64_t maximum = 0;bool anyRate=false;
    for (const auto& point : points) {
      const auto rate = values(point,mode_,sending_);
      for(const auto& value:rate){if(value){maximum=std::max(maximum,*value);anyRate=true;}}
      const auto limit = sending_ ? point.rateLimits.upload : point.rateLimits.download;
      if (limit.state == core::RateLimitState::Limited) { maximum = std::max(maximum, limit.bytesPerSecond); }
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
      painter.drawText(area, Qt::AlignCenter, tr("No measured native session rate samples yet"));
      return;
    }
    if(!anyRate){
      painter.setPen(palette().text().color());painter.drawText(area,Qt::AlignCenter,tr("Selected rate observations unavailable"));
    }
    // History guarantees ordered nonnegative int64 times. Subtract FIRST, then
    // convert the bounded nonnegative differences; close large times stay apart.
    const auto first = points.front().elapsedNanoseconds;
    const auto span = points.back().elapsedNanoseconds - first;
    const std::array<QColor, 4> colors{QColor(30, 125, 205), QColor(38, 150, 90), QColor(190, 95, 20), QColor(130, 70, 180)};
    const std::array<Qt::PenStyle, 4> styles{Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine};
    std::array<QPointF, NativeActivityHistory::MaximumPoints> run{};
    const auto scale = static_cast<qreal>(std::max<std::int64_t>(1, maximum));
    for (std::size_t series = 0; series < colors.size(); ++series) {
      painter.setPen(QPen(colors[series], 2.0, styles[series]));
      std::size_t count = 0;
      std::optional<core::SourceSmoothingPolicy> priorPolicy;
      const auto flush = [&] {
        if (count == 1) { painter.drawPoint(run[0]); }
        else if (count > 1) { painter.drawPolyline(run.data(), static_cast<int>(count)); }
        count = 0;
      };
      for (const auto& point : points) {
        if (!point.connectedToPrevious) { flush();priorPolicy.reset(); }
        if(mode_==NativeActivityRateMode::Smoothed && series<3 && point.sourceRates){
          if(priorPolicy && *priorPolicy!=point.sourceRates->smoothing){flush();}
          priorPolicy=point.sourceRates->smoothing;
        }
        const auto elapsed = point.elapsedNanoseconds - first;
        const auto x = span > 0 ? area.left() + area.width() * (static_cast<qreal>(elapsed) / static_cast<qreal>(span)) : area.right();
        const auto limit = sending_ ? point.rateLimits.upload : point.rateLimits.download;
        if (series == 3 && limit.state != core::RateLimitState::Limited) { flush(); continue; }
        const auto rate = series == 3 ? std::optional<std::int64_t>{limit.bytesPerSecond} : values(point,mode_,sending_)[series];
        if(!rate){flush();continue;}
        run[count++] = {x, area.bottom() - area.height() * (static_cast<qreal>(*rate) / scale)};
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
  NativeActivityRateMode mode_=NativeActivityRateMode::Source10s;
  NativeActivityPresentation frame_{};
};

NativeActivityView::NativeActivityView(QWidget* parent) : QWidget(parent),
    state_(plainLabel(tr("No session traffic observations yet"), QStringLiteral("native-activity-status"), this)),
    samples_(plainLabel(tr("No measured native session rate samples yet"), QStringLiteral("native-activity-samples"), this)) {
  setObjectName(QStringLiteral("statistics-native-activity-view"));
  setAccessibleName(tr("Native session peer traffic history"));
  auto* layout = new QVBoxLayout(this); layout->addWidget(state_);
  auto* rates = new QGridLayout;
  rates->addWidget(plainLabel(tr("Latest interval receive"), QStringLiteral("native-activity-receive-heading"), this), 0, 1);
  rates->addWidget(plainLabel(tr("Latest interval send"), QStringLiteral("native-activity-send-heading"), this), 0, 2);
  const std::array<QString, 3> titles{tr("Combined:"), tr("Payload:"), tr("Protocol:")};
  for (std::size_t i = 0; i < titles.size(); ++i) {
    rates->addWidget(plainLabel(titles[i], QStringLiteral("native-activity-rate-heading-") + QString::number(i), this), static_cast<int>(i) + 1, 0);
  }
  for (std::size_t i = 0; i < rates_.size(); ++i) {
    rates_[i] = plainLabel(unavailable(), QStringLiteral("native-activity-rate-") + QString::fromLatin1(RateIds[i]), this);
    rates->addWidget(rates_[i], static_cast<int>(i / 2) + 1, static_cast<int>(i % 2) + 1);
  }
  rates->addWidget(plainLabel(tr("Session limit:"), QStringLiteral("native-activity-limit-heading"), this), 4, 0);
  limits_[0] = plainLabel(unavailable(), QStringLiteral("native-activity-download-limit"), this);
  limits_[1] = plainLabel(unavailable(), QStringLiteral("native-activity-upload-limit"), this);
  rates->addWidget(limits_[0], 4, 1); rates->addWidget(limits_[1], 4, 2);
  rates->setColumnStretch(1, 1); rates->setColumnStretch(2, 1);
  layout->addLayout(rates); layout->addWidget(samples_);
  modeChoice_=new QComboBox(this);modeChoice_->setObjectName(QStringLiteral("native-activity-rate-mode"));
  modeChoice_->setAccessibleName(tr("Activity graph rate calculation"));
  modeChoice_->addItems({tr("10-second rates"),tr("Measured interval rates"),tr("Configured smoothed combined rates")});
  modeChoice_->setCurrentIndex(0);layout->addWidget(modeChoice_);
  graphRates_=plainLabel(unavailable(),QStringLiteral("native-activity-selected-rates"),this);layout->addWidget(graphRates_);
  legend_=plainLabel(QString{},QStringLiteral("native-activity-legend"),this);layout->addWidget(legend_);
  connect(modeChoice_,&QComboBox::currentIndexChanged,this,[this](int index){
    if(index<0 || index>2 || revision_==std::numeric_limits<quint64>::max()){return;}
    ++revision_;mode_=static_cast<NativeActivityRateMode>(index);dirty_=true;
    for(const auto& chart:charts_){if(chart){chart->cacheMode(mode_);}}
    // Qt setters still execute after their direct signal; render only after the
    // control returns, so tooltip/accessibility callbacks may safely delete it.
    queueRefresh();
  });
  charts_[0] = new Chart(false, this); charts_[1] = new Chart(true, this);
  layout->addWidget(charts_[0], 1); layout->addWidget(charts_[1], 1);
  layout->addWidget(plainLabel(tr("One native engine epoch across transfers and idle time. Up to 600 observed intervals. The graph can use 10-second sample-timed rates, measured intervals or configured smoothed combined rates. Missing values stay unavailable. Peer traffic excludes IP, tracker and DHT. Closed sessions retain observations, not exhaustive final totals."),
      QStringLiteral("native-activity-scope"), this));
}
bool NativeActivityView::beginSession(quint64 session) {
  if (!session || session <= session_ || revision_ == std::numeric_limits<quint64>::max()) { return false; }
  ++revision_; session_ = session; frame_.reset();
  for (const auto& chart : charts_) { if (chart) { chart->cacheFrame({}); } }
  dirty_ = true; refresh(); return true;
}
bool NativeActivityView::setPresentation(NativeActivityPresentation frame) {
  if (!frame || !frame->latest() || frame->latest()->applicationRun < run_ || revision_ == std::numeric_limits<quint64>::max()
      || !acceptNativeSession(frame_ ? frame_->latest() : NativeSessionPresentation{}, frame->latest(), session_)) { return false; }
  ++revision_; run_ = frame->latest()->applicationRun; frame_ = std::move(frame);
  for (const auto& chart : charts_) { if (chart) { chart->cacheFrame(frame_); } }
  dirty_ = true; refresh(); return true;
}
std::size_t NativeActivityView::sampleCount() const { return frame_ ? frame_->points().size() : 0; }
void NativeActivityView::refresh() {
  if (!dirty_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<NativeActivityView> guard(this); const auto frame = frame_; const auto revision = revision_;
  const auto current = [&] { return guard && guard->frame_ == frame && guard->revision_ == revision
      && guard->isVisible() && !guard->window()->isMinimized(); };
  const auto latest = frame ? frame->latest() : NativeSessionPresentation{};
  const bool live = latest && (latest->state == NativeSessionState::Idle || latest->state == NativeSessionState::Active
      || latest->state == NativeSessionState::Retiring);
  const auto rates = live && latest->sample ? latest->sample->rates : std::nullopt;
  const std::array<std::int64_t, 6> values = rates ? std::array{rates->received, rates->sent, rates->payloadReceived,
      rates->payloadSent, rates->protocolReceived, rates->protocolSent} : std::array<std::int64_t, 6>{};
  QString status = tr("No session traffic observations yet");
  if (latest) {
    switch (latest->state) {
      case NativeSessionState::Starting: status = tr("Starting; no measured interval rates yet"); break;
      case NativeSessionState::Idle: status = rates ? tr("Idle; latest actual interval rates") : tr("Idle; latest interval rate unavailable"); break;
      case NativeSessionState::Active: status = rates ? tr("Active; latest actual interval rates") : tr("Active; latest interval rate unavailable"); break;
      case NativeSessionState::Retiring: status = tr("Retiring; last measured peer traffic intervals"); break;
      case NativeSessionState::Closing: status = tr("Closing; current rates unavailable, prior observations retained"); break;
      case NativeSessionState::Closed: status = tr("Closed; observations retained, not exhaustive final totals"); break;
      case NativeSessionState::Uncertified: status = tr("Measurement unavailable; prior observations retained with a gap"); break;
    }
  }
  struct Fact { QPointer<QLabel> target; QString text; QString tip; };
  std::array<Fact, 12> facts{};
  facts[0] = {state_, status, latest ? tr("Profile %1; run %2; epoch %3; sequence %4")
      .arg(latest->session).arg(latest->applicationRun).arg(latest->epoch).arg(latest->sequence) : QString{}};
  facts[1] = {samples_, frame && !frame->points().empty() ? tr("%1 actual rate observations (maximum 600)").arg(frame->points().size())
      : tr("No measured native session rate samples yet"), tr("Steady elapsed time since the first accepted counter observation; no inferred samples")};
  for (std::size_t i = 0; i < rates_.size(); ++i) {
    facts[2 + i] = {rates_[i], rates ? tr("%1 bytes/s").arg(values[i]) : unavailable(),
        rates ? tr("%1 actual measured interval bytes per second").arg(values[i]) : tr("No current measured interval rate; no zero inferred")};
  }
  const auto limitValues = latest ? latest->rateLimits : core::SessionRateLimits{};
  const auto limitScope = (latest && !live ? tr("Last observed. ") : QString{}) + effectiveRateLimitScope();
  facts[8] = {limits_[0], effectiveRateLimitText(limitValues.download), limitScope};
  facts[9] = {limits_[1], effectiveRateLimitText(limitValues.upload), limitScope};
  const auto selectedReceive=nativeActivityRates(rates,live && latest?latest->sourceRates:std::nullopt,mode_,false);
  const auto selectedSend=nativeActivityRates(rates,live && latest?latest->sourceRates:std::nullopt,mode_,true);
  const auto number=[](const auto& value){return value?QString::number(*value):unavailable();};
  const auto scope=mode_==NativeActivityRateMode::Interval?tr("Actual counter-observation intervals, not averaged windows.")
      :mode_==NativeActivityRateMode::Source10s?tr("10-second source-style buckets over actual sample delivery. Byte-arrival timing is not reconstructed.")
      :tr("Configured source-style combined-rate smoothing. Payload/protocol smoothing is unavailable.");
  const auto text=mode_==NativeActivityRateMode::Smoothed
      ?tr("Smoothed receive / send: %1 / %2 bytes/s").arg(number(selectedReceive[0]),number(selectedSend[0]))
      :tr("Graph receive / send: %1 / %2 bytes/s; payload %3 / %4; protocol %5 / %6")
          .arg(number(selectedReceive[0]),number(selectedSend[0]),number(selectedReceive[1]),number(selectedSend[1]),number(selectedReceive[2]),number(selectedSend[2]));
  const auto policy=latest && latest->sourceRates?tr(" Window %1 seconds; update interval %2 seconds.")
      .arg(latest->sourceRates->smoothing.windowSeconds).arg(latest->sourceRates->smoothing.intervalSeconds):QString{};
  facts[10]={graphRates_,text,scope+(mode_==NativeActivityRateMode::Smoothed?policy:QString{})};
  facts[11]={legend_,mode_==NativeActivityRateMode::Smoothed
      ?tr("Combined: blue solid. Session limit: purple dash-dot when finite. Missing observations break the line.")
      :tr("Combined: blue solid; payload: green dashed; protocol: orange dotted. Session limit: purple dash-dot when finite. Gaps stay disconnected."),scope};
  dirty_ = false;
  for (const auto& fact : facts) {
    if (!current() || !fact.target) { if (guard) { guard->dirty_ = true; } return; }
    fact.target->setText(fact.text);
    if (!current() || !fact.target) { if (guard) { guard->dirty_ = true; } return; }
    fact.target->setToolTip(fact.tip);
    if (!current() || !fact.target) { if (guard) { guard->dirty_ = true; } return; }
    fact.target->setAccessibleDescription(fact.text + QLatin1Char(' ') + fact.tip);
    if (!current()) { if (guard) { guard->dirty_ = true; } return; }
  }
  for (const auto& chart : charts_) {
    QPointer<Chart> plot(chart);
    if (!current() || !plot) { if (guard) { guard->dirty_ = true; } return; }
    plot->repaintFrame();
    if (!current()) { if (guard) { guard->dirty_ = true; } return; }
  }
}
void NativeActivityView::queueRefresh() {
  if(queued_){return;}queued_=true;QPointer<NativeActivityView> guard(this);
  QMetaObject::invokeMethod(this,[guard]{if(guard){guard->queued_=false;guard->refresh();}},Qt::QueuedConnection);
}
void NativeActivityView::showEvent(QShowEvent* event) {
  QPointer<NativeActivityView> guard(this);QWidget::showEvent(event);if(!guard){return;}
  if (observedWindow_ != window()) {
    if (observedWindow_) { observedWindow_->removeEventFilter(this); }
    observedWindow_ = window(); observedWindow_->installEventFilter(this);
  }
  refresh();
}
bool NativeActivityView::eventFilter(QObject* object, QEvent* event) {
  if (object == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    QPointer<NativeActivityView> guard(this);
    setUpdatesEnabled(!window()->isMinimized());
    if (!guard) { return false; }
    QMetaObject::invokeMethod(this, [guard] { if (guard) { guard->refresh(); } }, Qt::QueuedConnection);
  }
  return QWidget::eventFilter(object, event);
}
} // namespace biglybtc::ui


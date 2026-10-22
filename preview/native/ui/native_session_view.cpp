#include "native_session_view.hpp"
#include "effective_rate_limit_text.hpp"
#include "overall_transfer_summary.hpp"

#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QShowEvent>
#include <QVBoxLayout>

#include <array>
#include <limits>
#include <utility>

namespace biglybtc::ui {
namespace {
QLabel* label(QString value, const QString& id, QWidget* parent) {
  auto* result = new QLabel(std::move(value), parent);
  result->setObjectName(id); result->setTextFormat(Qt::PlainText); result->setWordWrap(true);
  result->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  return result;
}
constexpr std::array<const char*, 6> Names{
    "received", "sent", "payload-received", "payload-sent", "protocol-received", "protocol-sent"};
bool currentRates(NativeSessionState state) {
  return state == NativeSessionState::Idle || state == NativeSessionState::Active
      || state == NativeSessionState::Retiring;
}
QString stateText(const NativeSessionPresentation& packet) {
  if (!packet) { return QObject::tr("No session traffic observations yet"); }
  switch (packet->state) {
    case NativeSessionState::Starting: return QObject::tr("Starting; no counter observation yet");
    case NativeSessionState::Idle: return QObject::tr("Idle; latest observed native session traffic");
    case NativeSessionState::Active: return QObject::tr("Active; latest observed native session traffic");
    case NativeSessionState::Retiring: return QObject::tr("Retiring; latest observed native session traffic");
    case NativeSessionState::Closing: return QObject::tr("Closing; counters are last observed, current rates unavailable");
    case NativeSessionState::Closed: return QObject::tr("Closed; counters are last observed, not exhaustive final totals");
    case NativeSessionState::Uncertified: return QObject::tr("Measurement unavailable; counters are last observed");
  }
  return QObject::tr("Measurement unavailable");
}
}
NativeSessionView::NativeSessionView(QWidget* parent) : QWidget(parent),
    state_(label(stateText({}), QStringLiteral("native-session-status"), this)),
    uptime_(label(tr("Unavailable"), QStringLiteral("native-session-uptime"), this)) {
  setObjectName(QStringLiteral("statistics-native-session-view"));
  setAccessibleName(tr("Native session peer traffic observations"));
  auto* layout = new QVBoxLayout(this); layout->addWidget(state_); layout->addWidget(uptime_);
  auto* grid = new QGridLayout;
  const std::array<QString, 4> headings{tr("Received bytes"), tr("Sent bytes"), tr("Receive bytes/s"), tr("Send bytes/s")};
  for (std::size_t i = 0; i < headings.size(); ++i) {
    grid->addWidget(label(headings[i], QStringLiteral("native-session-heading-%1").arg(i), this), 0, static_cast<int>(i) + 1);
    grid->setColumnStretch(static_cast<int>(i) + 1, 1);
  }
  const std::array<QString, 3> titles{tr("Combined"), tr("Payload"), tr("Protocol")};
  for (std::size_t i = 0; i < titles.size(); ++i) {
    grid->addWidget(label(titles[i], QStringLiteral("native-session-kind-%1").arg(i), this), static_cast<int>(i) + 1, 0);
  }
  for (std::size_t i = 0; i < Names.size(); ++i) {
    counters_[i] = label(tr("Unavailable"), QStringLiteral("native-session-counter-") + QString::fromLatin1(Names[i]), this);
    rates_[i] = label(tr("Unavailable"), QStringLiteral("native-session-rate-") + QString::fromLatin1(Names[i]), this);
    grid->addWidget(counters_[i], static_cast<int>(i / 2) + 1, static_cast<int>(i % 2) + 1);
    grid->addWidget(rates_[i], static_cast<int>(i / 2) + 1, static_cast<int>(i % 2) + 3);
  }
  layout->addLayout(grid);
  ratio_ = label(tr("Session share ratio: Unavailable"), QStringLiteral("native-session-share-ratio"), this);
  ratio_->setAccessibleName(tr("Session share ratio")); layout->addWidget(ratio_);
  windows_ = label(tr("Windowed rates unavailable"), QStringLiteral("native-session-windowed-rates"), this);
  layout->addWidget(windows_);
  limits_[0] = label(tr("Session download limit: Unavailable"), QStringLiteral("native-session-download-limit"), this);
  limits_[1] = label(tr("Session upload limit: Unavailable"), QStringLiteral("native-session-upload-limit"), this);
  layout->addWidget(limits_[0]); layout->addWidget(limits_[1]);
  configuration_ = label(sessionConnectionSettingsText({}), QStringLiteral("native-session-connection-settings"), this);
  layout->addWidget(configuration_);
  layout->addWidget(label(tr("One native engine session: peer payload and protocol. No IP, tracker or DHT totals. Counters are absolute observations, not operation sums or saved lifetime totals."),
      QStringLiteral("native-session-scope"), this));
}
bool NativeSessionView::beginSession(quint64 session) {
  if (!session || session <= session_ || revision_ == std::numeric_limits<quint64>::max()) { return false; }
  ++revision_; session_ = session; packet_.reset(); dirty_ = true; refresh(); return true;
}
bool NativeSessionView::setPresentation(NativeSessionPresentation packet) {
  if (revision_ == std::numeric_limits<quint64>::max() || !packet || packet->applicationRun < run_
      || !acceptNativeSession(packet_, packet, session_)) { return false; }
  ++revision_; run_ = packet->applicationRun; packet_ = std::move(packet); dirty_ = true; refresh(); return true;
}
void NativeSessionView::refresh() {
  if (!dirty_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<NativeSessionView> guard(this); const auto packet = packet_; const auto revision = revision_;
  const auto current = [&] { return guard && guard->packet_ == packet && guard->revision_ == revision
      && guard->isVisible() && !guard->window()->isMinimized(); };
  struct Fact { QPointer<QLabel> target; QString text; QString tip; };
  std::array<Fact, 19> facts{};
  const auto identity = packet ? tr("Profile %1; run %2; epoch %3; sequence %4")
      .arg(packet->session).arg(packet->applicationRun).arg(packet->epoch).arg(packet->sequence) : QString{};
  facts[0] = {state_, stateText(packet), identity};
  facts[1] = {uptime_, packet ? tr("Observed owner uptime: %1.%2 s")
      .arg(packet->ownerElapsedNanoseconds / 1000000000)
      .arg((packet->ownerElapsedNanoseconds % 1000000000) / 1000000, 3, 10, QLatin1Char('0')) : tr("Unavailable"),
      packet ? tr("%1 steady nanoseconds since owner creation; updated only by actual packets").arg(packet->ownerElapsedNanoseconds) : QString{}};
  const auto sample = packet ? packet->sample : std::nullopt;
  const std::array<std::int64_t, 6> counters = sample ? std::array{sample->counters.received, sample->counters.sent,
      sample->counters.payloadReceived, sample->counters.payloadSent,
      sample->counters.received - sample->counters.payloadReceived, sample->counters.sent - sample->counters.payloadSent}
      : std::array<std::int64_t, 6>{};
  const auto rates = packet && currentRates(packet->state) && sample ? sample->rates : std::nullopt;
  const std::array<std::int64_t, 6> values = rates ? std::array{rates->received, rates->sent, rates->payloadReceived,
      rates->payloadSent, rates->protocolReceived, rates->protocolSent} : std::array<std::int64_t, 6>{};
  for (std::size_t i = 0; i < Names.size(); ++i) {
    facts[2 + i] = {counters_[i], sample ? QString::number(counters[i]) : tr("Unavailable"),
        sample ? tr("%1 last-observed bytes in this native epoch").arg(counters[i]) : tr("No accepted native counter sample; no zero inferred")};
    facts[8 + i] = {rates_[i], rates ? QString::number(values[i]) : tr("Unavailable"),
        rates ? tr("%1 actual interval bytes per second").arg(values[i]) : tr("No current measured interval rate; no zero inferred")};
  }
  const auto windowed = packet && currentRates(packet->state) ? packet->sourceRates : std::nullopt;
  facts[14] = {windows_, windowed ? tr("10 s receive: payload %1, protocol %2 bytes/s; send: payload %3, protocol %4 bytes/s. %7 s smoothed receive / send (%8 s interval): %5 / %6 bytes/s.")
      .arg(windowed->payloadReceived).arg(windowed->protocolReceived).arg(windowed->payloadSent).arg(windowed->protocolSent)
      .arg(windowed->smoothedReceived).arg(windowed->smoothedSent)
      .arg(windowed->smoothing.windowSeconds).arg(windowed->smoothing.intervalSeconds) : tr("Windowed rates unavailable"),
      tr("Source-style windows use actual cumulative sample arrival times; raw measured interval rates above remain separate.")};
  const auto limitValues = packet ? packet->rateLimits : core::SessionRateLimits{};
  const bool lastLimits = packet && (packet->state == NativeSessionState::Closing
      || packet->state == NativeSessionState::Closed || packet->state == NativeSessionState::Uncertified);
  facts[15] = {limits_[0], (lastLimits ? tr("Last observed session download limit: %1") : tr("Session download limit: %1"))
      .arg(effectiveRateLimitText(limitValues.download)), effectiveRateLimitScope()};
  facts[16] = {limits_[1], (lastLimits ? tr("Last observed session upload limit: %1") : tr("Session upload limit: %1"))
      .arg(effectiveRateLimitText(limitValues.upload)), effectiveRateLimitScope()};
  facts[17] = {configuration_, (lastLimits ? tr("Last observed session settings: %1") : tr("Session settings: %1"))
      .arg(sessionConnectionSettingsText(limitValues.connections)), sessionConnectionSettingsScope()};
  const auto ratio = sample ? QString::fromStdString(overallShareRatioText(
      overallShareRatio(sample->counters.sent, sample->counters.received))) : tr("Unavailable");
  auto ratioScope = sample ? tr("Absolute peer payload + protocol counters: %1 sent bytes / %2 received bytes. IP, tracker and DHT are excluded.")
      .arg(sample->counters.sent).arg(sample->counters.received)
      : tr("No accepted session counter sample; no zero ratio is inferred");
  if (sample && sample->counters.received == 0) { ratioScope += tr(" Zero received bytes uses denominator 1."); }
  if (lastLimits && sample) { ratioScope += tr(" Last observed counters; this is not an exhaustive final ratio."); }
  facts[18] = {ratio_, (lastLimits ? tr("Last observed session share ratio: %1") : tr("Session share ratio: %1"))
      .arg(ratio), ratioScope};
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
}
void NativeSessionView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (observedWindow_ != window()) {
    if (observedWindow_) { observedWindow_->removeEventFilter(this); }
    observedWindow_ = window(); observedWindow_->installEventFilter(this);
  }
  refresh();
}
bool NativeSessionView::eventFilter(QObject* object, QEvent* event) {
  if (object == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    QPointer<NativeSessionView> guard(this);
    setUpdatesEnabled(!window()->isMinimized());
    if (!guard) { return false; }
    QMetaObject::invokeMethod(this, [guard] { if (guard) { guard->refresh(); } }, Qt::QueuedConnection);
  }
  return QWidget::eventFilter(object, event);
}
} // namespace biglybtc::ui

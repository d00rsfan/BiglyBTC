#include "statistics_page.hpp"

#include "engine_activity_history.hpp"
#include "engine_activity_view.hpp"
#include "native_activity_history.hpp"
#include "native_activity_view.hpp"
#include "native_session_view.hpp"
#include "overall_view.hpp"
#include "runtime_metrics_view.hpp"
#include "history_view.hpp"
#include "history_week_view.hpp"
#include "history_month_view.hpp"
#include "history_range_view.hpp"

#include <QDateTime>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QTabWidget>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace biglybtc::ui {
namespace {
QString unavailable() { return QObject::tr("Unavailable"); }
QLabel* label(QString text, const QString& id, QWidget* parent) {
  auto* value = new QLabel(std::move(text), parent);
  value->setObjectName(id);
  value->setTextFormat(Qt::PlainText);
  value->setWordWrap(true);
  value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  return value;
}
QString seconds(qint64 value) {
  return QObject::tr("%1d %2h %3m %4s").arg(value / 86400).arg((value % 86400) / 3600)
      .arg((value % 3600) / 60).arg(value % 60);
}
QString sourceName(StatisticsReadSource source) {
  switch (source) {
    case StatisticsReadSource::Primary: return QObject::tr("primary");
    case StatisticsReadSource::Backup: return QObject::tr("backup (.bak)");
    case StatisticsReadSource::Saving: return QObject::tr("saving (.saving)");
  }
  return {};
}
bool admitted(const std::shared_ptr<const core::SavedStatistics>& value) {
  if (!value || !value->source || value->source->originalBytes().size() > core::Limits{}.inputBytes) { return false; }
  for (const auto& field : value->values) {
    if ((field.invalidType && field.value) || (!value->hasAll && (field.invalidType || field.value))) { return false; }
  }
  return true;
}
} // namespace

class StatisticsPage::ActivityPlot final : public QWidget {
public:
  struct Sample { qint64 time; qint64 down; qint64 up; };
  explicit ActivityPlot(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("statistics-activity-plot"));
    setAccessibleName(tr("Observed transfer payload download and upload rates"));
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }
  void clear() { first_ = 0; count_ = 0; }
  void append(Sample sample) {
    if (count_ == samples_.size()) { samples_[first_] = sample; first_ = (first_ + 1) % samples_.size(); }
    else { samples_[(first_ + count_) % samples_.size()] = sample; ++count_; }
  }
  std::size_t count() const { return count_; }
  const Sample& at(std::size_t index) const { return samples_[(first_ + index) % samples_.size()]; }
protected:
  void paintEvent(QPaintEvent*) override {
    if (!isVisible() || window()->isMinimized()) { return; }
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    const QRectF area = QRectF(rect()).adjusted(18, 30, -18, -30);
    if (area.width() <= 0 || area.height() <= 0) { return; }
    painter.setPen(palette().mid().color()); painter.drawRect(area);
    if (count_ == 0) {
      painter.setPen(palette().text().color()); painter.drawText(area, Qt::AlignCenter, tr("No activity samples yet"));
      return;
    }
    qint64 maximum = 0;
    for (std::size_t i = 0; i < count_; ++i) { maximum = std::max({maximum, at(i).down, at(i).up}); }
    const auto span = at(count_ - 1).time - at(0).time;
    std::array<QPointF, MaximumSamples> down{}, up{};
    for (std::size_t i = 0; i < count_; ++i) {
      const auto x = span > 0 ? area.left() + area.width() * (static_cast<qreal>(at(i).time - at(0).time) / static_cast<qreal>(span)) : area.right();
      const auto scale = static_cast<qreal>(std::max<qint64>(1, maximum));
      down[i] = {x, area.bottom() - area.height() * (static_cast<qreal>(at(i).down) / scale)};
      up[i] = {x, area.bottom() - area.height() * (static_cast<qreal>(at(i).up) / scale)};
    }
    painter.setPen(QPen(QColor(30, 125, 205), 2));
    if (count_ == 1) { painter.drawPoint(down[0]); } else { painter.drawPolyline(down.data(), static_cast<int>(count_)); }
    painter.setPen(QPen(QColor(40, 150, 90), 2));
    if (count_ == 1) { painter.drawPoint(up[0]); } else { painter.drawPolyline(up.data(), static_cast<int>(count_)); }
    painter.setPen(palette().text().color());
    painter.drawText(QRectF(18, 2, width() - 36, 24), Qt::AlignLeft | Qt::AlignVCenter,
        tr("Observed maximum: %1 bytes/s").arg(maximum));
    painter.drawText(QRectF(18, height() - 26, width() - 36, 24), Qt::AlignRight | Qt::AlignVCenter,
        tr("Actual arrival times; blue download / green upload"));
  }
private:
  std::array<Sample, MaximumSamples> samples_{};
  std::size_t first_ = 0, count_ = 0;
};

StatisticsPage::StatisticsPage(QWidget* parent, std::optional<StatisticsNavigation> initial)
    : QWidget(parent), tabs_(new QTabWidget(this)),
    activity_(new QWidget(tabs_)), plot_(new ActivityPlot(activity_)),
    transfer_(label(tr("No observed transfer"), QStringLiteral("statistics-transfer-name"), activity_)),
    downloadRate_(label(unavailable(), QStringLiteral("statistics-payload-download-rate"), activity_)),
    uploadRate_(label(unavailable(), QStringLiteral("statistics-payload-upload-rate"), activity_)),
    observations_(label(tr("No activity samples yet"), QStringLiteral("statistics-activity-observations"), activity_)),
    status_(label(tr("Not loaded"), QStringLiteral("statistics-read-status"), this)),
    provenance_(label(tr("Source: unavailable; save time unknown"), QStringLiteral("statistics-source"), this)) {
  if (initial && !validStatisticsNavigation(*initial)) { initial.reset(); }
  setObjectName(QStringLiteral("classic-statistics-page"));
  setAccessibleName(tr("Statistics")); tabs_->setObjectName(QStringLiteral("statistics-tabs"));
  activity_->setObjectName(QStringLiteral("statistics-activity"));
  auto* activityLayout = new QVBoxLayout(activity_);
  activityLayout->addWidget(label(tr("Latest observed transfer payload rates"), QStringLiteral("statistics-activity-heading"), activity_));
  activityLayout->addWidget(transfer_);
  auto* rates = new QFormLayout;
  rates->addRow(tr("Payload download:"), downloadRate_); rates->addRow(tr("Payload upload:"), uploadRate_);
  activityLayout->addLayout(rates); activityLayout->addWidget(observations_); activityLayout->addWidget(plot_, 1);
  activityLayout->addWidget(label(tr("Per-transfer payload only. Global data + protocol rates and historical samples are unavailable. Lines connect actual observations; no samples are inferred between them."),
      QStringLiteral("statistics-activity-scope"), activity_));
  auto* activitySection = new QWidget(tabs_); activitySection->setObjectName(QStringLiteral("statistics-activity-section"));
  auto* activitySectionLayout = new QVBoxLayout(activitySection);
  activityViews_ = new QTabWidget(activitySection); activityViews_->setObjectName(QStringLiteral("statistics-activity-views"));
  activityViews_->addTab(activity_, tr("Payload"));
  engineActivity_ = new EngineActivityView(activityViews_); activityViews_->addTab(engineActivity_, tr("Engine traffic"));
  nativeActivityView_ = new NativeActivityView(activityViews_); activityViews_->addTab(nativeActivityView_, tr("Session traffic"));
  activitySectionLayout->addWidget(activityViews_); tabs_->addTab(activitySection, tr("Activity"));
  auto* transfers = new QWidget(tabs_); transfers->setObjectName(QStringLiteral("statistics-transfers"));
  auto* transferLayout = new QVBoxLayout(transfers);
  auto* transferTabs = new QTabWidget(transfers); transferTabs_ = transferTabs; transferTabs->setObjectName(QStringLiteral("statistics-transfer-tabs"));
  auto* current = new QScrollArea(transferTabs); current->setWidgetResizable(true); current->setObjectName(QStringLiteral("statistics-transfers-current"));
  auto* contents = new QWidget(current); auto* currentLayout = new QVBoxLayout(contents);
  auto* native = new QGroupBox(tr("Current session"), contents);
  native->setObjectName(QStringLiteral("statistics-native-session"));
  auto* nativeLayout = new QVBoxLayout(native);
  nativeSessionView_ = new NativeSessionView(native); nativeLayout->addWidget(nativeSessionView_);
  currentLayout->addWidget(native);
  auto* engine = new QGroupBox(tr("Transfer operation"), contents);
  engine->setObjectName(QStringLiteral("statistics-engine-session"));
  auto* engineLayout = new QVBoxLayout(engine);
  engineStatus_ = label(tr("Unavailable: no owned transfer engine has been observed"), QStringLiteral("statistics-engine-status"), engine);
  engineTarget_ = label({}, QStringLiteral("statistics-engine-target"), engine);
  engineTarget_->hide();
  engineLayout->addWidget(engineStatus_); engineLayout->addWidget(engineTarget_);
  engineDetails_ = new QWidget(engine); engineDetails_->setObjectName(QStringLiteral("statistics-engine-counters"));
  auto* engineForm = new QFormLayout(engineDetails_);
  const std::array<const char*, 12> engineIds{"received", "sent", "payload-received", "payload-sent",
      "protocol-received", "protocol-sent", "receive-rate", "send-rate",
      "payload-receive-rate", "payload-send-rate", "protocol-receive-rate", "protocol-send-rate"};
  const std::array<QString, 12> engineTitles{tr("Received (payload + protocol):"), tr("Sent (payload + protocol):"),
      tr("Payload received:"), tr("Payload sent:"), tr("Protocol received:"), tr("Protocol sent:"),
      tr("Combined receive rate:"), tr("Combined send rate:"), tr("Payload receive rate:"), tr("Payload send rate:"),
      tr("Protocol receive rate:"), tr("Protocol send rate:")};
  for (std::size_t i = 0; i < engineValues_.size(); ++i) {
    engineValues_[i] = label(unavailable(), QStringLiteral("statistics-engine-") + QString::fromLatin1(engineIds[i]), engineDetails_);
    engineForm->addRow(engineTitles[i], engineValues_[i]);
  }
  engineDetails_->hide(); engineLayout->addWidget(engineDetails_);
  engineLayout->addWidget(label(tr("One experimental transfer engine only. Peer payload + protocol traffic; IP, tracker and DHT totals are not included. These measurements are separate from application lifetime, global totals, saved totals and history. Ended counters are the last observation, not an exhaustive final accounting."),
      QStringLiteral("statistics-engine-scope"), engine));
  auto* live = new QGroupBox(tr("Overall transfers"), contents);
  auto* liveLayout = new QVBoxLayout(live);
  overallView_ = new OverallView(live); liveLayout->addWidget(overallView_);
  connect(overallView_, &OverallView::markRequested, this, &StatisticsPage::markRequested);
  currentLayout->addWidget(live);
  currentLayout->addWidget(engine);
  auto* totals = new QGroupBox(tr("Last-saved overall totals"), contents); auto* totalsForm = new QFormLayout(totals);
  const std::array<const char*, 13> ids{"downloaded", "uploaded", "uptime", "mark-time", "mark-downloaded", "mark-uploaded", "mark-uptime",
      "dht-downloaded", "dht-uploaded", "protocol-uploaded", "data-uploaded", "protocol-downloaded", "data-downloaded"};
  const std::array<QString, 13> titles{tr("Downloaded (data + protocol):"), tr("Uploaded (data + protocol):"), tr("Uptime:"), tr("Mark date:"),
      tr("Downloaded mark baseline:"), tr("Uploaded mark baseline:"), tr("Uptime mark baseline:"), tr("DHT downloaded (separate):"), tr("DHT uploaded (separate):"),
      tr("Protocol uploaded:"), tr("Data uploaded:"), tr("Protocol downloaded:"), tr("Data downloaded:")};
  for (std::size_t i = 0; i < raw_.size(); ++i) {
    raw_[i] = label(unavailable(), QStringLiteral("statistics-saved-") + QString::fromLatin1(ids[i]), totals);
    totalsForm->addRow(titles[i], raw_[i]);
  }
  currentLayout->addWidget(totals);
  auto* since = new QGroupBox(tr("Since saved mark"), contents); auto* sinceForm = new QFormLayout(since);
  const std::array<const char*, 3> sinceIds{"downloaded", "uploaded", "uptime"};
  const std::array<QString, 3> sinceTitles{tr("Downloaded:"), tr("Uploaded:"), tr("Uptime:")};
  for (std::size_t i = 0; i < since_.size(); ++i) {
    since_[i] = label(unavailable(), QStringLiteral("statistics-since-mark-") + QString::fromLatin1(sinceIds[i]), since);
    sinceForm->addRow(sinceTitles[i], since_[i]);
  }
  currentLayout->addWidget(since);
  currentLayout->addWidget(label(tr("These are last-read saved totals; Refresh saved totals reloads them. Live overall totals and mark controls are shown above. Saved combined counters already include data and protocol. Split counters and DHT are shown separately, never added again. The file contains no save timestamp."),
      QStringLiteral("statistics-saved-scope"), contents));
  currentLayout->addStretch(); current->setWidget(contents); transferTabs->addTab(current, tr("Current"));
  auto* history = new QWidget(transferTabs); history->setObjectName(QStringLiteral("statistics-transfers-history"));
  auto* historyLayout = new QVBoxLayout(history);
  historyLayout->setContentsMargins(0, 0, 0, 0);
  historyTabs_ = new QTabWidget(history); historyTabs_->setObjectName(QStringLiteral("statistics-history-tabs"));
  history_ = new HistoryView(historyTabs_, initial ? std::optional{initial->dailyDate} : std::nullopt,
      initial ? initial->dailyTraffic : SavedHistoryTraffic::Payload);
  historyWeek_ = new HistoryWeekView(historyTabs_, initial ? std::optional{initial->weekEnding} : std::nullopt,
      initial && initial->hourly, initial ? initial->weekTraffic : SavedHistoryTraffic::Payload);
  historyMonth_ = new HistoryMonthView(historyTabs_, initial ? std::optional{initial->monthEnding} : std::nullopt,
      initial ? initial->monthTraffic : SavedHistoryTraffic::Payload);
  historyRange_ = new HistoryRangeView(historyTabs_, initial ? initial->rangePeriod : core::HistoryPeriod::Week,
      initial ? initial->rangeOffset : 0, initial ? initial->rangeTraffic : SavedHistoryTraffic::Payload);
  historyLayout->addWidget(historyTabs_);
  connect(history_, &HistoryView::readRequested, this, [this](core::HistoryDate date) {
    // HistoryView emits only when visible; do not drop its first show signal
    // while QTabWidget is finishing synchronous activation.
    emit historyRequested(date);
  });
  connect(historyWeek_, &HistoryWeekView::readRequested, this, [this](core::HistoryDate date) { emit historyWeekRequested(date); });
  connect(historyMonth_, &HistoryMonthView::readRequested, this, [this](core::HistoryDate date) { emit historyMonthRequested(date); });
  historyTabs_->addTab(history_, tr("Daily")); historyTabs_->addTab(historyWeek_, tr("7 days"));
  historyTabs_->addTab(historyMonth_, tr("30 days"));
  connect(historyRange_, &HistoryRangeView::readRequested, this, &StatisticsPage::historyRangeRequested);
  historyTabs_->addTab(historyRange_, tr("Rolling history"));
  transferTabs->addTab(history, tr("History")); transferLayout->addWidget(transferTabs); tabs_->addTab(transfers, tr("Transfers"));
  runtimeViews_[0] = new RuntimeMetricsView(core::RuntimeMetricGroup::Peer, transferTabs);
  runtimeViews_[0]->setObjectName(QStringLiteral("statistics-connections-metrics"));
  transferTabs->addTab(runtimeViews_[0], tr("Connections"));
  runtimeViews_[1] = new RuntimeMetricsView(core::RuntimeMetricGroup::Disk, tabs_);
  runtimeViews_[1]->setObjectName(QStringLiteral("statistics-cache-metrics"));
  tabs_->addTab(runtimeViews_[1], tr("Cache"));
  runtimeViews_[2] = new RuntimeMetricsView(core::RuntimeMetricGroup::Tracker, tabs_);
  runtimeViews_[2]->setObjectName(QStringLiteral("statistics-tracker-metrics"));
  tabs_->addTab(runtimeViews_[2], tr("Tracker"));
  runtimeViews_[3] = new RuntimeMetricsView({}, tabs_);
  runtimeViews_[3]->setObjectName(QStringLiteral("statistics-all-metrics"));
  tabs_->addTab(runtimeViews_[3], tr("Counters"));
  auto* refresh = new QPushButton(tr("Refresh saved totals"), this); refresh->setObjectName(QStringLiteral("statistics-refresh"));
  connect(refresh, &QPushButton::clicked, this, &StatisticsPage::refreshRequested);
  auto* layout = new QVBoxLayout(this); layout->addWidget(status_); layout->addWidget(provenance_); layout->addWidget(tabs_, 1); layout->addWidget(refresh);
  if (initial) {
    activityViews_->setCurrentIndex(initial->activity);
    historyTabs_->setCurrentIndex(initial->history);
    transferTabs_->setCurrentIndex(initial->transfers);
    tabs_->setCurrentIndex(initial->section);
  } else { activityViews_->setCurrentIndex(2); }
  connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
    QPointer<StatisticsPage> guard(this); refreshActivity(); if (guard) { guard->refreshEngine(); }
    if (guard) { guard->queueHistoryVisibility(); }
  });
  connect(transferTabs_, &QTabWidget::currentChanged, this, [this](int) {
    QPointer<StatisticsPage> guard(this); refreshEngine(); if (guard) { guard->queueHistoryVisibility(); }
  });
  connect(historyTabs_, &QTabWidget::currentChanged, this, [this](int) { queueHistoryVisibility(); });
  connect(activityViews_, &QTabWidget::currentChanged, this, [this](int) { refreshActivity(); });
}
std::optional<StatisticsNavigation> StatisticsPage::navigation() const {
  const StatisticsNavigation result{tabs_->currentIndex(), activityViews_->currentIndex(),
      transferTabs_->currentIndex(), historyTabs_->currentIndex(), history_->selectedDate(),
      historyWeek_->selectedDate(), historyWeek_->hourlyResolution(), history_->traffic(), historyWeek_->traffic(),
      historyMonth_->selectedDate(), historyMonth_->traffic(), historyRange_->period(), historyRange_->offset(), historyRange_->traffic()};
  if (!validStatisticsNavigation(result)) { return {}; }
  return result;
}
void StatisticsPage::beginHistoryRequest(quint64 session, quint64 request, core::HistoryDate date) {
  history_->beginRequest(session, request, date);
}
void StatisticsPage::setHistory(HistoryPresentation packet) { history_->setPresentation(std::move(packet)); }
void StatisticsPage::setHistoryFailed(quint64 session, quint64 request, core::HistoryDate date, const QString& failure) {
  history_->setFailed(session, request, date, failure);
}
void StatisticsPage::resetHistorySession(quint64 session) {
  QPointer<StatisticsPage> guard(this); history_->resetSession(session);
  if (guard) { guard->historyWeek_->resetSession(session); }
  if (guard) { guard->historyMonth_->resetSession(session); }
  if (guard) { guard->historyRange_->resetSession(session); }
}
core::HistoryDate StatisticsPage::selectedHistoryDate() const { return history_->selectedDate(); }
HistoryPresentation StatisticsPage::historyPresentation() const { return history_->presentation(); }
bool StatisticsPage::historySelected() const {
  return tabs_->currentIndex() == 1 && transferTabs_->currentIndex() == 1 && historyTabs_->currentIndex() == 0;
}
void StatisticsPage::beginHistoryWeekRequest(quint64 session, quint64 request, core::HistoryDate date) {
  historyWeek_->beginRequest(session, request, date);
}
void StatisticsPage::setHistoryWeek(HistoryWeekPresentation packet) { historyWeek_->setPresentation(std::move(packet)); }
void StatisticsPage::setHistoryWeekFailed(quint64 session, quint64 request, core::HistoryDate date, const QString& failure) {
  historyWeek_->setFailed(session, request, date, failure);
}
core::HistoryDate StatisticsPage::selectedHistoryWeekDate() const { return historyWeek_->selectedDate(); }
HistoryWeekPresentation StatisticsPage::historyWeekPresentation() const { return historyWeek_->presentation(); }
bool StatisticsPage::historyWeekSelected() const {
  return tabs_->currentIndex() == 1 && transferTabs_->currentIndex() == 1 && historyTabs_->currentIndex() == 1;
}
void StatisticsPage::beginHistoryMonthRequest(quint64 session, quint64 request, core::HistoryDate date) {
  historyMonth_->beginRequest(session, request, date);
}
void StatisticsPage::setHistoryMonth(HistoryMonthPresentation packet) { historyMonth_->setPresentation(std::move(packet)); }
void StatisticsPage::setHistoryMonthFailed(quint64 session, quint64 request, core::HistoryDate date, const QString& failure) {
  historyMonth_->setFailed(session, request, date, failure);
}
core::HistoryDate StatisticsPage::selectedHistoryMonthDate() const { return historyMonth_->selectedDate(); }
HistoryMonthPresentation StatisticsPage::historyMonthPresentation() const { return historyMonth_->presentation(); }
bool StatisticsPage::historyMonthSelected() const {
  return tabs_->currentIndex() == 1 && transferTabs_->currentIndex() == 1 && historyTabs_->currentIndex() == 2;
}
void StatisticsPage::beginHistoryRangeRequest(quint64 session, quint64 request, core::HistoryRangeRequest query) {
  historyRange_->beginRequest(session, request, query);
}
void StatisticsPage::setHistoryRange(HistoryRangePresentation packet) { historyRange_->setPresentation(std::move(packet)); }
void StatisticsPage::setHistoryRangeFailed(quint64 session, quint64 request, core::HistoryRangeRequest query, const QString& failure) {
  historyRange_->setFailed(session, request, query, failure);
}
HistoryRangePresentation StatisticsPage::historyRangePresentation() const { return historyRange_->presentation(); }
bool StatisticsPage::historyRangeSelected() const {
  return tabs_->currentIndex() == 1 && transferTabs_->currentIndex() == 1 && historyTabs_->currentIndex() == 3;
}
void StatisticsPage::refreshHistoryRange() { historyRange_->requestRefresh(); }
QTabWidget& StatisticsPage::tabs() { return *tabs_; }
std::size_t StatisticsPage::sampleCount() const { return plot_->count(); }
bool StatisticsPage::accepts(quint64 session, quint64 request) const {
  return loading_ && session != 0 && request != 0 && session == session_ && request == request_;
}
StatisticsPage::Facts StatisticsPage::emptyFacts(QString status, QString provenance) const {
  Facts result;
  for (std::size_t i = 0; i < raw_.size(); ++i) { result[i] = {raw_[i], unavailable(), tr("No saved value is available")}; }
  for (std::size_t i = 0; i < since_.size(); ++i) { result[13 + i] = {since_[i], unavailable(), tr("Requires two nonnegative saved operands")}; }
  result[16] = {status_, std::move(status), {}}; result[17] = {provenance_, std::move(provenance), {}};
  return result;
}
bool StatisticsPage::writeFacts(const Facts& facts) {
  QPointer<StatisticsPage> guard(this);
  const auto session = session_, request = request_; const auto state = state_; const bool loading = loading_;
  const auto current = [&] { return guard && guard->session_ == session && guard->request_ == request && guard->state_ == state && guard->loading_ == loading; };
  for (const auto& fact : facts) {
    if (!current() || !fact.label) { return false; }
    fact.label->setText(fact.text);
    if (!current() || !fact.label) { return false; }
    fact.label->setToolTip(fact.tip);
    if (!current()) { return false; }
  }
  return true;
}
void StatisticsPage::beginRequest(quint64 session, quint64 request) {
  if (session == 0 || request == 0 || session < session_ || (session == session_ && request <= request_)) { return; }
  const bool newEngineSession = session != session_;
  if (session_ != 0 && newEngineSession) { clearActivity(); engine_.reset(); engineDirty_ = true; }
  session_ = session; request_ = request; state_.reset(); loading_ = true;
  QPointer<StatisticsPage> lifetime(this);
  if (newEngineSession) {
    engineActivity_->clear();
    if (!lifetime || lifetime->session_ != session || lifetime->request_ != request) { return; }
    beginNativeSession(session);
    if (!lifetime || lifetime->session_ != session || lifetime->request_ != request) { return; }
  }
  const auto facts = emptyFacts(tr("Loading last-saved statistics…"), tr("Source: unavailable; save time unknown"));
  if (!writeFacts(facts)) { return; }
  QPointer<StatisticsPage> guard(this); refreshActivity(); if (guard) { guard->refreshEngine(); }
}
void StatisticsPage::setFailed(quint64 session, quint64 request, const QString& message) {
  if (!accepts(session, request)) { return; }
  loading_ = false; state_ = StatisticsReadState::Failed;
  (void)writeFacts(emptyFacts(message.isEmpty() ? tr("Saved statistics could not be read") : message.left(8192), tr("Source: unavailable; save time unknown")));
}
void StatisticsPage::setPresentation(StatisticsPresentation packet) {
  if (!packet || !accepts(packet->session, packet->request)) { return; }
  if (packet->state == StatisticsReadState::Failed) { setFailed(packet->session, packet->request, packet->failure); return; }
  if (packet->state == StatisticsReadState::Missing) {
    if (packet->source || packet->snapshot || !packet->failure.isEmpty()) { setFailed(packet->session, packet->request, tr("Saved statistics result is incomplete")); return; }
    loading_ = false; state_ = StatisticsReadState::Missing;
    (void)writeFacts(emptyFacts(tr("Saved statistics file not found"), tr("Source: all three candidates observed absent; save time unknown")));
    return;
  }
  if (packet->state != StatisticsReadState::Ready || !packet->source || sourceName(*packet->source).isEmpty()
      || !packet->failure.isEmpty() || !admitted(packet->snapshot)) {
    setFailed(packet->session, packet->request, tr("Saved statistics result is incomplete")); return;
  }
  const auto& saved = *packet->snapshot;
  auto facts = emptyFacts(saved.hasAll ? tr("Last-saved statistics loaded") : tr("Saved document has no overall statistics section"),
      tr("Source: BiglyBT.statistics — %1; save time unknown").arg(sourceName(*packet->source)));
  for (std::size_t i = 0; i < saved.values.size(); ++i) {
    const auto& field = saved.values[i];
    const bool uptime = i == 2 || i == 6;
    const bool markTime = i == 3;
    const auto unit = uptime ? tr("seconds") : markTime ? tr("epoch milliseconds") : tr("bytes");
    if (field.invalidType) { facts[i].tip = tr("Saved field has an invalid type; value unavailable"); continue; }
    if (!field.value) { facts[i].tip = tr("Saved field is absent; no zero is inferred"); continue; }
    facts[i].tip = QString::number(*field.value) + QLatin1Char(' ') + unit;
    if (*field.value < 0) { facts[i].tip += tr("; negative saved value is unavailable"); continue; }
    if (markTime) {
      if (*field.value == 0) { facts[i].text = tr("No mark date recorded"); }
      else {
        const auto date = QDateTime::fromMSecsSinceEpoch(*field.value, QTimeZone::UTC).toString(Qt::ISODateWithMs);
        facts[i].text = date.isEmpty() ? tr("%1 ms since epoch").arg(*field.value) : date;
      }
      facts[i].tip += tr("; mark label only, not a save timestamp");
    } else { facts[i].text = uptime ? seconds(*field.value) : tr("%1 bytes").arg(*field.value); }
  }
  const std::array<core::SavedStatistic, 3> totals{core::SavedStatistic::Downloaded, core::SavedStatistic::Uploaded, core::SavedStatistic::Uptime};
  const std::array<core::SavedStatistic, 3> baselines{core::SavedStatistic::MarkDownloaded, core::SavedStatistic::MarkUploaded, core::SavedStatistic::MarkUptime};
  for (std::size_t i = 0; i < totals.size(); ++i) {
    const auto value = core::deriveSavedSinceMark(saved.get(totals[i]).value, saved.get(baselines[i]).value);
    if (!value) { continue; }
    facts[13 + i].text = i == 2 ? seconds(value->value) : tr("%1 bytes").arg(value->value);
    facts[13 + i].tip = QString::number(value->value) + (i == 2 ? tr(" seconds") : tr(" bytes"));
    if (value->clamped) { facts[13 + i].tip += tr("; saved baseline exceeded total and was clamped for display only"); }
  }
  loading_ = false; state_ = StatisticsReadState::Ready;
  (void)writeFacts(facts);
}
void StatisticsPage::setRuntimeMetrics(RuntimeMetricsPresentation packet) {
  QPointer<StatisticsPage> guard(this); const auto views = runtimeViews_;
  for (const auto& view : views) {
    if (!guard) { return; }
    if (view) { view->setPresentation(packet); }
  }
}
void StatisticsPage::setOverall(OverallPresentation packet) { if (overallView_) { overallView_->setPresentation(std::move(packet)); } }
void StatisticsPage::setAccountingBusy(bool busy) { if (overallView_) { overallView_->setCommandBusy(busy); } }
void StatisticsPage::beginNativeSession(quint64 session) {
  if (!session || session <= nativeSessionBinding_) { return; }
  nativeSessionBinding_ = session; nativeActivityFrame_.reset();
  QPointer<StatisticsPage> guard(this);
  if (nativeSessionView_) { nativeSessionView_->beginSession(session); }
  if (!guard || guard->nativeSessionBinding_ != session) { return; }
  if (guard->nativeActivityView_) { guard->nativeActivityView_->beginSession(session); }
  if (!guard || guard->nativeSessionBinding_ != session) { return; }
  // A synchronous child callback may already have delivered an actual packet
  // for this binding. Replay the latest cache after BOTH children are bound.
  const auto views = guard->runtimeViews_;
  for (const auto& view : views) {
    if (!guard || guard->nativeSessionBinding_ != session) { return; }
    if (view) { view->beginSession(session); }
  }
  if (!guard || guard->nativeSessionBinding_ != session) { return; }
  if (guard->overallView_) { guard->overallView_->beginSession(session); }
  if (!guard || guard->nativeSessionBinding_ != session) { return; }
  guard->refreshNative();
}
void StatisticsPage::setNativeActivity(NativeActivityPresentation frame) {
  if (!frame || !frame->latest() || frame->latest()->applicationRun < nativeApplicationRun_
      || !acceptNativeSession(nativeActivityFrame_ ? nativeActivityFrame_->latest() : NativeSessionPresentation{},
          frame->latest(), nativeSessionBinding_)) { return; }
  nativeApplicationRun_ = frame->latest()->applicationRun;
  nativeActivityFrame_ = std::move(frame); refreshNative();
}
NativeSessionPresentation StatisticsPage::nativeSessionSnapshot() const {
  return nativeActivityFrame_ ? nativeActivityFrame_->latest() : NativeSessionPresentation{};
}
void StatisticsPage::refreshNative() {
  const auto frame = nativeActivityFrame_;
  if (!frame || !frame->latest()) { return; }
  QPointer<StatisticsPage> guard(this); const auto session = nativeSessionBinding_;
  const auto current = [&] { return guard && guard->nativeSessionBinding_ == session && guard->nativeActivityFrame_ == frame; };
  if (nativeSessionView_) { nativeSessionView_->setPresentation(frame->latest()); }
  if (!current()) { return; }
  if (guard->nativeActivityView_) { guard->nativeActivityView_->setPresentation(frame); }
  // No work after the final callback-capable child setter.
}
void StatisticsPage::setEngineStatistics(EngineStatisticsPresentation packet) {
  if (!acceptEngineStatistics(engine_, packet, session_)) { return; }
  // A terminal notification need not contain a fresh alert. Preserve the actual
  // last observation in the owned immutable cache for a later page reopen.
  if (packet->state == EngineStatisticsState::Ended && !packet->sample && engine_ && engine_->sample
      && packet->session == engine_->session && packet->operation == engine_->operation && packet->target == engine_->target) {
    auto retained = std::make_shared<EngineStatisticsPacket>(*packet);
    retained->sample = engine_->sample; packet = std::move(retained);
  }
  engine_ = std::move(packet); engineDirty_ = true; refreshEngine();
}
void StatisticsPage::setEngineActivity(EngineActivityPresentation frame) {
  if (!frame || !frame->latest()) { return; }
  const auto latest = frame->latest();
  QPointer<StatisticsPage> guard(this); setEngineStatistics(latest);
  // Admission and all synchronous render reentry complete before the graph is
  // allowed to consume the exact same immutable binding.
  if (!guard || guard->engine_ != latest) { return; }
  guard->engineActivity_->setPresentation(std::move(frame));
}
EngineActivityPresentation StatisticsPage::engineActivitySnapshot() const { return engineActivity_->presentation(); }
void StatisticsPage::refreshEngine() {
  if (!engineDirty_ || !isVisible() || tabs_->currentIndex() != 1 || transferTabs_->currentIndex() != 0
      || window()->isMinimized()) { return; }
  QPointer<StatisticsPage> guard(this);
  const auto packet = engine_; const auto session = session_;
  const auto current = [&] {
    return guard && guard->engine_ == packet && guard->session_ == session && guard->isVisible()
        && guard->tabs_->currentIndex() == 1 && guard->transferTabs_->currentIndex() == 0
        && !guard->window()->isMinimized();
  };
  const auto sample = packet ? packet->sample : std::nullopt;
  QString status = tr("Unavailable: no owned transfer engine has been observed");
  if (packet) {
    switch (packet->state) {
      case EngineStatisticsState::Preparing: status = tr("Preparing; no engine counters observed yet"); break;
      case EngineStatisticsState::Observed: status = tr("Measured transfer engine session"); break;
      case EngineStatisticsState::Ended:
        status = sample ? (packet->successful ? tr("Ended successfully; last observed counters") : tr("Ended; last observed counters"))
                        : tr("Ended; no engine counters were observed"); break;
    }
  }
  const auto target = packet ? tr("Transfer: %1").arg(packet->name.isEmpty() ? tr("(unnamed transfer)") : packet->name) : QString{};
  const auto identity = packet ? tr("%1; generation %2; engine operation %3").arg(packet->target.hash).arg(packet->target.generation).arg(packet->operation) : QString{};
  std::array<Fact, 14> facts{};
  facts[0] = {engineStatus_, std::move(status), sample ? tr("Last actual observation: %1 nanoseconds since the first accepted observation").arg(sample->elapsedNanoseconds) : QString{}};
  facts[1] = {engineTarget_, target, identity};
  for (std::size_t i = 0; i < engineValues_.size(); ++i) {
    facts[2 + i] = {engineValues_[i], unavailable(), tr("No measured value is available")};
  }
  if (sample) {
    const auto& counters = sample->counters;
    const std::array<std::int64_t, 6> values{counters.received, counters.sent, counters.payloadReceived, counters.payloadSent,
        counters.received - counters.payloadReceived, counters.sent - counters.payloadSent};
    for (std::size_t i = 0; i < values.size(); ++i) {
      facts[2 + i].text = tr("%1 bytes").arg(values[i]);
      facts[2 + i].tip = QString::number(values[i]) + tr(" measured bytes in this engine operation");
    }
    if (sample->rates) {
      const auto& rates = *sample->rates;
      const std::array<std::int64_t, 6> values{rates.received, rates.sent, rates.payloadReceived,
          rates.payloadSent, rates.protocolReceived, rates.protocolSent};
      for (std::size_t i = 0; i < values.size(); ++i) {
        facts[8 + i].text = tr("%1 bytes/s").arg(values[i]);
        facts[8 + i].tip = tr("%1 measured bytes per second over the last accepted engine interval").arg(values[i]);
      }
    } else {
      for (std::size_t i = 8; i < facts.size(); ++i) {
        facts[i].tip = tr("Requires an actual engine interval; no rate is inferred");
      }
    }
  }
  engineDirty_ = false;
  if (!sample) { engineDetails_->hide(); if (!current()) { if (guard) { guard->engineDirty_ = true; } return; } }
  for (const auto& fact : facts) {
    if (!current() || !fact.label) { if (guard) { guard->engineDirty_ = true; } return; }
    fact.label->setText(fact.text);
    if (!current() || !fact.label) { if (guard) { guard->engineDirty_ = true; } return; }
    fact.label->setToolTip(fact.tip);
    if (!current()) { if (guard) { guard->engineDirty_ = true; } return; }
  }
  engineTarget_->setVisible(static_cast<bool>(packet));
  if (!current()) { if (guard) { guard->engineDirty_ = true; } return; }
  engineDetails_->setVisible(static_cast<bool>(sample));
  if (!current() && guard) { guard->engineDirty_ = true; }
}
void StatisticsPage::clearActivity() {
  plot_->clear(); target_.reset(); displayName_.clear(); clock_.invalidate(); activityDirty_ = true;
}
void StatisticsPage::recordPayload(const Telemetry& value, const QString& displayName) {
  if (value.target.generation == 0 || value.target.hash.isEmpty() || value.target.hash.size() > 256
      || value.completed < 0 || value.down < 0 || value.up < 0
      || activityRevision_ == std::numeric_limits<quint64>::max()) { return; }
  ++activityRevision_;
  if (!target_ || *target_ != value.target) { clearActivity(); target_ = value.target; clock_.start(); }
  const auto elapsed = clock_.nsecsElapsed();
  if (elapsed < 0) { return; }
  plot_->append({elapsed, value.down, value.up}); displayName_ = displayName.left(512); activityDirty_ = true;
  refreshActivity();
}
void StatisticsPage::refreshActivity() {
  if (!activityDirty_ || !isVisible() || !activity_->isVisible() || activityViews_->currentIndex() != 0 || window()->isMinimized()) { return; }
  QPointer<StatisticsPage> guard(this); QPointer<ActivityPlot> plot = plot_;
  const std::optional<Target> target = target_; const auto count = plot->count();
  const auto revision = activityRevision_, session = session_;
  const auto current = [&] {
    return guard && plot && guard->activityRevision_ == revision && guard->session_ == session
        && guard->target_ == target && guard->isVisible() && guard->activity_->isVisible()
        && guard->activityViews_->currentIndex() == 0 && !guard->window()->isMinimized();
  };
  const auto latest = count ? std::optional<ActivityPlot::Sample>{plot->at(count - 1)} : std::nullopt;
  const std::array<Fact, 4> facts{{{transfer_, target ? tr("Transfer: %1").arg(displayName_.isEmpty() ? tr("(unnamed transfer)") : displayName_) : tr("No observed transfer"),
      target ? tr("%1; generation %2").arg(target->hash).arg(target->generation) : QString{}},
      {downloadRate_, latest ? tr("%1 bytes/s").arg(latest->down) : unavailable(), latest ? tr("%1 payload bytes per second").arg(latest->down) : QString{}},
      {uploadRate_, latest ? tr("%1 bytes/s").arg(latest->up) : unavailable(), latest ? tr("%1 payload bytes per second").arg(latest->up) : QString{}},
      {observations_, latest ? tr("%1 actual observations (maximum 600)").arg(count) : tr("No activity samples yet"),
          latest ? tr("Monotonic arrival interval: %1 nanoseconds").arg(latest->time - plot->at(0).time) : QString{}}}};
  activityDirty_ = false;
  for (const auto& fact : facts) {
    if (!current() || !fact.label) { if (guard) { guard->activityDirty_ = true; } return; }
    fact.label->setText(fact.text); if (!current() || !fact.label) { if (guard) { guard->activityDirty_ = true; } return; }
    fact.label->setToolTip(fact.tip); if (!current()) { if (guard) { guard->activityDirty_ = true; } return; }
  }
  if (plot && isVisible() && activity_->isVisible() && !window()->isMinimized()) { plot->update(); }
}
void StatisticsPage::queueHistoryVisibility() {
  if (historyVisibilityQueued_) { return; }
  historyVisibilityQueued_ = true;
  QMetaObject::invokeMethod(this, [this] {
    historyVisibilityQueued_ = false;
    if (isVisible() && !window()->isMinimized()
        && (historySelected() || historyWeekSelected() || historyMonthSelected() || historyRangeSelected())) {
      emit historyVisible(); // Final access; Window may rebind or delete this page.
    }
  }, Qt::QueuedConnection);
}
void StatisticsPage::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (observedWindow_ != window()) {
    if (observedWindow_) { observedWindow_->removeEventFilter(this); }
    observedWindow_ = window(); observedWindow_->installEventFilter(this);
  }
  QPointer<StatisticsPage> guard(this);
  plot_->setUpdatesEnabled(!window()->isMinimized());
  if (!guard) { return; }
  refreshActivity(); if (guard) { guard->refreshEngine(); }
  if (guard) { guard->queueHistoryVisibility(); }
}
bool StatisticsPage::eventFilter(QObject* object, QEvent* event) {
  if (object == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    QPointer<StatisticsPage> guard(this);
    // Nested tab layouts can have deferred backing-store paints when minimized.
    // Suspend the actual QWidget update path, not merely work inside paintEvent.
    plot_->setUpdatesEnabled(!window()->isMinimized());
    if (!guard) { return false; }
    QMetaObject::invokeMethod(this, [guard] {
      if (guard) { guard->refreshActivity(); } if (guard) { guard->refreshEngine(); }
      if (guard) { guard->queueHistoryVisibility(); }
    }, Qt::QueuedConnection);
  }
  return QWidget::eventFilter(object, event);
}
} // namespace biglybtc::ui

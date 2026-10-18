#include "statistics_page.hpp"

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

StatisticsPage::StatisticsPage(QWidget* parent) : QWidget(parent), tabs_(new QTabWidget(this)),
    activity_(new QWidget(tabs_)), plot_(new ActivityPlot(activity_)),
    transfer_(label(tr("No observed transfer"), QStringLiteral("statistics-transfer-name"), activity_)),
    downloadRate_(label(unavailable(), QStringLiteral("statistics-payload-download-rate"), activity_)),
    uploadRate_(label(unavailable(), QStringLiteral("statistics-payload-upload-rate"), activity_)),
    observations_(label(tr("No activity samples yet"), QStringLiteral("statistics-activity-observations"), activity_)),
    status_(label(tr("Not loaded"), QStringLiteral("statistics-read-status"), this)),
    provenance_(label(tr("Source: unavailable; save time unknown"), QStringLiteral("statistics-source"), this)) {
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
  tabs_->addTab(activity_, tr("Activity"));
  auto* transfers = new QWidget(tabs_); transfers->setObjectName(QStringLiteral("statistics-transfers"));
  auto* transferLayout = new QVBoxLayout(transfers);
  auto* transferTabs = new QTabWidget(transfers); transferTabs->setObjectName(QStringLiteral("statistics-transfer-tabs"));
  auto* current = new QScrollArea(transferTabs); current->setWidgetResizable(true); current->setObjectName(QStringLiteral("statistics-transfers-current"));
  auto* contents = new QWidget(current); auto* currentLayout = new QVBoxLayout(contents);
  currentLayout->addWidget(label(tr("Current global rates and this-session totals: unavailable"), QStringLiteral("statistics-current-unavailable"), contents));
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
  currentLayout->addWidget(label(tr("Saved combined counters already include data and protocol. Split counters and DHT are shown separately, never added again. The file contains no save timestamp. No mark/reset or write is performed."),
      QStringLiteral("statistics-saved-scope"), contents));
  currentLayout->addStretch(); current->setWidget(contents); transferTabs->addTab(current, tr("Current"));
  auto* history = new QWidget(transferTabs); history->setObjectName(QStringLiteral("statistics-transfers-history"));
  auto* historyLayout = new QVBoxLayout(history);
  historyLayout->addWidget(label(tr("History unavailable: no owned global long-term statistics service is connected. Saved totals are not historical samples."),
      QStringLiteral("statistics-history-unavailable"), history)); historyLayout->addStretch();
  transferTabs->addTab(history, tr("History")); transferLayout->addWidget(transferTabs); tabs_->addTab(transfers, tr("Transfers"));
  auto* refresh = new QPushButton(tr("Refresh saved totals"), this); refresh->setObjectName(QStringLiteral("statistics-refresh"));
  connect(refresh, &QPushButton::clicked, this, &StatisticsPage::refreshRequested);
  auto* layout = new QVBoxLayout(this); layout->addWidget(status_); layout->addWidget(provenance_); layout->addWidget(tabs_, 1); layout->addWidget(refresh);
  connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { refreshActivity(); });
}
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
  if (session_ != 0 && session != session_) { clearActivity(); }
  session_ = session; request_ = request; state_.reset(); loading_ = true;
  const auto facts = emptyFacts(tr("Loading last-saved statistics…"), tr("Source: unavailable; save time unknown"));
  if (!writeFacts(facts)) { return; }
  refreshActivity();
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
  if (!activityDirty_ || !isVisible() || !activity_->isVisible() || window()->isMinimized()) { return; }
  QPointer<StatisticsPage> guard(this); QPointer<ActivityPlot> plot = plot_;
  const std::optional<Target> target = target_; const auto count = plot->count();
  const auto revision = activityRevision_, session = session_;
  const auto current = [&] {
    return guard && plot && guard->activityRevision_ == revision && guard->session_ == session
        && guard->target_ == target && guard->isVisible() && guard->activity_->isVisible() && !guard->window()->isMinimized();
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
void StatisticsPage::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (observedWindow_ != window()) {
    if (observedWindow_) { observedWindow_->removeEventFilter(this); }
    observedWindow_ = window(); observedWindow_->installEventFilter(this);
  }
  refreshActivity();
}
bool StatisticsPage::eventFilter(QObject* object, QEvent* event) {
  if (object == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    QPointer<StatisticsPage> guard(this);
    QMetaObject::invokeMethod(this, [guard] { if (guard) { guard->refreshActivity(); } }, Qt::QueuedConnection);
  }
  return QWidget::eventFilter(object, event);
}
} // namespace biglybtc::ui

#include "overall_view.hpp"
#include "overall_transfer_summary.hpp"
#include <QDateTime>
#include <bit>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>
namespace biglybtc::ui {
namespace {
QLabel* value(const QString& id, QWidget* parent) {
  auto* result = new QLabel(QObject::tr("Unavailable"), parent);
  result->setObjectName(id); result->setTextFormat(Qt::PlainText);
  result->setWordWrap(true);
  result->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  return result;
}
std::int64_t field(const core::OverallValues& v, core::SavedStatistic key) {
  return v[static_cast<std::size_t>(key)];
}
}
OverallView::OverallView(QWidget* parent) : QWidget(parent),
    status_(value(QStringLiteral("overall-live-status"), this)),
    saved_(value(QStringLiteral("overall-save-status"), this)),
    mark_(new QPushButton(tr("Set mark"), this)), clear_(new QPushButton(tr("Clear mark"), this)) {
  setObjectName(QStringLiteral("statistics-overall-live"));
  auto* layout = new QVBoxLayout(this); layout->addWidget(status_);
  auto* form = new QFormLayout;
  const std::array<QString, 11> names{tr("Downloaded (payload + protocol):"), tr("Uploaded (payload + protocol):"),
      tr("Uptime (seconds):"), tr("Mark date:"), tr("Downloaded since mark:"), tr("Uploaded since mark:"),
      tr("Uptime since mark (seconds):"), tr("DHT downloaded / uploaded:"),
      tr("Average download / upload (bytes/s):"), tr("Average download / upload since mark (bytes/s):"),
      tr("Share ratio / since mark:")};
  for (std::size_t i = 0; i < names.size(); ++i) {
    values_[i] = value(QStringLiteral("overall-live-value-%1").arg(i), this);
    auto* title = new QLabel(names[i], this); title->setTextFormat(Qt::PlainText);
    title->setBuddy(values_[i]); form->addRow(title, values_[i]);
  }
  layout->addLayout(form); layout->addWidget(saved_);
  auto* buttons = new QHBoxLayout;
  mark_->setObjectName(QStringLiteral("statistics-set-mark")); clear_->setObjectName(QStringLiteral("statistics-clear-mark"));
  mark_->setEnabled(false); clear_->setEnabled(false);
  buttons->addWidget(mark_); buttons->addWidget(clear_); buttons->addStretch(); layout->addLayout(buttons);
  connect(mark_, &QPushButton::clicked, this, [this] { if (!busy_ && packet_ && packet_->writable && !packet_->closed) { emit markRequested(false); } });
  connect(clear_, &QPushButton::clicked, this, [this] { if (!busy_ && packet_ && packet_->writable && !packet_->closed) { emit markRequested(true); } });
}
void OverallView::beginSession(quint64 session) {
  if (!session || session <= session_) { return; }
  session_ = session; packet_.reset(); dirty_ = true; render();
}
void OverallView::setPresentation(OverallPresentation packet) {
  if (!packet || packet->session != session_ || !packet->sequence
      || (packet_ && (packet->sequence <= packet_->sequence || packet_->closed))) { return; }
  packet_ = std::move(packet); dirty_ = true; render();
}
void OverallView::setCommandBusy(bool busy) { busy_ = busy; dirty_ = true; render(); }
void OverallView::render() {
  if (!dirty_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<OverallView> guard(this); const auto packet = packet_; const bool busy = busy_;
  const auto current = [&] { return guard && guard->packet_ == packet && guard->busy_ == busy
      && guard->isVisible() && !guard->window()->isMinimized(); };
  struct Fact { QPointer<QLabel> target; QString text; };
  std::array<Fact, 13> facts{};
  facts[0] = {status_, !packet ? tr("Accounting starts with a qualified transfer session")
      : packet->closed ? (packet->finalCounters ? tr("Closed; peer counters settled") : tr("Closed; last observed peer counters"))
      : tr("Live overall accounting; peer traffic observed in this session")};
  facts[1] = {saved_, !packet ? tr("Saved totals remain available below")
      : !packet->failure.isEmpty() ? tr("Saving blocked: %1").arg(packet->failure)
      : packet->dirty ? tr("Changes pending; automatic save every 10 minutes and on close")
      : tr("Current values saved")};
  if (packet && !packet->historyFailure.isEmpty()) { facts[1].text += tr("\nHistory: %1").arg(packet->historyFailure); }
  std::array<QString, 11> texts; texts.fill(tr("Unavailable"));
  if (packet && packet->available) {
    const auto& v = packet->values;
    using S = core::SavedStatistic;
    const auto down = field(v, S::Downloaded), up = field(v, S::Uploaded), uptime = field(v, S::Uptime);
    // Match Java long subtraction and the original clamp of a future mark.
    const auto difference = [&](std::int64_t total, std::int64_t mark) {
      if (mark > total) { mark = total; }
      return QString::number(std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(total) - static_cast<std::uint64_t>(mark)));
    };
    texts = {QString::number(down), QString::number(up), QString::number(uptime),
        field(v, S::MarkTime) ? QDateTime::fromMSecsSinceEpoch(field(v, S::MarkTime)).toString(Qt::ISODate) : tr("No mark"),
        difference(down, field(v, S::MarkDownloaded)), difference(up, field(v, S::MarkUploaded)),
        difference(uptime, field(v, S::MarkUptime)),
        tr("%1 / %2 bytes (separate)").arg(field(v, S::DhtDownloaded)).arg(field(v, S::DhtUploaded))};
    const auto summary = overallTransferSummary(v);
    texts[8] = summary ? tr("%1 / %2").arg(summary->averageDownload).arg(summary->averageUpload) : tr("Unavailable");
    texts[9] = summary ? tr("%1 / %2").arg(summary->sinceMarkAverageDownload).arg(summary->sinceMarkAverageUpload) : tr("Unavailable");
    texts[10] = summary ? tr("%1 / %2").arg(QString::fromStdString(overallShareRatioText(summary->shareRatioThousandths)))
        .arg(QString::fromStdString(overallShareRatioText(summary->sinceMarkShareRatioThousandths))) : tr("Unavailable");
  }
  for (std::size_t i = 0; i < texts.size(); ++i) { facts[i + 2] = {values_[i], texts[i]}; }
  dirty_ = false;
  for (const auto& fact : facts) {
    if (!current() || !fact.target) { if (guard) { guard->dirty_ = true; } return; }
    fact.target->setText(fact.text);
  }
  const bool enabled = packet && packet->writable && !packet->closed && !busy;
  if (!current() || !mark_) { return; } mark_->setEnabled(enabled);
  if (!current() || !clear_) { return; } clear_->setEnabled(enabled);
}
void OverallView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (observedWindow_ != window()) {
    if (observedWindow_) { observedWindow_->removeEventFilter(this); }
    observedWindow_ = window(); observedWindow_->installEventFilter(this);
  }
  render();
}
bool OverallView::eventFilter(QObject* object, QEvent* event) {
  if (object == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    QPointer<OverallView> guard(this);
    QMetaObject::invokeMethod(this, [guard] { if (guard) { guard->render(); } }, Qt::QueuedConnection);
  }
  return QWidget::eventFilter(object, event);
}
}

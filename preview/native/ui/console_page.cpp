// SPDX-License-Identifier: GPL-2.0-or-later
#include "console_page.hpp"
#include "biglybtc/console_history.hpp"

#include <QCheckBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace biglybtc::ui {

ConsolePage::ConsolePage(QWidget* parent)
    : QWidget(parent), output_(new QPlainTextEdit(this)), pause_(new QCheckBox(tr("Pause"), this)),
      autoScroll_(new QCheckBox(tr("Auto-scroll"), this)), timer_(new QTimer(this)) {
  setObjectName(QStringLiteral("classic-console-page"));
  output_->setObjectName(QStringLiteral("application-console-log"));
  output_->setAccessibleName(tr("Application log"));
  output_->setReadOnly(true);
  output_->setMaximumBlockCount(2048);
  output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  autoScroll_->setChecked(true);
  auto* clear = new QPushButton(tr("Clear"), this);
  clear->setObjectName(QStringLiteral("application-console-clear"));
  auto* controls = new QHBoxLayout;
  controls->addWidget(pause_);
  controls->addWidget(autoScroll_);
  controls->addWidget(clear);
  controls->addStretch();
  auto* layout = new QVBoxLayout(this);
  layout->addLayout(controls);
  layout->addWidget(output_);
  timer_->setInterval(350);
  connect(timer_, &QTimer::timeout, this, &ConsolePage::refresh);
  connect(clear, &QPushButton::clicked, this, [this] {
    sequence_ = core::applicationConsole().latestSequence();
    output_->clear();
  });
  connect(pause_, &QCheckBox::toggled, this, [this](bool paused) {
    if (paused) { timer_->stop(); }
    else if (isVisible()) { refresh(); timer_->start(); }
  });
}

void ConsolePage::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (!pause_->isChecked()) { refresh(); timer_->start(); }
}

void ConsolePage::hideEvent(QHideEvent* event) {
  timer_->stop();
  QWidget::hideEvent(event);
}

void ConsolePage::refresh() {
  const auto entries = core::applicationConsole().recent(sequence_);
  if (entries.empty()) { return; }
  sequence_ = entries.back().sequence;
  QString lines;
  for (const auto& entry : entries) {
    if (!lines.isEmpty()) { lines += u'\n'; }
    lines += QDateTime::fromMSecsSinceEpoch(entry.wallMillis).toString(QStringLiteral("HH:mm:ss.zzz"));
    lines += QStringLiteral("  ");
    lines += QString::fromUtf8(entry.message.data(), static_cast<qsizetype>(entry.message.size()));
  }
  auto* scrollbar = output_->verticalScrollBar();
  const auto previous = scrollbar->value();
  output_->appendPlainText(lines);
  scrollbar->setValue(autoScroll_->isChecked() ? scrollbar->maximum() : previous);
}

} // namespace biglybtc::ui

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QWidget>

class QCheckBox;
class QPlainTextEdit;
class QTimer;

namespace biglybtc::ui {

class ConsolePage final : public QWidget {
public:
  explicit ConsolePage(QWidget* parent);

protected:
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;

private:
  void refresh();

  QPlainTextEdit* output_;
  QCheckBox* pause_;
  QCheckBox* autoScroll_;
  QTimer* timer_;
  quint64 sequence_ = 0;
};

} // namespace biglybtc::ui

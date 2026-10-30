// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "i2p_presentation.hpp"
#include <QWidget>
class QLabel;
namespace biglybtc::ui {
class I2pStatisticsView final:public QWidget {
  Q_OBJECT
public:
  explicit I2pStatisticsView(QWidget* parent=nullptr);
  void beginSession(quint64);
  bool setPresentation(I2pPresentation);
  I2pPresentation presentation()const{return packet_;}
private:
  quint64 session_=0;
  I2pPresentation packet_;
  QLabel *state_,*traffic_,*peers_,*trackers_,*failure_;
};
}

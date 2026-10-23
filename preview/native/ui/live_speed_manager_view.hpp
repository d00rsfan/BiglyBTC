#pragma once
#include "live_speed_manager_presentation.hpp"
#include <QPointer>
#include <QWidget>
#include <array>
class QLabel;
class QShowEvent;
namespace biglybtc::ui {
class LiveSpeedMapWidget;
class LiveSpeedManagerView final : public QWidget {
  Q_OBJECT
public:
  explicit LiveSpeedManagerView(QWidget* parent = nullptr);
  bool beginSession(quint64);
  bool setPresentation(LiveSpeedManagerPresentation);
  LiveSpeedManagerPresentation presentation() const { return packet_; }
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  void refresh();
  void queueRefresh();
  quint64 session_ = 0, revision_ = 0;
  bool dirty_ = true, queued_ = false, rendering_ = false;
  LiveSpeedManagerPresentation packet_;
  std::array<QPointer<QLabel>, 6> facts_{};
  QPointer<LiveSpeedMapWidget> plot_;
  QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui

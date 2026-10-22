#pragma once
#include "native_session_presentation.hpp"

#include <QPointer>
#include <QWidget>
#include <array>

class QLabel;
class QShowEvent;
namespace biglybtc::ui {
class NativeSessionView final : public QWidget {
  Q_OBJECT
public:
  explicit NativeSessionView(QWidget* parent = nullptr);
  bool beginSession(quint64);
  bool setPresentation(NativeSessionPresentation);
  NativeSessionPresentation presentation() const { return packet_; }
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  void refresh();
  quint64 session_ = 0, run_ = 0, revision_ = 0;
  NativeSessionPresentation packet_;
  bool dirty_ = true;
  QPointer<QLabel> state_;
  QPointer<QLabel> uptime_;
  std::array<QPointer<QLabel>, 6> counters_{}, rates_{};
  QPointer<QLabel> windows_;
  std::array<QPointer<QLabel>, 2> limits_{};
  QPointer<QLabel> configuration_;
  QPointer<QLabel> ratio_;
  QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui

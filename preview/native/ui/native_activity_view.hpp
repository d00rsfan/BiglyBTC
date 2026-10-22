#pragma once
#include "native_activity_presentation.hpp"

#include <QPointer>
#include <QWidget>
#include <array>
#include <cstddef>

class QLabel;
class QShowEvent;
namespace biglybtc::ui {
class NativeActivityView final : public QWidget {
  Q_OBJECT
public:
  explicit NativeActivityView(QWidget* parent = nullptr);
  bool beginSession(quint64);
  bool setPresentation(NativeActivityPresentation);
  NativeActivityPresentation presentation() const { return frame_; }
  std::size_t sampleCount() const;
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  class Chart;
  void refresh();
  quint64 session_ = 0, run_ = 0, revision_ = 0;
  NativeActivityPresentation frame_;
  bool dirty_ = true;
  QPointer<QLabel> state_;
  QPointer<QLabel> samples_;
  std::array<QPointer<QLabel>, 6> rates_{};
  std::array<QPointer<QLabel>, 2> limits_{};
  std::array<QPointer<Chart>, 2> charts_{};
  QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui

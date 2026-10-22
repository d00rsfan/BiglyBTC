#pragma once
#include "native_activity_presentation.hpp"
#include "native_activity_rates.hpp"

#include <QPointer>
#include <QWidget>
#include <array>
#include <cstddef>

class QComboBox;
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
  NativeActivityRateMode rateMode() const noexcept { return mode_; }
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  class Chart;
  void refresh();
  void queueRefresh();
  quint64 session_ = 0, run_ = 0, revision_ = 0;
  NativeActivityPresentation frame_;
  bool dirty_ = true, queued_ = false;
  NativeActivityRateMode mode_ = NativeActivityRateMode::Source10s;
  QPointer<QComboBox> modeChoice_;
  QPointer<QLabel> graphRates_;
  QPointer<QLabel> legend_;
  QPointer<QLabel> state_;
  QPointer<QLabel> samples_;
  std::array<QPointer<QLabel>, 6> rates_{};
  std::array<QPointer<QLabel>, 2> limits_{};
  std::array<QPointer<Chart>, 2> charts_{};
  QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui

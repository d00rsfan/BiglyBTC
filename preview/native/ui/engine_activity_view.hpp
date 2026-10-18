#pragma once

#include "engine_activity_presentation.hpp"

#include <QPointer>
#include <QWidget>

#include <array>
#include <cstddef>

class QLabel;
class QShowEvent;

namespace biglybtc::ui {

// GUI-thread only, immutable History-factory frames. No sampling, clock reads,
// IO or timers. clear() relinquishes the current frame; the enclosing Page
// separately binds the currently published profile session.
class EngineActivityView final : public QWidget {
  Q_OBJECT
public:
  explicit EngineActivityView(QWidget* parent = nullptr);
  void setPresentation(EngineActivityPresentation);
  void clear();
  EngineActivityPresentation presentation() const { return frame_; }
  std::size_t sampleCount() const;
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  class Chart;
  void refresh();
  EngineActivityPresentation frame_{};
  bool dirty_ = true;
  quint64 renderRevision_ = 0;
  QLabel* name_;
  QLabel* state_;
  QLabel* samples_;
  std::array<QLabel*, 6> rates_{};
  std::array<Chart*, 2> charts_{};
  QPointer<QWidget> observedWindow_;
};

} // namespace biglybtc::ui

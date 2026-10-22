#pragma once
#include "overall_presentation.hpp"
#include <QPointer>
#include <QWidget>
#include <array>
class QLabel;
class QPushButton;
namespace biglybtc::ui {
class OverallView final : public QWidget {
  Q_OBJECT
public:
  explicit OverallView(QWidget* parent = nullptr);
  void beginSession(quint64);
  void setPresentation(OverallPresentation);
  void setCommandBusy(bool);
signals:
  void markRequested(bool clear);
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  void render();
  quint64 session_ = 0;
  OverallPresentation packet_;
  bool busy_ = false, dirty_ = true;
  QPointer<QLabel> status_;
  QPointer<QLabel> saved_;
  std::array<QPointer<QLabel>, 8> values_{};
  QPointer<QPushButton> mark_;
  QPointer<QPushButton> clear_;
  QPointer<QWidget> observedWindow_;
};
}

#pragma once
#include "scheduler_presentation.hpp"
#include <QDialog>
#include <memory>
namespace biglybtc::ui {
class SchedulerDialog final : public QDialog {
  Q_OBJECT
public:
  explicit SchedulerDialog(QWidget* parent=nullptr);
  ~SchedulerDialog() override;
  bool beginSession(quint64);
  void invalidateSession();
  bool dirty() const noexcept;
  bool pending() const noexcept;
public slots:
  void setBusy(bool);
  bool setSnapshot(biglybtc::ui::SchedulerPresentation);
  bool setSaveResult(biglybtc::ui::SchedulerSavePresentation);
  void accept() override;
  void reject() override;
signals:
  void loadRequested(biglybtc::ui::SchedulerRequest);
  void saveRequested(biglybtc::ui::SchedulerSubmission);
protected:
  void showEvent(QShowEvent*) override;
  void closeEvent(QCloseEvent*) override;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::ui

#pragma once
#include "file_operation_presentation.hpp"
#include <QDialog>
#include <memory>

namespace biglybtc::ui {
class FileOperationDialog final:public QDialog {
  Q_OBJECT
public:
  explicit FileOperationDialog(QWidget* parent=nullptr);
  ~FileOperationDialog() override;
  bool beginOperation(quint64 session,quint64 revision,Target,FileOperation,
      QString destinationParent={});
  void invalidateSession();
  bool pending() const noexcept;
  FileOperationPlanPresentation plan() const;
  void setBusy(bool);
  bool setPlan(FileOperationPlanPresentation);
  bool setProgress(FileOperationProgressPresentation);
  bool setResult(FileOperationResultPresentation);
public slots:
  void accept() override;
  void reject() override;
signals:
  void planRequested(biglybtc::ui::FileOperationRequest);
  void executeRequested(biglybtc::ui::FileOperationSubmission);
  void cancelRequested(biglybtc::ui::FileOperationRequest);
protected:
  void showEvent(QShowEvent*) override;
  void closeEvent(QCloseEvent*) override;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::ui

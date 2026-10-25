#pragma once
#include "file_operation_presentation.hpp"
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <memory>
class QWidget;
namespace biglybtc::ui {
class FileOperationDialog;
inline constexpr std::size_t MaximumFileOperationSequence=256;
enum class FileOperationOutcome { NotAttempted, Completed, Failed, Cancelled };
struct FileOperationItemResult {
  QString hash,name;
  FileOperationOutcome outcome=FileOperationOutcome::NotAttempted;
  QString failure;
};
struct FileOperationSequenceReport {
  quint64 session=0;
  FileOperation operation=FileOperation::RemoveKeepData;
  std::vector<FileOperationItemResult> items;
  bool cancelled=false;
};
using FileOperationSequencePresentation=std::shared_ptr<const FileOperationSequenceReport>;
// Passive GUI coordinator, not a new worker/transaction/IO owner. Every selected
// item retains the existing independent path review, confirmation and journal.
class FileOperationSequence final:public QObject {
  Q_OBJECT
public:
  explicit FileOperationSequence(QWidget* owner);
  ~FileOperationSequence() override;
  bool begin(quint64,FileOperation,DownloadTargets,QStringList names);
  void setPresentation(const Presentation&);
  void setBusy(bool);
  bool setPlan(FileOperationPlanPresentation);
  bool setResult(FileOperationResultPresentation);
  void invalidateSession();
  bool closeForOwner();
  FileOperationDialog* dialog() const noexcept;
  bool active() const noexcept;
  FileOperationSequencePresentation report() const;
signals:
  void planRequested(biglybtc::ui::FileOperationRequest);
  void executeRequested(biglybtc::ui::FileOperationSubmission);
  void cancelRequested(biglybtc::ui::FileOperationRequest);
  void finished(biglybtc::ui::FileOperationSequencePresentation);
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
Q_DECLARE_METATYPE(biglybtc::ui::FileOperationSequencePresentation)

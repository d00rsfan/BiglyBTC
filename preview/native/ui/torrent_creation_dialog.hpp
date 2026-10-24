#pragma once
#include "file_operation_presentation.hpp"
#include <QDialog>
#include <memory>

namespace biglybtc::ui {
class TorrentCreationDialog final:public QDialog {
  Q_OBJECT
public:
  explicit TorrentCreationDialog(QWidget* parent=nullptr);
  ~TorrentCreationDialog() override;
  bool beginSession(quint64 session,quint64 revision);
  void invalidateSession();
  bool pending() const noexcept;
  void setBusy(bool);
  bool setProgress(TorrentCreationProgressPresentation);
  bool setResult(TorrentCreationResultPresentation);
public slots:
  void accept() override;
  void reject() override;
signals:
  void createRequested(biglybtc::ui::TorrentCreationRequest);
  void cancelRequested(biglybtc::ui::TorrentCreationRequest);
protected:
  void closeEvent(QCloseEvent*) override;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::ui

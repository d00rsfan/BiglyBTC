#pragma once
#include "torrent_intake_presentation.hpp"
#include <QDialog>
#include <QPointer>
class QLineEdit;class QCheckBox;class QLabel;class QPushButton;class QTableView;class QComboBox;class QSpinBox;
namespace biglybtc::ui {
class IntakeFileModel;
class TorrentIntakeDialog final:public QDialog {
  Q_OBJECT
public:
  explicit TorrentIntakeDialog(QWidget* parent=nullptr);
  bool beginSession(quint64,QString defaultDirectory={});
  void invalidateSession();
  void setPreview(TorrentIntakePresentation);
  void setResult(TorrentIntakeResultPresentation);
  void rejectPreview(const TorrentIntakeRequest&);
  void rejectSubmission(const TorrentIntakeSubmission&);
  void setBusy(bool);
  bool pending()const noexcept{return loading_||committing_;}
  TorrentIntakePresentation preview()const{return preview_;}
  void accept() override;
  void reject() override;
signals:
  void previewRequested(biglybtc::ui::TorrentIntakeRequest);
  void submitted(biglybtc::ui::TorrentIntakeSubmission);
  void previewCancelled(biglybtc::ui::TorrentIntakeRequest);
private:
  void requestPreview();
  void schedule();
  void render();
  quint64 session_=0,nextRequest_=0,revision_=0;
  bool loading_=false,committing_=false,busy_=false,queued_=false;
  TorrentIntakeRequest request_;
  TorrentIntakePresentation preview_;
  QString failure_;
  QPointer<QLineEdit> source_,destination_;
  QPointer<QCheckBox> start_,sequential_;
  QPointer<QLabel> status_;
  QPointer<QPushButton> load_,add_;
  QPointer<QComboBox> priority_;
  QPointer<QSpinBox> downloadLimit_,uploadLimit_;
  QPointer<QTableView> table_;
  QPointer<IntakeFileModel> model_;
};
} // namespace biglybtc::ui
